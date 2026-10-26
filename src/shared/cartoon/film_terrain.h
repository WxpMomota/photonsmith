#pragma once
// film_terrain.h -- a landscape for a film file (film_file.h `terrain`): hills, mountains, dunes, a canyon of mesas or a coast, carved by running water (droplet erosion), coloured by
// what lies where (grass in the valleys, darker where water gathers, rock on the steep faces, scree under them, snow above the snow line on the slopes it can hold to, sand, the
// red bands of a canyon's walls), finer near where the film happens, and ringed by distant ranges that fade into the haze.
//
// What it is made of:
//   the field      a grid of heights (257 x 257 over the square) made from noise of the kind's shape, then eroded by tens of thousands of droplets that run downhill, pick up
//                  ground where they speed up and drop it where they slow; where they ran most is the field's `flow` (gullies, the beds of streams);
//   the surface    the field smoothed (Catmull-Rom) with a little fine detail on top, as one mesh whose rows and columns crowd round the focus (a few tens of centimetres
//                  apart there, metres at the edge), written once as a PLY file in the film's cache (film_cache.h);
//   the colours    one painted picture over the whole square (film_sky::picture), from the height, the slope and the flow at each point;
//   the far ranges a ring of mountains (or hills, dunes, mesas) beyond the square, out to four times its size, coloured towards the blue-grey of distance.
//
// Coordinates are pbrt's, as a film file's (x is mirrored into story space where the mesh is made). A terrain's heights are known to the film (heightAt): things placed or
// scattered on it stand on it, and walks find their way over it (film_paths.h reads its mesh like any set's floor).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "film_cache.h"
#include "film_sky.h"
#include "nature_lib.h"

namespace cartoon {
namespace terrain {

struct Spec {
	std::string kind = "hills";
	double size = 600.0, height = 60.0;   // the square's side and its highest ground above `y` (metres)
	double x = 0.0, z = 0.0, y = 0.0;     // its centre (pbrt's x z) and its base height
	bool ySet = false;                    // (whether the film gave `y`: a coast without it is put so its beach is at the sea's level)
	double focusX = 0.0, focusZ = 0.0;    // where the film happens: the mesh is finest there, and mountains and canyons leave a floor round it
	bool focusSet = false;
	double snow = -1.0;                   // the snow line, metres above `y` (below 0: the kind's own - mountains at 0.45 of the height, none for the others)
	double erosion = -1.0;                // how much water has carved it (0 to 3; below 0: the kind's own)
	int seed = 1;
	bool ranges = true;
	// rivers' channels, cut into the ground under them (set by the film from its rivers): each a path of points (pbrt's x z, and the water's height there) and a width
	struct Channel {
		std::vector<Vec3> path;
		double width = 8.0;
	};
	std::vector<Channel> channels;
	// waterfalls' cliffs (set by the film from its waterfalls): the ground behind each lip raised to it (a plateau whose front is the cliff), and a basin below for its pool
	struct Step {
		Vec3 lip;   // pbrt's x y z
		double width = 6.0, drop = 20.0, facing = 0.0;
	};
	std::vector<Step> steps;
	// volcanoes (set by the film from its `volcano` lines): a cone raised to `height` above the land's base over `radius`, a crater at its top
	struct Cone {
		double x = 0.0, z = 0.0, height = 400.0, radius = 800.0;
	};
	std::vector<Cone> cones;
	double waterLevel = -1e30;   // a sea's or lake's level (pbrt's y), set by the film when it has one: the ground just above it is drawn under it (a clean shore, not a
	                             // scatter of islands as small as the fine detail), and the shore is sand and mud                      // the distant ranges round it
	double fx() const { return focusSet ? focusX : x; }
	double fz() const { return focusSet ? focusZ : z; }
	double snowLine() const { return snow >= 0.0 ? snow : kind == "mountains" ? 0.45 * height : 1e9; }
	double erosionAmount() const { return erosion >= 0.0 ? erosion : kind == "mountains" ? 1.0 : kind == "hills" ? 0.7 : kind == "canyon" ? 0.5 : 0.0; }
	// every number it is made from: names its files
	std::string key() const {
		char b[320];
		std::snprintf(b, sizeof b, "v1|%s|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%.3f|%d|%d|%.3f", kind.c_str(), size, height, x, z, y, fx(), fz(), snowLine(), erosionAmount(), seed, ranges ? 1 : 0,
					  waterLevel > -1e29 ? waterLevel : -1.0);
		std::string all = b;
		for (const Channel &c : channels) {
			char w[32];
			std::snprintf(w, sizeof w, "|c%.2f", c.width);
			all += w;
			for (const Vec3 &p : c.path) {
				std::snprintf(w, sizeof w, ",%.1f %.2f %.1f", p.x, p.y, p.z);
				all += w;
			}
		}
		for (const Cone &c : cones) {
			char w[96];
			std::snprintf(w, sizeof w, "|v%.2f %.2f %.2f %.2f", c.x, c.z, c.height, c.radius);
			all += w;
		}
		for (const Step &st : steps) {
			char w[96];
			std::snprintf(w, sizeof w, "|s%.2f %.2f %.2f %.2f %.2f %.2f", st.lip.x, st.lip.y, st.lip.z, st.width, st.drop, st.facing);
			all += w;
		}
		char name[40];
		std::snprintf(name, sizeof name, "terrain_%016llx", static_cast<unsigned long long>(std::hash<std::string>()(all)));
		return name;
	}
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"hills", "mountains", "dunes", "canyon", "coast"};
	return k;
}

namespace detail {

// Gradient (Perlin) noise in [0, 1): smoother and less blocky than value noise, whose lattice shows as cones and pyramids once it is folded into ridges.
inline double perlin(double x, double z, int salt) {
	const double fx = std::floor(x), fz = std::floor(z);
	const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
	const double u = x - fx, v = z - fz;
	const auto grad = [salt](int a, int b, double dx, double dz) {
		const double ang = 2.0 * kPi * hash01(a * 73856093 ^ b * 19349663, salt);
		return std::cos(ang) * dx + std::sin(ang) * dz;
	};
	const auto fade = [](double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); };
	const double su = fade(u), sv = fade(v);
	const double n = lerp(lerp(grad(ix, iz, u, v), grad(ix + 1, iz, u - 1, v), su), lerp(grad(ix, iz + 1, u, v - 1), grad(ix + 1, iz + 1, u - 1, v - 1), su), sv);
	return std::clamp(0.5 + 0.75 * n, 0.0, 1.0);
}
inline double ridged(double u, double v, int salt, int octaves) {   // ridged multifractal in [0, ~1]: sharp crests, smooth valleys
	double sum = 0.0, amp = 0.5, freq = 1.0, weight = 1.0, total = 0.0;
	for (int o = 0; o < octaves; ++o) {
		double r = 1.0 - std::abs(2.0 * perlin(u * freq, v * freq, salt + 131 * o) - 1.0);
		r = r * r * weight;
		weight = std::min(1.0, std::max(0.0, r * 1.8));
		sum += r * amp;
		total += amp;
		freq *= 2.07;
		amp *= 0.5;
	}
	return sum / total;
}
inline double fbm(double u, double v, int salt, int octaves) {
	double sum = 0.0, amp = 0.5, total = 0.0;
	for (int o = 0; o < octaves; ++o) {
		sum += amp * perlin(u, v, salt + 97 * o);
		total += amp;
		u *= 2.03;
		v *= 2.03;
		amp *= 0.5;
	}
	return sum / total;
}
inline double cr(double p0, double p1, double p2, double p3, double t) {   // Catmull-Rom
	return p1 + 0.5 * t * (p2 - p0 + t * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + t * (3.0 * (p1 - p2) + p3 - p0)));
}

