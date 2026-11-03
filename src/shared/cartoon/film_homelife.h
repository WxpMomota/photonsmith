#pragma once
// film_homelife.h -- what goes on at home (indoor round 2): the small things characters carry about, cooking in a kitchen, bath time, and parties - each drawn at a moment from
// its lines (film_rooms.h Home; film_parse_home.h reads them, film_homeworld.cpp puts them in the frames and on the soundtrack).
//
//   thing NAME KIND on ITEM [in ROOM]        a mug, a plate, a bowl, a book, a vase, a ball, a box, a bottle, a block, a cushion or a shoe on a piece's top (a prop: `pick`, `put`,
//                                            `give` take it, and `carry WHO NAME to ITEM` walks it over)
//   cook ROOM pot|kettle|toast|oven|meal at T [until T]
//       pot     a pan on the hob's front ring, its blue flame lit, steam rising once it boils (4 s on) and its lid rattling
//       kettle  on the back ring: it rumbles, and 6 s on whistles, a jet of steam from its spout, until `until` (or 6 s more)
//       toast   a toaster on the counter: the bread goes down, and 5 s on pops up
//       oven    the oven's window glowing (an `open` swings its door down)
//       meal    a plate at each chair round the table, eaten up from T to `until`
//   bath ROOM at T [until T]                 the bath filling from its tap over 10 s, bubbles on it; let out at `until`
//   shower ROOM at T [until T]               a shower over the bath's end running; steam fogs the mirror over the basin as hot water runs, clearing a minute after
//   party ROOM birthday|christmas|halloween|plain [at T] [until T]
//                                            balloons tied about the room and streamers from the ceiling; a birthday cake with candles on the table (`blowout WHO`), a Christmas
//                                            tree with twinkling lights and presents in a corner, or pumpkins with glowing faces
//   confetti ROOM at T                       a burst of it from the ceiling, falling for 4 s and lying where it lands
//
// Coordinates are a room's (film_rooms.h): story space, x mirrored from pbrt's.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_furniture.h"
#include "film_roomlife.h"
#include "film_rooms.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace rooms {

// ---- things on a piece's top ----
// The height things stand at on a piece (local): its top, a counter's worktop, a mantelpiece, a seat.
inline double topOf(const Item &it) {
	if (it.kind == "counter") return 0.9;
	if (it.kind == "fireplace") return 1.16;
	if (it.kind == "tv") return 0.44;
	if (it.kind == "till") return 0.94;
	if (it.seat >= 0.0) return it.seat + 0.02;
	if (it.kind == "bookshelf" || it.kind == "shelves" || it.kind == "cabinet" || it.kind == "wardrobe") return std::min(it.h, 1.2);
	return it.h;
}
// A place on a piece's top near its front edge (so a hand reaches it from in front), `along` -0.5 .. 0.5 across it: story space.
inline Vec3 onTopOf(const Room &r, const Item &it, double along) {
	const Mat4 I = detail::itemFrame(frameOf(r), it);
	const double front = it.kind == "bookshelf" || it.kind == "shelves" ? 0.0 : 0.5 * it.d - std::min(0.15, 0.3 * it.d);
	return transformPoint(I, {along * std::max(0.0, it.w - 0.25), topOf(it), front});
}
inline const std::vector<std::string> &thingKinds() {
	static const std::vector<std::string> k = {"mug", "plate", "bowl", "book", "vase", "ball", "box", "bottle", "block", "cushion", "shoe"};
	return k;
}
inline bool fragile(const std::string &kind) { return kind == "mug" || kind == "plate" || kind == "bowl" || kind == "vase" || kind == "bottle"; }
inline bool holdsWater(const std::string &kind) { return kind == "mug" || kind == "vase" || kind == "bottle" || kind == "bowl"; }
inline Vec3 thingColour(const std::string &kind, const std::string &name) {
	int h = 0;
	for (const char ch : name) h = h * 31 + ch;
	return bitColour(kind, std::abs(h) % 5);
}
// A thing at `at` (story space, its base), turned `turn` degrees and tipped `tip` about its side: scene text.
inline std::string thingText(const std::string &kind, const std::string &name, Vec3 at, double turn, double tip = 0.0) {
	const Mat4 M = mul(translate(at), rotate({0.0, 1.0, 0.0}, turn), rotate({1.0, 0.0, 0.0}, tip));
	return attr(coated(thingColour(kind, name), 0.3), emitMesh(bitMesh(kind), M));
}

