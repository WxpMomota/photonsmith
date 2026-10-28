#pragma once
// film_surf.h -- surf on a coast (film_file.h `surf`, with a `water sea` over a `terrain`): the swell comes in from the open sea, slows in the shallows and turns to meet the
// shore, grows and steepens as the water shoals, breaks, and runs in as white water; foam is left on the water and drifts along the shore, spray blows off the crests, and
// in the shallows the water is clear over sand netted with caustics.
//
//   refraction   the time the swell takes to reach each point of a grid over the land - from the deep water (or the square's edge), at the speed sqrt(g d) a depth d allows
//                (a shortest-time sweep over the grid): a crest is where that time is a whole number of periods ago, so crests bend to follow the depth lines and turn
//                to face the beach, as real swell does
//   shoaling     each crest's height grows as d^(-1/4) (Green's law) and its shape sharpens (a narrow crest, flat troughs); where it reaches 0.78 of the depth it
//                breaks: from there in it stays 0.78 of the depth (a bore), its front white
//   white water  the breaking crest and the turbulent water just behind it; and long after, streaks of foam on the water that drift along the shore
//   shallows     the water of the surf zone is clear: it shows the sand under it, which takes on the deep water's colour with depth, netted with caustics
//
// Coordinates are pbrt's. A film's swell is a function of its time alone; the grid is made once (cached by the land, the sea's level and the swell).

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_terrain.h"
#include "film_underwater.h"
#include "nature_lib.h"