// Where a canyon's river runs: its u (across the square, 0 to 1) at v (along it), winding through the focus `fu`.
inline double canyonRiverU(const Spec &s, double v, double fu) {
	const int salt = s.seed * 7717;
	return fu + 0.09 * std::sin(v * 9.0 + 6.0 * hash01(salt, 7)) + 0.05 * std::sin(v * 21.0 + 2.0);
}

// The kind's shape at (u, v) over the square (0 to 1 each way), before it is scaled to the height. `fu`, `fv`: the focus.
inline double shape(const Spec &s, double u, double v, double fu, double fv) {
	const int salt = s.seed * 7717;
	const double df = std::hypot(u - fu, v - fv);
	if (s.kind == "mountains") {
		// ridges warped so they bend and branch, gathered into massifs, with a valley floor round the focus
		const double wu = u + 0.18 * (fbm(u * 2.0, v * 2.0, salt + 21, 3) - 0.5), wv = v + 0.18 * (fbm(u * 2.0 + 5.2, v * 2.0, salt + 22, 3) - 0.5);
		const double massif = smoothstep(0.25, 0.75, fbm(wu * 1.3 + 3.7, wv * 1.3, salt + 5, 3));
		const double range = ridged(wu * 1.7, wv * 1.7, salt, 5) * (0.35 + 0.65 * massif) + 0.3 * massif;
		return range * (0.04 + 0.96 * smoothstep(0.035, 0.2, df));
	}
	if (s.kind == "dunes") {
		const double a = 2.0 * kPi * hash01(salt, 3);
		const double w = 0.55 * fbm(u * 2.0, v * 2.0, salt + 9, 3);
		const double along = (u * std::cos(a) + v * std::sin(a)) * 7.0 + w * 3.0;
		const double p = along - std::floor(along);
		const double saw = p < 0.78 ? smoothstep(0.0, 0.78, p) : 1.0 - smoothstep(0.78, 1.0, p);   // (a gentle windward face, a steep slip face)
		return 0.25 * fbm(u * 1.4, v * 1.4, salt + 2, 3) + saw * (0.35 + 0.65 * fbm(u * 1.6 + 2.1, v * 1.6, salt + 4, 3));
	}
	if (s.kind == "canyon") {
		const double plateau = fbm(u * 2.6, v * 2.6, salt + 1, 5);
		const double q = plateau * 5.0, k = std::floor(q), fr = q - k;
		double h = (k + smoothstep(0.55, 0.92, fr)) / 5.0;   // mesas: flat tops, cliffs between
		// a river winding through the focus
		const double d = std::abs(u - canyonRiverU(s, v, fu));
		h *= 0.04 + 0.96 * smoothstep(0.012, 0.07, d);
		return h;
	}
	if (s.kind == "coast") {
		// the sea floor on the -z side rising to a beach at a quarter of the height (Dean's profile, d ~ x^(2/3), with a sandbar off it), the shore bending into bays, rocky
		// headlands pushed out where the cliffs drop straight into deeper water, and hills behind the beach
		const double head = smoothstep(0.58, 0.72, fbm(u * 3.0 + 1.3, 0.2, salt + 33, 3));
		const double shore = 0.45 + 0.2 * (fbm(u * 1.6, 0.5, salt + 31, 3) - 0.5) - 0.08 * head;
		const double sd = v - shore;   // (below 0: under the sea)
		double h;
		if (sd < 0.0) {
			const double dean = 0.25 * (1.0 - std::pow(std::min(1.0, -sd / 0.35), 0.67)) + 0.03 * std::exp(-std::pow((-sd - 0.07) / 0.018, 2.0));
			h = lerp(dean, 0.25 * (1.0 - smoothstep(0.0, 0.08, -sd)), head);
		} else {
			const double hills = 0.4 + 0.6 * fbm(u * 2.4, v * 2.4, salt, 4);
			h = 0.25 + 0.05 * (1.0 - head) * smoothstep(0.0, 0.03, sd) + 0.7 * hills * smoothstep(0.03 * (1.0 - head), lerp(0.35, 0.08, head), sd);
		}
		return std::clamp(h, 0.0, 1.0);
	}
	// hills
	const double f = fbm(u * 2.4, v * 2.4, salt, 4);
	return std::pow(std::max(0.0, f - 0.2), 1.25) * (0.6 + 0.4 * smoothstep(0.02, 0.2, df));
}

// Droplet erosion (as Sebastian Lague's, after Hans Theobald Beyer's thesis) on an n x n grid of heights in [0, 1]: `drops` droplets, each run downhill up to 30 steps.
// `flow` collects how much water passed each point.
inline void erode(std::vector<float> &h, std::vector<float> &flow, int n, int drops, int salt) {
	const double inertia = 0.05, capacityFactor = 4.0, minCapacity = 0.01, depositSpeed = 0.3, erodeSpeed = 0.15, evaporate = 0.02, gravity = 4.0;
	const auto heightGrad = [&](double x, double y, double &gx, double &gy) {
		const int ix = static_cast<int>(x), iy = static_cast<int>(y);
		const double u = x - ix, v = y - iy;
		const std::size_t i = static_cast<std::size_t>(iy) * n + ix;
		const double a = h[i], b = h[i + 1], c = h[i + n], d = h[i + n + 1];
		gx = (b - a) * (1 - v) + (d - c) * v;
		gy = (c - a) * (1 - u) + (d - b) * u;
		return a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v + d * u * v;
	};
	for (int k = 0; k < drops; ++k) {
		double x = 1.0 + (n - 3) * hash01(salt + k, 11), y = 1.0 + (n - 3) * hash01(salt + k, 12);
		double dx = 0.0, dy = 0.0, speed = 1.0, water = 1.0, sediment = 0.0;
		for (int step = 0; step < 30; ++step) {
			const int ix = static_cast<int>(x), iy = static_cast<int>(y);
			const double u = x - ix, v = y - iy;
			double gx = 0.0, gy = 0.0;
			const double hOld = heightGrad(x, y, gx, gy);
			dx = dx * inertia - gx * (1 - inertia);
			dy = dy * inertia - gy * (1 - inertia);
			const double len = std::hypot(dx, dy);
			if (len < 1e-12) break;
			dx /= len;
			dy /= len;
			x += dx;
			y += dy;
			if (x < 1.0 || y < 1.0 || x >= n - 2 || y >= n - 2) break;
			double ngx = 0.0, ngy = 0.0;
			const double dh = heightGrad(x, y, ngx, ngy) - hOld;
			const std::size_t i = static_cast<std::size_t>(iy) * n + ix;
			flow[i] += static_cast<float>(water);
			const double capacity = std::max(-dh * speed * water * capacityFactor, minCapacity * 0.01);
			const double w[4] = {(1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v};
			const std::size_t at[4] = {i, i + 1, i + n, i + n + 1};
			if (sediment > capacity || dh > 0) {
				const double amount = dh > 0 ? std::min(dh, sediment) : (sediment - capacity) * depositSpeed;
				sediment -= amount;
				for (int c = 0; c < 4; ++c) h[at[c]] += static_cast<float>(amount * w[c]);
			} else {
				const double amount = std::min((capacity - sediment) * erodeSpeed, -dh);
				// from a 3 x 3 brush, so the bed is a channel, not a row of pits
				double total = 0.0;
				for (int b = -1; b <= 1; ++b)
					for (int a = -1; a <= 1; ++a) total += 1.0 / (1.0 + a * a + b * b);
				for (int b = -1; b <= 1; ++b)
					for (int a = -1; a <= 1; ++a) {
						const std::size_t j = static_cast<std::size_t>(iy + b) * n + (ix + a);
						const double take = std::min(static_cast<double>(h[j]), amount * (1.0 / (1.0 + a * a + b * b)) / total);
						h[j] -= static_cast<float>(take);
					}
				sediment += amount;
			}
			speed = std::sqrt(std::max(0.0, speed * speed - dh * gravity * 8.0));
			water *= 1.0 - evaporate;
		}
	}
}

}  // namespace detail

