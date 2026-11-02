#pragma once
// film_stir.h -- water that answers what touches it (water round 1): every touch of the surface starts rings, and whatever moves through it leaves them behind.
//
//   rings        a ring is the Cauchy-Poisson solution for something dropped in deep water: at age a, distance r from the place, the phase is g a^2 / 4r and the waves there are
//                g a^2 / 4r^2 to the metre, so long waves run on ahead and short ones come after, as they do. What is excited is what the size of the thing can excite: the
//                waves about as long as it (`size` metres across, a Gaussian in wavenumber), and a hollow at the place that fills and bounces in the first half second.
//   trails       something moving is a string of rings, one every tenth of a second, from where it was: added up they are its wake - the V of 19.5 degrees and the curved waves
//                inside it, from nothing but the same formula (trail()).
//   rain         a ring from each drop near the camera, worked out cell by cell so that it costs the same however long it rains.
//   froth        churned water: a patch that fades, as the fold the sea's foam already follows.
//
// The surface's mesh is far too coarse for all this (it is half a metre between points 5 m from the camera), so refineGrid cuts the cells rings touch into pieces a few centimetres
// across (never smaller than a pixel or so), each piece on the plain surface plus the rings. A ring has faded to nothing where the cut cells meet the others, so no crack shows.
//
// Coordinates are pbrt's, as a film file's. Every frame is a function of its time alone.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace stir {

constexpr double kG = 9.81;

// A ring: where and when (nature's time), how big the thing that made it (metres), how high its waves start, how fast they fade (seconds). `splash`: drops are thrown up.
struct Ring {
	double x = 0.0, z = 0.0, t0 = 0.0, size = 0.05, height = 0.01, life = 2.0;
	bool splash = false;
};
inline double lifeOf(double size) { return 1.0 + 3.0 * std::sqrt(size / 0.05); }
inline Ring ringOf(double x, double z, double t0, double size, double height, bool splash = false) { return {x, z, t0, size, height, lifeOf(size), splash}; }
// Churned water: a patch `radius` across that fades over `life` seconds (it lifts the sea's fold: foam).
struct Froth {
	double x = 0.0, z = 0.0, t0 = 0.0, radius = 1.0, life = 4.0;
};

// (`ripple X Z at T [size S]`, `skipstone X Z toward A at T ...`)
struct Plop {
	double x = 0.0, z = 0.0, at = 0.0, size = 0.05;
};
struct Skip {
	double x = 0.0, z = 0.0, toward = 0.0, at = 0.0, speed = 9.0, height = 1.0;
	int bounces = 6, seed = 1;
};

// How far out the waves of a ring have got at `age`, and inside what they have all gone by.
inline double reachOf(const Ring &q, double age) { return age * std::sqrt(2.5 * kG * q.size) + 4.0 * q.size; }
inline double insideOf(const Ring &q, double age) { return std::max(0.0, age * std::sqrt(kG * q.size / 36.0) - 4.0 * q.size); }
inline bool alive(const Ring &q, double t) { return t > q.t0 && t < q.t0 + 5.0 * q.life; }

// A ring's height at (x, z) and its slopes, added in.
inline void addRing(const Ring &q, double x, double z, double t, double &h, double &sx, double &sz) {
	const double age = t - q.t0;
	if (age <= 0.0) return;
	const double dx = x - q.x, dz = z - q.z, r = std::max(1e-3, std::sqrt(dx * dx + dz * dz));
	const double fade = q.height * std::exp(-age / q.life);
	if (age < 0.7) {   // the hollow at the place, filling and bouncing
		const double w = r / (2.0 * q.size);
		if (w < 3.0) {
			const double e = std::exp(-w * w), c = std::cos(0.8 * age * std::sqrt(kG / q.size)) * std::exp(-age / 0.22), a = -2.0 * q.height * e * c;
			h += a;
			const double dr = -2.0 * w / (2.0 * q.size) * a;
			sx += dr * dx / r, sz += dr * dz / r;
		}
	}
	const double k = kG * age * age / (4.0 * r * r), u = k * q.size;
	if (u >= 6.0 || u <= 0.1) return;
	const double phase = kG * age * age / (4.0 * r), e = u * u * std::exp(1.0 - u * u) * smoothstep(0.1, 0.2, u), a = fade * std::sqrt(3.0 * q.size / (3.0 * q.size + r)) * e;
	h += a * std::sin(phase);
	const double dr = -a * k * std::cos(phase);
	sx += dr * dx / r, sz += dr * dz / r;
}

