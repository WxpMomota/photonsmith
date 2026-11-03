#pragma once
// film_rooms.h -- rooms for a cartoon to happen in (film_file.h `room`, `house`; indoor round 1): their walls, floors and ceilings, their doors and windows, and the furniture
// a room of its kind has, set out by rule.
//
//   room KIND NAME at X Z [size W D H] [facing A] [style cosy|modern|old] [windows N] [y Y]
//       KIND: kitchen (a fridge, a counter with a sink and a stove, a table and chairs), bedroom (a bed, a wardrobe, a bedside table and its lamp, a rug), living (a sofa, an
//       armchair, a coffee table, a television, a bookshelf, a rug, a fireplace), bathroom (a bath, a basin, a toilet), classroom (rows of desks and chairs, a board),
//       shop (shelves of goods, a counter and its till), hall (a coat rack, a mat; in a house, the stairs). Walls a hand thick, skirting along them, a light switch by each door.
//   house NAME at X Z rooms KIND KIND ... [upstairs KIND ...] [facing A] [style S] [depth D]
//       the rooms in a row along its width, a doorway in each wall between two, the front door in the first; `upstairs`: a second floor over the first, with stairs up
//       from the first room (a hall, best) through a hole in its ceiling. Its rooms are called NAME.KIND (a second of a kind NAME.KIND2).
//
// Coordinates: pbrt's in a film file. A room's own frame (story space, as a mesh is): local x across it, y up from its floor, z from its front wall (-z, its door) to its back
// (+z, its windows). Walls: 0 the front, 1 the back, 2 the left (-x), 3 the right (+x).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace rooms {

constexpr double kWall = 0.12, kSlab = 0.2;   // (a wall's thickness; a floor's between two storeys)

// A doorway or a window in a wall: `along` 0 .. 1 across it from its left as one stands inside facing it... (from local -x on the front and back, from local -z on the sides).
struct Opening {
	int wall = 0;
	double along = 0.5, width = 1.0, height = 2.05, sill = 0.0;
	bool door = true;
	std::string to;   // (a doorway into another room: its name; "" the front door, or a window)
};
// A piece of furniture: where its middle is on the floor (local), which way its front faces (`turn` degrees about y: 0 local +z, 90 +x), and its size.
struct Item {
	std::string kind;
	double x = 0.0, z = 0.0, turn = 0.0, w = 1.0, d = 1.0, h = 1.0;
	double seat = -1.0;   // (the height a character sits at on it; below 0: not a seat)
	bool flat = false;    // (a rug: walked over, other things stand on it)
	int salt = 0;
};
struct Lamp {
	std::string kind, name;   // ceiling, table, floor, candles, fairy, fire, tv
	Vec3 at;                  // (local: where its light is)
	Vec3 colour{1.0, 0.8, 0.58};
	double power = 1.0;
	bool electric = true;     // (candles and a fire: not; a power cut leaves them)
};
struct Look {
	Vec3 wall{0.86, 0.8, 0.68}, paper2{0.7, 0.45, 0.4};
	std::string paper;        // (`walls ... wallpaper P`: stripes, roses, checks, herringbone, tiles; "" paint)
	std::string floor;        // (`floor ROOM wood|tiles|carpet|checks`; "": its kind's)
	Vec3 floorColour{-1.0, 0.0, 0.0};
};
struct Room {
	std::string name, kind = "living", style = "cosy", house;
	double x = 0.0, z = 0.0, y = 0.0, w = 5.0, d = 4.2, h = 2.6, facing = 0.0;
	int windows = 1, seed = 1, line = 0;
	std::vector<Opening> openings;
	std::vector<Item> items;
	std::vector<Lamp> lamps;
	Look look;
	std::string clutter = "lived";           // tidy, lived, messy (film_roomlife.h)
	double messFrom = -1.0, messTo = -1.0;   // (`clutter ... grows T0 T1`: the mess comes in over that time)
	std::vector<std::pair<double, std::string>> curtains;   // (`curtains ROOM open|closed|blowing [at T]`, in time order; none: open)
	std::vector<std::string> extras;         // (`furnish ROOM mirror|tv|aquarium|cabinet|lamp|fireplace|candles|fairy`)
	bool stairs = false;                     // (the stairs up from it: a house's first room with an upstairs)
	bool ceilingHole = false, floorHole = false;   // (over the stairs, below and above)
	double holeX0 = 0.0, holeX1 = 0.0, holeZ0 = 0.0, holeZ1 = 0.0;   // (local)
};
// A door that opens and shuts: its room, which of its doorways (`to` another room, "" its front door), and when.
struct Door {
	std::string room, to;
	std::vector<std::pair<double, double>> open;   // (from when, how far: 0 shut .. 1 wide)
	int roomIndex = -1, opening = -1;
};
// A lamp switched on and off (`switch ROOM [LAMP] on T [off T]`: LAMP its kind or name, "" all of the room's).
struct Switch {
	std::string room, lamp;
	double on = 0.0, off = 1e30;
};
// What a character does with a piece of furniture (`sit WHO on ITEM`, `lie WHO in bed`, `open WHO ITEM`, `lean WHO on ITEM`, `lookout WHO`): worked out when the film is loaded.
struct Use {
	std::string who, verb, item, room;
	double t0 = 0.0, t1 = 1e30;
	int line = 0;
	// (worked out)
	int roomIndex = -1, itemIndex = -1;
	Vec3 stand, seat;           // pbrt's: where it walks to, where it ends up (on a seat, in a bed, at a window)
	double faceTo = 0.0;        // the way it faces there (a character's facing: 0 +z, 90 +x, pbrt's)
	double seatHeight = 0.0;
	double arrive = -1.0;       // when it gets there (its walk's end)
};
struct Indoors {
	std::vector<Room> rooms;
	std::vector<Door> doors;
	std::vector<Switch> switches;
	std::vector<Use> uses;
	std::vector<std::pair<double, double>> powerCuts;
	bool any() const { return !rooms.empty(); }
};

inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"kitchen", "bedroom", "living", "bathroom", "classroom", "shop", "hall"};
	return k;
}
inline void sizeOf(const std::string &kind, double &w, double &d, double &h) {
	h = 2.6;
	if (kind == "kitchen") w = 4.2, d = 3.6;
	else if (kind == "bedroom") w = 3.8, d = 3.6;
	else if (kind == "bathroom") w = 2.6, d = 2.4;
	else if (kind == "classroom") w = 7.0, d = 6.0, h = 3.0;
	else if (kind == "shop") w = 6.0, d = 5.0, h = 3.2;
	else if (kind == "hall") w = 2.6, d = 4.2;
	else w = 5.0, d = 4.2;
}

// ---- a room's frame ----
inline Mat4 frameOf(const Room &r) { return mul(translate(-r.x, r.y, r.z), rotate({0.0, 1.0, 0.0}, -r.facing)); }
inline Vec3 toPbrt(const Room &r, Vec3 local) {
	const Vec3 s = transformPoint(frameOf(r), local);
	return {-s.x, s.y, s.z};
}
inline Vec3 toLocal(const Room &r, Vec3 pbrt) { return transformPoint(invertAffine(frameOf(r)), Vec3{-pbrt.x, pbrt.y, pbrt.z}); }
// A pbrt facing (0 +z, 90 +x) for a local direction of a room.
inline double facingOf(const Room &r, Vec3 localDir) {
	const Vec3 a = toPbrt(r, {0.0, 0.0, 0.0}), b = toPbrt(r, localDir);
	return std::atan2(b.x - a.x, b.z - a.z) * 180.0 / kPi;
}
inline bool inside(const Room &r, Vec3 pbrt, double margin = 0.0) {
	const Vec3 p = toLocal(r, pbrt);
	return std::abs(p.x) < 0.5 * r.w - margin && std::abs(p.z) < 0.5 * r.d - margin && p.y > -0.3 && p.y < r.h + 0.2;
}
// Which room a pbrt point is in (-1: none).
inline int roomAt(const Indoors &in, Vec3 pbrt) {
	for (std::size_t i = 0; i < in.rooms.size(); ++i) {
		const Vec3 p = toLocal(in.rooms[i], pbrt);
		if (std::abs(p.x) < 0.5 * in.rooms[i].w && std::abs(p.z) < 0.5 * in.rooms[i].d && p.y > -0.3 && p.y < in.rooms[i].h + 0.1) return static_cast<int>(i);
	}
	return -1;
}
// A wall's length and where a point `along` it is (local, on the wall's inner face), and the way into the room from it.
inline double wallLength(const Room &r, int wall) { return wall < 2 ? r.w : r.d; }
inline Vec3 wallPoint(const Room &r, int wall, double along, double y = 0.0) {
	const double inX = 0.5 * r.w - kWall, inZ = 0.5 * r.d - kWall;
	switch (wall) {
		case 0: return {-0.5 * r.w + along * r.w, y, -inZ};
		case 1: return {-0.5 * r.w + along * r.w, y, inZ};
		case 2: return {-inX, y, -0.5 * r.d + along * r.d};
		default: return {inX, y, -0.5 * r.d + along * r.d};
	}
}
inline Vec3 wallInward(int wall) { return wall == 0 ? Vec3{0, 0, 1} : wall == 1 ? Vec3{0, 0, -1} : wall == 2 ? Vec3{1, 0, 0} : Vec3{-1, 0, 0}; }
inline double turnFacing(int wall) { return wall == 0 ? 0.0 : wall == 1 ? 180.0 : wall == 2 ? 90.0 : -90.0; }   // (an item against it, its front into the room)
inline Vec3 frontOf(const Item &it) { return {std::sin(it.turn * kPi / 180.0), 0.0, std::cos(it.turn * kPi / 180.0)}; }