// The terrain's heights, worked out once for a Spec (and shared by every frame of the process).
struct Field {
	Spec spec;
	static constexpr int kMargin = 8;          // samples beyond the square on every side (so its edge has neighbours: no false slope there)
	int n = 256 + 1 + 2 * kMargin;
	double cell = 1.0, x0 = 0.0, z0 = 0.0;   // metres between samples; pbrt's x z of sample (0, 0)
	double sx0 = 0.0, sz0 = 0.0;             // pbrt's x z of the square's corner (where the colour picture's u and v are 0)
	std::vector<float> h, flow;              // n x n: metres above the base; how much water ran there (0 to 1)
	double lo = 0.0, hi = 1.0;               // the shape's lowest and highest value over the square (what became 0 and the height: the far ranges are scaled the same way)

	double sample(int i, int j) const { return h[static_cast<std::size_t>(std::clamp(j, 0, n - 1)) * n + std::clamp(i, 0, n - 1)]; }
	double flowAt(double px, double pz) const {
		const double gx = std::clamp((px - x0) / cell, 0.0, n - 1.001), gz = std::clamp((pz - z0) / cell, 0.0, n - 1.001);
		const int i = static_cast<int>(gx), j = static_cast<int>(gz);
		const double u = gx - i, v = gz - j;
		const auto f = [&](int a, int b) { return static_cast<double>(flow[static_cast<std::size_t>(b) * n + a]); };
		return lerp(lerp(f(i, j), f(i + 1, j), u), lerp(f(i, j + 1), f(i + 1, j + 1), u), v);
	}
	// fine detail on top of the field: rough rock on mountains, almost none on sand
	double detail(double px, double pz) const {
		const double amp = spec.kind == "mountains" ? 0.6 : spec.kind == "dunes" ? 0.03 : spec.kind == "canyon" ? 0.3 : spec.kind == "coast" ? 0.06 : 0.25;
		const int salt = spec.seed * 31 + 5;
		return amp * ((detail::fbm(px / 11.0, pz / 11.0, salt, 4) - 0.5) * 1.6 + (nature::noise2(px / 2.3, pz / 2.3, salt + 7) - 0.5) * 0.25);
	}
	// The ground's height (pbrt's y) at pbrt's x z.
	double heightAt(double px, double pz) const {
		const double gx = (px - x0) / cell, gz = (pz - z0) / cell;
		const int i = static_cast<int>(std::floor(gx)), j = static_cast<int>(std::floor(gz));
		const double u = gx - i, v = gz - j;
		double rows[4];
		for (int b = 0; b < 4; ++b) rows[b] = detail::cr(sample(i - 1, j - 1 + b), sample(i, j - 1 + b), sample(i + 1, j - 1 + b), sample(i + 2, j - 1 + b), u);
		const double h = spec.y + std::max(0.0, detail::cr(rows[0], rows[1], rows[2], rows[3], v)) + detail(px, pz);
		if (spec.waterLevel < -1e29) return h;
		const double above = h - spec.waterLevel;   // the shallows pulled under: what was a hand's breadth above the water is below it, the bed a little deeper
		return above > 1.5 ? h : h - smoothstep(1.5, -0.5, above) * (0.8 + 0.5 * std::min(4.0, std::max(0.0, -above)));
	}
	Vec3 normalAt(double px, double pz, double e = 0.4) const {   // (pbrt's)
		return normalize(Vec3{heightAt(px - e, pz) - heightAt(px + e, pz), 2.0 * e, heightAt(px, pz - e) - heightAt(px, pz + e)});
	}
	bool inside(double px, double pz) const { return std::abs(px - spec.x) <= spec.size / 2 && std::abs(pz - spec.z) <= spec.size / 2; }
};

