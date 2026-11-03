#pragma once
// film_parse_home.h -- the lines of a film file for what is done at home (indoor round 2; its grammar: film_parse.h). film_parse_rooms.h hands them here, and settles them
// with the rooms.
//
//   go WHO to ROOM [ITEM] at T [speed S]                (film_parse_rooms.h: a `use` - up or down the stairs to a room on another floor, film_ways.cpp)
//   thing NAME KIND on ITEM [in ROOM]                   carry WHO THING to ITEM [in ROOM] at T [speed S]                      (film_homelife.h)
//   cook ROOM pot|kettle|toast|oven|meal at T [until T]    bath ROOM at T [until T]    shower ROOM at T [until T]
//   party ROOM birthday|christmas|halloween|plain [at T] [until T]    confetti ROOM at T    blowout WHO [in ROOM] at T
//   guests ROOM N [kind person|pip] [name NAME]         dance WHO|ROOM at T [until T]
//   torch WHO at T [until T]    flicker ROOM [LAMP] at T [until T]    shadowplay WHO [animal rabbit|bird|wolf] at T [until T]      (film_homefun.h)
//   fall THING at T    knock PET THING at T    puddle ROOM X Z at T [size R]    pile ROOM books|boxes|plates [on ITEM] falls T
//   nap PET [on ITEM] [in ROOM] at T [until T]    wait PET [in ROOM] at T [until T]    heel PET WHO at T [until T]                (film_pets.h)
//   (in a shot)  trail WHO [behind D] [up H]                                                                                       (film_homeworld.cpp)

#include "film_homelife.h"
#include "film_homefun.h"
#include "film_rooms.h"
#include "film_spec.h"