// The rain's rings at (x, z), `amount` 0 to 1: a drop in each half-metre cell, more often the harder it rains. The same every time it is asked, for any t.
constexpr double kRainCell = 0.5, kRainReach = 16.0;   // (no further from the camera than this: past that a ring is a pixel)
inline void addRain(double amount, double x, double z, double t, double &h, double &sx, double &sz) {
	const double P = 0.45 / std::max(0.05, amount);
	const int ix = static_cast<int>(std::floor(x / kRainCell)), iz = static_cast<int>(std::floor(z / kRainCell));
	for (int a = ix - 1; a <= ix + 1; ++a)
		for (int b = iz - 1; b <= iz + 1; ++b) {
			const int key = static_cast<int>((static_cast<unsigned>(a) * 73856093u ^ static_cast<unsigned>(b) * 19349663u) & 0x7fffffffu);
			const double shift = P * hash01(key, 1);
			const double n = std::floor((t - shift) / P);
			for (int back = 0; back < 3; ++back) {
				const double t0 = shift + (n - back) * P;
				if (t - t0 > 0.9) break;
				const int k = key + static_cast<int>(n - back) * 7;
				Ring q;
				q.x = (a + hash01(k, 2)) * kRainCell, q.z = (b + hash01(k, 3)) * kRainCell, q.t0 = t0, q.size = 0.018 + 0.012 * hash01(k, 4), q.height = 0.009, q.life = 0.42;
				addRing(q, x, z, t, h, sx, sz);
			}
		}
}

struct Field {
	std::vector<Ring> rings;
	std::vector<Froth> froth;
	double rain = 0.0;   // (0 to 1)
	double eyeX = 0.0, eyeZ = 0.0;

	bool any() const { return !rings.empty() || !froth.empty() || rain > 0.0; }
	// Keep only what is still there at t.
	void trim(double t) {
		rings.erase(std::remove_if(rings.begin(), rings.end(), [t](const Ring &q) { return !alive(q, t); }), rings.end());
		froth.erase(std::remove_if(froth.begin(), froth.end(), [t](const Froth &f) { return t < f.t0 || t > f.t0 + 3.0 * f.life; }), froth.end());
	}
	// The rings whose waves are somewhere in the circle round (x, z).
	std::vector<int> touching(double x, double z, double radius, double t) const {
		std::vector<int> out;
		for (std::size_t n = 0; n < rings.size(); ++n) {
			const Ring &q = rings[n];
			const double age = t - q.t0, d = std::hypot(x - q.x, z - q.z);
			if (d - radius < reachOf(q, age) && d + radius > insideOf(q, age)) out.push_back(static_cast<int>(n));
		}
		return out;
	}
	void at(const std::vector<int> &near_, double x, double z, double t, double &h, double &sx, double &sz) const {
		for (int n : near_) addRing(rings[static_cast<std::size_t>(n)], x, z, t, h, sx, sz);
		if (rain > 0.0 && std::hypot(x - eyeX, z - eyeZ) < kRainReach) {
			double rh = 0.0, rx = 0.0, rz = 0.0;
			addRain(rain, x, z, t, rh, rx, rz);
			const double fade = smoothstep(kRainReach, 0.8 * kRainReach, std::hypot(x - eyeX, z - eyeZ));
			h += rh * fade, sx += rx * fade, sz += rz * fade;
		}
	}
	// Plain height and slopes at a point (a floating thing's: how the water lifts and tips it).
	void at(double x, double z, double t, double &h, double &sx, double &sz) const {
		for (const Ring &q : rings) addRing(q, x, z, t, h, sx, sz);
	}
	// How churned the water is at (x, z): for the sea's fold.
	double foldAt(double x, double z, double t) const {
		double f = 0.0;
		for (const Froth &p : froth) {
			const double d = std::hypot(x - p.x, z - p.z) / (p.radius * (1.0 + 0.25 * (t - p.t0)));
			if (d < 2.0) f = std::max(f, 1.25 * std::exp(-d * d) * std::exp(-(t - p.t0) / p.life));
		}
		return f;
	}
};