inline std::shared_ptr<const Field> make(const Spec &s) {
	auto f = std::make_shared<Field>();
	f->spec = s;
	const int n = f->n;
	f->cell = s.size / 256.0;
	f->sx0 = s.x - s.size / 2;
	f->sz0 = s.z - s.size / 2;
	f->x0 = f->sx0 - Field::kMargin * f->cell;
	f->z0 = f->sz0 - Field::kMargin * f->cell;
	const double fu = (s.fx() - f->sx0) / s.size, fv = (s.fz() - f->sz0) / s.size;
	std::vector<float> h(static_cast<std::size_t>(n) * n), flow(h.size(), 0.0f);
	double lo = 1e30, hi = -1e30;
	for (int j = 0; j < n; ++j)
		for (int i = 0; i < n; ++i) {
			const double v = detail::shape(s, (i - Field::kMargin) / 256.0, (j - Field::kMargin) / 256.0, fu, fv);
			h[static_cast<std::size_t>(j) * n + i] = static_cast<float>(v);
			lo = std::min(lo, v);
			hi = std::max(hi, v);
		}
	if (s.kind == "coast") lo = 0.0, hi = 1.0;   // (a coast keeps its own scale: its beach is at a quarter of the height)
	for (float &v : h) v = static_cast<float>((v - lo) / std::max(1e-9, hi - lo));
	f->lo = lo;
	f->hi = std::max(lo + 1e-9, hi);
	const double amount = s.erosionAmount();
	if (amount > 0.0) {
		detail::erode(h, flow, n, static_cast<int>(amount * n * n * 0.8), s.seed * 104729);
		double peak = 0.0;
		for (float v : flow) peak = std::max(peak, static_cast<double>(v));
		for (float &v : flow) v = static_cast<float>(std::min(1.0, std::log1p(v) / std::log1p(std::max(1.0, peak * 0.25))));
	}
	for (float &v : h) v = static_cast<float>(std::max(0.0f, v) * s.height);
	for (const Spec::Channel &c : s.channels) {   // the rivers' beds: a trough under each, below its water, and banks rising from it
		const double half = 0.5 * c.width, depth = 1.5 + 0.05 * c.width, bank = std::max(6.0, c.width);
		for (std::size_t k = 0; k + 1 < c.path.size(); ++k) {
			const Vec3 a = c.path[k], b = c.path[k + 1];
			const double reach = half + bank + 2.0 * f->cell;
			const int i0 = std::max(0, static_cast<int>((std::min(a.x, b.x) - reach - f->x0) / f->cell)), i1 = std::min(n - 1, static_cast<int>((std::max(a.x, b.x) + reach - f->x0) / f->cell) + 1);
			const int j0 = std::max(0, static_cast<int>((std::min(a.z, b.z) - reach - f->z0) / f->cell)), j1 = std::min(n - 1, static_cast<int>((std::max(a.z, b.z) + reach - f->z0) / f->cell) + 1);
			const double ex = b.x - a.x, ez = b.z - a.z, ll = std::max(1e-9, ex * ex + ez * ez);
			for (int j = j0; j <= j1; ++j)
				for (int i = i0; i <= i1; ++i) {
					const double px = f->x0 + i * f->cell, pz = f->z0 + j * f->cell;
					const double u = std::clamp(((px - a.x) * ex + (pz - a.z) * ez) / ll, 0.0, 1.0);
					const double d = std::hypot(px - (a.x + u * ex), pz - (a.z + u * ez));
					const double level = a.y + u * (b.y - a.y) - s.y;
					const double target = d < half ? level - depth * (1.0 - 0.6 * (d / half) * (d / half)) : level - 0.4 * depth + (d - half) * 0.8;
					float &v = h[static_cast<std::size_t>(j) * n + i];
					v = static_cast<float>(std::min(static_cast<double>(v), std::max(target, -2.0 * depth)));
				}
		}
	}
	for (const Spec::Cone &c : s.cones)   // a volcano: its cone, steeper towards the top, ridged by noise, a crater in its top
		for (int j = 0; j < n; ++j)
			for (int i = 0; i < n; ++i) {
				const double dx = f->x0 + i * f->cell - c.x, dz = f->z0 + j * f->cell - c.z, d = std::hypot(dx, dz) / c.radius;
				if (d >= 1.0) continue;
				const double ridge = 1.0 + 0.08 * (nature::noise2(std::atan2(dz, dx) * 4.0, d * 3.0, s.seed + 71) - 0.5);
				double cone = c.height * std::pow(1.0 - d, 1.6) * ridge;
				const double crater = 0.1;   // (the crater: a bowl in the top tenth of the radius)
				if (d < crater) cone = c.height * std::pow(1.0 - crater, 1.6) - c.height * 0.12 * (1.0 - d / crater) * (1.0 - d / crater);
				float &v = h[static_cast<std::size_t>(j) * n + i];
				v = static_cast<float>(std::max(static_cast<double>(v), cone));
			}
	for (const Spec::Step &st : s.steps) {   // a waterfall's cliff: the plateau behind its lip, the basin before it
		const double a = st.facing * kPi / 180.0, ox = std::sin(a), oz = std::cos(a);
		const double reach = std::max(40.0, 4.0 * st.width), wide = 2.5 * st.width, top = st.lip.y - s.y, foot = top - st.drop;
		for (int j = 0; j < n; ++j)
			for (int i = 0; i < n; ++i) {
				const double dx = f->x0 + i * f->cell - st.lip.x, dz = f->z0 + j * f->cell - st.lip.z;
				const double along = dx * ox + dz * oz, across = std::abs(dx * oz - dz * ox);   // (along: in front of the lip, positive)
				float &v = h[static_cast<std::size_t>(j) * n + i];
				if (along <= 0.0) {   // behind: up to the lip, falling away to the land round it (an outline rounded and broken by noise)
					const double d = std::hypot(across / wide, -along / reach) * (0.8 + 0.4 * nature::noise2(dx * 0.04, dz * 0.04, 53)) + (across < 0.6 * st.width ? 0.0 : 0.0);
					const double keep = 1.0 - smoothstep(1.0, 1.9, d);
					v = static_cast<float>(std::max(static_cast<double>(v), lerp(static_cast<double>(v), top - 0.3 * (nature::noise2(dx * 0.1, dz * 0.1, 51) - 0.5), keep)));
				} else {             // in front: down to the pool, rising back to the land beyond it
					const double fall = 0.5 * st.width + 1.5 * st.drop * 0.1;   // (how far out the water lands, about)
					const double keep = (1.0 - smoothstep(fall + st.width, fall + 3.0 * st.width, along)) * (1.0 - smoothstep(0.8 * st.width, 1.8 * st.width, across));
					v = static_cast<float>(std::min(static_cast<double>(v), lerp(static_cast<double>(v), foot - 1.2, keep)));
				}
			}
	}
	f->h = std::move(h);
	f->flow = std::move(flow);
	return f;
}
// The Field of a Spec, made the first time it is asked for in this process.
inline std::shared_ptr<const Field> fieldOf(const Spec &s) {
	static std::mutex m;
	static std::map<std::string, std::shared_ptr<const Field>> made;
	std::lock_guard<std::mutex> hold(m);
	std::shared_ptr<const Field> &f = made[s.key()];
	if (!f) f = make(s);
	return f;
}

