#pragma once
// film_roomlife.h -- what moves in a room (indoor round 1): its doors swinging, the curtains drawn or blowing, the lamps on and off (and a power cut), candles and a fire
// flickering, a television's glow, rain running down the window panes, dust in a sunbeam, the clutter of a lived-in room (and a mess that grows), fish in an aquarium.
//
//   door ROOM [to ROOM2] open T [AMOUNT] [close T] ...     the front door (or the doorway into ROOM2) swinging open at T (all the way, or AMOUNT 0 .. 1), shut again at `close T`
//   curtains ROOM open|closed|blowing [at T]              how its curtains are from T (drawn over 1.5 s; blowing: open, a breeze in them)
//   switch ROOM [LAMP] on T [off T]                       its lamps (LAMP: one of them, by kind - ceiling, table, floor, tv, fairy - or name) switched on at T, off at T;
//                                                         a room's lamps no `switch` names are on while it is dark outside, and with no sky, always
//   powercut T [until T]                                  every electric lamp off (candles and fires go on), and back
//   clutter ROOM tidy|lived|messy [grows T0 T1]          what lies about: on tables and shelves, on the floor; `grows`: from tidy to that over the time
//   furnish ROOM mirror|tv|aquarium|cabinet|lamp|candles|fireplace|plant|fairy   another piece (fairy: a string of little lights along its back wall)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_furniture.h"
#include "film_roomlook.h"
#include "film_rooms.h"
#include "nature_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace rooms {

// A wall's own frame at `along` (local x along it, y up, z into the room), on its inner face.
inline Mat4 wallFrame(const Room &r, int wall, double along) { return mul(frameOf(r), translate(wallPoint(r, wall, along)), rotate({0.0, 1.0, 0.0}, turnFacing(wall))); }

// How far open a door is at t: eased (0.9 s) from each of its moments' amounts to the next.
inline double openAt(const Door &d, double t) {
	double v = 0.0;
	for (const auto &[at, amount] : d.open) v = lerp(v, amount, smoothstep(at, at + 0.9, t));
	return v;
}
// The doors of a room at t: its front door and any doorway a `door` line names, a leaf swinging into the room on its hinge, a knob on it.
inline std::string doorsText(const Indoors &in, int room, double t) {
	const Room &r = in.rooms[static_cast<std::size_t>(room)];
	Mesh leaf, knobs;
	for (const Opening &o : r.openings) {
		if (!o.door) continue;
		const Door *d = nullptr;
		for (const Door &each : in.doors)
			if (each.roomIndex == room && each.to == o.to) d = &each;
		if (!d && !o.to.empty()) continue;   // (a doorway between two rooms no one shuts: an open arch)
		const double open = d ? openAt(*d, t) : 0.0;
		const Mat4 W = mul(wallFrame(r, o.wall, o.along), translate(-0.5 * o.width + 0.02, 0.0, -0.5 * kWall), rotate({0.0, 1.0, 0.0}, -95.0 * open));
		const double lw = o.width - 0.04, lh = o.height - 0.015;
		detail::addBox(leaf, W, {0.5 * lw, 0.5 * lh, 0.0}, {lw, lh, 0.045});
		for (const double side : {-1.0, 1.0}) detail::addMesh(knobs, uvSphere(0.03, 10, 6), mul(W, translate(lw - 0.08, 1.0, side * 0.05)));
	}
	if (leaf.tris.empty()) return std::string();
	return attr(coated(paletteOf(r.style).wood * 0.9, 0.35), emitMesh(leaf, identity())) + attr(metalMaterial({0.8, 0.7, 0.35}, 0.2), emitMesh(knobs, identity()));
}
// The doors of what someone opens (a fridge, a wardrobe), at how far open they are at t (`openness` by item).
inline std::string itemDoorsText(const Room &r, const std::vector<std::pair<int, double>> &openness) {
	Mesh wood, white, dark;
	for (const auto &[i, open] : openness) {
		const Item &it = r.items[static_cast<std::size_t>(i)];
		Mesh &m = it.kind == "fridge" ? white : it.kind == "stove" ? dark : wood;
		doorOf(it, open, m, detail::itemFrame(frameOf(r), it));
	}
	std::string out;
	const Palette p = paletteOf(r.style);
	if (!wood.tris.empty()) out += attr(coated(p.wood, 0.4), emitMesh(wood, identity()));
	if (!white.tris.empty()) out += attr(coated({0.9, 0.9, 0.88}, 0.3), emitMesh(white, identity()));
	if (!dark.tris.empty()) out += attr(coated({0.05, 0.05, 0.055}, 0.25), emitMesh(dark, identity()));
	return out;
}