// One cell of a surface's grid, in the terms refineGrid asks for: its middle and how far from there its corners are, its size along and across, how far it is from the camera.
struct Cell {
	double x = 0.0, z = 0.0, radius = 0.0, along = 0.0, across = 0.0, away = 0.0;
};
// Its corners: p[0], p[1] along u, p[2] at (1, 1), p[3] along v; points and normals in story space, as a mesh's.
struct Quad {
	Vec3 p[4], n[4];
};

constexpr double kFineBudget = 240000.0;   // (points a frame, at most: about 20 MB of scene text)

// Cuts the cells of a rows x cols grid that rings touch. `cellAt(j, i)` a Cell, `quadAt(j, i)` a Quad, `restAt(j, i, u, v, x, z)` the pbrt (x, z) at rest of a point in the cell, `meshOf(j, i)`
// the mesh its pieces go in. Gives back which cells it cut, so the caller leaves those out of the coarse mesh.
template <class CellAt, class QuadAt, class RestAt, class MeshOf>
inline std::vector<char> refineGrid(const Field &f, double t, int rows, int cols, CellAt cellAt, QuadAt quadAt, RestAt restAt, MeshOf meshOf) {
	struct Plan {
		int j, i;
		double spacing, along, across;
		std::vector<int> here;
	};
	std::vector<char> cut(static_cast<std::size_t>(rows) * cols, 0);
	std::vector<Plan> plans;
	double wanted = 0.0;
	for (int j = 0; j < rows; ++j)
		for (int i = 0; i < cols; ++i) {
			const Cell c = cellAt(j, i);
			std::vector<int> here = f.touching(c.x, c.z, c.radius, t);
			const bool rainy = f.rain > 0.0 && std::hypot(c.x - f.eyeX, c.z - f.eyeZ) < kRainReach + c.radius;
			if (here.empty() && !rainy) continue;
			double finest = rainy ? 0.03 : 1e9;
			for (int n : here) finest = std::min(finest, f.rings[static_cast<std::size_t>(n)].size);
			const double spacing = std::max(0.8 * finest, 0.0012 * c.away);
			wanted += (c.along / spacing + 1.0) * (c.across / spacing + 1.0);
			plans.push_back({j, i, spacing, c.along, c.across, std::move(here)});
		}
	const double stretch = std::max(1.0, std::sqrt(wanted / kFineBudget));
	const auto steps = [stretch](double length, double spacing) {   // (a power of two, so neighbours' points mostly agree)
		const int want = static_cast<int>(std::ceil(length / (spacing * stretch)));
		int k = 1;
		while (k < want && k < 64) k *= 2;
		return k;
	};
	for (const Plan &p : plans) {
		cut[static_cast<std::size_t>(p.j) * cols + p.i] = 1;
		const Quad q = quadAt(p.j, p.i);
		const int ku = steps(p.along, p.spacing), kv = steps(p.across, p.spacing);
		Mesh &m = meshOf(p.j, p.i);
		const int base = static_cast<int>(m.verts.size());
		for (int b = 0; b <= kv; ++b)
			for (int a = 0; a <= ku; ++a) {
				const double u = a / double(ku), v = b / double(kv);
				const Vec3 pt = lerp(lerp(q.p[0], q.p[1], u), lerp(q.p[3], q.p[2], u), v), nm = lerp(lerp(q.n[0], q.n[1], u), lerp(q.n[3], q.n[2], u), v);
				double x = 0.0, z = 0.0, h = 0.0, sx = 0.0, sz = 0.0;
				restAt(p.j, p.i, u, v, x, z);
				f.at(p.here, x, z, t, h, sx, sz);
				m.verts.push_back({pt.x, pt.y + h, pt.z});
				m.normals.push_back(normalize(nm + Vec3{sx, 0.0, -sz}));   // (story space: x mirrored)
			}
		const int w = ku + 1;
		for (int b = 0; b < kv; ++b)
			for (int a = 0; a < ku; ++a) {
				const int k = base + b * w + a;
				m.tris.push_back({k, k + 1, k + w});
				m.tris.push_back({k + 1, k + w + 1, k + w});
			}
	}
	return cut;
}