// ---- setting it out ----
namespace detail {
struct Rect {
	double x0, z0, x1, z1;
	bool overlaps(const Rect &o) const { return x0 < o.x1 && o.x0 < x1 && z0 < o.z1 && o.z0 < z1; }
};
inline Rect footprint(const Item &it, double grow = 0.0) {
	const bool across = std::abs(std::sin(it.turn * kPi / 180.0)) > 0.7;
	const double hw = 0.5 * (across ? it.d : it.w) + grow, hd = 0.5 * (across ? it.w : it.d) + grow;
	return {it.x - hw, it.z - hd, it.x + hw, it.z + hd};
}
// Where nothing may stand: in front of each doorway (a metre into the room), and the stairs.
inline std::vector<Rect> keepClear(const Room &r) {
	std::vector<Rect> out;
	for (const Opening &o : r.openings) {
		if (!o.door) continue;
		const Vec3 c = wallPoint(r, o.wall, o.along), in = wallInward(o.wall);
		const double half = 0.5 * o.width + 0.15;
		const Vec3 a = c + in * 1.05;
		out.push_back(o.wall < 2 ? Rect{c.x - half, std::min(c.z, a.z), c.x + half, std::max(c.z, a.z)} : Rect{std::min(c.x, a.x), c.z - half, std::max(c.x, a.x), c.z + half});
	}
	if (r.stairs) out.push_back({-0.5 * r.w, -0.5 * r.d, -0.5 * r.w + 1.25, 0.5 * r.d});
	return out;
}
}  // namespace detail

// A piece of furniture's size and seat (its kind's).
inline Item itemOf(const std::string &kind) {
	Item it;
	it.kind = kind;
	static const std::map<std::string, std::array<double, 4>> sizes = {   // w d h seat
		{"sofa", {2.0, 0.9, 0.85, 0.45}},   {"armchair", {0.9, 0.85, 0.85, 0.45}}, {"chair", {0.45, 0.48, 0.9, 0.46}},  {"table", {1.4, 0.85, 0.75, -1}},
		{"coffeetable", {1.1, 0.6, 0.42, -1}}, {"bed", {1.5, 2.05, 0.55, 0.5}},  {"wardrobe", {1.2, 0.6, 2.0, -1}},  {"bedside", {0.45, 0.4, 0.55, -1}},
		{"desk", {1.2, 0.6, 0.75, -1}},     {"bookshelf", {1.0, 0.32, 1.9, -1}}, {"counter", {2.4, 0.62, 0.9, -1}}, {"fridge", {0.7, 0.68, 1.8, -1}},
		{"stove", {0.6, 0.62, 0.9, -1}},    {"tv", {1.1, 0.42, 1.15, -1}},       {"rug", {2.2, 1.5, 0.01, -1}},      {"bath", {1.7, 0.75, 0.6, -1}},
		{"basin", {0.6, 0.45, 0.85, -1}},   {"toilet", {0.4, 0.65, 0.78, 0.42}}, {"board", {2.6, 0.06, 1.2, -1}},    {"schooldesk", {0.7, 0.5, 0.72, -1}},
		{"shelves", {1.8, 0.45, 2.0, -1}},  {"till", {1.6, 0.6, 1.0, -1}},       {"fireplace", {1.3, 0.4, 1.15, -1}}, {"coatrack", {0.5, 0.5, 1.8, -1}},
		{"mat", {0.9, 0.6, 0.01, -1}},      {"stool", {0.38, 0.38, 0.65, 0.65}}, {"plant", {0.45, 0.45, 1.0, -1}},  {"cabinet", {1.0, 0.42, 1.6, -1}},
		{"aquarium", {1.0, 0.4, 0.95, -1}}, {"mirror", {0.7, 0.04, 0.9, -1}},    {"lamp", {0.4, 0.4, 1.6, -1}},     {"candles", {0.3, 0.3, 0.9, -1}}};
	const auto s = sizes.find(kind);
	if (s != sizes.end()) it.w = s->second[0], it.d = s->second[1], it.h = s->second[2], it.seat = s->second[3];
	it.flat = kind == "rug" || kind == "mat";
	return it;
}
inline bool knownItem(const std::string &kind) { return itemOf(kind).h != 1.0 || kind == "plant"; }

