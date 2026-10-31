#pragma once
// film_boat.h -- round 16: a boat overhead, and the sardine run.
//
//   boat X Z [toward A] [length L] [speed S] [during T0 T1] [anchor T] [seed K]
//            a boat on the surface (its hull red with antifouling below the waterline, white above), under way along A at S metres a second from T0, rocking on the
//            swell; seen from below a dark shape against the bright water, its shadow sweeping over the seabed, the water churned white behind it and a trail of
//            bubbles from its propeller; at `anchor T` it stops and drops its anchor: the anchor falls on its chain, lands in a puff of sand and silt, and the chain
//            settles in a curve from the bow. Heard under the water: the engine's drone, nearer as it passes (film_world.cpp soundscape).
//   sardinerun X Y Z [radius R] [gannets N] [dolphins N] [from T] [seed K]
//            the sardine run: a bait ball (film_sealife.h) that sharks dash through every few seconds from T, dolphins circling it to keep it tight, and gannets plunging
//            through the surface one after another from twenty metres up - a splash, then a column of bubbles where each one went in, three or four metres down
//            after a fish, and back up to sit on the water
//
// Coordinates: pbrt's in the film file (a sardine run's height above the seabed, as round 14's lines); meshes in story space (x mirrored).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_deepsea.h"
#include "film_giants.h"
#include "film_school.h"
#include "film_sealife.h"
#include "film_undersea.h"
#include "film_water.h"