namespace cartoon {
namespace surf {

// The swell: its height out at sea (metres, crest to trough), the time between its crests, and which way it runs (a facing: 0 +z, 90 +x; the sea's wind unless given).
struct Swell {
	bool on = false;
	double height = 1.2, period = 9.0, toward = 0.0;
	bool towardSet = false;
};

// The grid: the water's depth and the swell's arrival time (seconds) at each point of the land's grid; arrival < 0 where it does not reach (land, a pond cut off).
struct Grid {
	int n = 0;
	double x0 = 0.0, z0 = 0.0, cell = 1.0, deep = 10.0;
	std::vector<float> depth, arrive;
	double sample(const std::vector<float> &f, double x, double z, bool &inside) const {
		const double gx = (x - x0) / cell, gz = (z - z0) / cell;
		inside = gx >= 0.0 && gz >= 0.0 && gx <= n - 1.001 && gz <= n - 1.001;
		const double cx = std::clamp(gx, 0.0, n - 1.001), cz = std::clamp(gz, 0.0, n - 1.001);
		const int i = static_cast<int>(cx), j = static_cast<int>(cz);
		const double u = cx - i, v = cz - j;
		const auto at = [&](int a, int b) { return static_cast<double>(f[static_cast<std::size_t>(b) * n + a]); };
		return lerp(lerp(at(i, j), at(i + 1, j), u), lerp(at(i, j + 1), at(i + 1, j + 1), u), v);
	}
};

namespace detail {
inline double speed(double d, double deep) { return std::sqrt(9.81 * std::clamp(d, 0.3, deep)); }
}  // namespace detail

// The grid for a land, a sea's level and a swell (made once; a shortest-time sweep from the deep water).
inline std::shared_ptr<const Grid> gridOf(const terrain::Field &ground, double level, const Swell &sw) {
	static std::mutex lock;
	static std::map<std::string, std::shared_ptr<const Grid>> made;
	char key[200];
	std::snprintf(key, sizeof key, "%p|%.4f|%.4f|%.4f|%.4f", static_cast<const void *>(&ground), level, sw.height, sw.period, sw.toward);
	{
		const std::lock_guard<std::mutex> hold(lock);
		if (const auto it = made.find(key); it != made.end()) return it->second;
	}
	auto g = std::make_shared<Grid>();
	g->n = ground.n, g->x0 = ground.x0, g->z0 = ground.z0, g->cell = ground.cell;
	const int n = g->n;
	const std::size_t count = static_cast<std::size_t>(n) * n;
	g->depth.assign(count, 0.0f);
	double deepest = 0.0;
	for (int j = 0; j < n; ++j)
		for (int i = 0; i < n; ++i) {
			const double d = level - ground.heightAt(g->x0 + i * g->cell, g->z0 + j * g->cell);
			g->depth[static_cast<std::size_t>(j) * n + i] = static_cast<float>(d);
			deepest = std::max(deepest, d);
		}
	g->deep = std::clamp(0.6 * deepest, 2.0, 25.0);   // (where the swell first feels the bottom, within what this land has)
	// the sweep: from every point of deep water, and from the water at the square's edge (the open sea beyond it), each at the time a straight swell would reach it
	const double a = sw.toward * kPi / 180.0, tx = std::sin(a), tz = std::cos(a), cDeep = detail::speed(g->deep, g->deep);
	g->arrive.assign(count, -1.0f);
	std::vector<double> best(count, 1e30);
	using Item = std::pair<double, std::size_t>;
	std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
	for (int j = 0; j < n; ++j)
		for (int i = 0; i < n; ++i) {
			const std::size_t at = static_cast<std::size_t>(j) * n + i;
			const bool edge = i == 0 || j == 0 || i == n - 1 || j == n - 1;
			if (g->depth[at] <= 0.0f || !(g->depth[at] >= g->deep || edge)) continue;
			best[at] = ((g->x0 + i * g->cell) * tx + (g->z0 + j * g->cell) * tz) / cDeep;
			open.push({best[at], at});
		}
	while (!open.empty()) {
		const auto [time, at] = open.top();
		open.pop();
		if (time > best[at]) continue;
		const int i = static_cast<int>(at % n), j = static_cast<int>(at / n);
		for (int dj = -1; dj <= 1; ++dj)
			for (int di = -1; di <= 1; ++di) {
				if (!di && !dj) continue;
				const int a2 = i + di, b2 = j + dj;
				if (a2 < 0 || b2 < 0 || a2 >= n || b2 >= n) continue;
				const std::size_t to = static_cast<std::size_t>(b2) * n + a2;
				if (g->depth[to] <= 0.0f) continue;
				const double step = g->cell * std::sqrt(static_cast<double>(di * di + dj * dj));
				const double t = time + step / detail::speed(0.5 * (g->depth[at] + g->depth[to]), g->deep);
				if (t < best[to]) best[to] = t, open.push({t, to});
			}
	}
	// the times from 0 (below 0 marks where the swell does not reach), then carried a few cells onto the land, so a point between the last water and the first land
	// interpolates between times, not towards the mark
	double first = 1e30;
	for (double b : best) first = std::min(first, b);
	for (std::size_t at = 0; at < count; ++at) g->arrive[at] = best[at] < 1e29 ? static_cast<float>(best[at] - first) : -1.0f;
	for (int pass = 0; pass < 3; ++pass) {
		std::vector<float> next = g->arrive;
		for (int j = 0; j < n; ++j)
			for (int i = 0; i < n; ++i) {
				const std::size_t at = static_cast<std::size_t>(j) * n + i;
				if (g->arrive[at] >= 0.0f) continue;
				double sum = 0.0;
				int k = 0;
				for (const auto &[di, dj] : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
					const int a2 = i + di, b2 = j + dj;
					if (a2 < 0 || b2 < 0 || a2 >= n || b2 >= n) continue;
					const float v = g->arrive[static_cast<std::size_t>(b2) * n + a2];
					if (v >= 0.0f) sum += v, ++k;
				}
				if (k > 0) next[at] = static_cast<float>(sum / k);
			}
		g->arrive.swap(next);
	}
	const std::lock_guard<std::mutex> hold(lock);
	made[key] = g;
	return g;
}

// The surf at pbrt's (x, z) at t: how far the water rises, how white it is (0..1), how much of the open sea's waves are left (they give way to the surf as it shoals), and
// whether the water there is clear (the shallows). Nothing where the grid has no water or the swell does not reach.
struct At {
	double rise = 0.0, white = 0.0, open = 1.0;
	bool shallow = false, breaking = false;
};
inline At at(const Grid &g, const Swell &sw, double x, double z, double t) {
	At out;
	bool inside = false;
	const double d = g.sample(g.depth, x, z, inside);
	if (!inside || d <= 0.0) return out;
	out.open = std::clamp(d / g.deep, 0.0, 1.0);
	out.open *= out.open;
	out.shallow = d < g.deep;   // (clear water over the whole surf zone: the bed under it takes on the deep water's colour with depth, film_terrain.h seabed)
	const double arrive = g.sample(g.arrive, x, z, inside);
	if (arrive < 0.0 || d >= g.deep) return out;
	// the swell comes in sets (a bigger wave every seven or so) and is not the same all along the shore
	const double set = 0.8 + 0.2 * std::sin(2.0 * kPi * (t - arrive) / (7.3 * sw.period)) + 0.3 * (nature::noise2(x / 70.0, z / 70.0, 307) - 0.5);
	const double shoaled = sw.height * set * std::pow(g.deep / std::max(d, 0.15), 0.25);
	const double H = std::min(shoaled, 0.78 * d);
	out.breaking = shoaled >= 0.78 * d;
	const double steep = std::clamp(shoaled / (0.78 * d), 0.0, 1.0);
	const double p = 1.0 + 6.0 * steep * steep;   // (a narrow crest and flat troughs as it nears breaking)
	const double phase = 2.0 * kPi * (t - arrive) / sw.period;
	const double shape = std::pow(0.5 * (1.0 + std::cos(phase)), p);
	const double mean = std::tgamma(p + 0.5) / (std::sqrt(kPi) * std::tgamma(p + 1.0));   // (its mean over a cycle: the water's level stays where it is)
	out.rise = H * (shape - mean) * smoothstep(0.0, 0.5, d) * (1.0 - smoothstep(0.7 * g.deep, g.deep, d));   // (faded in from the deep water, where the open sea's own waves are)
	if (out.breaking) {   // white water: the crest that has broken and the turbulent water just behind it (the front of the wave is just before its crest in time)
		double u = std::fmod(phase / (2.0 * kPi), 1.0);
		if (u < 0.0) u += 1.0;
		const double behind = u, front = 1.0 - u;
		out.white = std::max(front < 0.06 ? 1.0 - front / 0.06 : 0.0, behind < 0.35 ? 1.0 - behind / 0.35 : 0.0) * smoothstep(0.0, 0.25, d);
	}
	return out;
}

// The streaks of foam left on the water of the surf zone: broken lines that drift along the shore (0..1).
inline double foamStreaks(double x, double z, double t, const Swell &sw) {
	const double a = sw.toward * kPi / 180.0, along = -std::cos(a) * x + std::sin(a) * z, across = std::sin(a) * x + std::cos(a) * z;
	const double n = nature::noise2(along * 0.08 + t * 0.15, across * 0.35, 211) * 0.7 + nature::noise2(along * 0.3 - t * 0.1, across * 1.1, 223) * 0.3;
	return smoothstep(0.62, 0.72, n);
}

// The spray blown off breaking crests near the camera, as little white specks: for each crest point (pbrt's), drops thrown up and back (against the swell), each living
// half a second.
inline std::string sprayText(const std::vector<Vec3> &crests, const Vec3 &eye, double t, const Swell &sw) {
	const double a = sw.toward * kPi / 180.0;
	const Vec3 back{-std::sin(a), 0.0, -std::cos(a)};
	std::string points, indices;
	int count = 0;
	char buf[96];
	for (std::size_t c = 0; c < crests.size(); ++c) {
		const Vec3 &crest = crests[c];
		const double dist = length(crest - eye);
		if (dist > 80.0) continue;
		for (int k = 0; k < 6; ++k) {
			const int id = static_cast<int>(c) * 7 + k;
			const double life = 0.5 + 0.3 * hash01(id, 31), age = std::fmod(t + life * hash01(id, 32), life);
			const double up = 1.5 + 1.5 * hash01(id, 33), out = 0.6 + 0.8 * hash01(id, 34);
			const Vec3 p = crest + Vec3{(hash01(id, 35) - 0.5) * 0.6, up * age - 4.9 * age * age, (hash01(id, 36) - 0.5) * 0.6} + back * (out * age);
			const double r = 0.012 + 0.02 * hash01(id, 37);
			for (const Vec3 &v : {p + Vec3{-r, 0.0, 0.0}, p + Vec3{0.0, 0.0, -r}, p + Vec3{r, 0.0, 0.0}, p + Vec3{0.0, r * 1.5, 0.0}}) {
				std::snprintf(buf, sizeof buf, "%.4f %.4f %.4f ", v.x, v.y, v.z);
				points += buf;
			}
			std::snprintf(buf, sizeof buf, "%d %d %d %d %d %d ", count, count + 1, count + 2, count, count + 2, count + 3);
			indices += buf;
			count += 4;
		}
	}
	if (count == 0) return std::string();
	return "# spray off the breaking crests (film_surf.h)\nAttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0.85 0.88 0.9 ]\n  Shape \"trianglemesh\" \"integer indices\" [ " + indices +
		   "] \"point3 P\" [ " + points + "]\nAttributeEnd\n";
}

// The sand under the water round the camera's ground point (pbrt's x z), netted with the moving caustics (film_underwater.h): a decal 48 m across just above the bed,
// wherever the water is more than a couple of centimetres deep, its picture baked for each frame of the caustics' loop - the bed's colour through the water (film_terrain.h's
// seabed, as the terrain under it is painted) brightened and darkened by the net of light, which fades out with depth. "" when no water is near.
inline std::string shallowsCausticText(const std::string &film, double t, double cx, double cz, const terrain::Field &ground, double level, bool traced = false) {
	const double size = 48.0, x0 = std::floor((cx - size / 2) / 16.0) * 16.0, z0 = std::floor((cz - size / 2) / 16.0) * 16.0;
	const int n = 96;
	Mesh m;
	std::vector<char> wet;
	for (int j = 0; j <= n; ++j)
		for (int i = 0; i <= n; ++i) {
			const double x = x0 + size * i / n, z = z0 + size * j / n, y = ground.heightAt(x, z);
			m.verts.push_back({-x, y + 0.03, z});
			const double e = 0.4;
			m.normals.push_back(normalize(Vec3{-(ground.heightAt(x - e, z) - ground.heightAt(x + e, z)), 2.0 * e, ground.heightAt(x, z - e) - ground.heightAt(x, z + e)}));
			m.uvs.push_back({(x - x0) / size, (z - z0) / size});
			wet.push_back(level - y > 0.02 ? 1 : 0);
		}
	for (int j = 0; j < n; ++j)
		for (int i = 0; i < n; ++i) {
			const int a = j * (n + 1) + i;
			if (wet[a] && wet[a + 1] && wet[a + n + 1]) m.tris.push_back({a, a + 1, a + n + 1});
			if (wet[a + 1] && wet[a + n + 2] && wet[a + n + 1]) m.tris.push_back({a + 1, a + n + 2, a + n + 1});
		}
	if (m.tris.empty()) return std::string();
	const int k = underwater::causticFrame(t), W = 512;
	char name[96];
	std::snprintf(name, sizeof name, "shallows_%016llx_%d_%d_%02d%s", static_cast<unsigned long long>(std::hash<std::string>()(ground.spec.key() + std::to_string(level))),
				  static_cast<int>(x0), static_cast<int>(z0), k, traced ? "_t" : "");
	const std::string picture = film_sky::picture(film, name, W, W, [&](std::vector<float> &rgb) {
		for (int j = 0; j < W; ++j) {
			const double v = 1.0 - (j + 0.5) / W, pz = z0 + v * size;
			for (int i = 0; i < W; ++i) {
				const double u = (i + 0.5) / W, px = x0 + u * size, d = level - ground.heightAt(px, pz);
				const double grain = nature::noise2(px / 2.0, pz / 2.0, ground.spec.seed + 61);   // (as the terrain's own sand: film_terrain.h colourAt)
				const double net = traced ? 0.3 + 0.3 * (caustics::at(px / caustics::kTile, pz / caustics::kTile, k, std::max(0.0, d)) - 1.0)   // (`caustics traced`)
										  : underwater::causticAt(px / 2.5, pz / 2.5, k),
							 fade = std::exp(-d / 2.0) * smoothstep(0.02, 0.25, d);
				const Vec3 c = terrain::seabed(d, grain) * std::max(0.0, 1.0 + 1.6 * (net - 0.3) * fade);
				const std::size_t q = (static_cast<std::size_t>(j) * W + i) * 3;
				rgb[q] = static_cast<float>(c.x), rgb[q + 1] = static_cast<float>(c.y), rgb[q + 2] = static_cast<float>(c.z);
			}
		}
	});
	return "# the sand under the water, with its caustics (film_surf.h)\nAttributeBegin\n  Texture \"shallows\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + picture +
		   "\" ]\n  Material \"diffuse\" \"texture reflectance\" \"shallows\"\n" + emitMesh(m, identity()) + "AttributeEnd\n";
}

}  // namespace surf
}  // namespace cartoon
