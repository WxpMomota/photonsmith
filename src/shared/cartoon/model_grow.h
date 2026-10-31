#pragma once
// model_grow.h -- modeling round 2, parts 8 and 9: generators of living things and of whole places (`make KIND [option value]...` in a model, as model_kit.h's).
//
//   tree       kind oak|pine|palm|birch, height H: branches that split and taper (each a sweep), a crown of leaf clusters (oak, birch), tiers of needles (pine), fronds (palm)
//   bush       size S: a mound of leafy clumps
//   rocks      count N, size S: a cluster of rocks, cracked and lumpy
//   coral      size S: branching fingers
//   mushroom   size S: a stem and a spotted cap
//   creature   legs N (0 to 8), arms N (0, 2), size S, tail 1, eyes N: a body plan's creature - its pieces named body, head, eyes, pupils, mouth, leg.1 .. leg.N,
//              arm.l, arm.r, tail, so that as a character (film_model_kind.h) it walks on its legs, waves its arms, looks, blinks and talks
//   street     houses N (a side), length L, seed: two rows of houses of different heights, widths, colours and roofs, a road, pavements and lamp posts
//   room       width W, depth D, height H, furnished 1: walls with a door and a window cut through, a floor, and furniture set out by rule (a table and its chairs, a
//              shelf on a wall, a lamp)
//
// All take `colour R G B` and `seed K`; sizes default to the thing's usual size.

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "model_kit.h"