namespace cartoon {
namespace boat {

struct Boat {
	double x = 0.0, z = 0.0, toward = 0.0, length = 8.0, speed = 2.0, t0 = 0.0, t1 = 1e30, anchor = -1.0;
	int seed = 1;
};
struct Run {
	Vec3 at;
	double radius = 2.5, from = 0.0;
	int gannets = 8, dolphins = 3, seed = 1;
};

// ---- the boat ----
inline Vec3 headingOf(double toward) { return {std::sin(toward * kPi / 180.0), 0.0, std::cos(toward * kPi / 180.0)}; }
// How far along its way the boat has gone by t (it eases to a stop over 3 s for its anchor, and at T1).
inline double travelled(const Boat &b, double t) {
	const double stop = b.anchor >= 0.0 ? std::min(b.t1, b.anchor) : b.t1;
	const auto run = [&](double u) { return std::max(0.0, u - b.t0); };
	if (t <= stop - 3.0) return b.speed * run(t);
	const double before = b.speed * run(stop - 3.0), s = std::clamp(t - (stop - 3.0), 0.0, 3.0);
	return before + b.speed * (s - s * s / 6.0);   // (slowing evenly to rest over 3 s)
}
// Where the boat's middle is at t (pbrt's, on the surface at `level`) and its heading.
inline Vec3 boatAt(const Boat &b, double t, double level) {
	const Vec3 d = headingOf(b.toward);
	return Vec3{b.x, level, b.z} + d * travelled(b, t);
}
// The hull (local: forward +z, the waterline at y 0): a red bottom up to the waterline and white topsides, a cabin; `L` long.
inline void hullMesh(double L, Mesh &bottom, Mesh &topsides) {
	const double beam = 0.34 * L, draft = 0.11 * L, free = 0.09 * L;
	const int nz = 32, na = 14;
	for (int part = 0; part < 2; ++part) {
		Mesh &m = part == 0 ? bottom : topsides;
		const int base = static_cast<int>(m.verts.size());
		for (int j = 0; j <= nz; ++j) {
			const double s = -0.5 + j / double(nz);
			const double taper = s > 0.15 ? std::sqrt(std::max(0.0, 1.0 - std::pow((s - 0.15) / 0.35, 2.0))) : 1.0, half = 0.5 * beam * std::max(0.02, taper);
			for (int i = 0; i <= na; ++i) {
				const double a = kPi * i / na;   // (round from one side to the other)
				if (part == 0) m.verts.push_back({-half * std::cos(a), -draft * std::pow(std::sin(a), 0.6) * (0.6 + 0.4 * taper), s * L});   // (the bottom: under the waterline)
				else m.verts.push_back({-half * std::cos(a) * (1.0 + 0.04 * std::sin(a)), free * std::sin(a) * 0.0 + (i == 0 || i == na ? 0.0 : free), s * L});
				m.normals.push_back({-std::cos(a), part == 0 ? -std::sin(a) : 1.0, 0.0});
			}
		}
		for (int j = 0; j < nz; ++j)
			for (int i = 0; i < na; ++i) {
				const int a = base + j * (na + 1) + i;
				m.tris.push_back({a, a + na + 1, a + 1}), m.tris.push_back({a + 1, a + na + 1, a + na + 2});
			}
		sdf::computeNormals(m);
	}
	deepsea::detail::add(topsides, roundedBox(0.5 * beam, 0.12 * L, 0.3 * L, 0.02 * L, 2), translate(0.0, free + 0.06 * L, -0.05 * L));   // (the cabin)
}
inline double anchorFall(double since) { return 2.4 * std::max(0.0, since - 0.3) + 1.2 * std::min(since, 0.3) * std::min(since, 0.3) / 0.6; }
// How long after it is let go the anchor has fallen `drop` metres (anchorFall turned round): when it lands, for its puff of silt.
inline double anchorTime(double drop) { return drop <= 0.18 ? std::sqrt(std::max(0.0, drop) / 2.0) : 0.3 + (drop - 0.18) / 2.4; }
// A boat at t as scene text: its hull rocking on the swell, its wake's bubbles, its anchor and chain.
inline std::string boatText(const Boat &b, double t, double level, const std::function<double(double, double)> &groundAt, const underwater::Current &c) {
	if (t < b.t0 - 1.0 || t > b.t1 + 600.0) return std::string();
	const Vec3 at = boatAt(b, t, level), d = headingOf(b.toward);
	const double L = b.length, roll = 3.0 * std::sin(0.9 * t + b.seed), pitch = 1.5 * std::sin(0.7 * t + 1.0);
	const Mat4 place = mul(translate(sealife::detail::story(at)), sealife::detail::facingOf(sealife::detail::story(d)), rotate({0, 0, 1}, roll), rotate({1, 0, 0}, pitch));
	Mesh bottom, topsides;
	hullMesh(L, bottom, topsides);
	std::string out = "# a boat (film_boat.h)\n" + attr(coated({0.42, 0.08, 0.06}, 0.4), emitMesh(bottom, place)) + attr(coated({0.9, 0.9, 0.88}, 0.2), emitMesh(topsides, place));
	// the propeller's bubbles: shed from under the stern while it is under way, rising from where it was
	std::vector<undersea::Bubble> wake;
	const auto stern = [&b, level, d](double tt) { return boatAt(b, tt, level) - d * (0.48 * b.length) + Vec3{0.0, -0.08 * b.length, 0.0}; };
	const double moving = b.speed * (travelled(b, t + 0.05) - travelled(b, t - 0.05)) / 0.1;
	if (moving > 0.05) undersea::trailBubbles(stern, t, 60.0 * std::min(1.0, moving / std::max(0.1, b.speed)), 1.6, b.seed * 17, level, c, wake);
	out += undersea::bubblesText(wake);
	if (b.anchor >= 0.0 && t >= b.anchor) {   // the anchor: falling on its chain, then lying on the sand with the chain curving up to the bow
		const Vec3 bow = boatAt(b, b.anchor + 3.0, level) + d * (0.45 * L) + Vec3{0.0, 0.05 * L, 0.0};
		const double floor = groundAt(bow.x, bow.z), since = t - b.anchor, depth = bow.y - floor;
		const double fallen = std::min(depth - 0.15, anchorFall(since));
		const Vec3 anchorAt{bow.x, bow.y - fallen, bow.z};
		const bool landed = fallen >= depth - 0.16;
		Mesh anchor;
		deepsea::detail::add(anchor, sdf::tubeMesh({{0.0, 0.0, 0.0}, {0.0, -0.6, 0.0}}, 0.03, 1.0, 6));   // (shank)
		deepsea::detail::add(anchor, sdf::tubeMesh({{-0.3, -0.45, 0.0}, {-0.2, -0.62, 0.0}, {0.0, -0.66, 0.0}, {0.2, -0.62, 0.0}, {0.3, -0.45, 0.0}}, 0.03, 0.5, 6));   // (flukes)
		deepsea::detail::add(anchor, torus(0.06, 0.012, 12, 5), mul(translate(0.0, 0.04, 0.0), rotate({1, 0, 0}, 90.0)));
		const Mat4 lie = landed ? mul(translate(sealife::detail::story(anchorAt + Vec3{0.0, -0.45, 0.0})), rotate({0, 0, 1}, 80.0)) : translate(sealife::detail::story(anchorAt));
		out += attr(metalMaterial({0.22, 0.22, 0.23}, 0.4), emitMesh(anchor, lie));
		// the chain: straight down while it falls; once landed, sagging from the bow to the anchor and lying along the sand
		Mesh chain;
		const double links = std::max(2.0, length(anchorAt - bow) / 0.08);
		const Vec3 lay = d * std::min(4.0, 0.3 * since);
		for (int k = 0; k <= static_cast<int>(links); ++k) {
			const double u = k / links;
			Vec3 p = lerp(bow, anchorAt, u);
			if (landed) p = lerp(bow, anchorAt + lay * -1.0, u) + Vec3{0.0, -std::sin(kPi * u) * 0.25 * depth, 0.0}, p.y = std::max(p.y, groundAt(p.x, p.z) + 0.03);
			deepsea::detail::add(chain, torus(0.035, 0.009, 8, 4), mul(translate(sealife::detail::story(p)), rotate({0, 1, 0}, (k % 2) * 90.0)));
		}
		out += attr(metalMaterial({0.25, 0.24, 0.23}, 0.5), emitMesh(chain, identity()));
		if (landed) out += undersea::siltText({anchorAt.x, floor, anchorAt.z}, since - anchorTime(depth - 0.16), 1.0, b.seed * 3);   // (from the moment it hits the sand)
	}
	return out;
}

// ---- the sardine run ----
inline double diveTime(const Run &r, int i) { return r.from + 1.5 + 2.6 * i + 1.2 * hash01(i, r.seed + 3); }
// Where gannet i is at t (pbrt's) and the way it faces, and how long since it hit the water (negative before).
inline Vec3 gannetAt(const Run &r, int i, double t, double level, Vec3 &fwd, double &sinceEntry) {
	const double a = 2.0 * kPi * hash01(i, r.seed + 5), off = r.radius * (0.4 + 0.8 * hash01(i, r.seed + 6));
	const Vec3 entry = Vec3{r.at.x + off * std::cos(a), level, r.at.z + off * std::sin(a)};
	const Vec3 in = normalize(Vec3{0.25 * std::cos(a + 1.0), -1.0, 0.25 * std::sin(a + 1.0)});
	const double hit = diveTime(r, i);
	sinceEntry = t - hit;
	if (sinceEntry < 0.0) {   // in the air: 20 m up, folding into the plunge at 22 m/s
		fwd = in;
		return entry - in * std::min(25.0, -sinceEntry * 22.0);
	}
	const double depth = 4.0 * (1.0 - std::exp(-sinceEntry / 0.35));   // (slowing fast in the water, to 3 or 4 m down)
	if (sinceEntry < 2.2) {
		fwd = normalize(in + Vec3{0.0, std::min(1.0, sinceEntry / 1.5) * 1.2, 0.0});
		return entry + in * depth;
	}
	const double up = std::min(1.0, (sinceEntry - 2.2) / 2.5);   // (floating back up, buoyant)
	fwd = normalize(Vec3{in.x, 0.6, in.z});
	return entry + in * (depth * (1.0 - up)) + Vec3{0.0, 0.1 * up, 0.0};
}
inline Mesh gannetMesh(double sinceEntry, Mesh &dark) {
	const double L = 0.9;
	Mesh m = sealife::detail::body(L, 0.42, 0.42, 0.0, {{0.0, -0.5}, {0.08, -0.35}, {0.13, -0.05}, {0.11, 0.2}, {0.07, 0.36}, {0.05, 0.42}, {0.0, 0.5}});
	deepsea::detail::add(dark, deepsea::detail::cone(0.02, 0.14, 6), alignY({0.0, 0.0, 0.48 * L}, {0.0, 0.0, 1.0}));   // (the beak)
	const double spread = sinceEntry > 0.4 && sinceEntry < 2.2 ? 0.6 : 0.0;   // (folded back for the plunge; half opened to swim after a fish)
	for (const double side : {-1.0, 1.0}) {
		sealife::detail::fin(m, {side * 0.05, 0.02, 0.1}, {side * (0.12 + spread * 0.5), 0.03, -0.35}, {side * 0.05, 0.02, -0.15});
		sealife::detail::fin(dark, {side * (0.12 + spread * 0.5), 0.03, -0.35}, {side * (0.14 + spread * 0.6), 0.03, -0.48}, {side * 0.08, 0.02, -0.3});
	}
	return m;
}
// A sardine run at t: the bait ball and its strikes, the dolphins herding it, the gannets diving.
inline std::string runText(const Run &r, double t, double level, const underwater::Current &c) {
	sealife::BaitBall ball;
	ball.at = r.at, ball.radius = r.radius, ball.count = 600, ball.seed = r.seed, ball.hunter = "sharks";
	for (double s = r.from + 4.0; s < r.from + 400.0; s += 6.5) ball.strikes.push_back(s);
	sealife::Life life;
	life.balls.push_back(ball);
	std::string out = "# the sardine run (film_boat.h)\n" + sealife::text(life, t);
	school::BigFish dolphins;
	dolphins.kind = "dolphins", dolphins.count = r.dolphins, dolphins.at = r.at, dolphins.radius = 2.6 * r.radius, dolphins.seed = r.seed;
	if (r.dolphins > 0) out += school::bigText(dolphins, t);
	std::vector<undersea::Bubble> bubbles;
	for (int i = 0; i < r.gannets; ++i) {
		Vec3 fwd;
		double since = 0.0;
		const Vec3 p = gannetAt(r, i, t, level, fwd, since);
		if (since < -1.2 || since > 40.0) continue;
		Mesh dark;
		const Mesh body = gannetMesh(since, dark);
		const Mat4 place = mul(translate(sealife::detail::story(p)), sealife::detail::facingOf(sealife::detail::story(fwd)));
		out += attr(coated({0.92, 0.92, 0.9}, 0.3), emitMesh(body, place)) + attr(coated({0.08, 0.08, 0.08}, 0.3), emitMesh(dark, place));
		if (since >= 0.0) {
			Vec3 f0;
			double s0 = 0.0;
			const Vec3 entry = gannetAt(r, i, diveTime(r, i) + 1e-6, level, f0, s0);
			out += giants::sprayText(entry, since, 160, 4.0, 0.012, r.seed * 31 + i);
			for (int k = 0; k < 90; ++k) {   // the column of air it drags down, rising back
				const int key = i * 1013 + k;
				const double born = diveTime(r, i) + 0.6 * hash01(key, 1);
				undersea::Bubble b;
				const Vec3 at = entry + normalize(fwd) * 0.0 + Vec3{0.0, -3.5 * hash01(key, 2), 0.0} + Vec3{0.2 * (hash01(key, 3) - 0.5), 0.0, 0.2 * (hash01(key, 4) - 0.5)};
				if (undersea::bubbleAt(at, 0.002 + 0.008 * hash01(key, 5), t - born, t, key, level, c, b)) bubbles.push_back(b);
			}
		}
	}
	return out + undersea::bubblesText(bubbles);
}
// The film's sardine runs with their heights made pbrt's (above the seabed there).
inline std::vector<Run> lifted(std::vector<Run> runs, const std::function<double(double, double)> &groundAt) {
	for (Run &r : runs) r.at.y += groundAt(r.at.x, r.at.z);
	return runs;
}

}  // namespace boat
}  // namespace cartoon