// ---- puffs of steam ----
// Puffs rising from `at` (story space) for `amount` 0 .. 1: each born on a cycle of its own, growing and drifting as it goes up `height`.
inline void steamPuffs(Mesh &m, Vec3 at, double t, double amount, double height, double size, int seed, Vec3 drift = {0.0, 0.0, 0.0}) {
	if (amount <= 0.01) return;
	const int n = static_cast<int>(std::ceil(10.0 * amount));
	for (int k = 0; k < n; ++k) {
		const double cycle = 1.6 + 0.8 * hash01(seed + k, 1), u = std::fmod(t / cycle + hash01(seed + k, 2), 1.0);
		const double r = size * (0.4 + 1.4 * u) * (1.0 - 0.6 * u * u);
		const Vec3 p = at + Vec3{0.05 * std::sin(6.0 * hash01(seed + k, 3) + 2.0 * t), height * u, 0.05 * std::cos(6.0 * hash01(seed + k, 4) + 1.7 * t)} + drift * u;
		detail::addMesh(m, uvSphere(1.0, 8, 6), mul(translate(p), scale(r, r * 0.8, r)));
	}
}
inline std::string steamText(const Mesh &puffs) {
	return puffs.tris.empty() ? std::string() : attr(translucentMaterial({0.82, 0.83, 0.85}, {0.6, 0.6, 0.62}), emitMesh(puffs, identity()));
}