// How drawn its curtains are at t (0 open .. 1 closed) and how much they blow.
inline void curtainsAt(const Room &r, double t, double &closed, double &blow) {
	closed = 0.0, blow = 0.0;
	for (const auto &[at, how] : r.curtains) {
		const double u = smoothstep(at, at + 1.5, t);
		closed = lerp(closed, how == "closed" ? 1.0 : 0.0, u);
		blow = lerp(blow, how == "blowing" ? 1.0 : 0.0, u);
	}
}
inline std::string curtainsText(const Room &r, double t) {
	if (r.curtains.empty()) return std::string();
	double closed = 0.0, blow = 0.0;
	curtainsAt(r, t, closed, blow);
	Mesh cloth, rail;
	for (const Opening &o : r.openings) {
		if (o.door) continue;
		const Mat4 W = wallFrame(r, o.wall, o.along);
		const double top = o.sill + o.height + 0.14, bottom = std::max(0.05, o.sill - 0.12), half = 0.5 * o.width;
		detail::addMesh(rail, sdf::tubeMesh({{-half - 0.25, top + 0.02, 0.07}, {half + 0.25, top + 0.02, 0.07}}, 0.012, 1.0, 6), W);
		for (const double side : {-1.0, 1.0}) {   // a panel each side: bunched at the window's edge, or drawn across to its middle
			const double outer = side * (half + 0.22), inner = side * lerp(half - 0.05, -0.01, closed);
			const int cols = 14, rows = 10;
			const int base = static_cast<int>(cloth.verts.size());
			for (int j = 0; j <= rows; ++j)
				for (int i = 0; i <= cols; ++i) {
					const double u = static_cast<double>(i) / cols, v = static_cast<double>(j) / rows, x = lerp(outer, inner, u), y = lerp(top, bottom, v);
					const double folds = (closed > 0.5 ? 0.025 : 0.05) * std::sin(u * kPi * (closed > 0.5 ? 9.0 : 5.0));   // (bunched, its folds deep)
					const double gust = blow * v * (0.18 + 0.08 * std::sin(1.3 * t + 3.0 * u + side)) * (0.6 + 0.4 * std::sin(0.7 * t + 2.0 * v));
					cloth.verts.push_back(transformPoint(W, {x + blow * 0.04 * std::sin(2.1 * t + 5.0 * v) * v, y, 0.09 + folds + gust}));
					cloth.normals.push_back(normalize(Vec3{0.0, 0.0, 1.0}));
				}
			for (int j = 0; j < rows; ++j)
				for (int i = 0; i < cols; ++i) {
					const int a = base + j * (cols + 1) + i;
					cloth.tris.push_back({a, a + 1, a + cols + 1}), cloth.tris.push_back({a + 1, a + cols + 2, a + cols + 1});
				}
		}
	}
	sdf::computeNormals(cloth);
	const Palette p = paletteOf(r.style);
	return attr(translucentMaterial(p.fabric2 * 0.9, p.fabric2 * 0.55), emitMesh(cloth, identity())) + attr(metalMaterial({0.5, 0.42, 0.25}, 0.3), emitMesh(rail, identity()));
}

