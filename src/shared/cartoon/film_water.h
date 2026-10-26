#pragma once
// film_water.h -- water for a film file (film_file.h `water`, `river`, `waterfall`): a sea or a lake whose waves roll, rivers that run down their valleys, and waterfalls.
//
//   the sea      a surface of Gerstner waves (twenty of them, long swells to short chop, about the wind's direction; each moves at the speed deep water gives its length),
//                written every frame as a mesh in front of the camera only: a wedge a little wider than the picture, its rings crowding round the camera (centimetres
//                apart at its feet, tens of metres at the horizon, out to 8 km). Ripples too small for the mesh are in its normals, fading with distance; foam lies on the
//                crests the waves fold over, and where water meets the land of a terrain it laps the shore. A lake is the same, calmer and greener; `clear` water
//                (glass, eta 1.33) shows the ground beneath it instead of a coloured body.
//   rivers       a ribbon through waypoints, smoothed, its surface never running uphill (on a terrain it lies in the lowest ground across its width), its ripples carried
//                downstream at its speed; white where it drops fast.
//   waterfalls   strands of white water falling from a lip in the arc thrown water takes, broken into clumps that fall at the speed the drop gives them, in front of a
//                darker sheet, with spray thrown up where they land (film_particles.h splashes) and a pool of foam.
//
// Coordinates are pbrt's, as a film file's; meshes are built in story space (x mirrored) and written with emitMesh. Every frame is a function of its time alone.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_terrain.h"
#include "nature_lib.h"