// ---- cooking ----
inline const Item *findItem(const Room &r, const std::string &kind) {
	for (const Item &it : r.items)
		if (it.kind == kind) return &it;
	return nullptr;
}
// What a `cook` line puts in its room at t.
inline std::string cookingText(const Room &r, const Doing &d, double t) {
	if (t < d.t0 - 0.01 && d.what != "meal") return std::string();
	const Mat4 F = frameOf(r);
	const bool on = t >= d.t0 && t < d.t1;
	std::string out;
	Mesh steam;
	if (d.what == "pot" || d.what == "kettle") {
		const Item *stove = findItem(r, "stove");
		if (!stove) return std::string();
		const Mat4 I = detail::itemFrame(F, *stove);
		const bool pot = d.what == "pot";
		const Vec3 ring = pot ? Vec3{-0.15, 0.9, 0.14} : Vec3{0.15, 0.9, -0.14};
		if (on)   // its gas flame: a ring of little blue tongues
			for (int q = 0; q < 8; ++q) {
				const double a = 2.0 * kPi * q / 8.0, flick = 0.8 + 0.2 * std::sin(17.0 * t + 2.3 * q);
				out += emissiveSphere(transformPoint(I, ring + Vec3{0.07 * std::cos(a), 0.008, 0.07 * std::sin(a)}), 0.008, Vec3{0.25, 0.45, 1.0} * (40.0 * flick));
			}
		Mesh body, dark;
		if (pot) {
			const double boil = smoothstep(d.t0 + 3.0, d.t0 + 5.0, t) * (on ? 1.0 : 1.0 - smoothstep(d.t1, d.t1 + 6.0, t)), rattle = boil > 0.5 && on ? 0.006 * std::abs(std::sin(23.0 * t)) : 0.0;
			detail::addMesh(body, lathe({{0.0, 0.0}, {0.11, 0.0}, {0.115, 0.13}, {0.105, 0.13}, {0.1, 0.01}, {0.0, 0.01}}, 24), mul(I, translate(ring)));
			detail::addMesh(body, lathe({{0.0, 0.135}, {0.118, 0.13}, {0.118, 0.137}, {0.03, 0.15}, {0.0, 0.15}}, 24), mul(I, translate(ring + Vec3{0.0, rattle, 0.0})));
			detail::addMesh(dark, uvSphere(0.018, 10, 6), mul(I, translate(ring + Vec3{0.0, 0.16 + rattle, 0.0})));
			detail::addMesh(dark, sdf::tubeMesh({ring + Vec3{0.11, 0.1, 0.0}, ring + Vec3{0.3, 0.11, 0.02}}, 0.012, 1.0, 6), I);   // (its handle)
			steamPuffs(steam, transformPoint(I, ring + Vec3{0.0, 0.17, 0.0}), t, boil, 0.55, 0.05, r.seed * 7 + 1);
		} else {
			const double boiled = d.t0 + 6.0, stop = std::min(d.t1, d.t0 + 12.0), jet = smoothstep(boiled, boiled + 0.5, t) * (1.0 - smoothstep(stop, stop + 1.0, t));
			detail::addMesh(body, lathe({{0.0, 0.0}, {0.09, 0.0}, {0.1, 0.06}, {0.085, 0.16}, {0.04, 0.2}, {0.0, 0.205}}, 24), mul(I, translate(ring)));
			detail::addMesh(body, sdf::tubeMesh({ring + Vec3{0.08, 0.05, 0.0}, ring + Vec3{0.16, 0.12, 0.0}, ring + Vec3{0.19, 0.17, 0.0}}, 0.014, 0.6, 8), I);   // (its spout)
			detail::addMesh(dark, torus(0.07, 0.01, 20, 6), mul(I, translate(ring + Vec3{0.0, 0.2, 0.0}), rotate({1.0, 0.0, 0.0}, 90.0)));   // (its handle over its top)
			steamPuffs(steam, transformPoint(I, ring + Vec3{0.2, 0.18, 0.0}), t, jet, 0.4, 0.03, r.seed * 7 + 2, transformPoint(I, {0.25, 0.0, 0.0}) - transformPoint(I, {0.0, 0.0, 0.0}));
		}
		out += attr(metalMaterial({0.75, 0.74, 0.72}, 0.15), emitMesh(body, identity())) + attr(coated({0.05, 0.05, 0.05}, 0.3), emitMesh(dark, identity()));
	} else if (d.what == "toast") {
		const Item *counter = findItem(r, "counter");
		if (!counter) return std::string();
		const Mat4 I = detail::itemFrame(F, *counter);
		const Vec3 at{0.5 * counter->w - 0.3, 0.9, -0.08};
		const double popped = d.t0 + 5.0, pop = t < popped ? 0.0 : std::max(0.0, 0.09 + 0.05 * std::exp(-6.0 * (t - popped)) * std::cos(18.0 * (t - popped)) - 0.09 * std::exp(-30.0 * (t - popped)));
		Mesh shell, toast;
		detail::addBox(shell, I, at + Vec3{0.0, 0.09, 0.0}, {0.27, 0.18, 0.15});
		detail::addBox(shell, I, at + Vec3{0.15, 0.07, 0.0}, {0.02, 0.03, 0.02});   // (its lever, down while it toasts)
		const double brown = smoothstep(d.t0, popped, t);
		for (const double z : {-0.03, 0.03}) detail::addBox(toast, I, at + Vec3{0.0, 0.13 + pop, z}, {0.11, 0.11, 0.014});
		out += attr(metalMaterial({0.8, 0.8, 0.82}, 0.12), emitMesh(shell, identity())) + attr(diffuseMaterial(lerp(Vec3{0.92, 0.84, 0.66}, Vec3{0.62, 0.36, 0.14}, brown)), emitMesh(toast, identity()));
	} else if (d.what == "oven") {
		const Item *stove = findItem(r, "stove");
		if (!stove || !on) return std::string();
		const Mat4 I = detail::itemFrame(F, *stove);
		const Vec3 glow = Vec3{1.0, 0.55, 0.2} * (4.0 * smoothstep(d.t0, d.t0 + 1.0, t));
		Mesh window;
		detail::addBox(window, I, {0.0, 0.4, 0.5 * stove->d + 0.034}, {0.32, 0.16, 0.004});
		out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(window, identity()), &glow);
	} else if (d.what == "meal") {
		const Item *table = findItem(r, "table");
		if (!table) table = findItem(r, "coffeetable");
		if (!table || t < d.t0) return std::string();
		const Mat4 T = mul(F, translate(table->x, 0.0, table->z), rotate({0.0, 1.0, 0.0}, table->turn)), Ti = invertAffine(mul(translate(table->x, 0.0, table->z), rotate({0.0, 1.0, 0.0}, table->turn)));
		std::vector<Vec3> places;   // (a place at the table's edge nearest each chair round it, else two)
		for (const Item &c : r.items)
			if (c.kind == "chair") {
				const Vec3 p = transformPoint(Ti, {c.x, 0.0, c.z});
				if (std::abs(p.x) < 0.5 * table->w + 0.6 && std::abs(p.z) < 0.5 * table->d + 0.6) places.push_back({std::clamp(p.x, -0.5 * table->w + 0.15, 0.5 * table->w - 0.15), 0.0, std::clamp(p.z, -0.5 * table->d + 0.15, 0.5 * table->d - 0.15)});
			}
		if (places.empty()) places = {{-0.25 * table->w, 0.0, 0.0}, {0.25 * table->w, 0.0, 0.0}};
		const double left = 1.0 - smoothstep(d.t0 + 1.0, std::min(d.t1, d.t0 + 600.0), t);
		Mesh plates, food, cutlery;
		for (std::size_t k = 0; k < places.size(); ++k) {
			const Vec3 p = places[k] + Vec3{0.0, table->h, 0.0};
			detail::addMesh(plates, bitMesh("plate"), mul(T, translate(p)));
			if (left > 0.02) {   // (a mound of it, smaller as it is eaten, a little different on each plate)
				const double s = std::cbrt(left), w = 0.07 * s * (0.85 + 0.3 * hash01(static_cast<int>(k), r.seed));
				detail::addMesh(food, uvSphere(1.0, 14, 8), mul(T, translate(p + Vec3{0.0, 0.015, 0.0}), scale(w, 0.035 * s, w * 0.9)));
			}
			const Vec3 side = normalize(Vec3{-p.z, 0.0, p.x} + Vec3{1e-6, 0.0, 0.0});
			detail::addMesh(cutlery, sdf::tubeMesh({p + side * 0.14 + Vec3{0.0, 0.004, -0.08}, p + side * 0.14 + Vec3{0.0, 0.004, 0.08}}, 0.005, 1.0, 5), T);
		}
		out += attr(coated({0.92, 0.92, 0.9}, 0.2), emitMesh(plates, identity())) + attr(metalMaterial({0.8, 0.8, 0.8}, 0.2), emitMesh(cutlery, identity()));
		if (!food.tris.empty()) out += attr(diffuseMaterial({0.75, 0.42, 0.16}), emitMesh(food, identity()));
		Mesh puffs;   // (hot when it is served)
		for (std::size_t k = 0; k < places.size(); ++k) steamPuffs(puffs, transformPoint(T, places[k] + Vec3{0.0, table->h + 0.05, 0.0}), t, left * (1.0 - smoothstep(d.t0, d.t0 + 25.0, t)), 0.3, 0.025, r.seed * 11 + static_cast<int>(k));
		out += steamText(puffs);
	}
	return out + steamText(steam);
}