// Whether a lamp is on at t: as a `switch` line says when one names it; else while it is dark outside (`dark` 0 .. 1 over the dusk); never in a power cut, if electric.
inline double lampOn(const Indoors &in, const Room &r, const Lamp &l, double t, double dark) {
	if (l.electric)
		for (const auto &[a, b] : in.powerCuts)
			if (t >= a && t < b) return 0.0;
	bool named = false;
	double on = 0.0;
	for (const Switch &s : in.switches) {
		if (s.room != r.name || !(s.lamp.empty() || s.lamp == l.kind || s.lamp == l.name)) continue;
		named = true;
		if (t >= s.on && t < s.off) on = 1.0;
	}
	double by = 1.0;   // (round 2: a `flicker` - mostly near full, wavering, a tenth of a second now and then nearly out - and the lamps turned down for shadow play)
	for (const Doing &d : in.home.doings) {
		if (d.room != r.name || t < d.t0 || t >= d.t1) continue;
		if (d.verb == "flicker" && l.electric && (d.what.empty() || d.what == l.kind || d.what == l.name)) {
			const int slot = static_cast<int>(std::floor(t * 10.0)), seed = r.seed * 17 + static_cast<int>(l.at.x * 100.0);
			by *= hash01(slot, seed) < 0.18 ? 0.08 + 0.1 * hash01(slot, seed + 1) : 0.72 + 0.18 * std::sin(37.0 * t + seed) * std::sin(11.0 * t);
		}
		if (d.verb == "dim") by *= 0.15;
	}
	if (named || !l.electric) return (named ? on : 1.0) * by;
	return dark * by;
}
// The lamps of a room at t: each one's bulb (its light) where it is on; candles and a fire flickering; a television's screen glowing in shifting colours.
inline std::string lampsText(const Indoors &in, const Room &r, double t, double dark) {
	const Mat4 F = frameOf(r);
	std::string out;
	for (std::size_t k = 0; k < r.lamps.size(); ++k) {
		const Lamp &l = r.lamps[k];
		const double on = lampOn(in, r, l, t, dark);
		if (on <= 0.01) continue;
		const Vec3 c = transformPoint(F, l.at);
		const int s = r.seed * 31 + static_cast<int>(k);
		if (l.kind == "ceiling") out += emissiveSphere(c, 0.05, l.colour * (200.0 * l.power * on));   // (about 0.4 on the floor two metres off: a room lit as a day's shade is)
		else if (l.kind == "table" || l.kind == "floor") out += emissiveSphere(c, 0.04, l.colour * (130.0 * l.power * on));
		else if (l.kind == "candles") {
			for (int q = 0; q < 3; ++q) {
				const double flick = 0.75 + 0.25 * std::sin(13.0 * t + 2.1 * q + s) * std::sin(7.3 * t + q);
				out += emissiveSphere(transformPoint(F, l.at + Vec3{-0.07 + 0.07 * q, -0.06 + 0.04 * q, 0.03 * (q % 2)}), 0.012, l.colour * (90.0 * flick * on));
			}
		} else if (l.kind == "fire") {
			for (int q = 0; q < 4; ++q) {
				const double flick = 0.7 + 0.3 * std::sin(9.0 * t + 1.7 * q + s) * std::sin(4.3 * t + 0.6 * q), lift = 0.05 * std::sin(6.0 * t + q);
				out += emissiveSphere(transformPoint(F, l.at + Vec3{-0.15 + 0.1 * q, 0.02 * q + lift, 0.0}), 0.05 + 0.02 * flick, l.colour * (30.0 * flick * on));
			}
		} else if (l.kind == "tv") {   // (the screen: a picture's light changing as a programme's does)
			const double a = std::sin(1.7 * t + s), b = std::sin(0.9 * t + 2.0) * std::sin(3.1 * t);
			const Vec3 glow = Vec3{0.45 + 0.25 * a, 0.55 + 0.2 * b, 0.9 + 0.1 * a * b} * (1.6 * on);
			for (const Item &it : r.items)
				if (it.kind == "tv") out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(box(it.w * 0.88, 0.5, 0.004), mul(detail::itemFrame(F, it), translate(0.0, 0.85, 0.034))), &glow);
		} else if (l.kind == "fairy") {
			const Mat4 W = wallFrame(r, 1, 0.5);
			for (int q = 0; q < 14; ++q) {
				const double u = (q + 0.5) / 14.0 - 0.5, sag = 0.12 * std::sin(kPi * std::fmod((q + 0.5) / 3.5, 1.0));
				const Vec3 col = q % 3 == 0 ? Vec3{1.0, 0.5, 0.3} : q % 3 == 1 ? Vec3{1.0, 0.9, 0.5} : Vec3{0.5, 0.7, 1.0};
				out += emissiveSphere(transformPoint(W, {u * (wallLength(r, 1) - 0.6), r.h - 0.2 - sag, 0.03}), 0.012, col * (24.0 * on * (0.85 + 0.15 * std::sin(2.0 * t + q))));
			}
		}
	}
	return out;
}