// Puts `kind` against `wall` as near `along` as there is room for it (sliding it either way; else against another wall), or in the middle of the floor (wall -1) at local
// (along * w, z); false when it fits nowhere. `gap`: how far out from the wall.
inline bool place(Room &r, const std::string &kind, int wall, double along, double gap = 0.02, double z = 0.0, double turn = 0.0) {
	Item it = itemOf(kind);
	it.salt = r.seed * 131 + static_cast<int>(r.items.size());
	const std::vector<detail::Rect> clear = detail::keepClear(r);
	const auto fits = [&](const Item &c, int wall) {
		const detail::Rect f = detail::footprint(c);
		if (f.x0 < -0.5 * r.w + kWall - 1e-6 || f.x1 > 0.5 * r.w - kWall + 1e-6 || f.z0 < -0.5 * r.d + kWall - 1e-6 || f.z1 > 0.5 * r.d - kWall + 1e-6) return false;
		if (!c.flat)
			for (const detail::Rect &k : clear)
				if (f.overlaps(k)) return false;
		for (const Item &o : r.items)
			if (!o.flat && !c.flat && f.overlaps(detail::footprint(o))) return false;
		if (wall >= 0 && c.h > 0.85)   // (a tall one does not stand in front of a window)
			for (const Opening &o : r.openings)
				if (!o.door && o.wall == wall) {
					const Vec3 a = wallPoint(r, o.wall, o.along);
					const double half = 0.5 * o.width + 0.5 * (wall < 2 ? c.w : c.w);
					if ((wall < 2 ? std::abs(c.x - a.x) : std::abs(c.z - a.z)) < half) return false;
				}
		return true;
	};
	if (wall < 0) {
		it.x = (along - 0.5) * r.w, it.z = z, it.turn = turn;
		if (!fits(it, -1)) return false;
		r.items.push_back(it);
		return true;
	}
	static const int others[4] = {1, 3, 2, 0};
	for (int n = 0; n <= 12; ++n) {   // (against the wall it was wanted on; where a doorway or the rest leaves no room there, against another: its middle, then nearer its ends)
		const int w = n == 0 ? wall : others[(n - 1) % 4];
		if (n > 0 && w == wall && n <= 4) continue;
		it.turn = turnFacing(w);
		const double L = wallLength(r, w), want = n == 0 ? along : n <= 4 ? 0.5 : n <= 8 ? 0.15 : 0.85;
		for (int k = 0; k <= 40; ++k) {   // (as near as it can to where it was wanted: 0, +1, -1, +2 ... of 5 cm)
			const double a = want + (k % 2 ? 1.0 : -1.0) * ((k + 1) / 2) * 0.05 / L;
			if (a < 0.0 || a > 1.0) continue;
			const Vec3 p = wallPoint(r, w, a) + wallInward(w) * (0.5 * it.d + gap);
			it.x = p.x, it.z = p.z;
			if (fits(it, w)) {
				r.items.push_back(it);
				return true;
			}
		}
	}
	return false;
}
// The doorways and windows of a room on its own (a house sets its own), then its furniture, by its kind.
inline void openUp(Room &r) {
	if (!r.openings.empty()) return;
	r.openings.push_back({0, 0.25, 1.0, 2.05, 0.0, true, ""});
	const int n = std::max(0, r.windows);
	for (int k = 0; k < n; ++k) {
		const bool small = r.kind == "bathroom";
		r.openings.push_back({1, (k + 0.5) / n, small ? 0.6 : 1.2, small ? 0.6 : 1.25, small ? 1.4 : 0.9, false, ""});
	}
}
inline void furnish(Room &r) {
	r.items.clear();
	const std::string &k = r.kind;
	if (k == "kitchen") {
		place(r, "fridge", 3, 0.88);
		place(r, "counter", 3, 0.5);
		place(r, "stove", 3, 0.16);
		place(r, "table", -1, 0.4, 0.0, 0.2);
		for (const auto &[dx, dz, turn] : std::vector<std::array<double, 3>>{{-0.45, -0.65, 0.0}, {0.45, -0.65, 0.0}, {-0.45, 0.65, 180.0}, {0.45, 0.65, 180.0}})
			place(r, "chair", -1, 0.4 + dx / r.w, 0.0, 0.2 + dz, turn);
		place(r, "plant", 2, 0.92);
	} else if (k == "bedroom") {
		place(r, "bed", 2, 0.55);
		place(r, "bedside", 2, 0.85);
		place(r, "wardrobe", 3, 0.3);
		place(r, "rug", -1, 0.5, 0.0, 0.1);
		place(r, "desk", 3, 0.75);
		place(r, "chair", -1, 0.5 + (0.5 * r.w - kWall - 1.0) / r.w, 0.0, (0.75 - 0.5) * r.d, -90.0);
	} else if (k == "living") {
		if (place(r, "sofa", 2, 0.5)) {   // (a table in front of it, wherever it went, with room to stand between to sit down)
			const Item s = r.items.back();
			const Vec3 f = frontOf(s), table = Vec3{s.x, 0.0, s.z} + f * (0.5 * s.d + 1.15), rug = Vec3{s.x, 0.0, s.z} + f * (0.5 * s.d + 0.95);
			place(r, "coffeetable", -1, 0.5 + table.x / r.w, 0.0, table.z, s.turn);
			place(r, "rug", -1, 0.5 + rug.x / r.w, 0.0, rug.z, s.turn);
		}
		place(r, "tv", 3, 0.5);
		place(r, "armchair", 1, 0.85);
		place(r, "bookshelf", 1, 0.12);
		place(r, "fireplace", 0, 0.75);
		place(r, "lamp", 2, 0.12);
		place(r, "plant", 3, 0.9);
	} else if (k == "bathroom") {
		place(r, "bath", 1, 0.5);
		place(r, "toilet", 3, 0.3);
		place(r, "basin", 2, 0.45);
	} else if (k == "classroom") {
		place(r, "board", 3, 0.5, 0.01);
		place(r, "desk", -1, 0.5 + (0.5 * r.w - 1.2) / r.w, 0.0, 0.0, -90.0);
		for (int row = 0; row < 3; ++row)
			for (int col = 0; col < 3; ++col) {
				const double x = -0.5 * r.w + 1.1 + 1.25 * row, z = -1.6 + 1.6 * col;
				place(r, "schooldesk", -1, 0.5 + x / r.w, 0.0, z, 90.0);
				place(r, "chair", -1, 0.5 + (x - 0.5) / r.w, 0.0, z, 90.0);
			}
		place(r, "bookshelf", 1, 0.15);
	} else if (k == "shop") {
		for (const int wall : {1, 2}) place(r, "shelves", wall, 0.3), place(r, "shelves", wall, 0.72);
		place(r, "till", -1, 0.72, 0.0, -0.4, 0.0);
		place(r, "plant", 0, 0.9);
	} else if (k == "hall") {
		place(r, "coatrack", 3, 0.2);
		place(r, "mat", -1, 0.5 + (wallPoint(r, 0, r.openings.empty() ? 0.25 : r.openings[0].along).x) / r.w, 0.0, -0.5 * r.d + 0.6);
		place(r, "plant", 3, 0.85);
	}
	for (const std::string &e : r.extras) {   // (`furnish ROOM ...`: against the first wall it fits)
		if (e == "fairy") continue;   // (a string of lights: a lamp, lightUp)
		bool done = false;
		for (const int wall : {1, 3, 2, 0})
			for (const double a : {0.5, 0.3, 0.7, 0.15, 0.85})
				if (!done && place(r, e, wall, a, e == "mirror" ? 0.005 : 0.02)) done = true;
	}
}
// The lamps a room has (its kind's, and those its furniture brings).
inline void lightUp(Room &r) {
	r.lamps.clear();
	const int ceilings = r.kind == "classroom" || r.kind == "shop" ? 2 : 1;
	for (int k = 0; k < ceilings; ++k) r.lamps.push_back({"ceiling", "ceiling" + std::to_string(k + 1), {ceilings == 1 ? 0.0 : (k ? 0.25 : -0.25) * r.w, r.h - 0.45, 0.0}, {1.0, 0.86, 0.68}, 1.0, true});
	for (const Item &it : r.items) {
		const Vec3 base{it.x, 0.0, it.z};
		if (it.kind == "bedside") r.lamps.push_back({"table", "bedside", base + Vec3{0.0, it.h + 0.32, 0.0}, {1.0, 0.78, 0.52}, 0.5, true});
		else if (it.kind == "lamp") r.lamps.push_back({"floor", "lamp", base + Vec3{0.0, it.h - 0.12, 0.0}, {1.0, 0.8, 0.55}, 0.7, true});
		else if (it.kind == "candles") r.lamps.push_back({"candles", "candles", base + Vec3{0.0, it.h + 0.06, 0.0}, {1.0, 0.62, 0.3}, 0.25, false});
		else if (it.kind == "fireplace") r.lamps.push_back({"fire", "fire", base + frontOf(it) * 0.05 + Vec3{0.0, 0.3, 0.0}, {1.0, 0.5, 0.18}, 1.0, false});
		else if (it.kind == "tv") r.lamps.push_back({"tv", "tv", base + frontOf(it) * 0.26 + Vec3{0.0, 0.85, 0.0}, {0.55, 0.7, 1.0}, 0.35, true});
	}
	if (std::find(r.extras.begin(), r.extras.end(), "fairy") != r.extras.end()) r.lamps.push_back({"fairy", "fairy", {0.0, r.h - 0.2, 0.5 * r.d - kWall}, {1.0, 0.85, 0.6}, 0.3, true});
}

