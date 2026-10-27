#pragma once
// film_geology.h -- the land in action, for a film file (film_file.h `geyser`, `rockfall`, `avalanche`, `quake`):
//
//   geyser     a vent on a pale mound of sinter with a steaming pool round it, erupting every so often: a column of water thrown up and falling back as spray (streaks
//              arcing out and down), and a plume of white steam rising and drifting
//   rockfall   rocks breaking away from a slope and coming down it the steepest way: sliding and rolling faster where it is steep (gravity along the slope, friction
//              against it), bouncing in hops, tumbling, a puff of dust at each impact, stopping where the ground levels out
//   avalanche  a powder avalanche: a cloud of snow racing down the steepest way from where it breaks away, billowing up behind its front as it grows, over a white flow
//              on the ground
//   quake      the ground shaking: the camera shaken (film_file.h's camera), a rumble on the soundtrack
//
// What moves is worked out when the film is loaded (each rock's way down and its impacts, the avalanche's way); each frame takes its moment. Coordinates are pbrt's.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_terrain.h"
#include "nature_lib.h"

namespace cartoon {
namespace geology {

struct Geyser {
	double x = 0.0, z = 0.0, height = 25.0, every = 20.0, last = 6.0, first = 3.0;   // where; how high it throws; how often (s); how long an eruption lasts; the first's start
	int seed = 1;
	double strengthAt(double t) const {   // 0 .. 1: erupting (a burst that builds over a second and dies away over its last two)
		if (t < first) return 0.0;
		const double u = std::fmod(t - first, every);
		return smoothstep(0.0, 1.0, u) * (1.0 - smoothstep(last - 2.0, last, u));
	}
};
struct Rock {
	std::vector<Vec3> at;            // its centre each step (1/48 s) from when it breaks away
	std::vector<double> speed;
	std::vector<double> impacts;     // the times it hits the ground (film seconds)
	double size = 1.0, t0 = 0.0;
	int seed = 1;
};
struct Rockfall {
	double x = 0.0, z = 0.0, t0 = 2.0, size = 1.2;
	int count = 12, seed = 1;
	std::vector<Rock> rocks;
};
struct Avalanche {
	double x = 0.0, z = 0.0, t0 = 2.0, width = 60.0, speed = 22.0;
	std::vector<Vec3> path;          // the way down, a point every 2 m (worked out when the film is loaded)
	double frontAt(double t) const { const double r = std::max(0.0, t - t0); return std::min(2.0 * (path.size() - 1), speed * (r - 2.0 * (1.0 - std::exp(-r / 2.0)))); }   // (from rest)
};
struct Quake {
	double t0 = 0.0, t1 = 5.0, strength = 1.0;
	double levelAt(double t) const { return strength * smoothstep(t0, t0 + 0.6, t) * (1.0 - smoothstep(t1 - 1.5, t1, t)); }
};

// The steepest way down the land from (x, z): a point every `step` metres, with a little momentum, until the land stops falling or the square ends.
inline std::vector<Vec3> downhill(const terrain::Field &g, double x, double z, double step = 2.0) {
	std::vector<Vec3> out;
	Vec3 p{x, g.heightAt(x, z), z}, dir{0, 0, 0};
	for (int k = 0; k < 4000 && g.inside(p.x, p.z); ++k) {
		out.push_back(p);
		const double e = 1.5, gx = g.heightAt(p.x + e, p.z) - g.heightAt(p.x - e, p.z), gz = g.heightAt(p.x, p.z + e) - g.heightAt(p.x, p.z - e);
		const Vec3 down = normalize(Vec3{-gx, 0.0, -gz} + Vec3{1e-9, 0, 0});
		dir = normalize(dir * 0.6 + down);
		const Vec3 next{p.x + dir.x * step, 0.0, p.z + dir.z * step};
		const double y = g.heightAt(next.x, next.z);
		if (y > p.y - 0.02 * step && k > 3) break;   // (it has stopped falling)
		p = {next.x, y, next.z};
	}
	return out;
}

// Each rock of a rockfall: from a scatter round where it breaks away, down the steepest way - gravity along the slope against friction, in hops (one each 0.4 to 0.8
// s, as high as its speed throws it), until it stops.
inline void fall(Rockfall &rf, const terrain::Field &g) {
	rf.rocks.clear();
	const double dt = 1.0 / 48.0, mu = 0.25;   // (rolling and bouncing: less held back than a sliding block)
	// rocks break away where it is steepest within 25 m of where the film says (a cliff, a scree slope)
	double bx = rf.x, bz = rf.z, steepest = -1.0;
	for (int k = 0; k < 64; ++k) {
		const double a = 2.0 * kPi * hash01(k, rf.seed + 7), r = 25.0 * std::sqrt(hash01(k, rf.seed + 8)), x = rf.x + r * std::cos(a), z = rf.z + r * std::sin(a);
		const double steep = g.inside(x, z) ? 1.0 - g.normalAt(x, z, 1.5).y : -1.0;
		if (steep > steepest) steepest = steep, bx = x, bz = z;
	}
	for (int i = 0; i < rf.count; ++i) {
		Rock r;
		r.seed = rf.seed * 101 + i;
		r.size = rf.size * (0.4 + 0.9 * hash01(r.seed, 1));
		r.t0 = rf.t0 + 1.2 * hash01(r.seed, 2);
		const double sx = bx + 6.0 * (hash01(r.seed, 3) - 0.5), sz = bz + 6.0 * (hash01(r.seed, 4) - 0.5);
		const std::vector<Vec3> way = downhill(g, sx, sz, 1.0);
		if (way.size() < 3) continue;
		double s = 0.0, v = 2.0, hopStart = 0.0, hopLength = 0.5, hopHeight = 0.0;   // (broken away with a push)
		for (int k = 0; k < 48 * 40; ++k) {   // (forty seconds at most)
			const double tt = k * dt;
			const std::size_t i0 = std::min(way.size() - 2, static_cast<std::size_t>(s)), i1 = i0 + 1;
			const double u = std::min(1.0, s - i0);
			const Vec3 a = way[i0], b = way[i1];
			const double run = std::max(1e-6, std::hypot(b.x - a.x, b.z - a.z)), slope = (a.y - b.y) / run, sinT = slope / std::sqrt(1.0 + slope * slope), cosT = 1.0 / std::sqrt(1.0 + slope * slope);
			v = std::max(0.0, v + 9.81 * (sinT - mu * cosT) * dt);
			s += v * dt / run;
			if (tt - hopStart >= hopLength) {   // a hop ends: it hits the ground, and hops again as its speed throws it
				if (v > 0.5) r.impacts.push_back(r.t0 + tt);
				hopStart = tt, hopLength = 0.4 + 0.4 * hash01(r.seed + k, 5), hopHeight = std::min(3.0, 0.03 * v * v);
			}
			const double h = hopHeight * std::sin(kPi * std::min(1.0, (tt - hopStart) / hopLength));
			const Vec3 p = a + (b - a) * u;
			r.at.push_back({p.x, p.y + 0.5 * r.size + h, p.z});
			r.speed.push_back(v);
			if ((v <= 0.0 && k > 48) || s >= way.size() - 2) break;
		}
		rf.rocks.push_back(r);
	}
}

// A rock's shape: a lumpy ball (a sphere pushed in and out by noise), `size` across.
inline Mesh rockShape(int seed, double size) {
	Mesh m = uvSphere(0.5 * size, 12, 8);
	for (std::size_t i = 0; i < m.verts.size(); ++i) {
		const Vec3 n = normalize(m.verts[i]);
		const double k = 0.75 + 0.45 * nature::noise2(n.x * 2.0 + n.y * 1.3 + seed, n.z * 2.0 - n.y * 0.7, seed + 3);
		m.verts[i] = m.verts[i] * k;
	}
	for (std::size_t i = 0; i < m.normals.size(); ++i) m.normals[i] = normalize(m.verts[i]);
	return m;
}
// The rocks of a rockfall at t, as scene text: each where it is, tumbling as it goes (turned by the ground it has covered).
inline std::string rockfallText(const Rockfall &rf, double t) {
	std::string out;
	for (const Rock &r : rf.rocks) {
		if (t < r.t0 || r.at.empty()) continue;
		const std::size_t k = std::min(r.at.size() - 1, static_cast<std::size_t>((t - r.t0) * 48.0));
		double rolled = 0.0;
		for (std::size_t j = 0; j < k && j < r.speed.size(); ++j) rolled += r.speed[j] / 48.0;
		const Vec3 p = r.at[k];
		const double turn = rolled / (0.5 * r.size) * 180.0 / kPi;
		out += attr(diffuseMaterial(Vec3{0.28, 0.26, 0.23} * (0.8 + 0.4 * hash01(r.seed, 9))),
					emitMesh(rockShape(r.seed, r.size), mul(translate(-p.x, p.y, p.z), rotate({std::cos(r.seed * 1.7), 0.0, std::sin(r.seed * 1.7)}, turn))));
	}
	return out.empty() ? std::string() : "# a rockfall (film_geology.h)\n" + out;
}

// An avalanche at t, as scene text: its powder cloud (white puffs crowding behind the front, rising and swelling the further back they are, churning) and the flow under
// it (a white ribbon on the ground from where it broke away to its front).
inline std::string avalancheText(const Avalanche &a, double t) {
	if (t < a.t0 || a.path.size() < 2) return std::string();
	const double front = a.frontAt(t);   // (metres along the way)
	const auto along = [&](double s, Vec3 &dir) {
		const double k = std::clamp(s / 2.0, 0.0, static_cast<double>(a.path.size() - 1) - 1e-6);
		const std::size_t i = static_cast<std::size_t>(k);
		const Vec3 p = a.path[i], q = a.path[std::min(a.path.size() - 1, i + 1)];
		dir = normalize(Vec3{q.x - p.x, 0.0, q.z - p.z} + Vec3{1e-9, 0, 0});
		return p + (q - p) * (k - i);
	};
	std::string puffs;
	char buf[200];
	const int n = 700;
	for (int i = 0; i < n; ++i) {
		const double back = 140.0 * std::pow(hash01(i, 1), 1.5);   // (crowded at the front)
		if (back > front) continue;
		Vec3 dir;
		const Vec3 p = along(front - back, dir), side{-dir.z, 0.0, dir.x};
		const double grow = 0.25 + 0.75 * std::min(1.0, (front - back) / 60.0), rise = back / 140.0;
		const double churn = std::fmod(t * 0.8 + hash01(i, 2), 1.0);
		const Vec3 c = p + side * ((hash01(i, 3) - 0.5) * a.width * grow) + Vec3{0.0, (2.0 + 28.0 * rise * hash01(i, 4) + 3.0 * churn) * grow, 0.0};
		const double r = (1.5 + 4.0 * hash01(i, 5)) * grow * (0.6 + 0.8 * rise);
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.3f %.3f %.3f\n  Shape \"sphere\" \"float radius\" [ %.3f ]\nAttributeEnd\n", c.x, c.y, c.z, r);   // (raw text: pbrt's x)
		puffs += buf;
	}
	Mesh flow;
	for (double s = 0.0; s <= front; s += 2.0) {
		Vec3 dir;
		const Vec3 p = along(s, dir), side{-dir.z, 0.0, dir.x};
		const double w = 0.5 * a.width * (0.4 + 0.6 * std::min(1.0, s / 60.0));
		for (const double k : {-1.0, 1.0}) {
			const Vec3 q = p + side * (k * w) + Vec3{0.0, 0.6, 0.0};
			flow.verts.push_back({-q.x, q.y, q.z});
			flow.normals.push_back({0.0, 1.0, 0.0});
		}
	}
	for (int i = 0; i + 3 < static_cast<int>(flow.verts.size()); i += 2) {
		flow.tris.push_back({i, i + 1, i + 2});
		flow.tris.push_back({i + 1, i + 3, i + 2});
	}
	std::string out = "# an avalanche (film_geology.h)\nAttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0.86 0.88 0.92 ]\n" + puffs + "AttributeEnd\n";
	if (!flow.tris.empty()) out += attr(diffuseMaterial({0.84, 0.86, 0.9}), emitMesh(flow, identity()));
	return out;
}

// A geyser at t, as scene text: its mound of sinter and steaming pool, and while it erupts its column of water (streaks thrown up and falling back out round it).
// `ground`: the land's height at the vent.
inline std::string geyserText(const Geyser &gy, double t, double ground) {
	char buf[320];
	std::string out = "# a geyser (film_geology.h)\n";
	out += attr(diffuseMaterial({0.72, 0.68, 0.6}), emitMesh(uvSphere(7.0, 24, 12), mul(translate(-gy.x, ground - 0.4, gy.z), scale(1.0, 0.18, 1.0))));   // (the mound: a mesh, as a
	                                                                                                                                                 // renderer's sphere is round)
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Material \"coateddiffuse\" \"rgb reflectance\" [ 0.05 0.25 0.35 ] \"float roughness\" [ 0 ]\n  Translate %.3f %.3f %.3f\n  Shape \"disk\" \"float radius\" [ 2.2 ]\nAttributeEnd\n",
				  gy.x, ground + 0.86, gy.z);
	out += buf;   // (the pool, its surface just under the mound's top; a disk faces +z: turned up below)
	out.insert(out.rfind("  Shape \"disk\""), "  Rotate -90 1 0 0\n");
	const double s = gy.strengthAt(t);
	if (s <= 0.0) return out;
	Mesh water;
	const int n = static_cast<int>(1400 * s);
	for (int i = 0; i < n; ++i) {   // each drop thrown up at a speed of its own, a little out from the vertical, living its flight again and again
		const double v = std::sqrt(2.0 * 9.81 * gy.height) * s * (0.55 + 0.45 * hash01(i, gy.seed)), out_ = 0.12 * hash01(i, gy.seed + 1), a = 2.0 * kPi * hash01(i, gy.seed + 2);
		const double life = 2.0 * v / 9.81, age = std::fmod(t + life * hash01(i, gy.seed + 3), std::max(0.05, life));
		const Vec3 vel{v * out_ * std::cos(a), v, v * out_ * std::sin(a)};
		const Vec3 p{gy.x + vel.x * age, ground + 0.9 + vel.y * age - 4.905 * age * age, gy.z + vel.z * age};
		const Vec3 d = normalize(Vec3{vel.x, vel.y - 9.81 * age, vel.z} + Vec3{1e-9, 0, 0});
		const double len = 0.6 + 0.05 * std::abs(vel.y - 9.81 * age), r = 0.12 + 0.2 * hash01(i, gy.seed + 4);   // (gouts of water and spray: they read from afar)
		const Vec3 side = normalize(cross(d, Vec3{0.3, 0.1, 1.0}));
		const int b = static_cast<int>(water.verts.size());
		for (const Vec3 &q : {p - side * r, p + side * r, p + side * r + d * len, p - side * r + d * len}) {
			water.verts.push_back({-q.x, q.y, q.z});
			water.normals.push_back(normalize(cross(side, d)));
		}
		water.tris.push_back({b, b + 1, b + 2});
		water.tris.push_back({b, b + 2, b + 3});
	}
	return out + attr(diffuseMaterial({0.85, 0.88, 0.92}), emitMesh(water, identity()));
}

}  // namespace geology
}  // namespace cartoon