namespace cartoon {
namespace model {
namespace grow {

using kit::add;
using kit::blend;
using kit::cut;
using kit::part;
using kit::prim;

// A part moved and turned into place (its meshing first, then this: `move` and `rotate` modifiers).
inline Part placed(Part p, Vec3 at, double turnY = 0.0) {
	if (turnY != 0.0) p.mods.push_back({"rotate", {0.0, turnY, 0.0}});
	p.mods.push_back({"move", {at.x, at.y, at.z}});
	return p;
}

// ---- trees ----
// Branches from `base` along `dir`, `len` long and `r` thick at the base, splitting `depth` more times: each a tapered sweep into `wood`, its tips' places into `tips`.
inline void branch(Part &wood, std::vector<Vec3> &tips, Vec3 base, Vec3 dir, double len, double r, int depth, int seed, double spread) {
	dir = normalize(dir);
	const Vec3 side = normalize(std::abs(dir.y) < 0.9 ? cross(dir, Vec3{0, 1, 0}) : cross(dir, Vec3{1, 0, 0}));
	const Vec3 bend = side * (0.12 * len * (hash01(seed, 1) - 0.5)) + Vec3{0, 0.05 * len, 0};
	const Vec3 mid = base + dir * (0.5 * len) + bend, end = base + dir * len + bend * 1.5;
	Sweep sw;
	sw.profile = circleProfile(r, 10), sw.path = {base - dir * (0.3 * r), mid, end}, sw.taper = 0.62;
	wood.sweeps.push_back(sw);
	if (depth <= 0) {
		tips.push_back(end);
		return;
	}
	const int n = 2 + static_cast<int>(hash01(seed, 2) * 2.0);
	for (int k = 0; k < n; ++k) {
		const double a = 2.0 * kPi * (k + hash01(seed * 7 + k, 3)) / n, tilt = spread * (0.7 + 0.6 * hash01(seed * 7 + k, 4));
		const Vec3 other = cross(dir, side);
		const Vec3 d = normalize(dir * std::cos(tilt) + (side * std::cos(a) + other * std::sin(a)) * std::sin(tilt));
		branch(wood, tips, end, d, len * (0.62 + 0.15 * hash01(seed * 7 + k, 5)), r * 0.62, depth - 1, seed * 31 + k + 1, spread);
	}
}
// A clump of leaves: overlapping blobs about `at`, `size` across, into `leaves` (meshed, lumpy).
inline void clump(Part &leaves, Vec3 at, double size, int seed) {
	for (int k = 0; k < 5; ++k) {
		const Vec3 off{size * (hash01(seed + k, 1) - 0.5), 0.6 * size * (hash01(seed + k, 2) - 0.4), size * (hash01(seed + k, 3) - 0.5)};
		leaves.sdf.steps.push_back(blend(prim("sphere", at + off * 0.6, {size * (0.3 + 0.15 * hash01(seed + k, 4)), 0, 0}), 0.25 * size));
	}
}
inline std::vector<Part> tree(const std::string &kind, double H, const Vec3 *colour, int seed) {
	Part wood = part("trunk", "matte", kind == "birch" ? Vec3{0.85, 0.83, 0.78} : Vec3{0.36, 0.24, 0.15}), leaves = part("leaves", "matte", colour ? *colour : Vec3{0.25, 0.5, 0.18});
	if (kind == "pine") {
		Sweep trunk;
		trunk.profile = circleProfile(0.04 * H, 10), trunk.path = {{0, 0, 0}, {0, 0.5 * H, 0}, {0, H, 0}}, trunk.taper = 0.3;
		wood.sweeps.push_back(trunk);
		for (int k = 0; k < 5; ++k) {   // tiers of needles, each a cone, smaller up the tree
			const double y = H * (0.25 + 0.15 * k), r = 0.32 * H * (1.0 - 0.17 * k);
			leaves.sdf.steps.push_back(blend(prim("cone", {0, y, 0}, {r, 0.02 * H, 0.32 * H}), 0.03 * H));
		}
		leaves.colour = colour ? *colour : Vec3{0.12, 0.35, 0.2};
		leaves.mods.push_back({"displace", {0}});
		leaves.displaces.push_back({"noise", 0.04 * H, 0.15 * H, seed});
	} else if (kind == "palm") {
		Sweep trunk;
		trunk.profile = circleProfile(0.035 * H, 10), trunk.path = {{0, 0, 0}, {0.08 * H, 0.5 * H, 0}, {0.18 * H, H, 0}}, trunk.taper = 0.7;
		wood.sweeps.push_back(trunk);
		wood.mods.push_back({"displace", {0}});
		wood.displaces.push_back({"ribs", 0.006 * H, 0.05 * H, seed});
		const Vec3 top{0.18 * H, H, 0};
		for (int k = 0; k < 8; ++k) {   // the fronds: flat sweeps arching out and down
			const double a = 2.0 * kPi * (k + 0.3 * hash01(seed + k, 1)) / 8.0;
			const Vec3 out{std::cos(a), 0, std::sin(a)};
			Sweep frond;
			frond.profile = rectProfile(0.12 * H, 0.008 * H), frond.path = {top, top + out * (0.2 * H) + Vec3{0, 0.08 * H, 0}, top + out * (0.42 * H) - Vec3{0, 0.12 * H, 0}};
			frond.taper = 0.15;
			leaves.sweeps.push_back(frond);
		}
	} else {   // oak, birch: branches and a crown of clumps at their tips
		std::vector<Vec3> tips;
		branch(wood, tips, {0, 0, 0}, {0, 1, 0}, 0.42 * H, 0.05 * H, kind == "birch" ? 2 : 3, seed, kind == "birch" ? 0.35 : 0.6);
		for (std::size_t k = 0; k < tips.size(); ++k) clump(leaves, tips[k], (kind == "birch" ? 0.22 : 0.3) * H, seed * 101 + static_cast<int>(k));
		leaves.cell = 0.02 * H;
	}
	return {wood, leaves};
}

// ---- small living things ----
inline std::vector<Part> mushroom(double S, const Vec3 *colour, int seed) {
	Part stem = part("stem", "matte", {0.92, 0.88, 0.78}), cap = part("cap", "plastic", colour ? *colour : Vec3{0.8, 0.12, 0.08}, 0.4);
	Loft l;
	l.rings = {{0, 0.07 * S, 0.07 * S}, {0.2 * S, 0.05 * S, 0.05 * S}, {0.38 * S, 0.06 * S, 0.06 * S}};
	stem.lofts.push_back(l);
	Loft c;
	for (int k = 0; k <= 8; ++k) {
		const double u = k / 8.0, r = 0.28 * S * std::sqrt(std::max(0.0, 1.0 - u * u));
		c.rings.push_back({0.36 * S + 0.18 * S * u, std::max(0.004 * S, r), std::max(0.004 * S, r)});
	}
	cap.lofts.push_back(c);
	cap.paint.on = true, cap.paint.base = cap.colour, cap.paint.spot = {0.97, 0.95, 0.9}, cap.paint.spotAmount = 0.25, cap.paint.seed = seed, cap.paint.res = 256;
	return {stem, cap};
}
inline std::vector<Part> rocks(int count, double S, const Vec3 *colour, int seed) {
	Part r = part("rocks", "matte", colour ? *colour : Vec3{0.5, 0.48, 0.45});
	for (int k = 0; k < std::clamp(count, 1, 40); ++k) {
		const double a = 2.0 * kPi * hash01(seed + k, 1), d = S * 0.9 * std::sqrt(hash01(seed + k, 2)) * (k > 0), s = S * (0.4 + 0.6 * hash01(seed + k, 3)) * (k == 0 ? 1.3 : 0.8);
		r.sdf.steps.push_back(add(prim("ellipsoid", {d * std::cos(a), 0.25 * s, d * std::sin(a)}, {s, 0.7 * s, 0.85 * s}, 0.0, {0, 360.0 * hash01(seed + k, 4), 0})));
	}
	r.mods.push_back({"displace", {0}});
	r.displaces.push_back({"rock", 0.12 * S, 0.5 * S, seed});
	return {r};
}
inline std::vector<Part> coral(double S, const Vec3 *colour, int seed) {
	Part c = part("coral", "plastic", colour ? *colour : Vec3{0.95, 0.45, 0.4}, 0.5);
	std::vector<Vec3> tips;
	for (int k = 0; k < 3; ++k) {
		const double a = 2.0 * kPi * k / 3.0 + hash01(seed, k);
		branch(c, tips, {0.05 * S * std::cos(a), 0, 0.05 * S * std::sin(a)}, {0.3 * std::cos(a), 1, 0.3 * std::sin(a)}, 0.35 * S, 0.05 * S, 2, seed * 13 + k, 0.5);
	}
	for (const Vec3 &t : tips) c.sdf.steps.push_back(add(prim("sphere", t, {0.022 * S, 0, 0})));   // (round tips)
	return {c};
}

// ---- a creature from a body plan ----
inline std::vector<Part> creature(const std::map<std::string, double> &opt, const Vec3 *colour, int seed) {
	const auto o = [&](const std::string &k, double d) { return opt.count(k) ? opt.at(k) : d; };
	const double S = o("size", 1.0);
	const int legs = std::clamp(static_cast<int>(o("legs", 2)), 0, 8), arms = std::clamp(static_cast<int>(o("arms", 2)), 0, 2), eyes = std::clamp(static_cast<int>(o("eyes", 2)), 1, 3);
	const Vec3 skin = colour ? *colour : lerp(Vec3{0.4, 0.75, 0.3}, Vec3{0.85, 0.5, 0.75}, hash01(seed, 9));
	const double hip = legs > 0 ? (legs == 2 ? 0.35 : 0.25) * S : 0.0;   // (how high its legs hold its body)
	const double bodyH = (legs == 2 ? 0.55 : 0.4) * S, bodyW = (legs > 2 ? 0.45 : 0.4) * S, bodyD = (legs > 2 ? 0.75 : 0.35) * S;
	Part body = part("body", "plastic", skin, 0.45), head = part("head", "plastic", skin, 0.45), white = part("eyes", "plastic", {0.97, 0.97, 0.95}, 0.15),
		 pupil = part("pupils", "plastic", {0.03, 0.03, 0.04}, 0.1), mouth = part("mouth", "matte", {0.35, 0.05, 0.08});
	const Vec3 bodyC{0, hip + 0.5 * bodyH, 0};
	body.sdf.steps = {add(prim("ellipsoid", bodyC, {bodyW, bodyH, bodyD}))};
	const Vec3 headC = legs > 2 ? Vec3{0, hip + 0.85 * bodyH, 0.5 * bodyD} : Vec3{0, hip + bodyH + 0.12 * S, 0.02 * S};
	const double hr = 0.17 * S;
	head.sdf.steps = {add(prim("sphere", headC, {hr, 0, 0}))};
	for (int k = 0; k < eyes; ++k) {
		const double x = eyes == 1 ? 0.0 : (k - 0.5 * (eyes - 1)) * 0.55 * hr;
		const Vec3 e = headC + Vec3{x, 0.25 * hr, 0.85 * hr};
		white.sdf.steps.push_back(add(prim("sphere", e, {0.28 * hr, 0, 0})));
		pupil.sdf.steps.push_back(add(prim("sphere", e + Vec3{0, 0, 0.2 * hr}, {0.13 * hr, 0, 0})));
	}
	mouth.sdf.steps = {add(prim("ellipsoid", headC + Vec3{0, -0.35 * hr, 0.88 * hr}, {0.5 * hr, 0.12 * hr, 0.2 * hr}))};
	std::vector<Part> parts = {body, head, white, pupil, mouth};
	for (int k = 0; k < legs; ++k) {   // legs: pairs down the body, left and right
		const int row = k / 2, rows = std::max(1, legs / 2);
		const double side = k % 2 == 0 ? 1.0 : -1.0, z = rows == 1 ? 0.0 : bodyD * (0.35 - 0.7 * row / (rows - 1));
		Part leg = part("leg." + std::to_string(k + 1), "plastic", skin * 0.85, 0.5);
		Prim c = prim("capsule", {side * 0.45 * bodyW, hip + 0.1 * S, z}, {0.06 * S, 0, 0});
		c.to = {side * 0.5 * bodyW, 0.05 * S, z};
		leg.sdf.steps = {add(c), blend(prim("ellipsoid", {side * 0.5 * bodyW, 0.03 * S, z + 0.04 * S}, {0.14 * S, 0.06 * S, 0.2 * S}), 0.02 * S)};
		parts.push_back(leg);
	}
	for (int k = 0; k < arms; ++k) {
		const double side = k == 0 ? 1.0 : -1.0;
		Part arm = part(k == 0 ? "arm.l" : "arm.r", "plastic", skin * 0.9, 0.5);
		Prim c = prim("capsule", {side * 0.48 * bodyW, hip + 0.82 * bodyH, 0}, {0.05 * S, 0, 0});
		c.to = {side * 0.72 * bodyW, hip + 0.4 * bodyH, 0.02 * S};
		arm.sdf.steps = {add(c), blend(prim("sphere", c.to, {0.07 * S, 0, 0}), 0.02 * S)};
		parts.push_back(arm);
	}
	if (o("tail", legs > 2 ? 1.0 : 0.0) > 0.5) {
		Part tail = part("tail", "plastic", skin * 0.9, 0.5);
		Sweep sw;
		sw.profile = circleProfile(0.05 * S, 10), sw.taper = 0.2;
		sw.path = {bodyC + Vec3{0, 0, -0.45 * bodyD}, bodyC + Vec3{0, 0.1 * S, -0.75 * bodyD}, bodyC + Vec3{0, 0.25 * S, -0.9 * bodyD}};
		tail.sweeps.push_back(sw);
		parts.push_back(tail);
	}
	return parts;
}

// ---- places ----
inline std::vector<Part> street(const std::map<std::string, double> &opt, int seed) {
	const auto o = [&](const std::string &k, double d) { return opt.count(k) ? opt.at(k) : d; };
	const int houses = std::clamp(static_cast<int>(o("houses", 4)), 1, 20);
	const double L = o("length", houses * 9.0), roadW = 7.0, pave = 2.5;
	std::vector<Part> parts;
	Part road = part("road", "matte", {0.18, 0.18, 0.2}), walk = part("pavement", "matte", {0.62, 0.6, 0.57}), lines = part("lines", "matte", {0.95, 0.92, 0.8});
	road.sdf.steps = {add(prim("box", {0, -0.05, 0}, {L, 0.1, roadW}))}, road.hard = true;
	for (const double z : {1.0, -1.0}) walk.sdf.steps.push_back(add(prim("box", {0, 0.05, z * (0.5 * roadW + 0.5 * pave)}, {L, 0.2, pave})));
	walk.hard = true;
	for (double x = -0.5 * L + 1.5; x < 0.5 * L; x += 4.0) lines.sdf.steps.push_back(add(prim("box", {x, 0.005, 0}, {2.0, 0.02, 0.15})));
	lines.hard = true, lines.cell = 0.04;
	parts = {road, walk, lines};
	Part posts = part("lamp-posts", "metal", {0.2, 0.2, 0.22}, 0.4), bulbs = part("lamps", "glow", {1.0, 0.85, 0.6});
	bulbs.glow = 4.0;
	for (int side = 0; side < 2; ++side)
		for (int k = 0; k < houses; ++k) {
			const double sz = side == 0 ? 1.0 : -1.0, x = -0.5 * L + (k + 0.5) * L / houses;
			std::map<std::string, double> b{{"floors", 1 + static_cast<int>(hash01(seed * 31 + side * 97 + k, 1) * 4.0)}, {"width", 6.0 + 2.5 * hash01(seed * 31 + side * 97 + k, 2)},
											{"depth", 6.0}, {"windows", 2 + static_cast<int>(hash01(seed * 31 + side * 97 + k, 3) * 2.0)}, {"seed", seed * 31 + side * 97 + k}};
			if (hash01(seed * 31 + side * 97 + k, 4) > 0.5) b["roofgable"] = 1.0;
			const Vec3 wall = lerp(Vec3{0.92, 0.82, 0.68}, Vec3{0.75, 0.5, 0.45}, hash01(seed * 31 + side * 97 + k, 5));
			std::string error;
			for (Part p : kit::make("building", b, &wall, error)) {
				p.name += "." + std::to_string(side) + "." + std::to_string(k);
				parts.push_back(placed(p, {x, 0.15, sz * (0.5 * roadW + pave + 3.2)}, side == 0 ? 180.0 : 0.0));   // (each faces the road)
			}
			Prim post = prim("capsule", {x + 0.5 * L / houses, 0.15, sz * (0.5 * roadW + 0.4)}, {0.06, 0, 0});
			post.to = post.at + Vec3{0, 4.2, 0};
			posts.sdf.steps.push_back(add(post));
			bulbs.sdf.steps.push_back(add(prim("sphere", post.to + Vec3{0, 0.2, 0}, {0.22, 0, 0})));
		}
	parts.push_back(posts), parts.push_back(bulbs);
	return parts;
}
inline std::vector<Part> room(const std::map<std::string, double> &opt, const Vec3 *colour, int seed) {
	const auto o = [&](const std::string &k, double d) { return opt.count(k) ? opt.at(k) : d; };
	const double W = o("width", 5.0), D = o("depth", 4.0), H = o("height", 2.7), t = 0.12;
	Part walls = part("walls", "matte", colour ? *colour : Vec3{0.88, 0.85, 0.78}), floor = part("floor", "plastic", {0.55, 0.38, 0.24}, 0.35);
	walls.hard = true, walls.cell = 0.04;
	walls.sdf.steps = {add(prim("box", {0, 0.5 * H, -0.5 * D}, {W + 2 * t, H, t})), add(prim("box", {-0.5 * W, 0.5 * H, 0}, {t, H, D})), add(prim("box", {0.5 * W, 0.5 * H, 0}, {t, H, D}))};
	walls.sdf.steps.push_back(cut(prim("box", {0.25 * W, 1.05, -0.5 * D}, {0.95, 2.1, 0.5})));        // (the door, in the back wall)
	walls.sdf.steps.push_back(cut(prim("box", {-0.5 * W, 1.45, -0.1 * D}, {0.5, 1.1, 1.3})));         // (the window, in the left wall)
	floor.sdf.steps = {add(prim("box", {0, -0.025, 0}, {W + 2 * t, 0.05, D}))}, floor.hard = true;
	floor.paint.on = true, floor.paint.base = floor.colour, floor.paint.stripe = floor.colour * 0.8, floor.paint.stripeWidth = 0.18, floor.paint.stripeAxis = 'x', floor.paint.mottle = 0.1;
	std::vector<Part> parts = {walls, floor};
	if (o("furnished", 1.0) > 0.5) {   // the rules: a table in the middle, a chair at each long side, a shelf against the right wall, a lamp in the back corner
		std::string error;
		const auto put = [&](const std::string &kind, Vec3 at, double turn, const std::string &suffix) {
			for (Part p : kit::make(kind, {}, nullptr, error)) p.name += suffix, parts.push_back(placed(p, at, turn));
		};
		put("table", {0, 0, 0.1 * D}, 0.0, ".1");
		put("chair", {0, 0, 0.1 * D + 0.6}, 180.0, ".1");
		put("chair", {0, 0, 0.1 * D - 0.6}, 0.0, ".2");
		put("shelf", {0.5 * W - 0.2, 0, -0.1 * D}, -90.0, ".1");
		put("lamp", {-0.5 * W + 0.4, 0, -0.5 * D + 0.4}, 0.0, ".1");
	}
	(void)seed;
	return parts;
}

// Any generator's parts: model_kit.h's, then these. `error` says when the kind is none of them.
inline std::vector<Part> makeAny(const std::string &kind, const std::map<std::string, double> &opt, const Vec3 *colour, std::string &error) {
	const auto o = [&](const std::string &k, double d) { return opt.count(k) ? opt.at(k) : d; };
	const int seed = static_cast<int>(o("seed", 1));
	error.clear();
	if (kind == "tree") {
		const int k = static_cast<int>(o("kind", 0));   // (kind as a word: film_model.h turns `kind pine` into kind 1, palm 2, birch 3)
		return tree(k == 1 ? "pine" : k == 2 ? "palm" : k == 3 ? "birch" : "oak", o("height", 6.0), colour, seed);
	}
	if (kind == "bush") {
		Part leaves = part("leaves", "matte", colour ? *colour : Vec3{0.22, 0.45, 0.16});
		const double S = o("size", 1.0);
		for (int k = 0; k < 6; ++k) clump(leaves, {0.4 * S * (hash01(seed + k, 1) - 0.5), 0.35 * S + 0.2 * S * hash01(seed + k, 2), 0.4 * S * (hash01(seed + k, 3) - 0.5)}, 0.6 * S, seed * 7 + k);
		return {leaves};
	}
	if (kind == "rocks") return rocks(static_cast<int>(o("count", 5)), o("size", 0.6), colour, seed);
	if (kind == "coral") return coral(o("size", 0.6), colour, seed);
	if (kind == "mushroom") return mushroom(o("size", 0.3), colour, seed);
	if (kind == "creature") return creature(opt, colour, seed);
	if (kind == "street") return street(opt, seed);
	if (kind == "room") return room(opt, colour, seed);
	std::vector<Part> parts = kit::make(kind, opt, colour, error);
	if (!error.empty()) error = "`make " + kind + "`: building, chair, table, shelf, lamp, car, rocket, boat, tree, bush, rocks, coral, mushroom, creature, street or room";
	return parts;
}

}  // namespace grow
}  // namespace model
}  // namespace cartoon