// The daylight in at a room's windows (`day` 0 .. 1, `rain` 0 .. 1 greying it): from each, a wide soft spot just inside it into the room, the sky's light from the window
// (two windows' light alone leave a room too dark and too noisy to draw; the sun's own patch on the floor comes in through the panes on top of it).
inline std::string windowLightText(const Room &r, double day, double rain) {
	if (day <= 0.01) return std::string();
	const Mat4 F = frameOf(r);
	std::string out;
	for (const Opening &o : r.openings) {
		if (o.door) continue;
		const Vec3 at = wallPoint(r, o.wall, o.along, o.sill + 0.6 * o.height), in = wallInward(o.wall);
		const Vec3 from = transformPoint(F, at + in * 0.12), to = transformPoint(F, at + in * 2.0 + Vec3{0.0, -1.0, 0.0});
		const Vec3 I = (Vec3{0.9, 0.95, 1.0} * (1.0 - rain) + Vec3{0.62, 0.66, 0.72} * rain) * (3.0 * o.width * o.height * day);
		char buf[320];
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"spot\" \"point3 from\" [ %.4f %.4f %.4f ] \"point3 to\" [ %.4f %.4f %.4f ] \"float coneangle\" [ 80 ] \"float conedelta\" [ 40 ] \"rgb I\" [ %.4f %.4f %.4f ]\nAttributeEnd\n",
					  -from.x, from.y, from.z, -to.x, to.y, to.z, I.x, I.y, I.z);
		out += buf;
	}
	return out;
}

// Rain on the window panes at t (`amount` 0 .. 1): drops on the outside of the glass, some running down it.
inline std::string rainOnGlassText(const Room &r, double t, double amount) {
	if (amount <= 0.02) return std::string();
	Mesh drops;
	const Mesh bead = uvSphere(1.0, 6, 4);
	int n = 0;
	for (const Opening &o : r.openings) {
		if (o.door) continue;
		const Mat4 W = wallFrame(r, o.wall, o.along);
		const int count = static_cast<int>(70.0 * amount);
		for (int k = 0; k < count; ++k, ++n) {
			const int s = r.seed * 977 + n;
			const double x = (hash01(s, 1) - 0.5) * (o.width - 0.06), size = 0.003 + 0.004 * hash01(s, 2);
			const double runs = hash01(s, 3) < 0.35 ? 0.25 + 0.4 * hash01(s, 4) : 0.0, cycle = 2.0 + 3.0 * hash01(s, 5);   // (the big ones run)
			const double u = std::fmod(t / cycle + hash01(s, 6), 1.0), y = o.sill + o.height * (1.0 - std::fmod(hash01(s, 7) + runs * u, 1.0));
			detail::addMesh(drops, bead, mul(W, translate(x, y, -0.5 * kWall - 0.009), scale(size, size * (runs > 0.0 ? 1.5 : 1.0), size * 0.5)));
		}
	}
	return drops.tris.empty() ? std::string() : attr("Material \"dielectric\" \"float eta\" [ 1.33 ]", emitMesh(drops, identity()));
}