// ---- the colours ----
// What lies at pbrt's x z (linear albedo), from its height above the base as a share of the terrain's height (`frac`), how steep it is (`steep`: 1 - the normal's y, 0.29 at
// 45 degrees) and how much water runs there (`wet`).
inline Vec3 paint(const Spec &s, double px, double pz, double frac, double steep, double wet) {
	const int salt = s.seed * 53;
	const double var = detail::fbm(px / 23.0, pz / 23.0, salt, 4), fine = nature::noise2(px / 3.1, pz / 3.1, salt + 3);
	if (s.kind == "dunes") {
		Vec3 sand = lerp(Vec3{0.56, 0.38, 0.22}, Vec3{0.70, 0.52, 0.32}, var);
		sand = sand * (0.92 + 0.12 * fine);
		return lerp(sand, sand * 1.08, smoothstep(0.75, 1.0, frac));   // (paler on the crests)
	}
	if (s.kind == "canyon") {
		static const Vec3 band[6] = {{0.42, 0.18, 0.10}, {0.55, 0.28, 0.14}, {0.48, 0.22, 0.12}, {0.62, 0.40, 0.24}, {0.38, 0.16, 0.10}, {0.58, 0.34, 0.18}};
		const double q = frac * 14.0 + 0.6 * (var - 0.5);
		const int b = static_cast<int>(std::floor(q)) % 6;
		Vec3 rock = lerp(band[(b + 6) % 6], band[(b + 7) % 6], smoothstep(0.7, 1.0, q - std::floor(q)));
		rock = rock * (0.85 + 0.25 * fine);
		const Vec3 scrub = lerp(Vec3{0.30, 0.26, 0.14}, Vec3{0.20, 0.22, 0.10}, var);   // the dry tops and the floor
		Vec3 c = lerp(scrub, rock, smoothstep(0.035, 0.11, steep));
		if (frac < 0.06) c = lerp(Vec3{0.24, 0.26, 0.14}, c, smoothstep(0.02, 0.06, frac));   // (green by the river)
		return c;
	}
	// hills and mountains
	const Vec3 grass = lerp(Vec3{0.075, 0.13, 0.04}, Vec3{0.16, 0.19, 0.07}, var) * (0.9 + 0.2 * fine);
	const Vec3 meadow = Vec3{0.20, 0.21, 0.08} * (0.9 + 0.2 * fine);   // (lighter, yellower patches)
	const Vec3 dry = lerp(Vec3{0.24, 0.21, 0.11}, Vec3{0.20, 0.19, 0.12}, var);
	const Vec3 dark = Vec3{0.045, 0.08, 0.03};   // where the water gathers: thicker growth
	const Vec3 rock = lerp(Vec3{0.20, 0.19, 0.17}, Vec3{0.30, 0.28, 0.25}, fine) * (0.85 + 0.3 * var);
	const Vec3 scree = Vec3{0.33, 0.31, 0.28} * (0.9 + 0.2 * fine);
	Vec3 c = lerp(grass, meadow, smoothstep(0.55, 0.75, detail::fbm(px / 70.0, pz / 70.0, salt + 9, 3)));
	if (s.kind == "mountains") c = lerp(c, dry, smoothstep(0.25, 0.55, frac + 0.15 * (var - 0.5)));
	else c = lerp(c, dry, 0.6 * smoothstep(0.6, 0.8, detail::fbm(px / 140.0, pz / 140.0, salt + 13, 3)));   // (hills: a few patches burnt dry)
	c = lerp(c, dark, 0.5 * smoothstep(0.15, 0.7, wet) * (1.0 - smoothstep(0.4, 0.7, frac)));
	if (s.kind == "mountains") c = lerp(c, scree, smoothstep(0.38, 0.58, frac + 0.1 * (var - 0.5)) * 0.8);   // above the grass: stones
	if (s.kind == "coast") c = lerp(c, Vec3{0.50, 0.44, 0.33} * (0.9 + 0.2 * fine), 1.0 - smoothstep(0.262, 0.285, frac + 0.01 * (var - 0.5)));   // (the beach)
	c = lerp(c, rock, smoothstep(0.12, 0.24, steep + 0.04 * (var - 0.5)));         // the steep faces: bare rock
	const double line = s.snowLine() / std::max(1.0, s.height);
	if (line < 1e8) {   // snow above the line, where it is not too steep to hold
		const double above = smoothstep(line - 0.04, line + 0.06, frac + 0.08 * (var - 0.5));
		const double holds = 1.0 - smoothstep(0.20, 0.34, steep);
		c = lerp(c, Vec3{0.82, 0.84, 0.88} * (0.95 + 0.05 * fine), above * holds);
	}
	return c;
}
// A coast's sandy bed `depth` metres under clear water, as seen through it: the water takes the red first, then the green, until the bed is the deep sea's own colour
// (film_water.h's seaMaterial). `grain` (0 to 1) varies the sand.
inline Vec3 seabed(double depth, double grain) {
	const Vec3 sand = Vec3{0.46, 0.40, 0.30} * (0.9 + 0.2 * grain), deep{0.015, 0.055, 0.07};
	const double d = std::max(0.0, depth);
	return {deep.x + (sand.x - deep.x) * std::exp(-0.9 * d), deep.y + (sand.y - deep.y) * std::exp(-0.35 * d), deep.z + (sand.z - deep.z) * std::exp(-0.25 * d)};
}
// The ground's colour (linear albedo) at pbrt's x z.
inline Vec3 colourAt(const Field &f, double px, double pz) {
	const Spec &s = f.spec;
	const double h = f.heightAt(px, pz), frac = (h - s.y) / std::max(1.0, s.height);
	const Vec3 n = f.normalAt(px, pz, std::max(1.5, f.cell * 0.6));   // (the slope a few metres across: the fine detail does not speckle it with rock)
	Vec3 c = paint(s, px, pz, frac, 1.0 - n.y, f.flowAt(px, pz));
	if (s.waterLevel > -1e29) {   // the shore: pale sand just above the water, darker wet sand at its edge, mud and weed below
		const double above = h - s.waterLevel, grain = nature::noise2(px / 2.0, pz / 2.0, s.seed + 61);
		const Vec3 sand = Vec3{0.46, 0.40, 0.30} * (0.9 + 0.2 * grain), wet = Vec3{0.24, 0.21, 0.16}, bed = Vec3{0.12, 0.13, 0.08} * (0.8 + 0.4 * grain);
		c = lerp(c, sand, 1.0 - smoothstep(0.6, 2.2, above));
		c = lerp(c, wet, 1.0 - smoothstep(-0.1, 0.4, above));
		if (s.kind == "coast") c = lerp(c, seabed(-above, grain), 1.0 - smoothstep(-0.4, 0.0, above));   // (sand under clear water, the water's colour with depth)
		else c = lerp(c, bed, 1.0 - smoothstep(-1.5, -0.3, above));
	}
	return c;
}

// ---- the meshes ----
// The positions along one axis, from `lo` to `hi`, crowded round `focus`: `half` steps each side, a tenth as far apart there as the average.
inline std::vector<double> axis(double lo, double hi, double focus, int half) {
	focus = std::clamp(focus, lo, hi);
	std::vector<double> out;
	const auto side = [&](double length, double sign, bool includeFocus) {
		std::vector<double> d;
		for (int k = includeFocus ? 0 : 1; k <= half; ++k) {
			const double t = k / double(half);
			d.push_back(focus + sign * length * (0.12 * t + 0.88 * t * t * t));
		}
		return d;
	};
	std::vector<double> left = focus - lo > 1e-6 ? side(focus - lo, -1.0, false) : std::vector<double>{};
	std::reverse(left.begin(), left.end());
	out = left;
	out.push_back(focus);
	if (hi - focus > 1e-6) {
		const std::vector<double> right = side(hi - focus, 1.0, false);
		out.insert(out.end(), right.begin(), right.end());
	}
	return out;
}
// The surface, in story space (x mirrored), its (u, v) over the square for the colour picture.
inline Mesh surfaceMesh(const Field &f, int half = 200) {
	const Spec &s = f.spec;
	// story x runs the other way: build the columns over story x so the triangles turn the usual way
	std::vector<double> xs = axis(-(s.x + s.size / 2), -(s.x - s.size / 2), -s.fx(), half);
	const std::vector<double> zs = axis(s.z - s.size / 2, s.z + s.size / 2, s.fz(), half);
	Mesh m;
	const int nx = static_cast<int>(xs.size()), nz = static_cast<int>(zs.size());
	m.verts.reserve(static_cast<std::size_t>(nx) * nz);
	for (int j = 0; j < nz; ++j)
		for (int i = 0; i < nx; ++i) {
			const double px = -xs[i], pz = zs[j];
			const Vec3 n = f.normalAt(px, pz, 0.35);
			m.verts.push_back({xs[i], f.heightAt(px, pz), pz});
			m.normals.push_back({-n.x, n.y, n.z});
			m.uvs.push_back({(px - f.sx0) / s.size, (pz - f.sz0) / s.size});
		}
	for (int j = 0; j + 1 < nz; ++j)
		for (int i = 0; i + 1 < nx; ++i) {
			const int a = j * nx + i;
			m.tris.push_back({a, a + nx, a + 1});
			m.tris.push_back({a + 1, a + nx, a + nx + 1});
		}
	return m;
}
// A part of the surface as a mesh of its own (the vertices its triangles use, renumbered): `which` 0 the ground (the triangles not steeper than `steep`, 1 - the face
// normal's y), 1 the steeper ones that face more along x, 2 those that face more along z. A cliff takes a picture projected from the side it faces.
inline Mesh surfacePart(const Mesh &m, int which, double steep) {
	Mesh out;
	std::vector<int> renumber(m.verts.size(), -1);
	for (const auto &t : m.tris) {
		const Vec3 n = normalize(cross(m.verts[t[1]] - m.verts[t[0]], m.verts[t[2]] - m.verts[t[0]]));
		const int part = 1.0 - std::abs(n.y) <= steep ? 0 : std::abs(n.x) > std::abs(n.z) ? 1 : 2;
		if (part != which) continue;
		std::array<int, 3> nt{};
		for (int c = 0; c < 3; ++c) {
			int &k = renumber[static_cast<std::size_t>(t[c])];
			if (k < 0) {
				k = static_cast<int>(out.verts.size());
				out.verts.push_back(m.verts[t[c]]);
				out.normals.push_back(m.normals[t[c]]);
				out.uvs.push_back(m.uvs[t[c]]);
			}
			nt[c] = k;
		}
		out.tris.push_back(nt);
	}
	return out;
}