// ---- a house ----
// Its rooms laid out (in `in`), joined by doorways; "" or what is wrong.
inline std::string layHouse(Indoors &in, const std::string &name, double x, double z, double facing, const std::string &style, double depth, const std::vector<std::string> &down,
							const std::vector<std::string> &up, int line) {
	if (down.empty()) return "`house NAME at X Z rooms KIND ...`: its rooms";
	if (up.size() > down.size()) return "a house's upstairs has no more rooms than its ground floor";
	std::map<std::string, int> seen;
	const auto named = [&](const std::string &kind) {
		const int n = ++seen[kind];
		return name + "." + kind + (n > 1 ? std::to_string(n) : std::string());
	};
	const auto lay = [&](const std::vector<std::string> &kinds_, double y, bool upper) {
		double total = 0.0;
		std::vector<double> widths;
		for (const std::string &k : kinds_) {
			double w = 0, d = 0, h = 0;
			sizeOf(k, w, d, h);
			widths.push_back(w), total += w;
		}
		if (upper) {   // (over the rooms below: the same widths)
			widths.clear(), total = 0.0;
			for (std::size_t i = 0; i < kinds_.size(); ++i) widths.push_back(in.rooms[in.rooms.size() - down.size() + i].w), total += widths.back();
		}
		double from = -0.5 * total;
		const double a = facing * kPi / 180.0;
		const Vec3 across{std::cos(a), 0.0, -std::sin(a)};   // (the house's own +x: to the right as its front faces... pbrt's, for facing 0: -x mirrored to the story's +x)
		const std::size_t below = in.rooms.size() - (upper ? down.size() : 0);   // (upstairs: the rooms each is over)
		for (std::size_t i = 0; i < kinds_.size(); ++i) {
			Room r;
			r.kind = kinds_[i], r.name = named(kinds_[i]), r.house = name, r.style = style, r.facing = facing, r.line = line, r.seed = line * 7 + static_cast<int>(i) + (upper ? 50 : 0);
			double w = 0, d = 0, h = 0;
			sizeOf(r.kind, w, d, h);
			r.w = widths[i], r.d = depth, r.h = 2.6, r.y = y;
			const double mid = from + 0.5 * r.w;
			from += r.w;
			r.x = x - across.x * mid, r.z = z + across.z * mid;   // (the story's +x is pbrt's -x)
			if (upper) r.x = in.rooms[below + i].x, r.z = in.rooms[below + i].z;
			const bool small = r.kind == "bathroom";
			if (!upper && i == 0) {   // the front door (beside it a window, where the stairs push it along)
				r.openings.push_back({0, up.empty() ? 0.3 : 0.72, 1.0, 2.05, 0.0, true, ""});
				if (!up.empty()) r.openings.push_back({0, 0.42, 1.0, 1.2, 0.95, false, ""});
			} else r.openings.push_back({0, 0.5, small ? 0.6 : 1.2, small ? 0.6 : 1.25, small ? 1.4 : 0.9, false, ""});   // (a window onto the front)
			r.openings.push_back({1, 0.5, small ? 0.6 : 1.2, small ? 0.6 : 1.25, small ? 1.4 : 0.9, false, ""});   // and the back
			in.rooms.push_back(r);
		}
		const std::size_t first = in.rooms.size() - kinds_.size();
		for (std::size_t i = first; i + 1 < in.rooms.size(); ++i) {   // a doorway in each wall between two (near the back: the middle of the wall left for furniture)
			in.rooms[i].openings.push_back({3, 0.8, 1.0, 2.05, 0.0, true, in.rooms[i + 1].name});
			in.rooms[i + 1].openings.push_back({2, 0.8, 1.0, 2.05, 0.0, true, in.rooms[i].name});
		}
	};
	const std::size_t start = in.rooms.size();
	lay(down, 0.0, false);
	if (!up.empty()) {
		in.rooms[start].stairs = true;
		lay(up, in.rooms[start].h + kSlab, true);
		// the stairs: along the first room's left wall, from its front to its back, through a hole in its ceiling and the floor above (taken after the upstairs is laid:
		// the rooms may have moved as it was added)
		Room &hall = in.rooms[start], &above = in.rooms[start + down.size()];
		const double x0 = -0.5 * hall.w + kWall, x1 = x0 + 1.0, zTop = 0.5 * hall.d - kWall;
		hall.ceilingHole = above.floorHole = true;
		hall.holeX0 = above.holeX0 = x0, hall.holeX1 = above.holeX1 = x1, hall.holeZ0 = above.holeZ0 = zTop - 2.6, hall.holeZ1 = above.holeZ1 = zTop;
	}
	return std::string();
}