namespace cartoon {
namespace film_file {

inline bool homeVerb(const std::string &verb) {
	static const std::vector<std::string> k = {"thing", "carry", "cook", "bath", "shower", "party", "confetti", "blowout", "guests", "dance", "torch", "flicker",
											   "shadowplay", "fall", "knock", "puddle", "pile", "nap", "wait", "heel", "trail"};
	return std::find(k.begin(), k.end(), verb) != k.end();
}

namespace home {
// `at T [until T]` and the words a line allows besides (each with one value: `extra` gets them), from token i on: false when something else is there.
inline bool timesFrom(const std::vector<std::string> &t, std::size_t i, double &t0, double &t1, bool &timed, std::vector<std::pair<std::string, std::string>> *extra = nullptr,
					  const std::vector<std::string> &allowed = {}) {
	for (; i < t.size(); i += 2) {
		double v = 0.0;
		if (i + 1 >= t.size()) return false;
		if (t[i] == "at" && detail::number(t[i + 1], v) && v >= 0.0) t0 = v, timed = true;
		else if (t[i] == "until" && detail::number(t[i + 1], v)) t1 = v;
		else if (extra && std::find(allowed.begin(), allowed.end(), t[i]) != allowed.end()) extra->push_back({t[i], t[i + 1]});
		else return false;
	}
	return !timed || t1 > t0;
}
inline bool hasCharacter(const Spec &s, const std::string &name) {
	return std::any_of(s.characters.begin(), s.characters.end(), [&name](const Character &c) { return c.name == name; });
}
inline bool hasRoom(const Spec &s, const std::string &name) {
	return std::any_of(s.indoors.rooms.begin(), s.indoors.rooms.end(), [&name](const rooms::Room &r) { return r.name == name; });
}
}  // namespace home

// A line for what is done at home into `s`: "" or what is wrong with it (what it refers to in rooms is found when the rooms are set out: settleHome).
inline std::string homeLine(Spec &s, const std::string &verb, const std::string &line, int n) {
	using detail::number;
	const std::vector<std::string> t = detail::tokens(line);
	rooms::Home &h = s.indoors.home;
	double t0 = 0.0, t1 = 1e30;
	bool timed = false;
	const auto needRoom = [&](std::size_t i) -> std::string {
		if (i >= t.size()) return "which room";
		return home::hasRoom(s, t[i]) ? std::string() : "`" + verb + "`: no room called \"" + t[i] + "\" (before this line)";
	};
	const auto needCharacter = [&](std::size_t i) -> std::string {
		if (i >= t.size()) return "who";
		return home::hasCharacter(s, t[i]) ? std::string() : "`" + verb + "`: no character called \"" + t[i] + "\" (before this line)";
	};
	if (verb == "thing") {
		const char *usage = "`thing NAME KIND on ITEM [in ROOM]` (KIND: mug, plate, bowl, book, vase, ball, box, bottle, block, cushion, shoe)";
		rooms::Thing th;
		if ((t.size() != 5 && t.size() != 7) || t[3] != "on" || std::find(rooms::thingKinds().begin(), rooms::thingKinds().end(), t[2]) == rooms::thingKinds().end()) return usage;
		if (t.size() == 7 && t[5] != "in") return usage;
		th.name = t[1], th.kind = t[2], th.item = t[4], th.room = t.size() == 7 ? t[6] : std::string(), th.line = n;
		for (const rooms::Thing &o : h.things)
			if (o.name == th.name) return "there is a thing called \"" + th.name + "\" already";
		for (const Spec::Prop &p : s.props)
			if (p.name == th.name) return "there is a prop called \"" + th.name + "\" already";
		h.things.push_back(th);
		return std::string();
	}
	if (verb == "carry") {
		const char *usage = "`carry WHO THING to ITEM [in ROOM] at T [speed S]`: it walks to the thing, picks it up, takes it to ITEM and puts it down there";
		rooms::Carry c;
		if (t.size() < 7 || t[3] != "to") return usage;
		if (const std::string e = needCharacter(1); !e.empty()) return e;
		c.who = t[1], c.thing = t[2], c.item = t[4], c.line = n;
		std::vector<std::pair<std::string, std::string>> extra;
		if (!home::timesFrom(t, 5, c.t0, t1, timed, &extra, {"in", "speed"}) || !timed) return usage;
		for (const auto &[k, v] : extra) {
			double x = 0.0;
			if (k == "in") c.room = v;
			else if (!number(v, x) || x < 0.2 || x > 4.0) return usage;
			else c.speed = x;
		}
		h.carries.push_back(c);
		return std::string();
	}
	if (verb == "cook" || verb == "bath" || verb == "shower" || verb == "party" || verb == "confetti" || verb == "flicker") {
		rooms::Doing d;
		d.verb = verb, d.line = n;
		const std::string usage = verb == "cook" ? "`cook ROOM pot|kettle|toast|oven|meal at T [until T]`" : verb == "party" ? "`party ROOM birthday|christmas|halloween|plain [at T] [until T]`"
								: verb == "flicker" ? "`flicker ROOM [LAMP] at T [until T]` (LAMP: ceiling, table, floor, tv, fairy, or a lamp's name)" : "`" + verb + " ROOM at T" + (verb == "confetti" ? "`" : " [until T]`");
		if (t.size() < 2) return usage;
		if (const std::string e = needRoom(1); !e.empty()) return e;
		d.room = t[1];
		std::size_t i = 2;
		if (verb == "cook") {
			static const std::vector<std::string> what = {"pot", "kettle", "toast", "oven", "meal"};
			if (i >= t.size() || std::find(what.begin(), what.end(), t[i]) == what.end()) return usage;
			d.what = t[i++];
		} else if (verb == "party") {
			static const std::vector<std::string> what = {"birthday", "christmas", "halloween", "plain"};
			if (i >= t.size() || std::find(what.begin(), what.end(), t[i]) == what.end()) return usage;
			d.what = t[i++];
		} else if (verb == "flicker" && i < t.size() && t[i] != "at") {
			d.what = t[i++];
		}
		if (!home::timesFrom(t, i, d.t0, d.t1, timed)) return usage;
		if (!timed && verb != "party") return usage;
		if (verb == "confetti") d.t1 = d.t0 + 1e30;
		h.doings.push_back(d);
		return std::string();
	}
	if (verb == "torch" || verb == "shadowplay" || verb == "dance" || verb == "heel") {
		rooms::Act a;
		a.verb = verb, a.line = n;
		const std::string usage = verb == "torch" ? "`torch WHO at T [until T]`" : verb == "shadowplay" ? "`shadowplay WHO [animal rabbit|bird|wolf] at T [until T]`"
								: verb == "dance" ? "`dance WHO|ROOM at T [until T]` (a room: everyone in it then)" : "`heel PET WHO at T [until T]`: the pet follows WHO about";
		if (t.size() < 2) return usage;
		a.who = t[1];
		if (verb == "dance" ? !home::hasCharacter(s, a.who) && !home::hasRoom(s, a.who) : !home::hasCharacter(s, a.who)) return "`" + verb + "`: no character" + std::string(verb == "dance" ? " or room" : "") + " called \"" + a.who + "\" (before this line)";
		std::size_t i = 2;
		if (verb == "heel") {
			if (const std::string e = needCharacter(2); !e.empty()) return e;
			a.what = t[i++];
			if (a.what == a.who) return "a pet cannot follow itself";
		}
		std::vector<std::pair<std::string, std::string>> extra;
		if (!home::timesFrom(t, i, a.t0, a.t1, timed, &extra, verb == "shadowplay" ? std::vector<std::string>{"animal"} : std::vector<std::string>{}) || !timed) return usage;
		for (const auto &[k, v] : extra) {
			if (std::find(rooms::shadowAnimals().begin(), rooms::shadowAnimals().end(), v) == rooms::shadowAnimals().end()) return usage;
			a.what = v;
		}
		h.acts.push_back(a);
		return std::string();
	}
	if (verb == "guests") {
		const char *usage = "`guests ROOM N [kind person|pip] [name NAME]` (1 to 12 of them, NAME-1, NAME-2 ...: `guest` by default)";
		double count = 0.0;
		if (t.size() < 3 || !number(t[2], count) || count < 1 || count > 12) return usage;
		if (const std::string e = needRoom(1); !e.empty()) return e;
		std::string kind = "person", name = "guest";
		for (std::size_t i = 3; i < t.size(); i += 2) {
			if (i + 1 >= t.size()) return usage;
			if (t[i] == "kind" && (t[i + 1] == "person" || t[i + 1] == "pip")) kind = t[i + 1];
			else if (t[i] == "name") name = t[i + 1];
			else return usage;
		}
		const std::vector<Character> folk = crowdOf(name, kind, static_cast<int>(count), 0.0, 0.0, 1.0, 1.0, n * 7 + 3, 0.0, 0.0, n);   // (their looks; no wandering - settleHome sets them about the room)
		for (const Character &c : folk) {
			if (home::hasCharacter(s, c.name)) return "there is already a character called " + c.name;
			s.characters.push_back(c);
			h.guests.push_back({c.name, t[1]});
		}
		return std::string();
	}
	if (verb == "fall" || verb == "knock" || verb == "puddle" || verb == "pile") {
		rooms::Mishap m;
		m.verb = verb, m.line = n;
		if (verb == "fall") {
			if (t.size() != 4 || t[2] != "at" || !number(t[3], m.t) || m.t < 0.0) return "`fall THING at T`: it tumbles off whatever it is on";
			m.thing = t[1];
		} else if (verb == "knock") {
			if (t.size() != 5 || t[3] != "at" || !number(t[4], m.t) || m.t < 0.0) return "`knock PET THING at T`: the pet goes to it and bats it off onto the floor";
			if (const std::string e = needCharacter(1); !e.empty()) return e;
			m.who = t[1], m.thing = t[2];
			rooms::Use u;   // (its walk there: a use, settled with the rooms)
			u.verb = "knock", u.who = m.who, u.item = m.thing, u.t0 = m.t, u.t1 = m.t + 4.0, u.line = n;
			s.indoors.uses.push_back(u);
		} else if (verb == "puddle") {
			const char *usage = "`puddle ROOM X Z at T [size R]`: a spill on its floor there (R 0.1 to 1.5 m)";
			if (t.size() != 6 && t.size() != 8) return usage;
			if (const std::string e = needRoom(1); !e.empty()) return e;
			if (!number(t[2], m.at.x) || !number(t[3], m.at.z) || t[4] != "at" || !number(t[5], m.t)) return usage;
			if (t.size() == 8 && (t[6] != "size" || !number(t[7], m.size) || m.size < 0.1 || m.size > 1.5)) return usage;
			m.room = t[1];
		} else {
			const char *usage = "`pile ROOM books|boxes|plates [on ITEM] falls T`";
			if (t.size() != 5 && t.size() != 7) return usage;
			if (const std::string e = needRoom(1); !e.empty()) return e;
			if (t[2] != "books" && t[2] != "boxes" && t[2] != "plates") return usage;
			m.room = t[1], m.of = t[2];
			std::size_t i = 3;
			if (t.size() == 7) {
				if (t[3] != "on") return usage;
				m.item = t[4], i = 5;
			}
			if (t[i] != "falls" || !number(t[i + 1], m.t)) return usage;
		}
		h.mishaps.push_back(m);
		return std::string();
	}
	if (verb == "nap" || verb == "wait" || verb == "blowout") {   // (walks there and stays: a use, settled with the rooms)
		const std::string usage = verb == "nap" ? "`nap PET [on ITEM] [in ROOM] at T [until T]` (no ITEM: a sunny spot on the floor by a window)" : verb == "wait" ? "`wait PET [in ROOM] at T [until T]`: by the front door"
								: "`blowout WHO [in ROOM] at T`: it goes to the birthday cake and blows its candles out";
		rooms::Use u;
		u.verb = verb, u.line = n;
		if (t.size() < 2) return usage;
		if (const std::string e = needCharacter(1); !e.empty()) return e;
		u.who = t[1];
		std::size_t i = 2;
		if (verb == "nap" && i + 1 < t.size() && t[i] == "on") u.item = t[i + 1], i += 2;
		std::vector<std::pair<std::string, std::string>> extra;
		if (!home::timesFrom(t, i, u.t0, u.t1, timed, &extra, {"in"}) || !timed) return usage;
		for (const auto &[k, v] : extra) u.room = v;
		if (verb == "blowout") u.t1 = u.t0 + 60.0;
		s.indoors.uses.push_back(u);
		return std::string();
	}
	if (verb == "trail") {   // (a shot's)
		const char *usage = "`trail WHO [behind D] [up H]`: the camera along WHO's way behind it (D 0.5 to 6 m back; H over its floor)";
		if (s.shots.empty()) return "`trail` belongs to a shot: start one with `shot 0 NAME` first";
		if (t.size() < 2 || t.size() % 2) return usage;
		if (const std::string e = needCharacter(1); !e.empty()) return e;
		Shot &sh = s.shots.back();
		sh.trail = t[1];
		for (std::size_t i = 2; i < t.size(); i += 2) {
			double v = 0.0;
			if (!number(t[i + 1], v)) return usage;
			if (t[i] == "behind" && v >= 0.5 && v <= 6.0) sh.trailBehind = v;
			else if (t[i] == "up" && v >= 0.2 && v <= 4.0) sh.trailUp = v;
			else return usage;
		}
		return std::string();
	}
	return "`" + verb + "`?";
}

// What the home's lines refer to, found and worked out now the rooms are set out (film_parse_rooms.h settleRooms): where each thing is (a prop of the film), where a carry stands
// to pick it up and to put it down, where a pet naps or waits, where a cake is blown out, where the guests stand, the piles' and puddles' places. "" or what is wrong.
inline std::string settleHome(Spec &s) {
	rooms::Indoors &in = s.indoors;
	rooms::Home &h = in.home;
	const auto indexOf = [&in](const std::string &name) {
		for (std::size_t i = 0; i < in.rooms.size(); ++i)
			if (in.rooms[i].name == name) return static_cast<int>(i);
		return -1;
	};
	const auto pbrt = [](Vec3 story) { return Vec3{-story.x, story.y, story.z}; };
	// a piece of a kind in the room it names (or the first room with one): false when none
	const auto findPiece = [&](const std::string &kind, const std::string &room, int &ri, int &ii) {
		for (std::size_t r = 0; r < in.rooms.size(); ++r) {
			if (!room.empty() && in.rooms[r].name != room) continue;
			for (std::size_t i = 0; i < in.rooms[r].items.size(); ++i)
				if (in.rooms[r].items[i].kind == kind) {
					ri = static_cast<int>(r), ii = static_cast<int>(i);
					return true;
				}
		}
		return false;
	};
	const auto before = [&](const rooms::Room &r, const rooms::Item &it, double out) { return rooms::clearSpot(r, rooms::toPbrt(r, Vec3{it.x, 0.0, it.z} + rooms::frontOf(it) * (0.5 * it.d + out))); };
	const auto where = [](const std::string &verb, int line) { return "`" + verb + "` (line " + std::to_string(line) + ")"; };
	// the things, on their pieces' tops (side by side when there are several on one)
	std::map<std::pair<int, int>, int> onPiece, placed;
	std::vector<std::pair<int, int>> pieceOf(h.things.size());
	for (std::size_t k = 0; k < h.things.size(); ++k) {
		const rooms::Thing &th = h.things[k];
		int ri = -1, ii = -1;
		if (!findPiece(th.item, th.room, ri, ii)) return where("thing " + th.name, th.line) + ": no " + th.item + (th.room.empty() ? std::string(" in any room") : " in " + th.room);
		pieceOf[k] = {ri, ii};
		++onPiece[pieceOf[k]];
	}
	for (std::size_t k = 0; k < h.things.size(); ++k) {
		rooms::Thing &th = h.things[k];
		const rooms::Room &r = in.rooms[static_cast<std::size_t>(pieceOf[k].first)];
		const rooms::Item &it = r.items[static_cast<std::size_t>(pieceOf[k].second)];
		const int many = onPiece[pieceOf[k]], at = placed[pieceOf[k]]++;
		th.home = pbrt(rooms::onTopOf(r, it, many > 1 ? -0.5 + (at + 0.5) / many : 0.0));
		th.front = rooms::toPbrt(r, Vec3{it.x, 0.0, it.z} + rooms::frontOf(it)) - rooms::toPbrt(r, {it.x, 0.0, it.z});
		Spec::Prop p;
		p.name = th.name, p.home = th.home, p.line = th.line, p.thing = th.kind;
		s.props.push_back(p);
	}
	for (rooms::Carry &c : h.carries) {
		const auto th = std::find_if(h.things.begin(), h.things.end(), [&c](const rooms::Thing &x) { return x.name == c.thing; });
		if (th == h.things.end()) return where("carry " + c.who, c.line) + ": no thing called \"" + c.thing + "\" (`thing NAME KIND on ITEM`)";
		const std::pair<int, int> from = pieceOf[static_cast<std::size_t>(th - h.things.begin())];
		int ri = -1, ii = -1;
		if (!findPiece(c.item, c.room, ri, ii)) return where("carry " + c.who, c.line) + ": no " + c.item + (c.room.empty() ? std::string(" in any room") : " in " + c.room);
		const rooms::Room &fr = in.rooms[static_cast<std::size_t>(from.first)], &tr = in.rooms[static_cast<std::size_t>(ri)];
		c.pickStand = before(fr, fr.items[static_cast<std::size_t>(from.second)], 0.4);
		c.putStand = before(tr, tr.items[static_cast<std::size_t>(ii)], 0.4);
		c.putAt = pbrt(rooms::onTopOf(tr, tr.items[static_cast<std::size_t>(ii)], 0.25));
	}
	// the uses of round 2: a nap, a wait by the door, a knock, a blowout
	for (rooms::Use &u : in.uses) {
		if (u.verb != "nap" && u.verb != "wait" && u.verb != "knock" && u.verb != "blowout") continue;
		const std::string w = where(u.verb + " " + u.who, u.line);
		const Character *c = nullptr;
		for (const Character &each : s.characters)
			if (each.name == u.who) c = &each;
		int ri = u.room.empty() ? -1 : indexOf(u.room), ii = -1;
		if (!u.room.empty() && ri < 0) return w + ": no room called \"" + u.room + "\"";
		if (u.verb == "knock") {
			const auto th = std::find_if(h.things.begin(), h.things.end(), [&u](const rooms::Thing &x) { return x.name == u.item; });
			if (th == h.things.end()) return w + ": no thing called \"" + u.item + "\"";
			std::tie(ri, ii) = pieceOf[static_cast<std::size_t>(th - h.things.begin())];
			const rooms::Room &r = in.rooms[static_cast<std::size_t>(ri)];
			const rooms::Item &it = r.items[static_cast<std::size_t>(ii)];
			u.stand = u.seat = before(r, it, 0.3);
			u.faceTo = rooms::facingOf(r, rooms::frontOf(it) * -1.0);
		} else if (u.verb == "blowout") {
			if (ri < 0)
				for (const rooms::Doing &d : h.doings)
					if (d.verb == "party" && d.what == "birthday" && ri < 0) ri = indexOf(d.room);
			if (ri < 0) return w + ": no birthday party (`party ROOM birthday`) to blow out the candles of";
			const rooms::Room &r = in.rooms[static_cast<std::size_t>(ri)];
			for (const char *kind : {"table", "coffeetable", "desk"})
				if (ii < 0 && rooms::findItem(r, kind)) ii = static_cast<int>(rooms::findItem(r, kind) - r.items.data());
			if (ii < 0) return w + ": " + r.name + " has no table for a cake";
			const rooms::Item &it = r.items[static_cast<std::size_t>(ii)];
			u.stand = u.seat = before(r, it, 0.35);
			u.faceTo = rooms::facingOf(r, rooms::frontOf(it) * -1.0);
		} else {
			if (ri < 0 && c) ri = rooms::roomAt(in, {c->start[0], c->start[1], c->start[2]});
			if (ri < 0) return w + ": which room - `in ROOM`";
			const rooms::Room &r = in.rooms[static_cast<std::size_t>(ri)];
			if (u.verb == "wait") {   // inside its front door (else its first doorway), facing it
				const rooms::Opening *door = nullptr;
				for (const rooms::Opening &o : r.openings)
					if (o.door && (!door || (o.to.empty() && !door->to.empty()))) door = &o;
				if (!door) return w + ": " + r.name + " has no door";
				const Vec3 in_ = rooms::wallInward(door->wall);
				u.stand = u.seat = rooms::clearSpot(r, rooms::toPbrt(r, rooms::wallPoint(r, door->wall, door->along) + in_ * 0.8));
				u.faceTo = rooms::facingOf(r, in_ * -1.0);
			} else if (!u.item.empty()) {   // a nap on a piece: up on its seat (a sofa's, a bed's, an armchair's) or its top
				for (std::size_t i = 0; i < r.items.size() && ii < 0; ++i)
					if (r.items[i].kind == u.item) ii = static_cast<int>(i);
				if (ii < 0) return w + ": no " + u.item + " in " + r.name;
				const rooms::Item &it = r.items[static_cast<std::size_t>(ii)];
				u.stand = before(r, it, 0.35);
				u.seat = rooms::toPbrt(r, Vec3{it.x, 0.0, it.z} + rooms::frontOf(it) * (it.seat >= 0.0 ? 0.08 * it.d : 0.0));
				u.seatHeight = rooms::topOf(it) - (it.seat >= 0.0 ? 0.02 : 0.0);
				u.faceTo = rooms::facingOf(r, rooms::frontOf(it));
			} else {   // a nap in the sun: on the floor a little in from its first window
				const rooms::Opening *win = nullptr;
				for (const rooms::Opening &o : r.openings)
					if (!o.door && !win) win = &o;
				if (!win) return w + ": " + r.name + " has no window to nap in the sun of (`on ITEM`)";
				u.stand = u.seat = rooms::clearSpot(r, rooms::toPbrt(r, rooms::wallPoint(r, win->wall, win->along) + rooms::wallInward(win->wall) * 0.9));
				u.faceTo = rooms::facingOf(r, rooms::wallInward(win->wall));
			}
		}
		u.roomIndex = ri, u.itemIndex = ii;
	}
	for (rooms::Doing &d : h.doings) {
		d.roomIndex = indexOf(d.room);
		rooms::Room &r = in.rooms[static_cast<std::size_t>(d.roomIndex)];
		const std::string w = where(d.verb + " " + d.room, d.line);
		if (d.verb == "cook" && (d.what == "pot" || d.what == "kettle" || d.what == "oven") && !rooms::findItem(r, "stove")) return w + ": " + r.name + " has no stove (a kitchen has)";
		if (d.verb == "cook" && d.what == "toast" && !rooms::findItem(r, "counter")) return w + ": " + r.name + " has no counter to stand a toaster on";
		if (d.verb == "cook" && d.what == "meal" && !rooms::findItem(r, "table") && !rooms::findItem(r, "coffeetable")) return w + ": " + r.name + " has no table to serve it on";
		if ((d.verb == "bath" || d.verb == "shower") && !rooms::findItem(r, "bath")) return w + ": " + r.name + " has no bath (a bathroom has)";
		if (d.verb == "bath")
			for (rooms::Item &it : r.items)
				if (it.kind == "bath") it.water = false;   // (its water comes and goes: film_homelife.h)
		if (d.verb == "confetti" && std::none_of(h.doings.begin(), h.doings.end(), [&d](const rooms::Doing &p) { return p.verb == "party" && p.room == d.room; }))
			return w + ": confetti is for a party (`party " + d.room + " ...`)";
	}
	// the guests, set about their room: in clear places a step apart, each turned to the room's middle
	std::vector<Vec3> taken;   // (where the others stand, and where anyone goes to use something)
	for (const Character &c : s.characters) taken.push_back({c.start[0], c.start[1], c.start[2]});
	for (const rooms::Use &u : in.uses) taken.push_back(u.stand);
	for (const rooms::Carry &c : h.carries) taken.push_back(c.pickStand), taken.push_back(c.putStand);
	for (const auto &[name, room] : h.guests) {
		const rooms::Room &r = in.rooms[static_cast<std::size_t>(indexOf(room))];
		Character *c = nullptr;
		for (Character &each : s.characters)
			if (each.name == name) c = &each;
		Vec3 best = rooms::toPbrt(r, {0.0, 0.0, 0.0});
		double bestGap = -1.0;
		const std::vector<rooms::detail::Rect> clear = rooms::detail::keepClear(r);
		for (double x = -0.5 * r.w + 0.6; x <= 0.5 * r.w - 0.6; x += 0.3)
			for (double z = -0.5 * r.d + 0.6; z <= 0.5 * r.d - 0.6; z += 0.3) {
				const Vec3 p = rooms::toPbrt(r, {x, 0.0, z});
				if (length(rooms::clearSpot(r, p) - p) > 1e-6) continue;   // (in the furniture's way)
				if (std::any_of(clear.begin(), clear.end(), [&](const rooms::detail::Rect &k) { return x > k.x0 - 0.3 && x < k.x1 + 0.3 && z > k.z0 - 0.3 && z < k.z1 + 0.3; })) continue;   // (in a doorway, on the stairs)
				double gap = 1e30;
				for (const Vec3 &o : taken) gap = std::min(gap, std::hypot(o.x - p.x, o.z - p.z) + (std::abs(o.y - p.y) > 1.5 ? 1e3 : 0.0));
				gap = std::min(gap, 1.6) - 0.05 * std::hypot(x, z);   // (a step apart is enough; then nearer the middle)
				if (gap > bestGap) bestGap = gap, best = p;
			}
		c->start[0] = best.x, c->start[1] = best.y, c->start[2] = best.z;
		const Vec3 mid = rooms::toPbrt(r, {0.0, 0.0, 0.0});
		c->facing = std::atan2(mid.x - best.x, mid.z - best.z) * 180.0 / kPi;
		taken.push_back(best);
	}
	for (rooms::Mishap &m : h.mishaps) {
		const std::string w = where(m.verb, m.line);
		if (m.verb == "fall" || m.verb == "knock") {
			if (std::none_of(h.things.begin(), h.things.end(), [&m](const rooms::Thing &x) { return x.name == m.thing; })) return w + ": no thing called \"" + m.thing + "\" (`thing NAME KIND on ITEM`)";
			continue;
		}
		m.roomIndex = indexOf(m.room);
		const rooms::Room &r = in.rooms[static_cast<std::size_t>(m.roomIndex)];
		if (m.verb == "puddle") {
			m.at.y = r.y;
			if (rooms::roomAt(in, m.at) != m.roomIndex) return w + ": " + std::to_string(m.at.x) + " " + std::to_string(m.at.z) + " is not in " + r.name;
			continue;
		}
		// a pile: on its piece's top, else on the floor in a corner (or the middle) - its foot, and which way it topples (out into the room)
		if (!m.item.empty()) {
			const rooms::Item *it = rooms::findItem(r, m.item);
			if (!it) return w + ": no " + m.item + " in " + r.name;
			m.at = pbrt(rooms::onTopOf(r, *it, -0.25));
		} else {
			Vec3 corner;
			m.at = rooms::cornerSpot(r, 0.3, corner) ? rooms::toPbrt(r, corner) : rooms::clearSpot(r, rooms::toPbrt(r, {0.0, 0.0, 0.0}));
		}
		m.from = rooms::toPbrt(r, {0.0, 0.0, 0.0});   // (it topples towards the middle of the room)
	}
	return std::string();
}

}  // namespace film_file
}  // namespace cartoon