// The distant ranges: a ring from the square's edge out to four times its size, the same land carried on beyond the square (its shape without the erosion; mountains grow
// higher with distance). Where it meets the square it takes the terrain's own height (its first row a little inside and a little under, so no sliver of sky shows between
// them). (a, t): the angle round the centre, and 0 at the edge to 1 at the outside.
inline double edgeRadius(const Spec &s, double a) { return 0.5 * s.size / std::max(std::abs(std::cos(a)), std::abs(std::sin(a))); }
inline double ringRadius(const Spec &s, double a, double t) {
	const double rs = edgeRadius(s, a);
	return t < 0.0 ? rs - 0.006 * s.size : rs + (4.0 * s.size - rs) * std::pow(t, 1.7);
}
inline double farHeight(const Field &f, double a, double t) {
	const Spec &s = f.spec;
	const double fu = (s.fx() - f.sx0) / s.size, fv = (s.fz() - f.sz0) / s.size;
	const auto shapeAt = [&](double px, double pz) {   // the land's own shape beyond the square, scaled as the square's was (without its erosion and detail)
		return s.y + s.height * std::max(0.0, (detail::shape(s, (px - f.sx0) / s.size, (pz - f.sz0) / s.size, fu, fv) - f.lo) / (f.hi - f.lo));
	};
	const double r = ringRadius(s, a, t), px = s.x + r * std::cos(a), pz = s.z + r * std::sin(a);
	const double rs = edgeRadius(s, a) * 0.9999, ex = s.x + rs * std::cos(a), ez = s.z + rs * std::sin(a);
	// the shape carried on, with the square's difference from it at the edge (its erosion, its detail) faded out over the first stretch: no crease, no step
	const double offset = f.heightAt(ex, ez) - shapeAt(ex, ez);
	if (t < 0.0) return f.heightAt(px, pz) - (0.002 * s.size + 0.3);   // (the first row: inside the square and under it)
	const double grow = s.kind == "mountains" ? 1.0 + 0.8 * smoothstep(0.1, 0.6, t) : 1.0;
	return s.y + (shapeAt(px, pz) - s.y) * grow + offset * (1.0 - smoothstep(0.0, 0.16, t));
}
inline Vec3 farPoint(const Field &f, double a, double t) {
	const double r = ringRadius(f.spec, a, t);
	return {f.spec.x + r * std::cos(a), farHeight(f, a, t), f.spec.z + r * std::sin(a)};
}
// The far ranges' height at pbrt's x z (outside the square), and their normal there from neighbours a few metres away each way: steps in (a, t) are uneven near the edge,
// where the ring blends from the square's edge into its own shape, and a normal taken from them shaded the first rows as a line.
inline double farHeightAt(const Field &f, double px, double pz) {
	const Spec &s = f.spec;
	const double a = std::atan2(pz - s.z, px - s.x), rs = edgeRadius(s, a), r = std::hypot(px - s.x, pz - s.z);
	const double t = std::pow(std::clamp((r - rs) / (4.0 * s.size - rs), 0.0, 1.0), 1.0 / 1.7);
	return farHeight(f, a, t);
}
inline Vec3 farNormal(const Field &f, double a, double t) {   // (pbrt's)
	const Vec3 p = farPoint(f, a, std::max(0.0, t));
	const double e = std::max(1.0, 0.004 * std::hypot(p.x - f.spec.x, p.z - f.spec.z));
	const Vec3 n = normalize(Vec3{farHeightAt(f, p.x - e, p.z) - farHeightAt(f, p.x + e, p.z), 2.0 * e, farHeightAt(f, p.x, p.z - e) - farHeightAt(f, p.x, p.z + e)});
	return n.y == n.y ? n : Vec3{0.0, 1.0, 0.0};
}
inline Mesh farMesh(const Field &f) {
	Mesh m;
	const int around = 1024, out = 96;
	for (int j = -1; j <= out; ++j) {   // (row -1: inside the square and under it, so no sliver shows at the edge)
		const double t = j < 0 ? -1.0 : j / double(out);
		for (int i = 0; i <= around; ++i) {
			const double a = 2.0 * kPi * i / around;
			const Vec3 p = farPoint(f, a, t), n = farNormal(f, a, t);
			m.verts.push_back({-p.x, p.y, p.z});
			m.normals.push_back({-n.x, n.y, n.z});
			m.uvs.push_back({i / double(around), std::max(0.0, t)});
		}
	}
	const int w = around + 1;
	for (int j = 0; j <= out; ++j)
		for (int i = 0; i < around; ++i) {
			const int a = j * w + i;
			m.tris.push_back({a, a + 1, a + w});
			m.tris.push_back({a + 1, a + w + 1, a + w});
		}
	return m;
}
// The far ranges' colours: painted as the terrain is (without its water), and towards the blue-grey of distance.
inline Vec3 farColour(const Field &f, double a, double t) {
	const Spec &s = f.spec;
	const Vec3 p = farPoint(f, a, t), n = farNormal(f, a, t);
	const double frac = (p.y - s.y) / std::max(1.0, s.height);
	Vec3 c = paint(s, p.x, p.z, frac, 1.0 - n.y, 0.45 * (1.0 - smoothstep(0.0, 0.4, frac)));   // (no water ran here: the low ground is taken to be as wet as the square's)
	const double rs = edgeRadius(s, a) * 0.999;   // (next to the square: its own colours, water and all, so its edge does not show)
	c = lerp(colourAt(f, s.x + rs * std::cos(a), s.z + rs * std::sin(a)), c, smoothstep(0.0, 0.12, t));
	return lerp(c, Vec3{0.46, 0.52, 0.62}, 0.5 * smoothstep(0.3, 1.0, t));
}