// ---- the things themselves ----

// A wake's rings: `at(t)` where it is (pbrt's x, z in .x, .z), from t0 to t (nature's) a ring a tenth of a second, as big as the waves `speed` makes (their length 2 pi U^2 / g) or
// `size` if that is smaller; rings are only kept as far back as they can still be seen. Height: how high its waves stand at the stern.
template <class At>
inline void trail(Field &f, At at, double t0, double t1, double t, double speed, double size, double height, double every = 0.1) {
	const double s = std::clamp(speed * speed / kG, 0.04, size), life = lifeOf(s);
	const double first = std::max(t0, t - 3.0 * life), last = std::min(t, t1);
	if (last < first) return;
	const int n0 = static_cast<int>(std::ceil(first / every)), n1 = static_cast<int>(std::floor(last / every));
	for (int n = n0; n <= n1; ++n) {
		const double u = n * every;
		const Vec3 p = at(u);
		f.rings.push_back({p.x, p.z, u, s, 0.5 * height * every / 0.08, life, false});
	}
}

// Where a thrown stone touches: its first touch where it comes down from its height, each hop shorter and quicker than the last, the last one where it sinks. t in nature's time.
struct Hop {
	double x, z, t, air;
};
inline std::vector<Hop> hopsOf(const Skip &s) {
	std::vector<Hop> out;
	const double a = s.toward * kPi / 180.0, dx = std::sin(a), dz = std::cos(a);
	double when = s.at + std::sqrt(2.0 * s.height / kG), d = s.speed * (when - s.at), v = s.speed;
	for (int n = 0; n < s.bounces; ++n) {
		const double air = n + 1 < s.bounces ? 0.05 * v * (1.0 - 0.04 * n) : 0.0;
		out.push_back({s.x + dx * d, s.z + dz * d, when, air});
		v *= 0.82;
		d += v * air, when += air;
	}
	return out;
}
inline void addSkip(Field &f, const Skip &s, const std::function<double(double)> &natureAt) {
	const double shift = natureAt(s.at) - s.at;
	const std::vector<Hop> hops = hopsOf(s);
	for (std::size_t n = 0; n < hops.size(); ++n) {
		const bool last = n + 1 == hops.size();
		f.rings.push_back(ringOf(hops[n].x, hops[n].z, hops[n].t + shift, last ? 0.05 : 0.03, last ? 0.008 : 0.005, true));
	}
}
// The stone in the air, as scene text ("" once it has gone in).
inline std::string skipText(const Skip &s, double t, double level) {
	const double ft = t;
	if (ft < s.at) return std::string();
	const double a = s.toward * kPi / 180.0, dx = std::sin(a), dz = std::cos(a);
	const std::vector<Hop> hops = hopsOf(s);
	double x, y, z;
	if (ft < hops[0].t) {
		const double u = ft - s.at;
		x = s.x + dx * s.speed * u, z = s.z + dz * s.speed * u, y = level + s.height - 0.5 * kG * u * u;
	} else {
		std::size_t n = 0;
		while (n + 1 < hops.size() && ft >= hops[n + 1].t) ++n;
		if (n + 1 == hops.size()) return std::string();
		const double u = ft - hops[n].t, air = hops[n].air, w = u / air;
		x = lerp(hops[n].x, hops[n + 1].x, w), z = lerp(hops[n].z, hops[n + 1].z, w), y = level + 0.5 * kG * u * (air - u) + 0.01;
	}
	Mesh stone = uvSphere(1.0, 16, 8);
	const Mat4 m = mul(translate(-x, y, z), rotate({0.0, 1.0, 0.0}, 2000.0 * ft), scale(0.04, 0.012, 0.03));
	return "# a skipping stone (film_stir.h)\n" + attr(diffuseMaterial({0.34, 0.33, 0.31}), emitMesh(stone, m));
}

