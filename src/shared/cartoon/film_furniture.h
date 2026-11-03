#pragma once
// film_furniture.h -- what a room's furniture is made of (indoor round 1, film_rooms.h): each piece of boxes, tubes and lathed shapes in its room's materials, its front to its
// local +z, standing on y = 0. What opens (a fridge, a wardrobe, a cabinet, an oven) has its door drawn on its own (doorOf), so a frame can swing it.

#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_rooms.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace rooms {

// Where a piece's opening door is hinged, how big it is (its own frame), and so how it is drawn at `open` (0 shut .. 1 a hundred degrees).
struct Leaf {
	bool has = false;
	Vec3 hinge, size;      // (the hinge's foot, local; the door's width along +x from it, its height, its thickness)
	double swing = 1.0;    // (+1 it opens to the left as one faces it, -1 to the right)
};
inline Leaf leafOf(const Item &it) {
	Leaf l;
	if (it.kind == "fridge") l = {true, {0.5 * it.w, 0.03, 0.5 * it.d}, {it.w, it.h - 0.05, 0.05}, -1.0};
	else if (it.kind == "wardrobe" || it.kind == "cabinet") l = {true, {-0.5 * it.w + 0.01, 0.08, 0.5 * it.d}, {0.5 * it.w - 0.02, it.h - 0.12, 0.03}, 1.0};
	else if (it.kind == "stove") l = {true, {-0.5 * it.w + 0.03, 0.08, 0.5 * it.d}, {it.w - 0.06, 0.5, 0.03}, 0.0};   // (an oven: its door drops down)
	return l;
}
// The door at `open`, into `m` (the item's frame `I`).
inline void doorOf(const Item &it, double open, Mesh &m, const Mat4 &I) {
	const Leaf l = leafOf(it);
	if (!l.has) return;
	Mat4 D;
	if (l.swing == 0.0) D = mul(I, translate(l.hinge.x, l.hinge.y, l.hinge.z), rotate({1.0, 0.0, 0.0}, 85.0 * open));   // (drops forward about its foot)
	else D = mul(I, translate(l.hinge.x, l.hinge.y, l.hinge.z), rotate({0.0, 1.0, 0.0}, l.swing * -100.0 * open));
	const double dir = l.swing < 0.0 ? -1.0 : 1.0;
	detail::addBox(m, D, {dir * 0.5 * l.size.x, 0.5 * l.size.y, 0.5 * l.size.z}, l.size);
}

namespace detail {
inline Mat4 itemFrame(const Mat4 &F, const Item &it) { return mul(F, translate(it.x, 0.0, it.z), rotate({0.0, 1.0, 0.0}, it.turn)); }
inline void tube(Mesh &m, const Mat4 &I, std::vector<Vec3> pts, double r) { addMesh(m, sdf::tubeMesh(pts, r, 1.0, 8), I); }
inline void ball(Mesh &m, const Mat4 &I, Vec3 at, Vec3 radii, int nu = 12, int nv = 8) { addMesh(m, uvSphere(1.0, nu, nv), mul(I, translate(at), scale(radii.x, radii.y, radii.z))); }
inline void legs(Mesh &m, const Mat4 &I, double w, double d, double h, double r) {
	for (const double sx : {-1.0, 1.0})
		for (const double sz : {-1.0, 1.0}) addBox(m, I, {sx * (0.5 * w - r - 0.02), 0.5 * h, sz * (0.5 * d - r - 0.02)}, {2 * r, h, 2 * r});
}
}  // namespace detail