// The weather on the land at a moment: how wet it is (0 dry .. 1 soaked: darker, glossy, puddles in its flat wet hollows) and how much snow lies on it (0 .. 1: on the ground
// level enough to hold it, more as it grows).
struct Weather {
	double wet = 0.0, snow = 0.0;
};
// The material lines for ground painted with the picture texture `tex` under `w`: the snow mixed in by `snowMask` (a float picture of where snow lies) times its amount, the
// whole darkened and coated when it is wet. (Texture graphs of pictures, constants, scale and mix: the GPU renderers draw them per pixel.)
inline std::string groundMaterial(const std::string &tex, const Weather &w, const std::string &snowMask) {
	std::string out, top = tex;
	char buf[160];
	if (w.snow > 0.0 && !snowMask.empty()) {
		std::snprintf(buf, sizeof buf, "\" \"float scale\" [ %.4f ]\n", w.snow);
		out += "  Texture \"" + tex + "_snowmask\" \"float\" \"imagemap\" \"string wrap\" \"clamp\" \"string filename\" [ \"" + snowMask + "\" ]\n";
		out += "  Texture \"" + tex + "_snowamount\" \"float\" \"scale\" \"texture tex\" \"" + tex + "_snowmask" + buf;
		out += "  Texture \"" + tex + "_snowy\" \"spectrum\" \"mix\" \"texture tex1\" \"" + top + "\" \"rgb tex2\" [ 0.84 0.86 0.9 ] \"texture amount\" \"" + tex + "_snowamount\"\n";
		top = tex + "_snowy";
	}
	if (w.wet > 0.0) {
		std::snprintf(buf, sizeof buf, "\" \"float scale\" [ %.4f ]\n", 1.0 - 0.45 * w.wet);
		out += "  Texture \"" + tex + "_wet\" \"spectrum\" \"scale\" \"texture tex\" \"" + top + buf;
		std::snprintf(buf, sizeof buf, "\" \"float roughness\" [ %.4f ]\n", 0.35 * (1.0 - w.wet) + 0.03);
		return out + "  Material \"coateddiffuse\" \"texture reflectance\" \"" + tex + "_wet" + buf;
	}
	return out + "  Material \"diffuse\" \"texture reflectance\" \"" + top + "\"\n";
}
// Where snow lies on the land (a float picture over the square): ground level enough to hold it, a little patchy.
inline std::string snowMaskPicture(const std::string &film, const Field &f) {
	const Spec &s = f.spec;
	return film_sky::picture(film, s.key() + "_snowmask", 1024, 1024, [&](std::vector<float> &rgb) {
		for (int j = 0; j < 1024; ++j)
			for (int i = 0; i < 1024; ++i) {
				const double px = f.sx0 + (i + 0.5) / 1024.0 * s.size, pz = f.sz0 + (1.0 - (j + 0.5) / 1024.0) * s.size;
				const Vec3 n = f.normalAt(px, pz, std::max(1.5, f.cell * 0.6));
				const float v = static_cast<float>(smoothstep(0.78, 0.93, n.y) * (0.75 + 0.25 * nature::noise2(px / 7.0, pz / 7.0, s.seed + 81)));
				const std::size_t k = (static_cast<std::size_t>(j) * 1024 + i) * 3;
				rgb[k] = rgb[k + 1] = rgb[k + 2] = v;
			}
	});
}
// Puddles: the flat hollows where the water gathers, each a patch of still water a hand's breadth above the ground (story space), made once.
inline Mesh puddleMesh(const Field &f) {
	Mesh m;
	const Spec &s = f.spec;
	for (int j = Field::kMargin; j < f.n - Field::kMargin; ++j)
		for (int i = Field::kMargin; i < f.n - Field::kMargin; ++i) {
			const double px = f.x0 + i * f.cell, pz = f.z0 + j * f.cell;
			if (f.flow[static_cast<std::size_t>(j) * f.n + i] < 0.6 || f.normalAt(px, pz, 2.0).y < 0.99 || hash01(i * 7919 + j, s.seed + 91) > 0.3) continue;
			if (s.waterLevel > -1e29 && f.heightAt(px, pz) < s.waterLevel + 0.3) continue;   // (not in the lake)
			const double r = f.cell * (0.2 + 0.25 * hash01(i * 31 + j, s.seed + 93));
			const int ring = 10;
			// a hollow only: the water lies at its lowest, and most of the ground round it is higher (its edge then runs under the ground)
			double lowest = f.heightAt(px, pz);
			std::vector<Vec3> rim;
			for (int k = 0; k < ring; ++k) {
				const double a = 2.0 * kPi * k / ring, rr = r * (0.7 + 0.5 * nature::noise2(std::cos(a) + px * 0.1, std::sin(a) + pz * 0.1, s.seed + 95));
				const double qx = px + rr * std::cos(a), qz = pz + rr * std::sin(a);
				rim.push_back({qx, f.heightAt(qx, qz), qz});
				lowest = std::min(lowest, rim.back().y);
			}
			const double y = lowest + 0.04;
			int higher = 0;
			for (const Vec3 &q : rim) higher += q.y > y;
			if (higher < 7) continue;
			const int base = static_cast<int>(m.verts.size());
			m.verts.push_back({-px, y, pz});
			m.normals.push_back({0, 1, 0});
			for (int k = 0; k <= ring; ++k) {
				const Vec3 &q = rim[static_cast<std::size_t>(k % ring)];
				m.verts.push_back({-q.x, y, q.z});
				m.normals.push_back({0, 1, 0});
				if (k > 0) m.tris.push_back({base, base + k, base + k + 1});
			}
		}
	return m;
}
inline std::string rangesText(const std::string &film, const Field &field, const Weather &w = {});