// The drops a touch throws up, `age` seconds after it, on the water at `level`: a crown of them from the rim of the hollow, and a jet from its middle as it closes (the thing's
// size says how high). Water drops, as spheres.
inline std::string splashDrops(const Ring &q, double t, double level) {
	const double age = t - q.t0;
	if (age < 0.0 || age > 1.4) return std::string();
	std::string out;
	char buf[160];
	const auto drop = [&](double x, double y, double z, double r) {
		if (y < level) return;
		std::snprintf(buf, sizeof buf, "  AttributeBegin\n    Translate %.4f %.4f %.4f\n    Shape \"sphere\" \"float radius\" [ %.4f ]\n  AttributeEnd\n", x, y, z, r);
		out += buf;
	};
	const double v = 1.1 * std::sqrt(kG * q.size * 12.0), rim = 2.0 * q.size;
	const int seed = static_cast<int>(std::fmod(std::abs(q.t0) * 1000.0 + std::abs(q.x) * 37.0, 1e6));
	const int count = std::clamp(static_cast<int>(600.0 * q.size), 10, 90);
	for (int n = 0; n < count; ++n) {
		const double a = 2.0 * kPi * (n + 0.6 * hash01(seed + n, 1)) / count, up = v * (0.55 + 0.6 * hash01(seed + n, 2)), out_ = v * (0.3 + 0.35 * hash01(seed + n, 3));
		const double u = age * (0.8 + 0.3 * hash01(seed + n, 4));
		drop(q.x + std::cos(a) * (rim + out_ * u), level + up * u - 0.5 * kG * u * u, q.z + std::sin(a) * (rim + out_ * u), 0.05 * q.size + 0.1 * q.size * hash01(seed + n, 5));
	}
	if (age > 0.16) {   // the jet: up from the middle once the hollow has closed, a drop pinched off its top
		const double u = age - 0.16, rise = 0.9 * std::sqrt(kG * q.size * 10.0);
		const double top = rise * u - 0.5 * kG * u * u;
		for (int k = 0; k < 5 && top > 0.0; ++k) drop(q.x, level + top * k / 5.0, q.z, q.size * (0.4 - 0.05 * k));
		if (u > 0.08) drop(q.x, level + top * 1.25, q.z, 0.3 * q.size);
	}
	return out;
}
template <class LevelAt>
inline std::string splashText(const Field &f, double t, LevelAt levelAt) {
	std::string drops;
	for (const Ring &q : f.rings)
		if (q.splash) drops += splashDrops(q, t, levelAt(q.x, q.z));
	return drops.empty() ? std::string() : "# splashes (film_stir.h)\nAttributeBegin\n  Material \"dielectric\" \"float eta\" [ 1.33 ]\n" + drops + "AttributeEnd\n";
}

}  // namespace stir
}  // namespace cartoon