// A piece of furniture into the room's groups. `doorShut`: draw its opening door shut with it (a room whose door no one opens).
inline void pieceOf(const Item &it, Groups &g, const Mat4 &F, bool doorShut = true) {
	using namespace detail;
	const Mat4 I = itemFrame(F, it);
	const double w = it.w, d = it.d, h = it.h;
	const int s = it.salt;
	const std::string &k = it.kind;
	if (k == "sofa" || k == "armchair") {
		addBox(g["fabric"], I, {0.0, 0.25, 0.05}, {w - 0.3, 0.2, d - 0.1});                       // seat
		for (const double x : {-0.5 * w + 0.12, 0.5 * w - 0.12}) addBox(g["fabric"], I, {x, 0.32, 0.0}, {0.24, 0.62, d});   // arms
		addBox(g["fabric"], I, {0.0, 0.52, -0.5 * d + 0.12}, {w - 0.3, 0.66, 0.24});              // back
		const int cushions = k == "sofa" ? 2 : 1;
		for (int c = 0; c < cushions; ++c) {
			const double cw = (w - 0.32) / cushions, cx = -0.5 * (w - 0.32) + cw * (c + 0.5);
			addBox(g["fabric2"], I, {cx, 0.4, 0.08}, {cw - 0.04, 0.12, d - 0.32});
			ball(g["accent"], I, {cx + 0.1 * (c ? 1 : -1), 0.62, -0.5 * d + 0.32}, {0.2, 0.18, 0.08});   // (a cushion against its back)
		}
		legs(g["dark"], I, w, d, 0.15, 0.03);
	} else if (k == "chair" || k == "stool") {
		const bool stool = k == "stool";
		addBox(g["wood"], I, {0.0, it.seat - 0.02, 0.0}, {w, 0.04, d});
		legs(g["wood"], I, w, d, it.seat - 0.04, 0.02);
		if (!stool) {
			for (const double x : {-0.5 * w + 0.03, 0.5 * w - 0.03}) addBox(g["wood"], I, {x, 0.5 * (it.seat + h), -0.5 * d + 0.03}, {0.04, h - it.seat, 0.04});
			addBox(g["wood"], I, {0.0, h - 0.1, -0.5 * d + 0.03}, {w, 0.16, 0.025});
		}
	} else if (k == "table" || k == "desk" || k == "coffeetable" || k == "bedside" || k == "schooldesk") {
		addBox(g["wood"], I, {0.0, h - 0.025, 0.0}, {w, 0.05, d});
		legs(g["wood"], I, w, d, h - 0.05, k == "coffeetable" ? 0.03 : 0.025);
		if (k == "bedside" || k == "desk") addBox(g["wood"], I, {0.0, h - 0.18, 0.0}, {w - 0.06, 0.2, d - 0.06}), addBox(g["metal"], I, {0.0, h - 0.18, 0.5 * d - 0.02}, {0.12, 0.02, 0.02});
		if (k == "bedside") {   // (its lamp: a little base and a shade round the bulb, film_rooms.h lightUp)
			addMesh(g["metal"], lathe({{0.0, 0.0}, {0.07, 0.0}, {0.02, 0.04}, {0.015, 0.22}, {0.0, 0.22}}, 12), mul(I, translate(0.0, h, 0.0)));
			addMesh(g["shade"], lathe({{0.14, h + 0.2}, {0.09, h + 0.42}, {0.085, h + 0.42}, {0.135, h + 0.2}}, 20), I);
		}
	} else if (k == "bed") {
		addBox(g["wood"], I, {0.0, 0.18, 0.0}, {w, 0.24, d});                                     // frame
		addBox(g["white"], I, {0.0, 0.4, 0.04}, {w - 0.08, 0.2, d - 0.12});                       // mattress
		addBox(g["fabric"], I, {0.0, 0.52, 0.22}, {w - 0.02, 0.06, d - 0.5});                     // its cover
		for (const double x : {-0.3 * w, 0.3 * w}) ball(g["white"], I, {x * (w > 1.2 ? 1.0 : 0.0), 0.56, -0.5 * d + 0.28}, {0.28, 0.08, 0.16});   // pillows
		addBox(g["wood"], I, {0.0, 0.55, -0.5 * d + 0.03}, {w, 1.1, 0.06});                        // its head
		legs(g["wood"], I, w, d, 0.08, 0.04);
	} else if (k == "wardrobe" || k == "cabinet" || k == "bookshelf" || k == "shelves") {
		const bool open = k == "bookshelf" || k == "shelves";
		addBox(g["wood"], I, {0.0, 0.5 * h, -0.5 * d + 0.01}, {w, h, 0.02});
		for (const double x : {-0.5 * w + 0.015, 0.5 * w - 0.015}) addBox(g["wood"], I, {x, 0.5 * h, 0.0}, {0.03, h, d});
		addBox(g["wood"], I, {0.0, h - 0.015, 0.0}, {w, 0.03, d}), addBox(g["wood"], I, {0.0, 0.04, 0.0}, {w, 0.08, d});
		const int shelves = k == "wardrobe" ? 1 : 4;
		for (int q = 1; q <= shelves; ++q) {
			const double y = 0.08 + q * (h - 0.1) / (shelves + 1.0);
			addBox(g["wood"], I, {0.0, y, 0.0}, {w - 0.04, 0.025, d - 0.02});
			if (!open) continue;
			// what stands on it: books of all colours, or a shop's goods - tins, boxes and jars
			double x = -0.5 * w + 0.06;
			for (int b = 0; x < 0.5 * w - 0.08 && b < 40; ++b) {
				const double bw = k == "shelves" ? 0.12 : 0.03 + 0.03 * hash01(s + q * 41 + b, 1), bh = k == "shelves" ? 0.14 + 0.1 * hash01(s + q * 41 + b, 2) : 0.17 + 0.09 * hash01(s + q * 41 + b, 2);
				static const char *colours[] = {"red", "blue", "green", "yellow", "white", "dark"};
				Mesh &into = g[colours[static_cast<int>(hash01(s + q * 41 + b, 3) * 6.0) % 6]];
				if (k == "shelves" && hash01(s + q * 41 + b, 4) < 0.4) addMesh(into, lathe({{0.0, 0.0}, {0.045, 0.0}, {0.045, bh}, {0.0, bh}}, 12), mul(I, translate(x + 0.05, y + 0.012, 0.0)));
				else addBox(into, I, {x + 0.5 * bw, y + 0.012 + 0.5 * bh, 0.03}, {bw - 0.004, bh, d - 0.12});
				x += bw + (hash01(s + q * 41 + b, 5) < 0.1 ? 0.06 : 0.002);
			}
		}
		if (!open) {
			addBox(g["metal"], I, {0.03, 0.5 * h, 0.5 * d + 0.02}, {0.02, 0.18, 0.02}), addBox(g["metal"], I, {-0.03, 0.5 * h, 0.5 * d + 0.02}, {0.02, 0.18, 0.02});
			if (doorShut) doorOf(it, 0.0, g["wood"], I);
			if (k == "wardrobe" || k == "cabinet") {   // (its other door, which stays shut)
				addBox(g["wood"], I, {0.25 * w, 0.5 * h + 0.02, 0.5 * d - 0.015}, {0.5 * w - 0.02, h - 0.12, 0.03});
				if (k == "cabinet")   // (glass-fronted: plates and glasses inside, seen through it)
					for (int q = 0; q < 6; ++q) addMesh(g["white"], lathe({{0.0, 0.0}, {0.11, 0.0}, {0.12, 0.02}, {0.0, 0.015}}, 16), mul(I, translate(-0.25 * w + 0.1 * (q % 3) - 0.1, 0.6 + 0.45 * (q / 3), 0.0), rotate({1, 0, 0}, 70.0)));
			}
		}
	} else if (k == "counter") {
		addBox(g["wood"], I, {0.0, 0.42, 0.0}, {w, 0.84, d - 0.02});
		addBox(g["stone"], I, {0.0, 0.88, 0.0}, {w + 0.02, 0.04, d});
		for (double x = -0.5 * w + 0.3; x < 0.5 * w; x += 0.6) addBox(g["metal"], I, {x, 0.7, 0.5 * d}, {0.14, 0.02, 0.02});
		addBox(g["metal"], I, {0.15 * w, 0.84, 0.0}, {0.55, 0.12, 0.4});                         // the sink
		tube(g["metal"], I, {{0.15 * w, 0.9, -0.22}, {0.15 * w, 1.15, -0.22}, {0.15 * w, 1.15, -0.05}}, 0.015);   // its tap
		addBox(g["accent"], I, {-0.3 * w, 0.95, -0.12}, {0.32, 0.1, 0.22});                       // (a bread bin)
	} else if (k == "fridge") {
		addBox(g["white"], I, {0.0, 0.5 * h, -0.02}, {w, h, d - 0.04});
		if (doorShut) doorOf(it, 0.0, g["white"], I);
		addBox(g["metal"], I, {-0.5 * w + 0.06, 0.62 * h, 0.5 * d + 0.03}, {0.02, 0.4, 0.03});
	} else if (k == "stove") {
		addBox(g["white"], I, {0.0, 0.44, 0.0}, {w, 0.88, d});
		for (const double x : {-0.15, 0.15})
			for (const double z : {-0.14, 0.14}) addMesh(g["dark"], torus(0.075, 0.012, 16, 6), mul(I, translate(x, 0.9, z)));
		if (doorShut) doorOf(it, 0.0, g["dark"], I);
		for (int q = 0; q < 4; ++q) ball(g["metal"], I, {-0.22 + 0.15 * q, 0.8, 0.5 * d + 0.015}, {0.02, 0.02, 0.015});
	} else if (k == "tv") {
		addBox(g["wood"], I, {0.0, 0.22, 0.0}, {w, 0.44, d});                                     // its stand
		addBox(g["dark"], I, {0.0, 0.85, 0.0}, {w * 0.95, 0.58, 0.06});                           // the set (its screen glows: film_roomlife.h)
		addBox(g["dark"], I, {0.0, 0.48, 0.0}, {0.25, 0.06, 0.18});
	} else if (k == "rug" || k == "mat") {
		addBox(g[k == "rug" ? "rug" : "dark"], I, {0.0, 0.006, 0.0}, {w, 0.012, d});
	} else if (k == "bath") {
		addMesh(g["white"], lathe({{0.0, 0.05}, {0.32, 0.05}, {0.36, 0.55}, {0.4, 0.6}, {0.0, 0.6}}, 24), mul(I, scale(w / 0.8, 1.0, d / 0.8)));
		addBox(g["water"], I, {0.0, 0.42, 0.0}, {w * 0.78, 0.01, d * 0.72});
		tube(g["metal"], I, {{-0.5 * w + 0.12, 0.55, 0.0}, {-0.5 * w + 0.12, 0.78, 0.0}, {-0.5 * w + 0.25, 0.78, 0.0}}, 0.015);
	} else if (k == "basin") {
		addBox(g["white"], I, {0.0, 0.42, -0.1}, {0.18, 0.84, 0.18});
		addMesh(g["white"], lathe({{0.0, 0.7}, {0.24, 0.72}, {0.28, 0.85}, {0.0, 0.83}}, 20), mul(I, scale(1.0, 1.0, 0.8)));
		tube(g["metal"], I, {{0.0, 0.85, -0.17}, {0.0, 0.98, -0.17}, {0.0, 0.98, -0.06}}, 0.012);
	} else if (k == "toilet") {
		addMesh(g["white"], lathe({{0.0, 0.0}, {0.14, 0.0}, {0.2, 0.36}, {0.18, 0.42}, {0.0, 0.42}}, 20), mul(I, translate(0.0, 0.0, 0.05), scale(1.0, 1.0, 1.4)));
		addBox(g["white"], I, {0.0, 0.62, -0.5 * d + 0.1}, {0.38, 0.4, 0.18});
	} else if (k == "board") {
		addBox(g["board"], I, {0.0, 1.4, 0.0}, {w, h, 0.03});
		addBox(g["wood"], I, {0.0, 0.78, 0.04}, {w, 0.04, 0.08});
	} else if (k == "till") {
		addBox(g["wood"], I, {0.0, 0.45, 0.0}, {w, 0.9, d});
		addBox(g["stone"], I, {0.0, 0.92, 0.0}, {w + 0.04, 0.04, d + 0.04});
		addBox(g["dark"], I, {0.3 * w, 1.02, 0.0}, {0.36, 0.16, 0.32});
		addBox(g["accent"], I, {0.3 * w, 1.13, -0.08}, {0.28, 0.08, 0.04});
	} else if (k == "fireplace") {
		addBox(g["stone"], I, {0.0, 0.55, -0.05}, {w, 1.1, d - 0.1});
		addBox(g["dark"], I, {0.0, 0.38, 0.05}, {w * 0.55, 0.6, d - 0.2});                        // its mouth (the fire in it: film_roomlife.h)
		addBox(g["wood"], I, {0.0, 1.13, 0.0}, {w + 0.12, 0.06, d + 0.04});                       // its mantelpiece
		for (int q = 0; q < 3; ++q) addBox(g["wood"], I, {-0.12 + 0.12 * q, 0.12, 0.05}, {0.08, 0.08, 0.4});   // (logs)
	} else if (k == "coatrack") {
		tube(g["wood"], I, {{0.0, 0.0, 0.0}, {0.0, h, 0.0}}, 0.025);
		for (int q = 0; q < 4; ++q) {
			const double a = q * kPi / 2.0;
			tube(g["wood"], I, {{0.0, h - 0.15, 0.0}, {0.18 * std::cos(a), h - 0.05, 0.18 * std::sin(a)}}, 0.012);
		}
		ball(g["fabric"], I, {0.12, h - 0.45, 0.0}, {0.18, 0.4, 0.12});                           // a coat on it
	} else if (k == "plant") {
		addMesh(g["pot"], lathe({{0.0, 0.0}, {0.14, 0.0}, {0.18, 0.32}, {0.0, 0.32}}, 16), I);
		for (int q = 0; q < 9; ++q) {
			const double a = 2.0 * kPi * q / 9.0 + hash01(s, q), up = 0.55 + 0.4 * hash01(s + q, 7);
			ball(g["leaf"], I, {0.16 * std::cos(a), 0.32 + up, 0.16 * std::sin(a)}, {0.13, 0.05, 0.08});
			tube(g["leaf"], I, {{0.0, 0.3, 0.0}, {0.12 * std::cos(a), 0.3 + up, 0.12 * std::sin(a)}}, 0.008);
		}
	} else if (k == "aquarium") {
		addBox(g["wood"], I, {0.0, 0.3, 0.0}, {w, 0.6, d});
		addBox(g["glass"], I, {0.0, 0.78, 0.0}, {w * 0.96, 0.36, d * 0.92});                       // its glass (the fish in it: film_roomlife.h)
		addBox(g["water"], I, {0.0, 0.76, 0.0}, {w * 0.93, 0.3, d * 0.88});
		addBox(g["stone"], I, {0.0, 0.62, 0.0}, {w * 0.9, 0.03, d * 0.85});
	} else if (k == "mirror") {
		addBox(g["wood"], I, {0.0, 1.45, -0.01}, {w + 0.08, h + 0.08, 0.025});
		addBox(g["mirror"], I, {0.0, 1.45, 0.006}, {w, h, 0.006});
	} else if (k == "lamp") {
		addMesh(g["metal"], lathe({{0.0, 0.0}, {0.16, 0.0}, {0.16, 0.03}, {0.02, 0.05}, {0.02, h - 0.3}, {0.0, h - 0.3}}, 16), I);
		addMesh(g["shade"], lathe({{0.2, h - 0.32}, {0.12, h - 0.02}, {0.11, h - 0.02}, {0.19, h - 0.32}}, 24), I);
	} else if (k == "candles") {
		addBox(g["wood"], I, {0.0, 0.42, 0.0}, {w, 0.84, d});                                     // (a little table)
		for (int q = 0; q < 3; ++q) addMesh(g["white"], lathe({{0.0, 0.0}, {0.022, 0.0}, {0.022, 0.06 + 0.04 * q}, {0.0, 0.06 + 0.04 * q}}, 10), mul(I, translate(-0.07 + 0.07 * q, 0.84, 0.03 * (q % 2))));
	}
}

}  // namespace rooms
}  // namespace cartoon