// The terrain as scene text: the surface with its painted colours, and the far ranges. `film` names the cache it is kept in.
inline std::string text(const std::string &film, const Spec &s, const Weather &w = {}) {
	const std::shared_ptr<const Field> f = fieldOf(s);
	const std::string key = s.key();
	const std::string mask = w.snow > 0.0 ? snowMaskPicture(film, *f) : std::string();
	std::string puddles;   // (in the flat wet hollows, once the ground is wet through)
	if (w.wet > 0.35) {
		const std::string file = film_cache::ply(film, key + "_puddles", [&] { return puddleMesh(*f); });
		if (!file.empty()) puddles = "AttributeBegin\n  " + coated({0.02, 0.025, 0.03}, 0.0) + "\n" + film_cache::plyShape(file) + "AttributeEnd\n";
	}
	const int W = 2048;
	const std::string picture = film_sky::picture(film, key + "_colours", W, W, [&](std::vector<float> &rgb) {
		for (int j = 0; j < W; ++j) {
			const double v = 1.0 - (j + 0.5) / W, pz = f->sz0 + v * s.size;
			for (int i = 0; i < W; ++i) {
				const double u = (i + 0.5) / W, px = f->sx0 + u * s.size;
				const Vec3 c = colourAt(*f, px, pz);
				const std::size_t k = (static_cast<std::size_t>(j) * W + i) * 3;
				rgb[k] = static_cast<float>(c.x);
				rgb[k + 1] = static_cast<float>(c.y);
				rgb[k + 2] = static_cast<float>(c.z);
			}
		}
	});
	// The cliffs: a picture painted from above smears down a cliff, so the steep triangles are a mesh of their own (two: those that face more along x, those along z)
	// with a picture projected from the side they face (pbrt's "planar" mapping): rock strata, or a canyon's bands by height. (Inside a mix of pictures the GPU renderers
	// read a picture by the surface's (u, v), whatever its mapping: a blend of the two by the slope was drawn as flat colour, so the cut is a hard one.)
	if (s.kind != "dunes") {
		const bool canyon = s.kind == "canyon";
		// the side picture: a canyon's bands, one row per height from the base to the top; or rock with its strata and streaks, tiled every 60 m
		const int CW = 512, CH = canyon ? 1024 : 512;
		const double period = canyon ? std::max(1.0, s.height) : 60.0;
		const std::string sidePicture = film_sky::picture(film, key + "_side", CW, CH, [&](std::vector<float> &rgb) {
			for (int j = 0; j < CH; ++j)
				for (int i = 0; i < CW; ++i) {
					const double u = (i + 0.5) / CW, t = 1.0 - (j + 0.5) / CH;
					const double streak = detail::fbm(u * 24.0, t * 2.0, s.seed + 41, 4), grain = nature::noise2(u * 160.0, t * 160.0, s.seed + 43);
					Vec3 c;
					if (canyon) {
						c = paint(s, f->sx0 + u * 300.0, f->sz0, t, 1.0, 0.0);   // (the bands at that height, as the picture from above has them)
						c = c * (0.82 + 0.25 * streak + 0.08 * grain);
					} else {
						const double strata = 0.5 + 0.5 * std::sin(t * 2.0 * kPi * 9.0 + 3.0 * detail::fbm(u * 3.0, t * 3.0, s.seed + 45, 3));
						c = lerp(Vec3{0.19, 0.18, 0.16}, Vec3{0.31, 0.29, 0.26}, 0.45 * strata + 0.4 * streak + 0.15 * grain);
					}
					const std::size_t k = (static_cast<std::size_t>(j) * CW + i) * 3;
					rgb[k] = static_cast<float>(c.x);
					rgb[k + 1] = static_cast<float>(c.y);
					rgb[k + 2] = static_cast<float>(c.z);
				}
		});
		const double steep = canyon ? 0.045 : 0.13;   // (1 - the face normal's y: 17 and 30 degrees)
		std::shared_ptr<Mesh> whole;                // (made only if a part's file is missing)
		const auto part = [&](int which) {
			static const char *names[3] = {"_ground", "_cliff_x", "_cliff_z"};
			return film_cache::ply(film, key + names[which], [&] {
				if (!whole) whole = std::make_shared<Mesh>(surfaceMesh(*f));
				return surfacePart(*whole, which, steep);
			});
		};
		const double vdelta = canyon ? -s.y / period : 0.0;
		std::string out = "# the terrain: " + s.kind + " (film_terrain.h)\nAttributeBegin\n  Texture \"" + key + "\" \"spectrum\" \"imagemap\" \"string wrap\" \"clamp\" \"string filename\" [ \"" +
						  picture + "\" ]\n" + groundMaterial(key, w, mask) + film_cache::plyShape(part(0)) + "AttributeEnd\n" + puddles;
		for (int k = 1; k <= 2; ++k) {   // the cliffs that face along x take the picture projected onto the z-y plane, those along z onto the x-y plane
			char proj[300];
			std::snprintf(proj, sizeof proj, "\"string mapping\" \"planar\" \"vector3 v1\" [ %.8f 0 %.8f ] \"vector3 v2\" [ 0 %.8f 0 ] \"float vdelta\" [ %.6f ]", k == 1 ? 0.0 : 1.0 / 300.0,
						  k == 1 ? 1.0 / 300.0 : 0.0, 1.0 / period, vdelta);
			const std::string name = key + (k == 1 ? "_cliff_x" : "_cliff_z");
			out += "AttributeBegin\n  Texture \"" + name + "\" \"spectrum\" \"imagemap\" " + proj + " \"string filename\" [ \"" + sidePicture + "\" ]\n" + groundMaterial(name, {w.wet, 0.0}, "") +
				   film_cache::plyShape(part(k)) + "AttributeEnd\n";
		}
		return out + rangesText(film, *f, w);
	}
	const std::string surface = film_cache::ply(film, key + "_surface", [&] { return surfaceMesh(*f); });
	return "# the terrain: " + s.kind + " (film_terrain.h)\nAttributeBegin\n  Texture \"" + key + "\" \"spectrum\" \"imagemap\" \"string wrap\" \"clamp\" \"string filename\" [ \"" +
		   picture + "\" ]\n" + groundMaterial(key, w, mask) + film_cache::plyShape(surface) + "AttributeEnd\n" + puddles + rangesText(film, *f, w);
}
// The far ranges as scene text ("" when they are turned off).
inline std::string rangesText(const std::string &film, const Field &field, const Weather &w) {
	const Spec &s = field.spec;
	const Field *f = &field;
	const std::string key = s.key();
	std::string out;
	if (s.ranges) {
		const std::string ring = film_cache::ply(film, key + "_far", [&] { return farMesh(*f); });
		const int FW = 4096, FH = 512;
		const std::string farPicture = film_sky::picture(film, key + "_far", FW, FH, [&](std::vector<float> &rgb) {
			for (int j = 0; j < FH; ++j)
				for (int i = 0; i < FW; ++i) {
					const Vec3 c = farColour(*f, 2.0 * kPi * (i + 0.5) / FW, 1.0 - (j + 0.5) / FH);
					const std::size_t k = (static_cast<std::size_t>(j) * FW + i) * 3;
					rgb[k] = static_cast<float>(c.x);
					rgb[k + 1] = static_cast<float>(c.y);
					rgb[k + 2] = static_cast<float>(c.z);
				}
		});
		// (snow on the far ranges: their picture is mixed with snow as the land's is, where their ground is level enough)
		std::string farMask;
		if (w.snow > 0.0)
			farMask = film_sky::picture(film, key + "_far_snowmask", 1024, 128, [&](std::vector<float> &rgb) {
				for (int j = 0; j < 128; ++j)
					for (int i = 0; i < 1024; ++i) {
						const double a = 2.0 * kPi * (i + 0.5) / 1024.0, t = 1.0 - (j + 0.5) / 128.0;
						const float v = static_cast<float>(smoothstep(0.75, 0.92, farNormal(*f, a, t).y));
						const std::size_t k = (static_cast<std::size_t>(j) * 1024 + i) * 3;
						rgb[k] = rgb[k + 1] = rgb[k + 2] = v;
					}
			});
		out += "AttributeBegin\n  Texture \"" + key + "_far\" \"spectrum\" \"imagemap\" \"string wrap\" \"clamp\" \"string filename\" [ \"" + farPicture + "\" ]\n" + groundMaterial(key + "_far", w, farMask) +
			   film_cache::plyShape(ring) + "AttributeEnd\n";
	}
	return out;
}

}  // namespace terrain
}  // namespace cartoon