// What it is all made of, a room at a time: its pieces' meshes by material (story space; film_roomlook.h gives each its material).
using Groups = std::map<std::string, Mesh>;

namespace detail {
// A box (local), its faces' uvs in metres (so a picture repeats by the metre), into `m` through `F`.
inline void addBox(Mesh &m, const Mat4 &F, Vec3 centre, Vec3 size) {
	const Mesh b = box(size.x, size.y, size.z);
	const int base = static_cast<int>(m.verts.size());
	const Mat4 M = mul(F, translate(centre.x, centre.y, centre.z));
	const Mat4 N = invertAffine(M);
	for (std::size_t i = 0; i < b.verts.size(); ++i) {
		const Vec3 v = b.verts[i], n = b.normals.size() == b.verts.size() ? b.normals[i] : Vec3{0, 1, 0};
		m.verts.push_back(transformPoint(M, v));
		const Vec3 tn{N[0] * n.x + N[4] * n.y + N[8] * n.z, N[1] * n.x + N[5] * n.y + N[9] * n.z, N[2] * n.x + N[6] * n.y + N[10] * n.z};
		m.normals.push_back(normalize(tn));
		// (by the face it is on: the two of its three local axes the face spans, in metres)
		const Vec3 p = v + centre;
		const double u = std::abs(n.x) > 0.5 ? p.z : p.x, w = std::abs(n.y) > 0.5 ? p.z : p.y;
		m.uvs.push_back({u, w});
	}
	for (const auto &t : b.tris) m.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
}
inline void addMesh(Mesh &into, const Mesh &m, const Mat4 &M) {
	const int base = static_cast<int>(into.verts.size());
	const Mat4 N = invertAffine(M);
	for (std::size_t i = 0; i < m.verts.size(); ++i) {
		into.verts.push_back(transformPoint(M, m.verts[i]));
		const Vec3 n = m.normals.size() == m.verts.size() ? m.normals[i] : Vec3{0, 1, 0};
		into.normals.push_back(normalize(Vec3{N[0] * n.x + N[4] * n.y + N[8] * n.z, N[1] * n.x + N[5] * n.y + N[9] * n.z, N[2] * n.x + N[6] * n.y + N[10] * n.z}));
		into.uvs.push_back(m.uvs.size() == m.verts.size() ? m.uvs[i] : std::make_pair(0.0, 0.0));
	}
	for (const auto &t : m.tris) into.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
}
}  // namespace detail