// ---- bath time ----
// The bath's water at t: its depth (0 .. 1 full), and whether the tap or the shower runs; steam 0 .. 1 (fogging a mirror, clearing slowly).
inline double bathLevel(const std::vector<Doing> &doings, int room, double t) {
	double v = 0.0;
	for (const Doing &d : doings)
		if (d.verb == "bath" && d.roomIndex == room) v = std::max(v, smoothstep(d.t0, d.t0 + 10.0, t) * (1.0 - smoothstep(d.t1, d.t1 + 8.0, t)));
	return v;
}
inline bool waterRuns(const std::vector<Doing> &doings, int room, double t, bool *shower = nullptr) {
	for (const Doing &d : doings) {
		if (d.roomIndex != room) continue;
		if (d.verb == "shower" && t >= d.t0 && t < d.t1) {
			if (shower) *shower = true;
			return true;
		}
		if (d.verb == "bath" && t >= d.t0 && t < d.t0 + 10.0 && t < d.t1) return true;
	}
	return false;
}
inline double steamOf(const std::vector<Doing> &doings, int room, double t) {
	double s = 0.0;
	for (const Doing &d : doings) {
		if (d.roomIndex != room || (d.verb != "bath" && d.verb != "shower")) continue;
		const double end = d.verb == "bath" ? std::min(d.t1, d.t0 + 10.0) : d.t1;
		if (t < d.t0) continue;
		const double built = smoothstep(0.0, 25.0, std::min(t, end) - d.t0);   // (hot water's steam builds over 25 s, clears over a minute after)
		s = std::max(s, built * (t <= end ? 1.0 : std::exp(-(t - end) / 25.0)));
	}
	return s;
}
// The bath's water, its bubbles, the tap's stream and the shower's spray at t.
inline std::string bathText(const Room &r, const std::vector<Doing> &doings, int room, double t) {
	const Item *bath = findItem(r, "bath");
	if (!bath) return std::string();
	const Mat4 I = detail::itemFrame(frameOf(r), *bath);
	const double level = bathLevel(doings, room, t);
	bool shower = false;
	const bool runs = waterRuns(doings, room, t, &shower);
	Mesh water, foam, spray, pipes;
	if (level > 0.01) {   // (the tub widens as it rises: lathe's radius 0.32 at its foot to 0.36 at its rim, scaled to its size)
		const double y = 0.06 + 0.37 * level, k = lerp(0.32, 0.36, (y - 0.05) / 0.5) / 0.4;
		detail::addBox(water, I, {0.0, 0.5 * (0.06 + y), 0.0}, {bath->w * k * 0.97, y - 0.06, bath->d * k * 0.97});
		for (int q = 0; q < 46; ++q) {   // (bubbles over it, more as it fills)
			if (hash01(q, r.seed + 5) > level) continue;
			const double a = 2.0 * kPi * hash01(q, r.seed + 6), rr = std::sqrt(hash01(q, r.seed + 7)) * 0.8, s = 0.02 + 0.03 * hash01(q, r.seed + 8);
			detail::addMesh(foam, uvSphere(1.0, 8, 6), mul(I, translate(0.5 * bath->w * k * rr * std::cos(a), y + 0.3 * s, 0.5 * bath->d * k * rr * std::sin(a)), scale(s, 0.6 * s, s)));
		}
	}
	const Vec3 tap{-0.5 * bath->w + 0.25, 0.77, 0.0};
	if (runs && !shower && level > 0.0)   // the tap's stream down into it
		detail::addMesh(spray, sdf::tubeMesh({tap, tap + Vec3{0.015, -0.5 * (0.77 - (0.06 + 0.37 * level)), 0.0}, Vec3{tap.x + 0.02, 0.06 + 0.37 * level, 0.0}}, 0.012, 0.7, 8), I);
	bool hasShower = false;
	for (const Doing &d : doings) hasShower = hasShower || (d.verb == "shower" && d.roomIndex == room);
	if (hasShower) {   // a pipe up the wall at the bath's end to a head over it, and its spray while it runs
		const Vec3 head{-0.5 * bath->w + 0.2, 1.95, -0.5 * bath->d + 0.12};
		detail::addMesh(pipes, sdf::tubeMesh({{-0.5 * bath->w + 0.05, 0.8, -0.5 * bath->d + 0.04}, {-0.5 * bath->w + 0.05, 2.05, -0.5 * bath->d + 0.04}, head + Vec3{0.0, 0.08, 0.0}}, 0.012, 1.0, 6), I);
		detail::addMesh(pipes, lathe({{0.0, 0.0}, {0.07, 0.0}, {0.02, 0.06}, {0.0, 0.06}}, 18), mul(I, translate(head)));
		if (shower)
			for (int q = 0; q < 70; ++q) {
				const double u = std::fmod(t * 2.2 + hash01(q, 31), 1.0), a = 2.0 * kPi * hash01(q, 32), spread = 0.05 + 0.18 * hash01(q, 33) * u;
				const Vec3 p = head + Vec3{spread * std::cos(a) + 0.12 * u, -1.5 * u, spread * std::sin(a) + 0.1 * u};
				detail::addMesh(spray, uvSphere(1.0, 5, 4), mul(I, translate(p), scale(0.004, 0.03, 0.004)));
			}
	}
	std::string out;
	if (!water.tris.empty()) out += attr("Material \"dielectric\" \"float eta\" [ 1.33 ]", emitMesh(water, identity()));
	if (!foam.tris.empty()) out += attr(coated({0.95, 0.95, 0.97}, 0.15), emitMesh(foam, identity()));
	if (!spray.tris.empty()) out += attr("Material \"dielectric\" \"float eta\" [ 1.33 ]", emitMesh(spray, identity()));
	if (!pipes.tris.empty()) out += attr(metalMaterial({0.8, 0.8, 0.82}, 0.1), emitMesh(pipes, identity()));
	return out;
}

