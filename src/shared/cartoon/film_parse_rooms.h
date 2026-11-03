#pragma once
// film_parse_rooms.h -- the lines of a film file for rooms and houses and what happens in them (indoor round 1; its grammar: film_parse.h). film_parse.cpp hands them here.
//
//   room KIND NAME at X Z [size W D H] [facing A] [style S] [windows N] [y Y]      (film_rooms.h)
//   house NAME at X Z rooms KIND ... [upstairs KIND ...] [facing A] [style S] [depth D]
//   door ROOM [to ROOM2] open T [AMOUNT] [close T] ...    curtains ROOM open|closed|blowing [at T]    (film_roomlife.h)
//   switch ROOM [LAMP] on T [off T]    powercut T [until T]    clutter ROOM tidy|lived|messy [grows T0 T1]    furnish ROOM KIND ...
//   walls ROOM paint R G B | wallpaper P [colour R G B] [colour R G B]    floor ROOM wood|boards|tiles|carpet|checks [colour R G B]    (film_roomlook.h)
//   sit WHO on ITEM [in ROOM] at T [until T]    lie WHO in|on ITEM [in ROOM] at T [until T]    open WHO ITEM [in ROOM] at T [until T]
//   lean WHO on ITEM [in ROOM] at T [until T]    lookout WHO [in ROOM] [window N] at T [until T]      (it walks there from where it is, film_roomworld.cpp)
//   (in a shot)  cutaway      peek ROOM [window N] [from D]                                            (film_roomworld.cpp)

#include "film_rooms.h"
#include "film_roomlook.h"
#include "film_spec.h"