// A wall with its openings cut out of it: boxes between them, a lintel over each, the wall under a window; their frames (trim) and a window's pane (glass).
inline void wallPieces(const Room &r, int wall, Groups &g, const Mat4 &F) {
	using detail::addBox;
	const double L = wallLength(r, wall), H = r.h;
	std::vector<const Opening *> on;
	for (const Opening &o : r.openings)
		if (o.wall == wall) on.push_back(&o);
	std::sort(on.begin(), on.end(), [](const Opening *a, const Opening *b) { return a->along < b->along; });
	// a span of the wall from s0 to s1 metres along it, from y0 to y1, as a box in the room's frame
	const auto span = [&](double s0, double s1, double y0, double y1, Mesh &m) {
		if (s1 - s0 < 1e-4 || y1 - y0 < 1e-4) return;
		const double mid = 0.5 * (s0 + s1) - 0.5 * L, len = s1 - s0, ym = 0.5 * (y0 + y1), hh = y1 - y0;
		const double off = (wall < 2 ? 0.5 * r.d : 0.5 * r.w) - 0.5 * kWall;
		if (wall == 0) addBox(m, F, {mid, ym, -off}, {len, hh, kWall});
		else if (wall == 1) addBox(m, F, {mid, ym, off}, {len, hh, kWall});
		else if (wall == 2) addBox(m, F, {-off, ym, mid}, {kWall, hh, len});
		else addBox(m, F, {off, ym, mid}, {kWall, hh, len});
	};
	Mesh &m = g["wall" + std::to_string(wall)];
	double s = 0.0;
	for (const Opening *o : on) {
		const double a = o->along * L - 0.5 * o->width, b = o->along * L + 0.5 * o->width;
		span(s, a, 0.0, H, m);
		span(a, b, o->sill + o->height, H, m);
		if (o->sill > 0.0) span(a, b, 0.0, o->sill, m);
		// its frame: round its sides and top (a window: its sill too, and a bar across), on the room's side
		Mesh &trim = g["trim"];
		const double t = 0.07, inset = 0.5 * kWall + 0.015;
		const auto piece = [&](double s0, double s1, double y0, double y1, double depth) {
			const double mid = 0.5 * (s0 + s1) - 0.5 * L, off = (wall < 2 ? 0.5 * r.d : 0.5 * r.w) - inset;
			const Vec3 size = wall < 2 ? Vec3{s1 - s0, y1 - y0, depth} : Vec3{depth, y1 - y0, s1 - s0};
			const Vec3 c = wall == 0 ? Vec3{mid, 0.5 * (y0 + y1), -off} : wall == 1 ? Vec3{mid, 0.5 * (y0 + y1), off} : wall == 2 ? Vec3{-off, 0.5 * (y0 + y1), mid} : Vec3{off, 0.5 * (y0 + y1), mid};
			addBox(trim, F, c, size);
		};
		piece(a - t, a, o->sill, o->sill + o->height + t, kWall + 0.05);
		piece(b, b + t, o->sill, o->sill + o->height + t, kWall + 0.05);
		piece(a - t, b + t, o->sill + o->height, o->sill + o->height + t, kWall + 0.05);
		if (!o->door) {
			piece(a - t, b + t, o->sill - 0.04, o->sill, kWall + 0.12);
			piece(a, b, o->sill + 0.5 * o->height - 0.02, o->sill + 0.5 * o->height + 0.02, 0.05);   // (the bar across)
			piece(0.5 * (a + b) - 0.02, 0.5 * (a + b) + 0.02, o->sill, o->sill + o->height, 0.05);
			// its pane, in the middle of the wall's thickness
			const double mid = o->along * L - 0.5 * L, off = (wall < 2 ? 0.5 * r.d : 0.5 * r.w) - 0.5 * kWall;
			const Vec3 size = wall < 2 ? Vec3{o->width, o->height, 0.012} : Vec3{0.012, o->height, o->width};
			const Vec3 c = wall == 0 ? Vec3{mid, o->sill + 0.5 * o->height, -off} : wall == 1 ? Vec3{mid, o->sill + 0.5 * o->height, off} : wall == 2 ? Vec3{-off, o->sill + 0.5 * o->height, mid} : Vec3{off, o->sill + 0.5 * o->height, mid};
			addBox(g["pane"], F, c, size);
		} else if (o->to.empty() || wall != 0) {   // a light switch beside a doorway, on the room's side
			const Vec3 at = wallPoint(r, wall, std::min(1.0, (b + 0.18) / L), 1.15) + wallInward(wall) * 0.012;
			addBox(g["white"], F, at, wall < 2 ? Vec3{0.08, 0.12, 0.02} : Vec3{0.02, 0.12, 0.08});
		}
		s = b;
	}
	span(s, L, 0.0, H, m);
	// skirting along its foot (not across its doorways)
	double from = 0.0;
	const auto skirt = [&](double s0, double s1) {
		if (s1 - s0 < 0.02) return;
		const double mid = 0.5 * (s0 + s1) - 0.5 * L, off = (wall < 2 ? 0.5 * r.d : 0.5 * r.w) - kWall - 0.01;
		const Vec3 size = wall < 2 ? Vec3{s1 - s0, 0.1, 0.02} : Vec3{0.02, 0.1, s1 - s0};
		const Vec3 c = wall == 0 ? Vec3{mid, 0.05, -off} : wall == 1 ? Vec3{mid, 0.05, off} : wall == 2 ? Vec3{-off, 0.05, mid} : Vec3{off, 0.05, mid};
		addBox(g["trim"], F, c, size);
	};
	for (const Opening *o : on)
		if (o->door) skirt(from + kWall, o->along * L - 0.5 * o->width - 0.07), from = o->along * L + 0.5 * o->width + 0.07 - kWall;
	skirt(from + kWall, L - kWall);
}