// ---- parties ----
inline Vec3 partyColour(int k) {
	static const Vec3 c[] = {{0.85, 0.12, 0.15}, {0.15, 0.4, 0.85}, {0.95, 0.78, 0.12}, {0.2, 0.65, 0.3}, {0.75, 0.3, 0.75}, {0.95, 0.5, 0.15}};
	return c[((k % 6) + 6) % 6];
}
// Where a party's cake stands: the room's table (else its coffee table, its desk) - story space; false when it has none.
inline bool cakeSpot(const Room &r, Vec3 &at) {
	for (const char *kind : {"table", "coffeetable", "desk"})
		if (const Item *it = findItem(r, kind)) {
			at = transformPoint(detail::itemFrame(frameOf(r), *it), {0.0, it->h, 0.0});
			return true;
		}
	return false;
}
// A corner of the floor with room for something `radius` round (local; clear of the furniture, the doorways and the stairs): false when none has.
inline bool cornerSpot(const Room &r, double radius, Vec3 &at) {
	const std::vector<detail::Rect> clear = detail::keepClear(r);
	for (const auto &[sx, sz] : std::vector<std::pair<double, double>>{{1.0, 1.0}, {-1.0, 1.0}, {1.0, -1.0}, {-1.0, -1.0}}) {
		const Vec3 c{sx * (0.5 * r.w - kWall - radius - 0.05), 0.0, sz * (0.5 * r.d - kWall - radius - 0.05)};
		const detail::Rect f{c.x - radius, c.z - radius, c.x + radius, c.z + radius};
		bool ok = true;
		for (const Item &it : r.items) ok = ok && (it.flat || !f.overlaps(detail::footprint(it)));
		for (const detail::Rect &k : clear) ok = ok && !f.overlaps(k);
		if (ok) {
			at = c;
			return true;
		}
	}
	return false;
}
// A party's room at t (`blown`: when its candles were blown out, or 1e30): balloons, streamers, and what its kind brings; `confetti` the times of bursts.
inline std::string partyText(const Room &r, const Doing &p, const std::vector<double> &confetti, double blown, double t) {
	if (t < p.t0 || t >= p.t1) return std::string();
	const Mat4 F = frameOf(r);
	std::string out;
	Mesh strings, streamers[3];
	std::vector<Mesh> balloons(6);
	// balloons: tied to the furniture, bobbing on their strings (a seat's back, a table's corner), or, with nothing to tie them to, up against the ceiling
	std::vector<Vec3> ties;
	for (const Item &it : r.items)
		if (!it.flat && (it.kind == "chair" || it.kind == "sofa" || it.kind == "armchair" || it.kind == "table" || it.kind == "bed" || it.kind == "desk"))
			ties.push_back(Vec3{it.x, it.h, it.z} + frontOf(it) * (-0.4 * it.d));
	for (int k = 0; k < 6; ++k) {
		const bool tied = !ties.empty() && k < 2 * static_cast<int>(ties.size());
		const Vec3 base = tied ? ties[static_cast<std::size_t>(k) % ties.size()] + Vec3{0.08 * (k / static_cast<int>(ties.size()) ? 1.0 : -1.0), 0.0, 0.0}
							   : Vec3{(hash01(k, r.seed + 40) - 0.5) * (r.w - 1.0), r.h - 0.2, (hash01(k, r.seed + 41) - 0.5) * (r.d - 1.0)};
		const double bob = 0.03 * std::sin(1.1 * t + 2.0 * k), sway = 0.04 * std::sin(0.7 * t + k);
		const Vec3 top = tied ? base + Vec3{sway, 0.95 + 0.25 * hash01(k, r.seed + 42) + bob, 0.5 * sway} : base + Vec3{sway, bob, 0.0};
		detail::addMesh(balloons[static_cast<std::size_t>(k)], uvSphere(1.0, 16, 10), mul(F, translate(top + Vec3{0.0, 0.17, 0.0}), scale(0.14, 0.17, 0.14)));
		detail::addMesh(balloons[static_cast<std::size_t>(k)], uvSphere(0.018, 6, 4), mul(F, translate(top)));
		if (tied) detail::addMesh(strings, sdf::tubeMesh({base, (base + top) * 0.5 + Vec3{0.03 * std::sin(1.3 * t + k), 0.0, 0.0}, top}, 0.002, 1.0, 4), F);
	}
	for (int k = 0; k < 6; ++k) out += attr(coated(partyColour(k + r.seed), 0.08), emitMesh(balloons[static_cast<std::size_t>(k)], identity()));
	if (!strings.tris.empty()) out += attr(diffuseMaterial({0.9, 0.9, 0.9}), emitMesh(strings, identity()));
	// streamers: from the middle of the ceiling to its corners, each a sagging twist of paper
	for (int k = 0; k < 4; ++k) {
		const Vec3 a{0.0, r.h - 0.04, 0.0}, b{(k & 1 ? 1.0 : -1.0) * (0.5 * r.w - kWall - 0.05), r.h - 0.08, (k & 2 ? 1.0 : -1.0) * (0.5 * r.d - kWall - 0.05)};
		std::vector<Vec3> pts;
		for (int q = 0; q <= 16; ++q) {
			const double u = q / 16.0;
			pts.push_back(lerp(a, b, u) + Vec3{0.02 * std::sin(u * 40.0), -0.45 * 4.0 * u * (1.0 - u) + 0.01 * std::sin(0.8 * t + k), 0.02 * std::cos(u * 40.0)});
		}
		detail::addMesh(streamers[k % 3], sdf::tubeMesh(pts, 0.012, 1.0, 4), F);
	}
	for (int k = 0; k < 3; ++k) out += attr(diffuseMaterial(partyColour(2 * k + 1)), emitMesh(streamers[k], identity()));
	Vec3 cake;
	if (p.what == "birthday" && cakeSpot(r, cake)) {   // a cake of two tiers with candles: lit until they are blown out, then a wisp of smoke from each
		Mesh sponge, icing, candles, smoke;
		detail::addMesh(sponge, lathe({{0.0, 0.0}, {0.17, 0.0}, {0.17, 0.09}, {0.0, 0.09}}, 28), translate(cake));
		detail::addMesh(icing, lathe({{0.0, 0.09}, {0.175, 0.09}, {0.175, 0.105}, {0.12, 0.105}, {0.12, 0.17}, {0.0, 0.17}}, 28), translate(cake));
		for (int q = 0; q < 6; ++q) {
			const double a = 2.0 * kPi * q / 6.0;
			const Vec3 c = cake + Vec3{0.075 * std::cos(a), 0.17, 0.075 * std::sin(a)};
			detail::addMesh(candles, lathe({{0.0, 0.0}, {0.006, 0.0}, {0.006, 0.06}, {0.0, 0.06}}, 8), translate(c));
			if (t < blown) {
				const double flick = 0.8 + 0.2 * std::sin(19.0 * t + 1.7 * q);
				out += emissiveSphere(c + Vec3{0.0, 0.075, 0.0}, 0.008, Vec3{1.0, 0.6, 0.25} * (60.0 * flick));
			} else {
				steamPuffs(smoke, c + Vec3{0.0, 0.07, 0.0}, t, 1.0 - smoothstep(blown, blown + 3.0, t), 0.25, 0.012, q + 77);
			}
		}
		out += attr(diffuseMaterial({0.85, 0.65, 0.4}), emitMesh(sponge, identity())) + attr(coated({0.95, 0.75, 0.85}, 0.3), emitMesh(icing, identity())) +
			   attr(diffuseMaterial({0.4, 0.75, 0.95}), emitMesh(candles, identity()));
		if (!smoke.tris.empty()) out += attr(translucentMaterial({0.5, 0.5, 0.52}, {0.4, 0.4, 0.42}), emitMesh(smoke, identity()));
	}
	Vec3 base;
	if (p.what == "christmas" && cornerSpot(r, 0.6, base)) {   // a tree in a corner with room for it, twinkling, a star on top, presents under it
		Mesh tree, trunk, presents[3], ribbon;
		const Vec3 in = normalize(Vec3{-base.x, 0.0, -base.z}), across{-in.z, 0.0, in.x};
		detail::addMesh(trunk, lathe({{0.0, 0.0}, {0.06, 0.0}, {0.06, 0.25}, {0.0, 0.25}}, 10), mul(F, translate(base)));
		for (int layer = 0; layer < 4; ++layer) {
			const double y0 = 0.22 + 0.36 * layer, rad = 0.55 - 0.12 * layer;
			detail::addMesh(tree, lathe({{0.0, y0}, {rad, y0}, {0.0, y0 + 0.55}}, 20), mul(F, translate(base)));
		}
		for (int q = 0; q < 18; ++q) {
			const double u = hash01(q, 71), y = 0.3 + 1.3 * u, rad = (0.55 - 0.33 * u) * 0.9, a = 2.0 * kPi * hash01(q, 72), on = 0.5 + 0.5 * std::sin(2.5 * t + 3.0 * q);
			out += emissiveSphere(transformPoint(F, base + Vec3{rad * std::cos(a), y, rad * std::sin(a)}), 0.012, partyColour(q) * (8.0 + 22.0 * on));
		}
		out += emissiveSphere(transformPoint(F, base + Vec3{0.0, 1.82, 0.0}), 0.05, Vec3{1.0, 0.85, 0.3} * 12.0);
		for (int q = 0; q < 4; ++q) {   // (in front of it, towards the room)
			const Vec3 c = base + in * (0.45 + 0.12 * (q % 2)) + across * (-0.36 + 0.24 * q);
			const double s = 0.16 + 0.08 * hash01(q, 73);
			detail::addBox(presents[q % 3], F, c + Vec3{0.0, 0.5 * s, 0.0}, {s * 1.2, s, s});
			detail::addBox(ribbon, F, c + Vec3{0.0, 0.5 * s, 0.0}, {s * 1.22, s * 1.01, 0.03});
		}
		out += attr(diffuseMaterial({0.1, 0.38, 0.14}), emitMesh(tree, identity())) + attr(diffuseMaterial({0.35, 0.2, 0.1}), emitMesh(trunk, identity())) + attr(coated({0.95, 0.8, 0.2}, 0.2), emitMesh(ribbon, identity()));
		for (int k = 0; k < 3; ++k)
			if (!presents[k].tris.empty()) out += attr(coated(partyColour(k * 2), 0.3), emitMesh(presents[k], identity()));
	}
	if (p.what == "halloween") {   // pumpkins with faces lit from inside: on the table and in the windows
		std::vector<Vec3> spots;
		if (cakeSpot(r, cake)) spots.push_back(cake);
		for (const Opening &o : r.openings)
			if (!o.door && spots.size() < 3) spots.push_back(transformPoint(F, wallPoint(r, o.wall, o.along, o.sill) + wallInward(o.wall) * 0.12));
		Mesh skin, stalks, faces;
		for (std::size_t k = 0; k < spots.size(); ++k) {
			const Vec3 c = spots[k] + Vec3{0.0, 0.12, 0.0};
			Mesh p_ = uvSphere(1.0, 24, 14);
			for (Vec3 &v : p_.verts) {   // (ribbed)
				const double ang = std::atan2(v.z, v.x), rib = 1.0 + 0.06 * std::cos(8.0 * ang);
				v = Vec3{v.x * 0.16 * rib, v.y * 0.12, v.z * 0.16 * rib};
			}
			sdf::computeNormals(p_);
			const Vec3 toRoom = normalize(transformPoint(F, Vec3{0.0, 0.0, 0.0}) - c) * 1.0, ahead = normalize(Vec3{toRoom.x, 0.0, toRoom.z} + Vec3{0.0, 0.0, 1e-6});
			const Mat4 M = mul(translate(c), rotate({0.0, 1.0, 0.0}, std::atan2(ahead.x, ahead.z) * 180.0 / kPi));
			detail::addMesh(skin, p_, M);
			detail::addMesh(stalks, sdf::tubeMesh({{0.0, 0.11, 0.0}, {0.01, 0.17, 0.01}}, 0.015, 0.7, 6), M);
			for (const double side : {-1.0, 1.0}) {   // two triangle eyes and a jagged grin, glowing
				const int b = static_cast<int>(faces.verts.size());
				for (const Vec3 &v : {Vec3{side * 0.075, 0.03, 0.15}, Vec3{side * 0.025, 0.03, 0.158}, Vec3{side * 0.05, 0.075, 0.153}}) faces.verts.push_back(transformPoint(M, v)), faces.normals.push_back({0, 0, 1});
				faces.tris.push_back({b, b + 1, b + 2}), faces.tris.push_back({b, b + 2, b + 1});
			}
			const int b = static_cast<int>(faces.verts.size());
			for (int q = 0; q <= 6; ++q) {
				const double x = -0.08 + 0.16 * q / 6.0;
				faces.verts.push_back(transformPoint(M, {x, -0.035 + (q % 2 ? 0.015 : 0.0), 0.15})), faces.normals.push_back({0, 0, 1});
				faces.verts.push_back(transformPoint(M, {x, -0.06, 0.145})), faces.normals.push_back({0, 0, 1});
			}
			for (int q = 0; q < 6; ++q) {
				const int a = b + 2 * q;
				faces.tris.push_back({a, a + 1, a + 2}), faces.tris.push_back({a + 1, a + 3, a + 2});
				faces.tris.push_back({a, a + 2, a + 1}), faces.tris.push_back({a + 1, a + 2, a + 3});
			}
		}
		const Vec3 glow = Vec3{1.0, 0.55, 0.12} * (6.0 * (0.85 + 0.15 * std::sin(9.0 * t)));
		out += attr(coated({0.92, 0.42, 0.06}, 0.35), emitMesh(skin, identity())) + attr(diffuseMaterial({0.25, 0.35, 0.1}), emitMesh(stalks, identity()));
		if (!faces.tris.empty()) out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(faces, identity()), &glow);
	}
	// confetti: a burst from the ceiling, falling with a flutter for about 4 s, lying where it lands
	Mesh flakes[6];
	for (const double at : confetti) {
		if (t < at) continue;
		for (int q = 0; q < 220; ++q) {
			const int s = q + static_cast<int>(at * 100.0);
			const double fallFor = 2.6 + 1.8 * hash01(s, 81), u = std::min(1.0, (t - at) / fallFor);
			const Vec3 from{(hash01(s, 82) - 0.5) * 1.4, r.h - 0.1, (hash01(s, 83) - 0.5) * 1.4}, to{(hash01(s, 84) - 0.5) * (r.w - 0.5), 0.004, (hash01(s, 85) - 0.5) * (r.d - 0.5)};
			const Vec3 p = lerp(from, to, u) + Vec3{0.08 * std::sin(9.0 * u + s) * (1.0 - u), 0.0, 0.08 * std::cos(7.0 * u + s) * (1.0 - u)};
			const double spin = u < 1.0 ? 720.0 * u + 97.0 * s : 97.0 * s;
			detail::addBox(flakes[q % 6], mul(F, translate(p), rotate(normalize(Vec3{hash01(s, 86), 1.0, hash01(s, 87)}), spin), rotate({1.0, 0.0, 0.0}, u < 1.0 ? 300.0 * u : 0.0)), {0.0, 0.0, 0.0}, {0.014, 0.0015, 0.01});
		}
	}
	for (int k = 0; k < 6; ++k)
		if (!flakes[k].tris.empty()) out += attr(diffuseMaterial(partyColour(k)), emitMesh(flakes[k], identity()));
	return out;
}

}  // namespace rooms
}  // namespace cartoon