// Dust in the sun coming in through its windows (`toSun`: pbrt's direction to the sun): specks in the beam, catching its light, drifting.
inline std::string motesText(const Room &r, double t, Vec3 toSun) {
	if (toSun.y < 0.02) return std::string();
	const Mat4 F = frameOf(r), Finv = invertAffine(F);
	const Vec3 s{-toSun.x, toSun.y, toSun.z};   // (story space)
	const Vec3 sun = normalize(Vec3{Finv[0] * s.x + Finv[1] * s.y + Finv[2] * s.z, Finv[4] * s.x + Finv[5] * s.y + Finv[6] * s.z, Finv[8] * s.x + Finv[9] * s.y + Finv[10] * s.z});   // (the room's own)
	Mesh specks;
	const Mesh dot_ = uvSphere(1.0, 4, 3);
	for (std::size_t w = 0; w < r.openings.size(); ++w) {
		const Opening &o = r.openings[w];
		if (o.door) continue;
		const Vec3 in = wallInward(o.wall);
		if (dot(in, sun) > -0.15) continue;   // (the sun not shining in through it)
		const Vec3 c = wallPoint(r, o.wall, o.along, o.sill + 0.5 * o.height), across = o.wall < 2 ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
		for (int k = 0; k < 140; ++k) {
			const int seed = r.seed * 131 + static_cast<int>(w) * 1009 + k;
			const Vec3 start = c + across * ((hash01(seed, 1) - 0.5) * o.width) + Vec3{0.0, (hash01(seed, 2) - 0.5) * o.height, 0.0};
			Vec3 p = start - sun * (0.2 + 3.0 * hash01(seed, 3));   // (along the beam into the room)
			p = p + Vec3{0.04 * std::sin(0.31 * t + 6.0 * hash01(seed, 4)), 0.03 * std::sin(0.23 * t + 6.0 * hash01(seed, 5)), 0.04 * std::cos(0.27 * t + 6.0 * hash01(seed, 6))};
			if (std::abs(p.x) > 0.5 * r.w - kWall || std::abs(p.z) > 0.5 * r.d - kWall || p.y < 0.05 || p.y > r.h - 0.05) continue;
			detail::addMesh(specks, dot_, mul(F, translate(p), scale(0.0018 + 0.0015 * hash01(seed, 7))));
		}
	}
	return specks.tris.empty() ? std::string() : attr(diffuseMaterial({0.95, 0.93, 0.88}), emitMesh(specks, identity()));
}