namespace cartoon {
namespace water {

// A sea or a lake: its surface's height, how high its waves are (the significant height, metres), which way they run (degrees as a character's facing: 0 +z, 90 +x), how
// sharp their crests are (0 to 1), and its kind - sea (deep blue-green), lake (calmer, greener) or clear (glass: the ground shows through).
struct Sea {
	double level = 0.0, waves = 0.8, wind = 0.0, choppy = 0.65;
	std::string kind = "sea";
	bool foam = true;
};
// A river: its waypoints (pbrt's x z), its width and its speed (metres a second), its surface's height when there is no terrain under it. On a terrain a river may
// instead start at one point and find its own way downhill (`from`), or run along a canyon's riverbed (`canyon`).
struct River {
	std::vector<std::pair<double, double>> points;
	double width = 8.0, speed = 1.4, y = 0.0;
	bool downhill = false, canyon = false;
	std::vector<Vec3> path;   // worked out when the film is loaded (pbrt's x, the water's height, z): the terrain is carved under it
};
// A waterfall: the middle of its lip (pbrt's x y z), how wide it is, how far it drops, and which way it pours (a facing).
struct Fall {
	Vec3 lip;
	double width = 6.0, drop = 20.0, facing = 0.0;
};
inline const std::vector<std::string> &seaKinds() {
	static const std::vector<std::string> k = {"sea", "lake", "clear"};
	return k;
}

namespace detail {

struct Wave {
	double dx, dz, k, omega, amp, phase, q;
};
// The waves of a sea: `n` of them from long swells to short chop. `scale` multiplies their lengths (1 for the waves; small for the ripples that only shade).
inline std::vector<Wave> waves(const Sea &s, int n, double longest, double shortest, double height, int salt) {
	std::vector<Wave> out;
	double total = 0.0;
	for (int i = 0; i < n; ++i) {
		const double t = n == 1 ? 0.0 : i / double(n - 1);
		const double L = longest * std::pow(shortest / longest, t) * (0.85 + 0.3 * hash01(salt + i, 1));
		const double spread = (0.35 + 0.65 * t) * 1.1;   // (the long swells come from the wind; the chop from all round it)
		const double a = (s.wind + (hash01(salt + i, 2) - 0.5) * 2.0 * spread * 57.29578) * kPi / 180.0;
		Wave w;
		w.dx = std::sin(a);
		w.dz = std::cos(a);
		w.k = 2.0 * kPi / L;
		w.omega = std::sqrt(9.81 * w.k);   // deep water
		w.amp = std::pow(L, 1.3) * (0.5 + 0.5 * hash01(salt + i, 3));   // (short waves gentler than long ones, as the sea's are)
		w.phase = 2.0 * kPi * hash01(salt + i, 4);
		total += w.amp * w.amp / 2.0;
		out.push_back(w);
	}
	const double f = height / 4.0 / std::sqrt(std::max(1e-18, total));   // (the significant height is four times the surface's rms)
	for (Wave &w : out) {
		w.amp *= f;
		w.q = s.choppy / std::max(1e-9, w.k * w.amp * n);
	}
	return out;
}
inline const std::vector<Wave> &seaWaves(const Sea &s) {
	thread_local Sea last;
	thread_local std::vector<Wave> w;
	thread_local bool made = false;
	if (!made || last.waves != s.waves || last.wind != s.wind || last.choppy != s.choppy) {
		const double longest = std::max(4.0, 22.0 * s.waves);
		w = waves(s, 20, longest, std::max(0.6, longest / 40.0), s.waves, 1301);
		last = s;
		made = true;
	}
	return w;
}
inline const std::vector<Wave> &rippleWaves(const Sea &s) {
	thread_local double lastWind = 1e9;
	thread_local std::vector<Wave> w;
	if (lastWind != s.wind) {
		Sea calm = s;
		calm.choppy = 0.0;
		w = waves(calm, 14, 1.8, 0.08, 0.018, 9001);
		lastWind = s.wind;
	}
	return w;
}

// The mesh with every triangle turned to face the way its corners' normals do (up, for water).
inline Mesh agreeing(Mesh m) {
	for (auto &t : m.tris) {
		const Vec3 n = cross(m.verts[t[1]] - m.verts[t[0]], m.verts[t[2]] - m.verts[t[0]]);
		if (dot(n, m.normals[t[0]] + m.normals[t[1]] + m.normals[t[2]]) < 0.0) std::swap(t[1], t[2]);
	}
	return m;
}

}  // namespace detail

// The sea's surface at pbrt's (x, z) at rest, at time t: where that point has moved to (pbrt's), its normal (pbrt's, ripples in it as far as `rippleWeight`), and how far the
// crest there has folded over (0 none, 1 and more: foam).
inline void seaAt(const Sea &s, double x, double z, double t, double rippleWeight, Vec3 &p, Vec3 &n, double &fold) {
	double dx = 0.0, dy = 0.0, dz = 0.0, nx = 0.0, ny = 1.0, nz = 0.0, jac = 1.0;
	for (const detail::Wave &w : detail::seaWaves(s)) {
		const double f = w.k * (w.dx * x + w.dz * z) - w.omega * t + w.phase;
		const double c = std::cos(f), sn = std::sin(f), wa = w.k * w.amp;
		dx += w.q * w.amp * w.dx * c;
		dz += w.q * w.amp * w.dz * c;
		dy += w.amp * sn;
		nx -= w.dx * wa * c;
		nz -= w.dz * wa * c;
		ny -= w.q * wa * sn;
		jac -= w.q * wa * sn;
	}
	if (rippleWeight > 0.0)
		for (const detail::Wave &w : detail::rippleWaves(s)) {
			const double f = w.k * (w.dx * x + w.dz * z) - w.omega * t + w.phase, wa = w.k * w.amp * rippleWeight;
			nx -= w.dx * wa * std::cos(f);
			nz -= w.dz * wa * std::cos(f);
		}
	p = {x + dx, s.level + dy, z + dz};
	n = normalize(Vec3{nx, std::max(0.05, ny), nz});
	fold = std::max(0.0, 1.0 - jac);
}

inline std::string shoreText(const Sea &s, double t, const terrain::Field &ground);

inline std::string seaMaterial(const Sea &s) {
	if (s.kind == "clear") return glassMaterial(1.333, 0.0);
	return s.kind == "lake" ? coated({0.025, 0.06, 0.045}, 0.0) : coated({0.015, 0.055, 0.07}, 0.0);   // (a smooth coat over the colour of deep water: the sky in the coat)
}

// The sea as scene text at t, seen from `eye` towards `target` (story space, as film_subjects::cameraOf gives them) with a horizontal field of view of `hfov` degrees.
// `ground`: the terrain (shallows lap its shore), or null.
inline std::string seaText(const Sea &s, double t, const Vec3 &eyeStory, const Vec3 &targetStory, double hfov, const terrain::Field *ground) {
	const Vec3 eye{-eyeStory.x, eyeStory.y, eyeStory.z}, target{-targetStory.x, targetStory.y, targetStory.z};   // (pbrt's)
	const double heading = std::atan2(target.z - eye.z, target.x - eye.x);
	const double half = std::min(kPi, (hfov * 0.5 + 25.0) * kPi / 180.0);   // (a little wider than the picture: waves move points sideways)
	const double height = std::max(0.5, std::abs(eye.y - s.level));
	const double r0 = std::max(0.05, 0.08 * height), r1 = 8000.0;
	const int around = 150, out = 110;
	Mesh water, foam;
	std::vector<double> folds;
	water.verts.reserve(static_cast<std::size_t>(around + 1) * (out + 1));
	for (int j = 0; j <= out; ++j) {
		const double r = r0 * std::pow(r1 / r0, j / double(out));
		const double ripple = std::exp(-r / (25.0 + 6.0 * height));
		for (int i = 0; i <= around; ++i) {
			const double a = heading - half + 2.0 * half * i / around;
			const double x = eye.x + r * std::cos(a), z = eye.z + r * std::sin(a);
			Vec3 p, n;
			double fold = 0.0;
			if (r < 2500.0) seaAt(s, x, z, t, ripple, p, n, fold);
			else p = {x, s.level, z}, n = {0.0, 1.0, 0.0};   // (beyond what the waves can show: flat, so the horizon is a clean line)
			water.verts.push_back({-p.x, p.y, p.z});
			water.normals.push_back({-n.x, n.y, n.z});
			folds.push_back(fold);
		}
	}
	const int w = around + 1;
	for (int j = 0; j < out; ++j)
		for (int i = 0; i < around; ++i) {
			const int a = j * w + i;
			water.tris.push_back({a, a + w, a + 1});
			water.tris.push_back({a + 1, a + w, a + w + 1});
		}
	std::string text = "# the " + s.kind + " (film_water.h)\n" + attr(seaMaterial(s), emitMesh(detail::agreeing(water), identity()));
	if (s.foam && s.kind != "clear") {   // the foam: the triangles where the crest folds, a hand's breadth above the water
		for (const auto &tri : water.tris) {
			const double f = (folds[tri[0]] + folds[tri[1]] + folds[tri[2]]) / 3.0;
			const Vec3 c = (water.verts[tri[0]] + water.verts[tri[1]] + water.verts[tri[2]]) * (1.0 / 3.0);
			if (f < 0.45 + 0.3 * nature::noise2(c.x * 0.9, c.z * 0.9 + t * 0.3, 77)) continue;
			const int base = static_cast<int>(foam.verts.size());
			for (int k = 0; k < 3; ++k) {
				foam.verts.push_back(water.verts[tri[k]] + Vec3{0.0, 0.03, 0.0});
				foam.normals.push_back(water.normals[tri[k]]);
			}
			foam.tris.push_back({base, base + 1, base + 2});
		}
		if (!foam.tris.empty()) text += attr(diffuseMaterial({0.75, 0.78, 0.8}), emitMesh(detail::agreeing(foam), identity()));
	}
	if (ground && s.foam && s.kind != "clear") text += shoreText(s, t, *ground);
	return text;
}

// The foam along a terrain's shore at t: the waterline traced over the terrain's grid (where the ground crosses the water's level), as a ribbon that laps up the beach
// and back, broken where the noise says the wash is thin. "" without a shore.
inline std::string shoreText(const Sea &s, double t, const terrain::Field &ground) {
	Mesh m;
	const int n = ground.n;
	const auto h = [&](int i, int j) { return ground.heightAt(ground.x0 + i * ground.cell, ground.z0 + j * ground.cell) - s.level; };
	std::vector<double> row0(static_cast<std::size_t>(n)), row1(static_cast<std::size_t>(n));
	for (int i = 0; i < n; ++i) row0[static_cast<std::size_t>(i)] = h(i, 0);
	for (int j = 0; j + 1 < n; ++j) {
		for (int i = 0; i < n; ++i) row1[static_cast<std::size_t>(i)] = h(i, j + 1);
		for (int i = 0; i + 1 < n; ++i) {
			const double c[4] = {row0[static_cast<std::size_t>(i)], row0[static_cast<std::size_t>(i + 1)], row1[static_cast<std::size_t>(i + 1)], row1[static_cast<std::size_t>(i)]};
			const double px[4] = {0, 1, 1, 0}, pz[4] = {0, 0, 1, 1};
			Vec3 cut[4];
			int k = 0;
			for (int e = 0; e < 4 && k < 4; ++e) {   // where each edge of the cell crosses the waterline
				const double a = c[e], b = c[(e + 1) % 4];
				if ((a < 0.0) == (b < 0.0)) continue;
				const double u = a / (a - b);
				cut[k++] = {ground.x0 + (i + px[e] + u * (px[(e + 1) % 4] - px[e])) * ground.cell, s.level, ground.z0 + (j + pz[e] + u * (pz[(e + 1) % 4] - pz[e])) * ground.cell};
			}
			for (int q = 0; q + 1 < k; q += 2) {
				const Vec3 a = cut[q], b = cut[q + 1];
				const Vec3 mid = (a + b) * 0.5;
				if (nature::noise2(mid.x * 0.15 + t * 0.2, mid.z * 0.15, 91) < 0.3) continue;   // (gaps in the wash)
				// which way is the land: up the slope of the ground
				const double e = 1.0;
				const Vec3 up = normalize(Vec3{ground.heightAt(mid.x + e, mid.z) - ground.heightAt(mid.x - e, mid.z), 0.0, ground.heightAt(mid.x, mid.z + e) - ground.heightAt(mid.x, mid.z - e)} + Vec3{1e-9, 0.0, 0.0});
				const double lap = 0.6 * std::sin(t * 1.2 + 0.05 * (mid.x + mid.z));   // up the beach and back
				const double width = 0.5 + 0.4 * nature::noise2(mid.x * 0.3, mid.z * 0.3 - t * 0.4, 93);
				const Vec3 in = up * lap, out = up * (lap - width);
				const int base = static_cast<int>(m.verts.size());
				for (const Vec3 &v : {a + out, b + out, b + in, a + in}) {
					m.verts.push_back({-v.x, s.level + 0.03, v.z});
					m.normals.push_back({0.0, 1.0, 0.0});
				}
				m.tris.push_back({base, base + 1, base + 2});
				m.tris.push_back({base, base + 2, base + 3});
			}
		}
		std::swap(row0, row1);
	}
	return m.tris.empty() ? std::string() : "# the foam along the shore (film_water.h)\n" + attr(diffuseMaterial({0.8, 0.82, 0.84}), emitMesh(detail::agreeing(m), identity()));
}

// ---- rivers ----
// The river's path: its waypoints smoothed (Catmull-Rom), a point about every metre, and the surface's height at each (pbrt's x y z), never rising downstream.
// The way water takes downhill from (x, z) over a terrain: steps of a few metres down the slope of the ground smoothed over the river's width, until it leaves the square
// or the ground no longer falls (a hollow: a lake would be there). Pbrt's x z.
inline std::vector<std::pair<double, double>> downhillFrom(const terrain::Field &g, double x, double z, double width) {
	std::vector<std::pair<double, double>> out{{x, z}};
	const double step = std::max(2.0, width * 0.5), e = std::max(2.0, width);
	double dx = 0.0, dz = 0.0, flat = 0.0;
	for (int k = 0; k < 4000 && g.inside(x, z); ++k) {
		const double gx = g.heightAt(x + e, z) - g.heightAt(x - e, z), gz = g.heightAt(x, z + e) - g.heightAt(x, z - e);
		const double len = std::hypot(gx, gz);
		if (len < 1e-6) break;
		dx = 0.7 * dx - 0.3 * gx / len;   // (a little momentum, so it does not zigzag down a gully)
		dz = 0.7 * dz - 0.3 * gz / len;
		const double d = std::hypot(dx, dz);
		const double h0 = g.heightAt(x, z);
		x += step * dx / d;
		z += step * dz / d;
		flat = g.heightAt(x, z) < h0 - 0.02 ? 0.0 : flat + step;
		if (flat > 12.0 * step) break;
		if (k % 3 == 2) out.push_back({x, z});   // (every third step a waypoint: the path is smoothed again)
	}
	out.push_back({x, z});
	return out;
}
inline std::vector<Vec3> riverPath(const River &r, const terrain::Field *ground) {
	std::vector<Vec3> out;
	std::vector<std::pair<double, double>> points = r.points;
	if (ground && r.downhill && !points.empty()) points = downhillFrom(*ground, points[0].first, points[0].second, r.width);
	if (ground && r.canyon) {   // along the canyon's riverbed, the length of the square
		const terrain::Spec &ts = ground->spec;
		const double fu = (ts.fx() - ground->sx0) / ts.size;
		points.clear();
		for (int k = 0; k <= 80; ++k) {
			const double v = k / 80.0;
			points.push_back({ground->sx0 + terrain::detail::canyonRiverU(ts, v, fu) * ts.size, ground->sz0 + v * ts.size});
		}
		if (ground->heightAt(points.front().first, points.front().second) < ground->heightAt(points.back().first, points.back().second)) std::reverse(points.begin(), points.end());
	}
	const auto &P = points;
	if (P.size() < 2) return out;
	const auto at = [&](int i) {
		const int k = std::clamp(i, 0, static_cast<int>(P.size()) - 1);
		return Vec3{P[static_cast<std::size_t>(k)].first, 0.0, P[static_cast<std::size_t>(k)].second};
	};
	for (int i = 0; i + 1 < static_cast<int>(P.size()); ++i) {
		const Vec3 p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
		const int steps = std::max(2, static_cast<int>(length(p2 - p1)));
		for (int k = 0; k < steps; ++k) {
			const double u = k / double(steps);
			const auto cr = [u](double a, double b, double c, double d) { return b + 0.5 * u * (c - a + u * (2.0 * a - 5.0 * b + 4.0 * c - d + u * (3.0 * (b - c) + d - a))); };
			out.push_back({cr(p0.x, p1.x, p2.x, p3.x), 0.0, cr(p0.z, p1.z, p2.z, p3.z)});
		}
	}
	out.push_back(at(static_cast<int>(P.size()) - 1));
	double level = 1e30;
	for (std::size_t i = 0; i < out.size(); ++i) {
		double y = r.y;
		if (ground) {   // the lowest ground across the river's width, and a little above it
			const Vec3 a = out[i > 0 ? i - 1 : i], b = out[i + 1 < out.size() ? i + 1 : i];
			const Vec3 along = normalize(b - a + Vec3{1e-9, 0.0, 0.0}), across{-along.z, 0.0, along.x};
			double lowest = 1e30;
			for (int k = -2; k <= 2; ++k) {
				const Vec3 q = out[i] + across * (k * 0.25 * r.width);
				lowest = std::min(lowest, ground->heightAt(q.x, q.z));
			}
			y = lowest + 0.35;
		}
		level = std::min(level, y);   // (water does not run uphill)
		out[i].y = level;
	}
	return out;
}
inline std::string riverText(const River &r, double t, const terrain::Field *ground) {
	const std::vector<Vec3> path = r.path.empty() ? riverPath(r, ground) : r.path;
	if (path.size() < 2) return std::string();
	Mesh m, foam;
	const int across = 8;
	double s = 0.0;
	std::vector<double> drop(path.size(), 0.0);
	for (std::size_t i = 0; i < path.size(); ++i) {
		const Vec3 a = path[i > 0 ? i - 1 : i], b = path[i + 1 < path.size() ? i + 1 : i];
		const Vec3 along = normalize(Vec3{b.x - a.x, 0.0, b.z - a.z} + Vec3{1e-9, 0.0, 0.0}), side{-along.z, 0.0, along.x};
		if (i > 0) s += length(Vec3{path[i].x - path[i - 1].x, 0.0, path[i].z - path[i - 1].z});
		drop[i] = (a.y - b.y) / std::max(0.5, length(Vec3{b.x - a.x, 0.0, b.z - a.z}));
		for (int k = 0; k <= across; ++k) {
			const double c = k / double(across) - 0.5;
			const Vec3 p = path[i] + side * (c * r.width);
			// ripples carried downstream: a tilt from noise that moves along the river at its speed, stronger where it runs fast
			const double flow = s - r.speed * t;
			const double e = 0.08 + 0.5 * std::min(1.0, drop[i] * 4.0);
			const double ta = e * (nature::noise2(flow * 0.9, c * 3.0, 31) - 0.5), tb = e * (nature::noise2(flow * 0.9 + 7.0, c * 3.0, 33) - 0.5);
			const Vec3 n = normalize(Vec3{0.0, 1.0, 0.0} + along * ta + side * tb);
			m.verts.push_back({-p.x, p.y, p.z});
			m.normals.push_back({-n.x, n.y, n.z});
		}
	}
	const int w = across + 1;
	for (int i = 0; i + 1 < static_cast<int>(path.size()); ++i)
		for (int k = 0; k < across; ++k) {
			const int a = i * w + k;
			m.tris.push_back({a, a + 1, a + w});
			m.tris.push_back({a + 1, a + w + 1, a + w});
			if (drop[static_cast<std::size_t>(i)] > 0.12 && nature::noise2(i * 0.6 - r.speed * t * 0.6, k * 0.7, 35) > 0.45) {   // white water where it drops fast
				const int base = static_cast<int>(foam.verts.size());
				for (int q : {a, a + 1, a + w, a + w + 1}) {
					foam.verts.push_back(m.verts[static_cast<std::size_t>(q)] + Vec3{0.0, 0.04, 0.0});
					foam.normals.push_back({0.0, 1.0, 0.0});
				}
				foam.tris.push_back({base, base + 1, base + 2});
				foam.tris.push_back({base + 1, base + 3, base + 2});
			}
		}
	std::string out = "# a river (film_water.h)\n" + attr(coated({0.05, 0.07, 0.05}, 0.0), emitMesh(detail::agreeing(m), identity()));   // (a murky green-brown under the sky in it)
	if (!foam.tris.empty()) out += attr(diffuseMaterial({0.72, 0.75, 0.76}), emitMesh(detail::agreeing(foam), identity()));
	return out;
}

// ---- waterfalls ----
inline double fallTime(const Fall &f) { return std::sqrt(2.0 * std::max(0.1, f.drop) / 9.81); }
// Where the water pouring over the lip at `across` (-0.5 to 0.5 of the width) is `tau` seconds after it went over (pbrt's).
inline Vec3 fallPoint(const Fall &f, double across, double tau) {
	const double a = f.facing * kPi / 180.0;
	const Vec3 out{std::sin(a), 0.0, std::cos(a)}, side{std::cos(a), 0.0, -std::sin(a)};
	const double v0 = 1.2 + 0.03 * f.drop;   // (how fast it leaves the lip)
	return f.lip + side * (across * f.width) + out * (v0 * tau) + Vec3{0.0, -0.5 * 9.81 * tau * tau, 0.0};
}
// The cliff a waterfall pours over: a rough face of rock behind the water, three times as wide, from a little above the lip to below where the water lands, bulging and
// hollowed by noise, with a ledge running back from its top. (The same every frame.)
inline std::string cliffText(const Fall &f) {
	const double a = f.facing * kPi / 180.0;
	const Vec3 out{std::sin(a), 0.0, std::cos(a)}, side{std::cos(a), 0.0, -std::sin(a)};
	const double top = f.lip.y + 0.4, bottom = f.lip.y - f.drop - 3.0, half = 1.5 * f.width;
	const auto face = [&](double u, double v) {   // u across (-1 to 1), v down (0 at the top, 1 at the bottom): pbrt's point
		const double bulge = 0.25 * f.width * (nature::noise2(u * 3.0 + 4.0, v * 2.5, 41) - 0.5) + 0.06 * f.width * (nature::noise2(u * 11.0, v * 9.0, 43) - 0.5);
		const double recess = 0.35 + 0.5 * std::abs(u) * std::abs(u);   // (the middle, where the water falls, worn back; the sides stand forward)
		return f.lip + side * (u * half) + out * (-recess + bulge + 0.25 * v) + Vec3{0.0, top - f.lip.y - v * (top - bottom), 0.0};
	};
	const auto ledge = [&](double u, double w) {   // w back from the top (0 to 1)
		return face(u, 0.0) - out * (w * 0.8 * f.width) + Vec3{0.0, 0.3 * (nature::noise2(u * 4.0, w * 4.0, 47) - 0.5), 0.0};
	};
	Mesh m;
	const int nu = 40, nv = 30, nw = 6;
	const auto put = [&](const Vec3 &p) { m.verts.push_back({-p.x, p.y, p.z}); };
	for (int j = 0; j <= nv; ++j)
		for (int i = 0; i <= nu; ++i) put(face(-1.0 + 2.0 * i / nu, j / double(nv)));
	const int ledgeBase = static_cast<int>(m.verts.size());
	for (int j = 0; j <= nw; ++j)
		for (int i = 0; i <= nu; ++i) put(ledge(-1.0 + 2.0 * i / nu, j / double(nw)));
	const auto grid = [&](int base, int rows) {
		for (int j = 0; j < rows; ++j)
			for (int i = 0; i < nu; ++i) {
				const int k = base + j * (nu + 1) + i;
				m.tris.push_back({k, k + 1, k + nu + 1});
				m.tris.push_back({k + 1, k + nu + 2, k + nu + 1});
			}
	};
	grid(0, nv);
	grid(ledgeBase, nw);
	m.normals.assign(m.verts.size(), Vec3{0.0, 0.0, 0.0});   // (from the faces round each point)
	for (const auto &t : m.tris) {
		const Vec3 n = cross(m.verts[t[1]] - m.verts[t[0]], m.verts[t[2]] - m.verts[t[0]]);
		for (int c = 0; c < 3; ++c) m.normals[t[c]] = m.normals[t[c]] + n;
	}
	for (Vec3 &n : m.normals) n = length(n) > 1e-12 ? normalize(n) : Vec3{0.0, 1.0, 0.0};
	return attr(diffuseMaterial({0.26, 0.245, 0.22}), emitMesh(m, identity()));
}
inline std::string fallText(const Fall &f, double t, const terrain::Field *ground = nullptr) {
	const bool onTerrain = ground != nullptr;
	const double T = fallTime(f);
	const double a = f.facing * kPi / 180.0;
	const Vec3 out{std::sin(a), 0.0, std::cos(a)};
	Mesh sheet, strands, pool;
	// the sheet behind: one curtain down the arc, a little narrower than the strands
	const int rows = 24, cols = 12;
	for (int j = 0; j <= rows; ++j)
		for (int i = 0; i <= cols; ++i) {
			const Vec3 p = fallPoint(f, (i / double(cols) - 0.5) * 0.92, T * j / rows) - out * 0.15;
			sheet.verts.push_back({-p.x, p.y, p.z});
			sheet.normals.push_back({-out.x, 0.0, out.z});
		}
	for (int j = 0; j < rows; ++j)
		for (int i = 0; i < cols; ++i) {
			const int k = j * (cols + 1) + i;
			sheet.tris.push_back({k, k + 1, k + cols + 1});
			sheet.tris.push_back({k + 1, k + cols + 2, k + cols + 1});
		}
	// the strands: ribbons down the arc, broken into clumps that fall with the water (each clump's place along the arc moves with time)
	const int count = std::max(16, static_cast<int>(f.width * 12.0));
	const double ribbon = f.width / count * 1.3;
	for (int s = 0; s < count; ++s) {
		const double across = (s + 0.5) / count - 0.5 + (hash01(s, 5) - 0.5) / count;
		const double speed = 0.85 + 0.3 * hash01(s, 6);
		for (int c = 0; c < 8; ++c) {   // clumps, each a stretch of the arc between two times since it went over
			const double start = std::fmod(t * speed / T + c / 8.0 + hash01(s * 7 + c, 7), 1.0);
			const double len = 0.04 + 0.12 * hash01(s * 7 + c, 8);
			const int seg = 4;
			const int base = static_cast<int>(strands.verts.size());
			for (int k = 0; k <= seg; ++k) {
				const double tau = T * std::min(1.0, start + len * k / seg);
				const Vec3 p = fallPoint(f, across, tau);
				const double spread = 1.0 + 1.5 * (tau / T);   // (it spreads as it falls)
				const Vec3 side{std::cos(a), 0.0, -std::sin(a)};
				for (const double e : {-0.5, 0.5}) {
					const Vec3 q = p + side * (e * ribbon * spread);
					strands.verts.push_back({-q.x, q.y, q.z});
					strands.normals.push_back({-out.x, 0.25, out.z});
				}
			}
			for (int k = 0; k < seg; ++k) {
				const int v = base + 2 * k;
				strands.tris.push_back({v, v + 1, v + 2});
				strands.tris.push_back({v + 1, v + 3, v + 2});
			}
		}
	}
	// the pool: still water round where it lands (in the basin a terrain was cut to), white where the falls churn it
	const Vec3 foot = fallPoint(f, 0.0, T);
	Mesh churn;
	const int ring = 36;
	const auto disc = [&](Mesh &m, double radius, double rise, double wobble) {
		m.verts.push_back({-foot.x, foot.y + rise, foot.z});
		m.normals.push_back({0.0, 1.0, 0.0});
		for (int i = 0; i <= ring; ++i) {
			const double b = 2.0 * kPi * i / ring, rr = radius * (1.0 + wobble * (nature::noise2(std::cos(b) * 2.0 + t * 0.8, std::sin(b) * 2.0, 57) - 0.5));
			m.verts.push_back({-(foot.x + rr * std::cos(b) * 1.2), foot.y + rise, foot.z + rr * std::sin(b)});
			m.normals.push_back({0.0, 1.0, 0.0});
			if (i > 0) m.tris.push_back({0, i, i + 1});
		}
	};
	double rise = 0.2;
	if (ground) {   // (on a terrain the pool fills its basin: no higher than the lowest ground round its rim)
		double rim = 1e30;
		for (int i = 0; i < 24; ++i) {
			const double b = 2.0 * kPi * i / 24.0;
			rim = std::min(rim, ground->heightAt(foot.x + 1.6 * f.width * std::cos(b) * 1.2, foot.z + 1.6 * f.width * std::sin(b)));
		}
		rise = std::min(rise, rim - 0.15 - foot.y);
	}
	disc(pool, 1.5 * f.width, rise, 0.2);
	disc(churn, 0.55 * f.width, rise + 0.06, 0.8);
	return "# a waterfall (film_water.h)\n" + (onTerrain ? std::string() : cliffText(f)) + attr(translucentMaterial({0.22, 0.26, 0.27}, {0.45, 0.5, 0.52}), emitMesh(sheet, identity())) +
		   attr(translucentMaterial({0.82, 0.85, 0.87}, {0.12, 0.13, 0.14}), emitMesh(strands, identity())) + attr(coated({0.025, 0.06, 0.05}, 0.0), emitMesh(detail::agreeing(pool), identity())) +
		   attr(diffuseMaterial({0.8, 0.83, 0.85}), emitMesh(detail::agreeing(churn), identity()));
}
// Where a waterfall's water lands (pbrt's): its spray is thrown from there.
inline Vec3 fallFoot(const Fall &f) { return fallPoint(f, 0.0, fallTime(f)); }

}  // namespace water
}  // namespace cartoon