// A slab over the room's footprint with a hole cut out of it (the stairs'): up to four boxes round the hole.
inline void slab(const Room &r, Mesh &m, const Mat4 &F, double y0, double y1, bool hole) {
	using detail::addBox;
	const double x0 = -0.5 * r.w, x1 = 0.5 * r.w, z0 = -0.5 * r.d, z1 = 0.5 * r.d, ym = 0.5 * (y0 + y1), hh = y1 - y0;
	if (!hole) {
		addBox(m, F, {0.0, ym, 0.0}, {r.w, hh, r.d});
		return;
	}
	const auto boxOver = [&](double ax, double az, double bx, double bz) {
		if (bx - ax > 1e-4 && bz - az > 1e-4) addBox(m, F, {0.5 * (ax + bx), ym, 0.5 * (az + bz)}, {bx - ax, hh, bz - az});
	};
	boxOver(x0, z0, x1, r.holeZ0);
	boxOver(x0, r.holeZ1, x1, z1);
	boxOver(x0, r.holeZ0, r.holeX0, r.holeZ1);
	boxOver(r.holeX1, r.holeZ0, x1, r.holeZ1);
}

// The stairs: steps of 20 cm up 26 cm treads along the left wall, from the front to the hole's back edge, a rail along them.
inline void stairPieces(const Room &r, Groups &g, const Mat4 &F) {
	const double rise = r.h + kSlab, x0 = r.holeX0, x1 = r.holeX1, zTop = r.holeZ1;
	const int steps = static_cast<int>(std::ceil(rise / 0.2));
	const double riseEach = rise / steps, tread = std::min(0.26, (zTop - (-0.5 * r.d + kWall + 0.1)) / steps);
	for (int k = 0; k < steps; ++k) {   // (open treads on a stringer each side: the room seen under them, not a wall of steps)
		const double z = zTop - (steps - k) * tread, top = (k + 1) * riseEach;
		detail::addBox(g["wood"], F, {0.5 * (x0 + x1), top - 0.025, z + 0.5 * tread}, {x1 - x0, 0.05, tread});
		for (const double x : {x0 + 0.03, x1 - 0.03}) detail::addBox(g["wood"], F, {x, top - 0.17, z + 0.5 * tread}, {0.06, 0.34, tread});
	}
	const double zBottom = zTop - steps * tread;
	detail::addMesh(g["wood"], sdf::tubeMesh({{x1 - 0.04, 0.95, zBottom}, {x1 - 0.04, rise + 0.95, zTop}}, 0.025, 1.0, 8), F);
	for (int k = 0; k <= steps; k += 3) {
		const double z = zBottom + k * tread;
		detail::addBox(g["wood"], F, {x1 - 0.04, 0.5 * (k * riseEach + 0.95 + k * riseEach), z}, {0.03, 0.95, 0.03});
	}
}

}  // namespace rooms
}  // namespace cartoon