// ---- clutter ----
struct Bit {
	std::string kind;
	Vec3 at;          // local
	double turn = 0.0, from = -1e30;   // (when it appears: a growing mess)
};
inline const std::vector<std::string> &bitKinds() {
	static const std::vector<std::string> k = {"mug", "plate", "bowl", "book", "vase", "block", "ball", "shoe", "bottle", "box", "cushion"};
	return k;
}
inline Mesh bitMesh(const std::string &kind) {
	if (kind == "mug") {
		Mesh m = lathe({{0.0, 0.0}, {0.04, 0.0}, {0.04, 0.09}, {0.036, 0.09}, {0.036, 0.01}, {0.0, 0.01}}, 16);
		detail::addMesh(m, torus(0.025, 0.007, 12, 6), mul(translate(0.045, 0.045, 0.0), rotate({1, 0, 0}, 90.0)));
		return m;
	}
	if (kind == "plate") return lathe({{0.0, 0.0}, {0.09, 0.0}, {0.12, 0.018}, {0.115, 0.02}, {0.0, 0.008}}, 20);
	if (kind == "bowl") return lathe({{0.0, 0.0}, {0.05, 0.0}, {0.08, 0.05}, {0.075, 0.055}, {0.0, 0.01}}, 18);
	if (kind == "book") return box(0.15, 0.03, 0.22);
	if (kind == "vase") {
		Mesh m = lathe({{0.0, 0.0}, {0.05, 0.0}, {0.06, 0.1}, {0.03, 0.2}, {0.035, 0.22}, {0.0, 0.22}}, 16);
		for (int q = 0; q < 3; ++q) detail::addMesh(m, uvSphere(0.03, 8, 6), translate(0.03 * std::cos(2.1 * q), 0.3 + 0.03 * q, 0.03 * std::sin(2.1 * q)));
		return m;
	}
	if (kind == "block") return box(0.07, 0.07, 0.07);
	if (kind == "ball") {
		Mesh m = uvSphere(0.09, 14, 8);
		for (Vec3 &v : m.verts) v.y += 0.09;
		return m;
	}
	if (kind == "shoe") {
		Mesh m = uvSphere(1.0, 12, 6);
		for (Vec3 &v : m.verts) v = Vec3{v.x * 0.045, std::max(0.0, v.y) * 0.05, v.z * 0.13};
		return m;
	}
	if (kind == "bottle") return lathe({{0.0, 0.0}, {0.035, 0.0}, {0.035, 0.16}, {0.014, 0.22}, {0.014, 0.26}, {0.0, 0.26}}, 14);
	if (kind == "box") {
		Mesh m = box(0.22, 0.16, 0.18);
		for (Vec3 &v : m.verts) v.y += 0.08;
		return m;
	}
	Mesh m = uvSphere(1.0, 12, 8);   // a cushion
	for (Vec3 &v : m.verts) v = Vec3{v.x * 0.2, (v.y + 1.0) * 0.05, v.z * 0.2};
	return m;
}
inline Vec3 bitColour(const std::string &kind, int which) {
	static const Vec3 bright[] = {{0.8, 0.2, 0.15}, {0.15, 0.4, 0.75}, {0.95, 0.75, 0.15}, {0.2, 0.6, 0.3}, {0.85, 0.85, 0.82}};
	if (kind == "plate" || kind == "bowl") return {0.9, 0.9, 0.86};
	if (kind == "box") return {0.6, 0.45, 0.28};
	if (kind == "bottle") return {0.2, 0.45, 0.3};
	return bright[which % 5];
}
// What lies about a room (local), worked out once from its furniture: on each surface and about the floor, as many as its `clutter` says; a growing mess's from tidy over its time.
inline std::vector<Bit> clutterOf(const Room &r) {
	std::vector<Bit> out;
	const auto level = [](const std::string &how) { return how == "tidy" ? 0 : how == "messy" ? 2 : 1; };
	const int to = level(r.clutter), from = r.messFrom >= 0.0 ? 0 : to;
	std::vector<detail::Rect> taken;
	for (const detail::Rect &k : detail::keepClear(r)) taken.push_back(k);
	for (const Item &it : r.items)
		if (!it.flat) taken.push_back(detail::footprint(it, 0.08));
	std::vector<std::pair<Vec3, double>> placed;   // (where, how wide: none of them on another)
	const auto room = [&](Vec3 p, double rad) {
		for (const auto &[q, w] : placed)
			if (std::hypot(p.x - q.x, p.z - q.z) < rad + w && std::abs(p.y - q.y) < 0.2) return false;
		return true;
	};
	const std::vector<std::string> kitchen = {"mug", "plate", "bowl", "bottle"}, home = {"mug", "book", "vase", "book"}, floorish = {"shoe", "block", "ball", "book", "cushion", "box"};
	int n = 0;
	const auto add = [&](const std::string &kind, Vec3 p, int lvl) {
		Bit b;
		b.kind = kind, b.at = p, b.turn = 360.0 * hash01(r.seed * 71 + n, 9);
		if (lvl > from) b.from = r.messFrom + (r.messTo - r.messFrom) * hash01(r.seed * 71 + n, 10);
		placed.push_back({p, kind == "cushion" ? 0.2 : kind == "ball" ? 0.1 : 0.08});
		out.push_back(b);
	};
	// on what has a top: a table's, a desk's, a counter's, a bedside table's, a mantelpiece
	for (const Item &it : r.items) {
		const bool top = it.kind == "table" || it.kind == "desk" || it.kind == "coffeetable" || it.kind == "bedside" || it.kind == "counter" || it.kind == "schooldesk" || it.kind == "fireplace";
		if (!top) continue;
		const double y = it.kind == "fireplace" ? 1.16 : it.kind == "counter" ? 0.9 : it.h;
		const int count[3] = {it.kind == "table" ? 1 : 0, 2, 5};
		const Mat4 I = mul(translate(it.x, 0.0, it.z), rotate({0.0, 1.0, 0.0}, it.turn));
		for (int lvl = 0; lvl <= to; ++lvl)
			for (int q = 0; q < count[lvl] - (lvl > 0 ? count[lvl - 1] : 0); ++q)
				for (int tries = 0; tries < 8; ++tries, ++n) {
					const double u = hash01(r.seed * 71 + n, 1) - 0.5, v = hash01(r.seed * 71 + n, 2) - 0.5;
					const Vec3 p = transformPoint(I, {u * (it.w - 0.2), y, v * (it.d - 0.16)});
					if (!room(p, 0.08)) continue;
					const std::vector<std::string> &from_ = r.kind == "kitchen" ? kitchen : home;
					add(lvl == 0 && it.kind == "table" ? std::string("vase") : from_[static_cast<std::size_t>(hash01(r.seed * 71 + n, 3) * from_.size()) % from_.size()], p, lvl);
					break;
				}
	}
	// about the floor
	const int floorCount[3] = {0, 2, 9};
	for (int lvl = 1; lvl <= to; ++lvl)
		for (int q = 0; q < floorCount[lvl] - floorCount[lvl - 1]; ++q)
			for (int tries = 0; tries < 30; ++tries, ++n) {
				const Vec3 p{(hash01(r.seed * 71 + n, 4) - 0.5) * (r.w - 0.6), 0.0, (hash01(r.seed * 71 + n, 5) - 0.5) * (r.d - 0.6)};
				bool free_ = room(p, 0.2);
				for (const detail::Rect &k : taken)
					if (p.x > k.x0 - 0.1 && p.x < k.x1 + 0.1 && p.z > k.z0 - 0.1 && p.z < k.z1 + 0.1) free_ = false;
				if (!free_) continue;
				add(floorish[static_cast<std::size_t>(hash01(r.seed * 71 + n, 6) * floorish.size()) % floorish.size()], p, lvl);
				break;
			}
	return out;
}
// The clutter at t as scene text: each kind of thing an object (written once a frame: `written`), a placement of it for each.
inline std::string clutterText(const Room &r, const std::vector<Bit> &bits, double t, std::set<std::string> &written) {
	std::string objects, places;
	const Mat4 F = frameOf(r);
	int which = 0;
	for (const Bit &b : bits) {
		if (t < b.from) continue;
		const int colour = b.kind == "book" || b.kind == "block" || b.kind == "mug" || b.kind == "cushion" ? static_cast<int>(hash01(static_cast<int>(b.at.x * 100.0 + b.at.z * 1000.0), 3) * 5.0) : 0;
		const std::string name = "room_bit_" + b.kind + std::to_string(colour);
		if (written.insert(name).second) objects += nature::objectBegin(name) + attr(coated(bitColour(b.kind, colour), 0.35), emitMesh(bitMesh(b.kind), identity())) + nature::objectEnd();
		places += nature::placeObject(name, mul(F, translate(b.at), rotate({0.0, 1.0, 0.0}, b.turn)));
		++which;
	}
	return which ? "# what lies about " + r.name + " (film_roomlife.h)\n" + objects + places : std::string();
}