namespace cartoon {
namespace film_file {

inline bool indoorVerb(const std::string &verb, const std::vector<std::string> &t) {
	static const std::vector<std::string> k = {"room", "house", "door", "curtains", "switch", "powercut", "clutter", "furnish", "walls", "floor", "lie", "open", "lean", "lookout", "cutaway", "peek"};
	if (verb == "sit") return t.size() > 2 && t[2] == "on";   // (`sit WHO on ITEM`: the other `sit` is film_parse.cpp's)
	return std::find(k.begin(), k.end(), verb) != k.end();
}

namespace detail {
inline rooms::Room *roomNamed(Spec &s, const std::string &name) {
	for (rooms::Room &r : s.indoors.rooms)
		if (r.name == name) return &r;
	return nullptr;
}
inline bool colourAt(const std::vector<std::string> &t, std::size_t i, Vec3 &c) {
	return i + 2 < t.size() && number(t[i], c.x) && number(t[i + 1], c.y) && number(t[i + 2], c.z) && std::min({c.x, c.y, c.z}) >= 0.0 && std::max({c.x, c.y, c.z}) <= 1.0;
}
// Somewhere to stand near `want` (pbrt) that a walker can get to: clear of the walls and the furniture by more than one's size indoors (film_file.cpp's findWays) - `want`
// itself, or the nearest such place round it.
inline Vec3 clearSpot(const rooms::Room &r, Vec3 want) {
	const double gap = 0.4;   // (its size, 0.28, and a map cell's half: where it can stand, not just where it would fit)
	const auto ok = [&](Vec3 p) {
		if (std::abs(p.x) > 0.5 * r.w - rooms::kWall - gap || std::abs(p.z) > 0.5 * r.d - rooms::kWall - gap) return false;
		for (std::size_t i = 0; i < r.items.size(); ++i) {
			if (r.items[i].flat) continue;
			const rooms::detail::Rect f = rooms::detail::footprint(r.items[i], gap);
			if (p.x > f.x0 && p.x < f.x1 && p.z > f.z0 && p.z < f.z1) return false;
		}
		return true;
	};
	const Vec3 w = rooms::toLocal(r, want);
	if (ok(w)) return want;
	for (double d = 0.1; d < 3.5; d += 0.1)
		for (int a = 0; a < 16; ++a) {
			const Vec3 p = w + Vec3{d * std::cos(a * kPi / 8.0), 0.0, d * std::sin(a * kPi / 8.0)};
			if (ok(p)) return rooms::toPbrt(r, p);
		}
	return want;
}
}  // namespace detail

// A line for rooms into `s`: "" or what is wrong with it. (The rooms are set out, and what refers to them checked, at the film's end: settleRooms.)
inline std::string indoorLine(Spec &s, const std::string &verb, const std::string &line, int n) {
	using detail::number;
	const std::vector<std::string> t = detail::tokens(line);
	rooms::Indoors &in = s.indoors;
	if (verb == "room") {
		const char *usage = "`room KIND NAME at X Z [size W D H] [facing A] [style cosy|modern|old] [windows N] [y Y]` (KIND: kitchen, bedroom, living, bathroom, classroom, shop, hall)";
		rooms::Room r;
		if (t.size() < 6 || std::find(rooms::kinds().begin(), rooms::kinds().end(), t[1]) == rooms::kinds().end() || t[3] != "at" || !number(t[4], r.x) || !number(t[5], r.z)) return usage;
		r.kind = t[1], r.name = t[2], r.line = n, r.seed = n;
		if (detail::roomNamed(s, r.name)) return "there is a room called \"" + r.name + "\" already";
		rooms::sizeOf(r.kind, r.w, r.d, r.h);
		for (std::size_t i = 6; i < t.size();) {
			double v = 0.0;
			if (t[i] == "size" && i + 3 < t.size() && number(t[i + 1], r.w) && number(t[i + 2], r.d) && number(t[i + 3], r.h) && r.w >= 1.5 && r.w <= 30.0 && r.d >= 1.5 && r.d <= 30.0 && r.h >= 2.0 && r.h <= 8.0) i += 4;
			else if (t[i] == "facing" && i + 1 < t.size() && number(t[i + 1], r.facing)) i += 2;
			else if (t[i] == "style" && i + 1 < t.size() && std::find(rooms::styles().begin(), rooms::styles().end(), t[i + 1]) != rooms::styles().end()) r.style = t[i + 1], i += 2;
			else if (t[i] == "windows" && i + 1 < t.size() && number(t[i + 1], v) && v >= 0.0 && v <= 6.0) r.windows = static_cast<int>(v), i += 2;
			else if (t[i] == "y" && i + 1 < t.size() && number(t[i + 1], r.y)) i += 2;
			else return usage + std::string(" (size: 1.5 to 30 m across and deep, 2 to 8 m high)");
		}
		in.rooms.push_back(r);
		return std::string();
	}
	if (verb == "house") {
		const char *usage = "`house NAME at X Z rooms KIND ... [upstairs KIND ...] [facing A] [style S] [depth D]`";
		if (t.size() < 7 || t[2] != "at" || t[5] != "rooms") return usage;
		double x = 0.0, z = 0.0, facing = 0.0, depth = 4.2;
		if (!number(t[3], x) || !number(t[4], z)) return usage;
		std::string style = "cosy";
		std::vector<std::string> down, up;
		bool upper = false;
		std::size_t i = 6;
		for (; i < t.size(); ++i) {
			if (t[i] == "upstairs") {
				if (upper) return usage;
				upper = true;
				continue;
			}
			if (std::find(rooms::kinds().begin(), rooms::kinds().end(), t[i]) == rooms::kinds().end()) break;
			(upper ? up : down).push_back(t[i]);
		}
		for (; i < t.size(); i += 2) {
			if (i + 1 >= t.size()) return usage;
			if (t[i] == "facing" && number(t[i + 1], facing)) continue;
			if (t[i] == "style" && std::find(rooms::styles().begin(), rooms::styles().end(), t[i + 1]) != rooms::styles().end()) {
				style = t[i + 1];
				continue;
			}
			if (t[i] == "depth" && number(t[i + 1], depth) && depth >= 2.5 && depth <= 12.0) continue;
			return usage + std::string(": \"") + t[i] + "\" (rooms: kitchen, bedroom, living, bathroom, classroom, shop, hall)";
		}
		if (down.size() > 8 || up.size() > 8) return "a house has 8 rooms a floor at most";
		for (const rooms::Room &r : in.rooms)
			if (r.house == t[1]) return "there is a house called \"" + t[1] + "\" already";
		return rooms::layHouse(in, t[1], x, z, facing, style, depth, down, up, n);
	}
	if (verb == "door") {
		const char *usage = "`door ROOM [to ROOM2] open T [AMOUNT] [close T] ...`: its front door (or the doorway into ROOM2) opening and shutting";
		if (t.size() < 4) return usage;
		rooms::Door d;
		d.room = t[1];
		std::size_t i = 2;
		if (t[i] == "to") {
			if (i + 1 >= t.size()) return usage;
			d.to = t[i + 1], i += 2;
		}
		while (i < t.size()) {
			double at = 0.0, amount = 1.0;
			if ((t[i] != "open" && t[i] != "close") || i + 1 >= t.size() || !number(t[i + 1], at)) return usage;
			const bool opening = t[i] == "open";
			i += 2;
			if (opening && i < t.size() && number(t[i], amount)) {
				if (amount < 0.0 || amount > 1.0) return usage + std::string(" (AMOUNT 0 to 1)");
				++i;
			}
			if (!d.open.empty() && at < d.open.back().first) return "a door's times go in order";
			d.open.push_back({at, opening ? amount : 0.0});
		}
		in.doors.push_back(d);
		return std::string();
	}
	if (verb == "curtains") {
		const char *usage = "`curtains ROOM open|closed|blowing [at T]`";
		double at = -1e9;
		if (t.size() != 3 && !(t.size() == 5 && t[3] == "at" && number(t[4], at))) return usage;
		if (t[2] != "open" && t[2] != "closed" && t[2] != "blowing") return usage;
		rooms::Room *r = detail::roomNamed(s, t[1]);
		if (!r) return "`curtains`: no room called \"" + t[1] + "\" (before this line)";
		if (!r->curtains.empty() && at < r->curtains.back().first) return "a room's curtains go in time order";
		r->curtains.push_back({at, t[2]});
		return std::string();
	}
	if (verb == "switch") {
		const char *usage = "`switch ROOM [LAMP] on T [off T]` (LAMP: ceiling, table, floor, tv, fairy, or a lamp's name)";
		rooms::Switch sw;
		if (t.size() < 4) return usage;
		sw.room = t[1];
		std::size_t i = 2;
		if (t[i] != "on") sw.lamp = t[i++];
		if (i + 1 >= t.size() || t[i] != "on" || !number(t[i + 1], sw.on)) return usage;
		i += 2;
		if (i < t.size()) {
			if (i + 2 != t.size() || t[i] != "off" || !number(t[i + 1], sw.off) || sw.off <= sw.on) return usage;
		}
		in.switches.push_back(sw);
		return std::string();
	}
	if (verb == "powercut") {
		double a = 0.0, b = 1e30;
		if (!(t.size() == 2 && number(t[1], a)) && !(t.size() == 4 && number(t[1], a) && t[2] == "until" && number(t[3], b) && b > a)) return "`powercut T [until T]`";
		in.powerCuts.push_back({a, b});
		return std::string();
	}
	if (verb == "clutter") {
		const char *usage = "`clutter ROOM tidy|lived|messy [grows T0 T1]`";
		rooms::Room *r = t.size() > 1 ? detail::roomNamed(s, t[1]) : nullptr;
		if (t.size() < 3 || (t[2] != "tidy" && t[2] != "lived" && t[2] != "messy")) return usage;
		if (!r) return "`clutter`: no room called \"" + t[1] + "\" (before this line)";
		r->clutter = t[2];
		if (t.size() == 6 && t[3] == "grows" && number(t[4], r->messFrom) && number(t[5], r->messTo) && r->messTo > r->messFrom && r->messFrom >= 0.0) return std::string();
		if (t.size() != 3) return usage;
		return std::string();
	}
	if (verb == "furnish") {
		static const std::vector<std::string> extras = {"mirror", "tv", "aquarium", "cabinet", "lamp", "candles", "fireplace", "plant", "fairy", "bookshelf", "armchair", "chair", "desk"};
		rooms::Room *r = t.size() > 1 ? detail::roomNamed(s, t[1]) : nullptr;
		if (t.size() < 3) return "`furnish ROOM KIND ...` (mirror, tv, aquarium, cabinet, lamp, candles, fireplace, plant, fairy, bookshelf, armchair, chair, desk)";
		if (!r) return "`furnish`: no room called \"" + t[1] + "\" (before this line)";
		for (std::size_t i = 2; i < t.size(); ++i) {
			if (std::find(extras.begin(), extras.end(), t[i]) == extras.end()) return "`furnish`: \"" + t[i] + "\"? (mirror, tv, aquarium, cabinet, lamp, candles, fireplace, plant, fairy, bookshelf, armchair, chair, desk)";
			r->extras.push_back(t[i]);
		}
		return std::string();
	}
	if (verb == "walls" || verb == "floor") {
		rooms::Room *r = t.size() > 1 ? detail::roomNamed(s, t[1]) : nullptr;
		const std::string usage = verb == "walls" ? "`walls ROOM paint R G B` or `walls ROOM wallpaper stripes|roses|checks|herringbone|tiles [colour R G B] [colour R G B]` (colours 0 to 1)"
												  : "`floor ROOM wood|boards|tiles|carpet|checks [colour R G B]`";
		if (t.size() < 3) return usage;
		if (!r) return "`" + verb + "`: no room called \"" + t[1] + "\" (before this line)";
		if (verb == "floor") {
			if (std::find(rooms::floors().begin(), rooms::floors().end(), t[2]) == rooms::floors().end()) return usage;
			r->look.floor = t[2];
			if (t.size() == 7 && t[3] == "colour" && detail::colourAt(t, 4, r->look.floorColour)) return std::string();
			return t.size() == 3 ? std::string() : usage;
		}
		if (t[2] == "paint") {
			if (t.size() != 6 || !detail::colourAt(t, 3, r->look.wall)) return usage;
			r->look.paper.clear();
			return std::string();
		}
		if (t[2] != "wallpaper" || t.size() < 4 || std::find(rooms::papers().begin(), rooms::papers().end(), t[3]) == rooms::papers().end()) return usage;
		r->look.paper = t[3];
		int colours = 0;
		for (std::size_t i = 4; i < t.size(); i += 4) {
			if (t[i] != "colour" || colours >= 2 || !detail::colourAt(t, i + 1, colours == 0 ? r->look.wall : r->look.paper2)) return usage;
			++colours;
		}
		return std::string();
	}
	if (verb == "cutaway" || verb == "peek") {   // (a shot's)
		if (s.shots.empty()) return "`" + verb + "` belongs to a shot: start one with `shot 0 NAME` first";
		Shot &sh = s.shots.back();
		if (verb == "cutaway") {
			if (t.size() != 1) return "`cutaway`: a wall between the camera and the room is not drawn";
			sh.cutaway = true;
			return std::string();
		}
		const char *usage = "`peek ROOM [window N] [from D]`: the camera outside a window of ROOM, looking in";
		if (t.size() < 2) return usage;
		sh.peek = t[1];
		for (std::size_t i = 2; i < t.size(); i += 2) {
			double v = 0.0;
			if (i + 1 >= t.size() || !number(t[i + 1], v)) return usage;
			if (t[i] == "window" && v >= 1.0 && v <= 12.0) sh.peekWindow = static_cast<int>(v) - 1;
			else if (t[i] == "from" && v >= 0.3 && v <= 30.0) sh.peekFrom = v;
			else return usage;
		}
		return std::string();
	}
	// what a character does with a room's furniture
	const char *usage = verb == "sit" ? "`sit WHO on ITEM [in ROOM] at T [until T]`" : verb == "lie" ? "`lie WHO in|on ITEM [in ROOM] at T [until T]`" : verb == "open" ? "`open WHO ITEM [in ROOM] at T [until T]`"
						: verb == "lean" ? "`lean WHO on ITEM [in ROOM] at T [until T]`" : "`lookout WHO [in ROOM] [window N] at T [until T]`";
	rooms::Use u;
	u.verb = verb, u.line = n;
	if (t.size() < 2) return usage;
	u.who = t[1];
	if (std::none_of(s.characters.begin(), s.characters.end(), [&u](const Character &c) { return c.name == u.who; })) return "`" + verb + "`: no character called \"" + u.who + "\" (before this line)";
	std::size_t i = 2;
	if (verb == "sit" || verb == "lean" || verb == "lie") {
		if (i + 1 >= t.size() || (t[i] != "on" && !(verb == "lie" && t[i] == "in"))) return usage;   // (lie in a bed, or on a sofa)
		u.item = t[i + 1], i += 2;
	} else if (verb == "open") {
		if (i >= t.size()) return usage;
		u.item = t[i++];
	} else {
		u.item = "window";
	}
	bool timed = false;
	for (; i < t.size(); i += 2) {
		double v = 0.0;
		if (i + 1 >= t.size()) return usage;
		if (t[i] == "in") u.room = t[i + 1];
		else if (t[i] == "at" && number(t[i + 1], v) && v >= 0.0) u.t0 = v, timed = true;
		else if (t[i] == "until" && number(t[i + 1], v)) u.t1 = v;
		else if (t[i] == "window" && verb == "lookout" && number(t[i + 1], v) && v >= 1.0) u.itemIndex = static_cast<int>(v) - 1;
		else return usage;
	}
	if (!timed || u.t1 <= u.t0 + 0.5) return usage + std::string(" (`until` at least half a second after `at`)");
	in.uses.push_back(u);
	return std::string();
}

// The rooms set out (their doorways and windows, furniture, lamps) and what refers to them found: a door's doorway, a lamp's room, the furniture a character uses and where it
// stands to and sits, lies or leans at. "" or what is wrong.
inline std::string settleRooms(Spec &s) {
	rooms::Indoors &in = s.indoors;
	for (rooms::Room &r : in.rooms) {
		rooms::openUp(r);
		rooms::furnish(r);
		rooms::lightUp(r);
	}
	const auto indexOf = [&in](const std::string &name) {
		for (std::size_t i = 0; i < in.rooms.size(); ++i)
			if (in.rooms[i].name == name) return static_cast<int>(i);
		return -1;
	};
	for (rooms::Door &d : in.doors) {
		d.roomIndex = indexOf(d.room);
		if (d.roomIndex < 0) return "`door`: no room called \"" + d.room + "\"";
		const rooms::Room &r = in.rooms[static_cast<std::size_t>(d.roomIndex)];
		for (std::size_t k = 0; k < r.openings.size(); ++k)
			if (r.openings[k].door && r.openings[k].to == d.to) d.opening = static_cast<int>(k);
		if (d.opening < 0) return d.to.empty() ? "`door " + d.room + "`: it has no front door" : "`door " + d.room + " to " + d.to + "`: there is no doorway between them";
	}
	for (const rooms::Switch &sw : in.switches)
		if (indexOf(sw.room) < 0) return "`switch`: no room called \"" + sw.room + "\"";
	for (std::size_t k = 0; k < in.uses.size(); ++k) {
		rooms::Use &u = in.uses[k];
		const std::string where = "`" + u.verb + " " + u.who + "` (line " + std::to_string(u.line) + ")";
		if (!u.room.empty()) {
			u.roomIndex = indexOf(u.room);
			if (u.roomIndex < 0) return where + ": no room called \"" + u.room + "\"";
		}
		if (u.verb == "lookout") {   // a window of the room (the first, or `window N`): the room it says, or the one it starts in
			for (const Character &c : s.characters)
				if (u.roomIndex < 0 && c.name == u.who) u.roomIndex = rooms::roomAt(in, {c.start[0], c.start[1], c.start[2]});
			if (u.roomIndex < 0) return where + ": which room's window - `in ROOM`";
			const rooms::Room &r = in.rooms[static_cast<std::size_t>(u.roomIndex)];
			std::vector<const rooms::Opening *> windows;
			for (const rooms::Opening &o : r.openings)
				if (!o.door) windows.push_back(&o);
			const int w = std::max(0, u.itemIndex);
			if (w >= static_cast<int>(windows.size())) return where + ": " + r.name + " has " + std::to_string(windows.size()) + " windows";
			const rooms::Opening &o = *windows[static_cast<std::size_t>(w)];
			const Vec3 in_ = rooms::wallInward(o.wall), at = rooms::wallPoint(r, o.wall, o.along);
			u.stand = detail::clearSpot(r, rooms::toPbrt(r, at + in_ * 0.6)), u.seat = rooms::toPbrt(r, at + in_ * 0.42);
			u.faceTo = rooms::facingOf(r, in_ * -1.0);
			continue;
		}
		// the piece: in the room it names, or the first room that has one; of several, one no one else is using then
		int found = -1, foundRoom = -1;
		for (std::size_t ri = 0; ri < in.rooms.size() && found < 0; ++ri) {
			if (u.roomIndex >= 0 && static_cast<int>(ri) != u.roomIndex) continue;
			const rooms::Room &r = in.rooms[ri];
			for (std::size_t ii = 0; ii < r.items.size() && found < 0; ++ii) {
				if (r.items[ii].kind != u.item) continue;
				bool taken = false;
				for (std::size_t j = 0; j < k; ++j) {
					const rooms::Use &o = in.uses[j];
					if (o.who != u.who && o.roomIndex == static_cast<int>(ri) && o.itemIndex == static_cast<int>(ii) && o.t0 < u.t1 && u.t0 < o.t1 && o.verb != "open") taken = true;
				}
				if (!taken) found = static_cast<int>(ii), foundRoom = static_cast<int>(ri);
			}
		}
		if (found < 0) return where + ": no " + u.item + (u.room.empty() ? std::string(" in any room") : " in " + u.room) + " (free then)";
		u.roomIndex = foundRoom, u.itemIndex = found;
		const rooms::Room &r = in.rooms[static_cast<std::size_t>(foundRoom)];
		const rooms::Item &it = r.items[static_cast<std::size_t>(found)];
		const Vec3 c{it.x, 0.0, it.z}, front = rooms::frontOf(it);
		const Vec3 side = [&] {   // (of a bed's two sides, or a sofa's two ends, the one with room to stand at)
			const Vec3 s{-front.z, 0.0, front.x}, a = rooms::toPbrt(r, c + s * (0.5 * it.w + 0.42)), b = rooms::toPbrt(r, c - s * (0.5 * it.w + 0.42));
			return length(detail::clearSpot(r, a) - a) <= length(detail::clearSpot(r, b) - b) ? s : s * -1.0;
		}();
		if (u.verb == "sit") {
			if (it.seat < 0.0) return where + ": a " + it.kind + " is not to sit on (a sofa, an armchair, a chair, a stool, a bed, a toilet)";
			u.stand = rooms::toPbrt(r, c + front * (0.5 * it.d + 0.42));
			u.seat = rooms::toPbrt(r, c + front * (it.kind == "bed" ? 0.5 * it.d - 0.3 : 0.12 * it.d));
			if (it.kind == "bed") u.stand = rooms::toPbrt(r, c + side * (0.5 * it.w + 0.42)), u.seat = rooms::toPbrt(r, c + side * (0.5 * it.w - 0.25));
			u.faceTo = rooms::facingOf(r, it.kind == "bed" ? side : front);
			u.seatHeight = it.seat;
		} else if (u.verb == "lie") {
			if (it.kind != "bed" && it.kind != "sofa") return where + ": one lies in a bed, or on a sofa";
			u.stand = rooms::toPbrt(r, c + side * (0.5 * it.w + 0.42));
			// (its feet at the foot, lying back towards the head: the bed's own `front` is its foot)
			u.seat = it.kind == "bed" ? rooms::toPbrt(r, c + front * (0.5 * it.d - 0.3)) : rooms::toPbrt(r, c + side * (0.5 * it.w - 0.3) + front * 0.1);
			u.faceTo = rooms::facingOf(r, it.kind == "bed" ? front : side);   // (it lies back from there: across the sofa, from its one end)
			u.seatHeight = it.kind == "bed" ? 0.56 : 0.48;
		} else {   // open, lean: in front of it, facing it
			const rooms::Leaf l = rooms::leafOf(it);
			if (u.verb == "open" && !l.has) return where + ": a " + it.kind + " has no door to open (a fridge, a wardrobe, a cabinet, a stove)";
			u.stand = rooms::toPbrt(r, c + front * (0.5 * it.d + (u.verb == "open" ? 0.7 : 0.42)));
			u.seat = u.stand;
			u.faceTo = rooms::facingOf(r, front * -1.0);
		}
		u.stand = detail::clearSpot(r, u.stand);   // (not in another piece: beside a chair pulled up to a table)
		if (u.verb == "open" || u.verb == "lean") u.seat = u.stand;
	}
	return std::string();
}

}  // namespace film_file
}  // namespace cartoon