// Fish in an aquarium at t: a few small bright ones going round it, a stream of bubbles.
inline std::string aquariumText(const Room &r, double t) {
	std::string out;
	for (const Item &it : r.items) {
		if (it.kind != "aquarium") continue;
		const Mat4 I = detail::itemFrame(frameOf(r), it);
		Mesh fish, bubbles;
		for (int k = 0; k < 5; ++k) {
			const double a = 0.6 * t * (0.7 + 0.2 * k) + 2.0 * k, x = 0.36 * it.w * std::cos(a), z = 0.28 * it.d * std::sin(a), y = 0.7 + 0.08 * std::sin(0.9 * t + k);
			detail::addMesh(fish, uvSphere(1.0, 10, 6), mul(I, translate(x, y, z), rotate({0.0, 1.0, 0.0}, -a * 180.0 / kPi), scale(0.035, 0.022, 0.012)));
		}
		for (int k = 0; k < 10; ++k) {
			const double u = std::fmod(0.35 * t + 0.1 * k, 1.0);
			detail::addMesh(bubbles, uvSphere(0.006, 6, 4), mul(I, translate(0.3 * it.w + 0.01 * std::sin(6.0 * u + k), 0.64 + 0.28 * u, 0.0)));
		}
		out += attr(coated({0.95, 0.45, 0.1}, 0.2), emitMesh(fish, identity())) + attr("Material \"dielectric\" \"float eta\" [ 1.1 ]", emitMesh(bubbles, identity()));
	}
	return out;
}

}  // namespace rooms
}  // namespace cartoon
