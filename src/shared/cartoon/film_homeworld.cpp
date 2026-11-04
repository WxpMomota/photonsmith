// film_homeworld.cpp -- what is done at home in a film's frames and on its soundtrack (indoor round 2: film_homelife.h, film_homefun.h, film_pets.h; its lines film_parse_home.h):
// DataFilm's members for it. Compiled once, in cpu_renderer.
//
//   the walks it makes     a carry's two (to the thing, then to where it goes), a pet's after its owner (`heel`: each of the owner's walks, a moment later, to just behind
//                          where it goes) - before the ways are found; then the picks and puts timed from where the walks end, a knock's fall, the spills of what breaks,
//                          and who slips on a puddle or is knocked back by a door opened into them
//   what is drawn          the things where they are (in a hand, on a piece, falling, in pieces), cooking, the bath, a party, torches, the lightning in at the windows,
//                          shadow play, puddles, piles, wet footprints
//   the poses              a pet's nap, a torch held up, hands making a shadow animal, a dance, a slip onto its back, a door in the face
//   the camera             `trail WHO`: along its own way behind it (through the doorways, up the stairs); with `cutaway`, beside the house, moving with it
//   the sound              the hob, a kettle's whistle, the toaster, the oven, a meal's clinks, taps and the shower, a torch's click, a lamp's buzz, a cat's purr, a dog's bark,
//                          crashes and clatters, a slip's squeak and thud, a bonk, confetti's pop, candles blown out, the guests' chatter

#include "film_file.h"
#include "film_homefun.h"
#include "film_homelife.h"

namespace cartoon {
namespace film_file {

namespace {
// How far into a use t is (film_roomworld.cpp's useWeight: in over 0.6 s from when it gets there, out over 0.6 s from `until`).
double weightOf(const rooms::Use &u, double t) {
	if (u.arrive < 0.0 || t < u.arrive) return 0.0;
	return smoothstep(u.arrive, u.arrive + 0.6, t) * (1.0 - smoothstep(u.t1, u.t1 + 0.6, t));
}
double within(double t, double t0, double t1, double ease) { return smoothstep(t0, t0 + ease, t) * (1.0 - smoothstep(t1 - ease, t1, t)); }
Vec3 pbrtOf(Vec3 story) { return {-story.x, story.y, story.z}; }
// The walk of a character that goes to `stand` from `t0` (one the film made: its line and its target): its index, or -1.
int walkTo(const Character &c, int line, Vec3 stand) {
	for (std::size_t i = 0; i < c.walks.size(); ++i)
		if (c.walks[i].line == line && c.walks[i].targets.size() == 1 && c.walks[i].targets[0].first == stand.x && c.walks[i].targets[0].second == stand.z) return static_cast<int>(i);
	return -1;
}
}  // namespace

// ---- the walks ----
void DataFilm::placeHome() {
	rooms::Home &h = spec.indoors.home;
	const auto insert = [](Character &c, const WalkLine &w) { c.walks.insert(std::upper_bound(c.walks.begin(), c.walks.end(), w, [](const WalkLine &a, const WalkLine &b) { return a.t0 < b.t0; }), w); };
	for (const rooms::Carry &k : h.carries) {   // to the thing, then (timed once the first is found: timeHome) to where it goes
		Character *c = nullptr;
		for (Character &each : spec.characters)
			if (each.name == k.who) c = &each;
		if (!c) continue;
		for (int leg = 0; leg < 2; ++leg) {
			const Vec3 to = leg ? k.putStand : k.pickStand;
			WalkLine w;
			w.line = k.line, w.t0 = k.t0 + 0.001 * leg, w.speed = k.speed;
			w.targets.push_back({to.x, to.z});
			w.floors.push_back(to.y);
			insert(*c, w);
		}
	}
	for (const rooms::Act &a : h.acts) {   // a pet after its owner: each of the owner's walks, a little after it sets off, to just behind where it goes
		if (a.verb != "heel") continue;
		Character *pet = nullptr, *owner = nullptr;
		for (Character &each : spec.characters) {
			if (each.name == a.who) pet = &each;
			if (each.name == a.what) owner = &each;
		}
		if (!pet || !owner) continue;
		std::pair<double, double> before{owner->start[0], owner->start[2]};
		std::vector<WalkLine> mine;
		for (const WalkLine &w : owner->walks) {
			const bool byPoints = w.targets.empty();   // (a walk to places, or along points it was given: both followed)
			const std::vector<std::pair<double, double>> &dest = byPoints ? w.points : w.targets;
			if (dest.empty()) continue;
			const std::pair<double, double> last = dest.back();
			if (w.t0 >= a.t0 && w.t0 < a.t1) {
				WalkLine d = w;
				d.t0 = w.t0 + 0.9, d.follows = true, d.line = a.line;
				const double prevX = dest.size() > 1 ? dest[dest.size() - 2].first : before.first, prevZ = dest.size() > 1 ? dest[dest.size() - 2].second : before.second;
				const double dx = prevX - last.first, dz = prevZ - last.second, l = std::max(1e-6, std::hypot(dx, dz)), y = w.floors.empty() || std::isnan(w.floors.back()) ? owner->start[1] : w.floors.back();
				// (0.8 m from where the owner ends up: behind it, back the way it came, else round it - somewhere clear; the nearest clear place to a spot in the furniture
				// may be right where the owner stands)
				Vec3 spot{last.first + 0.8 * dx / l, y, last.second + 0.8 * dz / l};
				const double back = std::atan2(dz, dx);
				for (int k = 0; k < 8; ++k) {
					const double a = back + (k % 2 ? 1.0 : -1.0) * ((k + 1) / 2) * kPi / 4.0;
					const Vec3 p{last.first + 0.8 * std::cos(a), y, last.second + 0.8 * std::sin(a)};
					const int room = rooms::roomAt(spec.indoors, p);
					if (room >= 0 && length(rooms::clearSpot(spec.indoors.rooms[static_cast<std::size_t>(room)], p) - p) < 1e-6) {
						spot = p;
						break;
					}
				}
				(byPoints ? d.points : d.targets).back() = {spot.x, spot.z};
				mine.push_back(d);
			}
			before = last;
		}
		for (const WalkLine &d : mine) insert(*pet, d);
	}
}

void DataFilm::timeHome() {
	rooms::Indoors &in = spec.indoors;
	rooms::Home &h = in.home;
	// the carries: picked up as it gets to the thing, off again a moment later, put down where it gets to
	for (rooms::Carry &k : h.carries) {
		Character *c = nullptr;
		for (Character &each : spec.characters)
			if (each.name == k.who) c = &each;
		const int a = c ? walkTo(*c, k.line, k.pickStand) : -1, b = c ? walkTo(*c, k.line, k.putStand) : -1;
		if (a < 0 || b < 0) continue;
		const double arrive1 = c->walks[a].t0 + pathOf(*c, a).length() / std::max(1e-6, k.speed);
		k.picked = arrive1 + kReach + 0.05;
		c->walks[b].t0 = std::max(c->walks[b].t0, k.picked + kSettle + 0.1);
		k.put = c->walks[b].t0 + pathOf(*c, b).length() / std::max(1e-6, k.speed) + kReach + 0.05;
		Spec::Handling pick, put;
		pick.kind = Spec::Handling::Pick, pick.who = k.who, pick.prop = k.thing, pick.t = k.picked, pick.line = k.line;
		put.kind = Spec::Handling::Put, put.who = k.who, put.prop = k.thing, put.t = k.put, put.at = k.putAt, put.line = k.line;
		spec.handling.push_back(pick);
		spec.handling.push_back(put);
	}
	std::stable_sort(spec.handling.begin(), spec.handling.end(), [](const Spec::Handling &x, const Spec::Handling &y) { return x.t < y.t; });
	// the uses of round 2: a knock bats its thing off a moment after the pet gets there; a nap, a wait and a blowout hold until their ends; a pet waiting sits
	for (rooms::Use &u : in.uses) {
		Character *c = nullptr;
		for (Character &each : spec.characters)
			if (each.name == u.who) c = &each;
		if (!c || u.arrive < 0.0) continue;
		const CharacterKind &kind = characterKinds().at(c->kind);
		if (u.verb == "knock" || u.verb == "blowout") u.t1 = u.arrive + 2.5;
		if (u.verb == "knock") {
			for (rooms::Mishap &m : h.mishaps)
				if (m.verb == "knock" && m.who == u.who && m.thing == u.item && m.line == u.line) m.t = u.arrive + 0.9;
			if (std::find(kind.poses.begin(), kind.poses.end(), "paw") != kind.poses.end()) c->gestures.push_back({"paw", u.arrive + 0.2, u.arrive + 0.5, u.arrive + 1.1, u.arrive + 1.4, -1.0});
		}
		if (u.verb == "wait" && kind.curls) c->sits.push_back({u.arrive + 0.3, u.t1, 0.0});
	}
	// a dance in a room: everyone in it then
	std::vector<rooms::Act> acts;
	for (const rooms::Act &a : h.acts) {
		int room = -1;
		for (std::size_t i = 0; i < in.rooms.size(); ++i)
			if (a.verb == "dance" && in.rooms[i].name == a.who) room = static_cast<int>(i);
		if (room < 0) {
			acts.push_back(a);
			continue;
		}
		for (const Character &c : spec.characters) {
			Vec3 at;
			momentOf(c, a.t0, &at);
			if (rooms::roomAt(in, pbrtOf(at)) != room) continue;
			rooms::Act one = a;
			one.who = c.name;
			acts.push_back(one);
		}
	}
	h.acts = acts;
	for (const rooms::Act &a : h.acts) {
		Character *c = nullptr;
		for (Character &each : spec.characters)
			if (each.name == a.who) c = &each;
		if (!c) continue;
		const std::vector<std::string> &poses = characterKinds().at(c->kind).poses;
		const auto has = [&poses](const char *p) { return std::find(poses.begin(), poses.end(), p) != poses.end(); };
		if (a.verb == "dance") {   // its arms up and waving by turns, unless it is doing something with them of its own then
			const bool busy = std::any_of(c->gestures.begin(), c->gestures.end(), [&a](const rig::Gesture &g) { return g.in0 < std::min(a.t1, 1e4) && (g.out1 >= rig::kForever || g.out1 > a.t0); });
			for (double t = a.t0 + 0.3; !busy && t + 1.8 < std::min(a.t1, a.t0 + 600.0); t += 2.4) {
				const char *pose = static_cast<int>((t - a.t0) / 2.4) % 2 ? (has("wave") ? "wave" : nullptr) : (has("cheer") ? "cheer" : has("beg") ? "beg" : nullptr);
				if (pose) c->gestures.push_back({pose, t, t + 0.35, t + 1.6, t + 2.0, -1.0});
			}
		}
		if (a.verb == "shadowplay") {   // the room's lamps turned down while it plays
			Vec3 at;
			momentOf(*c, a.t0 + 0.05, &at);
			if (const int room = rooms::roomAt(in, pbrtOf(at)); room >= 0) {
				rooms::Doing d;
				d.verb = "dim", d.room = in.rooms[static_cast<std::size_t>(room)].name, d.t0 = a.t0, d.t1 = a.t1, d.roomIndex = room;
				h.doings.push_back(d);
			}
		}
	}
	// the falls: off its piece's front edge, down onto the floor; what holds water leaves a puddle where it breaks
	holdingTimeline();   // (who holds what when, for where each thing is as it falls; loadFile plays it through again, and says what is wrong with it)
	fallen.clear();
	for (const rooms::Mishap &m : h.mishaps) {
		if (m.verb != "fall" && m.verb != "knock") continue;
		const auto th = std::find_if(h.things.begin(), h.things.end(), [&m](const rooms::Thing &x) { return x.name == m.thing; });
		if (th == h.things.end() || !holdings.count(m.thing)) continue;
		Fallen f;
		f.name = th->name, f.kind = th->kind, f.t = m.t, f.breaks = rooms::fragile(th->kind);
		f.from = propAt(m.thing, m.t - 0.02);
		Vec3 out = th->front;   // (off its front edge, where it stands - and where a pet bats it from: away from the pet it would go over the piece)
		out.y = 0.0;
		out = normalize(out + Vec3{1e-6, 0.0, 0.0});
		const int room = rooms::roomAt(in, pbrtOf(f.from));
		const double floor = room >= 0 ? in.rooms[static_cast<std::size_t>(room)].y : 0.0;
		f.to = f.from + Vec3{-out.x, 0.0, out.z} * 0.55;   // (out where it can be walked over)
		f.to.y = floor;
		fallen.push_back(f);
		if (f.breaks && rooms::holdsWater(f.kind)) {
			rooms::Mishap spill;
			spill.verb = "spill", spill.at = pbrtOf(f.to), spill.t = f.t + std::sqrt(2.0 * std::max(0.05, f.from.y - floor) / 9.81), spill.size = 0.3, spill.roomIndex = room, spill.line = m.line;
			h.mishaps.push_back(spill);
		}
	}
	// who slips: the first time each walker's feet come onto a puddle after it is there
	h.bumps.clear();
	for (const rooms::Mishap &m : h.mishaps) {
		if (m.verb != "puddle" && m.verb != "spill") continue;
		for (const Character &c : spec.characters)
			for (std::size_t i = 0; i < c.walks.size(); ++i) {
				const WalkLine &w = c.walks[i];
				if (w.points.empty() || w.style == WalkLine::Jump) continue;
				const rig::PathWalk path = pathOf(c, i);
				const double end = w.t0 + path.length() / std::max(1e-6, path.speed);
				bool slipped = false;
				for (double t = std::max(w.t0, m.t + 1.0); t < end && !slipped; t += 0.04) {
					const Vec3 p = pbrtOf(path.at(t).at);
					if (std::hypot(p.x - m.at.x, p.z - m.at.z) < m.size &&std::abs(p.y - m.at.y) < 0.5) h.bumps.push_back({c.name, t, true}), slipped = true;
				}
				if (slipped) break;
			}
	}
	// who a door is opened into: standing just inside it, in the way of its leaf, as it swings
	for (const rooms::Door &d : in.doors) {
		const rooms::Room &r = in.rooms[static_cast<std::size_t>(d.roomIndex)];
		const rooms::Opening &o = r.openings[static_cast<std::size_t>(d.opening)];
		double was = 0.0;
		for (const auto &[at, amount] : d.open) {
			if (amount > was + 0.3)
				for (const Character &c : spec.characters) {
					Vec3 p;
					momentOf(c, at + 0.35, &p);
					const Vec3 local = rooms::toLocal(r, pbrtOf(p)) - rooms::wallPoint(r, o.wall, o.along, 0.0), in_ = rooms::wallInward(o.wall);
					const double into = dot(local, in_), across = std::abs(dot(local, Vec3{in_.z, 0.0, -in_.x}));
					if (into > 0.05 && into < o.width + 0.15 && across < 0.5 * o.width + 0.2 && std::abs(local.y) < 0.5) h.bumps.push_back({c.name, at + 0.35, false});
				}
			was = amount;
		}
	}
	std::stable_sort(h.bumps.begin(), h.bumps.end(), [](const rooms::Bump &a, const rooms::Bump &b) { return a.t < b.t; });
}

// ---- the poses ----
Vec3 DataFilm::torchHand(const Character &c, const CharacterMoment &m) const {
	const CharacterKind &kind = characterKinds().at(c.kind);
	return transformPoint(m.frame, Vec3{-std::abs(kind.carry.x), kind.chest.y * 1.02, kind.carry.z * 1.25 + 0.08} * c.scale);   // (its right hand, up and out ahead)
}

void DataFilm::homePose(const Character &c, double t, CharacterMoment &m) const {
	const rooms::Home &h = spec.indoors.home;
	const CharacterKind &kind = characterKinds().at(c.kind);
	const double H = kind.height * c.scale;
	for (const rooms::Use &u : spec.indoors.uses) {
		if (u.who != c.name) continue;
		const double w = weightOf(u, t);
		if (w <= 0.0) continue;
		if (u.verb == "nap") {   // (a pet curls up; another lies back as `lie` lays it)
			m.frame = mul(translate(0.0, u.seatHeight * w, 0.0), m.frame);
			m.lie = std::max(m.lie, kind.curls ? w : smoothstep(0.2, 1.0, w));
		}
		if (u.verb == "blowout") m.lean = std::max(m.lean, 0.8 * w * (1.0 - smoothstep(u.arrive + 1.4, u.arrive + 2.0, t)));   // (bent to the cake to blow)
	}
	for (const rooms::Act &a : h.acts) {
		if (a.who != c.name || t < a.t0 - 0.5 || t > a.t1 + 0.5) continue;
		const double w = within(t, a.t0, a.t1, 0.4);
		if (w <= 0.0) continue;
		if (a.verb == "torch") m.hands[1] = {torchHand(c, m), w};
		if (a.verb == "shadowplay") {   // turned square to the wall nearest ahead of it (its shadow thrown flat on it)
			const Vec3 feet = transformPoint(m.frame, {0.0, 0.0, 0.0}), ahead = transformPoint(m.frame, {0.0, 0.0, 1.0}) - feet;
			const int room = rooms::roomAt(spec.indoors, pbrtOf(feet));
			const double facing = room >= 0 ? spec.indoors.rooms[static_cast<std::size_t>(room)].facing : 0.0;   // (a room's walls run at -facing in story space)
			const double heading = rig::headingOf(ahead), square = std::round((heading + facing) / 90.0) * 90.0 - facing;
			double by = square - heading;
			while (by > 180.0) by -= 360.0;
			while (by < -180.0) by += 360.0;
			m.frame = mul(translate(feet), rotate({0.0, 1.0, 0.0}, by * w), translate(feet * -1.0), m.frame);
		}
		if (a.verb == "shadowplay" && !a.what.empty()) {   // both hands up in front of its chest, making the animal
			const Vec3 at = transformPoint(m.frame, Vec3{0.0, kind.chest.y * 1.05, 0.32} * c.scale);
			const Vec3 side = normalize(transformPoint(m.frame, {1.0, 0.0, 0.0}) - transformPoint(m.frame, {0.0, 0.0, 0.0}));
			m.hands[0] = {at + side * (0.035 * c.scale), w}, m.hands[1] = {at - side * (0.035 * c.scale), w};
		}
		if (a.verb == "dance") {   // bobbing to a beat, swaying from side to side, turning a little
			const double beat = 2.0 * kPi * 1.1 * t, bob = 0.06 * H / 1.8 * std::abs(std::sin(beat)) * w, sway = 18.0 * std::sin(0.5 * beat) * w;
			m.frame = mul(translate(0.0, bob, 0.0), m.frame, rotate({0.0, 1.0, 0.0}, sway), translate(0.03 * H * std::sin(0.5 * beat) * w, 0.0, 0.0));
		}
	}
	for (const rooms::Bump &b : h.bumps) {
		if (b.who != c.name || t < b.t || t > b.t + 3.0) continue;
		const double s = t - b.t;
		if (b.slip) {   // feet up (a little hop), onto its back, a moment there, and up again
			const double down = smoothstep(0.0, 0.3, s) * (1.0 - smoothstep(1.7, 2.6, s));
			m.tilt = std::max(m.tilt, 88.0 * down);
			if (s < 0.3) m.frame = mul(translate(0.0, 0.18 * H / 1.8 * std::sin(kPi * s / 0.3), 0.0), m.frame);
		} else {   // a door in the face: knocked back a step, and steadying again
			const double k = smoothstep(0.0, 0.1, s) * (1.0 - smoothstep(0.1, 0.9, s));
			m.tilt = std::max(m.tilt, 26.0 * k);
			m.frame = mul(m.frame, translate(0.0, 0.0, -0.18 * c.scale * k));
		}
	}
}

// ---- what is drawn ----
std::string DataFilm::homeAt(const std::string &header, double t, double nt) const {
	(void)header, (void)nt;
	const rooms::Indoors &in = spec.indoors;
	const rooms::Home &h = in.home;
	std::string out;
	// the things: on their pieces, in a hand, falling, in pieces
	for (const Spec::Prop &p : spec.props) {
		if (p.thing.empty()) continue;
		const Fallen *f = nullptr;
		for (const Fallen &each : fallen)
			if (each.name == p.name && t >= each.t) f = &each;
		if (!f) {
			out += rooms::thingText(p.thing, p.name, propAt(p.name, t), 0.0);
			continue;
		}
		const rooms::Tumble k = rooms::tumbleAt(f->from, f->to, f->t, t, f->breaks);
		if (f->breaks && k.landed) out += rooms::shardsText(f->kind, f->name, f->to, k.since);
		else out += rooms::thingText(f->kind, f->name, k.at, 0.0, k.spin);
	}
	for (const rooms::Mishap &m : h.mishaps) {
		if (m.verb == "puddle" || m.verb == "spill") out += rooms::puddleText(DataFilm::story(m.at.x, m.at.y, m.at.z), m.size, t - m.t);
		if (m.verb == "pile") {   // (toppling out towards the middle of the room)
			const Vec3 foot = DataFilm::story(m.at.x, m.at.y, m.at.z), mid = DataFilm::story(m.from.x, m.at.y, m.from.z);
			const double floor = m.roomIndex >= 0 ? in.rooms[static_cast<std::size_t>(m.roomIndex)].y : 0.0;
			out += rooms::pileText(m.of, m.line * 13 + 1, mul(translate(foot), rotate({0.0, 1.0, 0.0}, rig::headingOf(mid - foot))), foot.y - floor, m.t, t);
		}
	}
	// what goes on in each room: cooking, the bath, a party
	for (std::size_t i = 0; i < in.rooms.size(); ++i) {
		const rooms::Room &r = in.rooms[i];
		const int room = static_cast<int>(i);
		bool bath = false;
		for (const rooms::Doing &d : h.doings) {
			if (d.roomIndex != room) continue;
			if (d.verb == "cook") out += rooms::cookingText(r, d, t);
			bath = bath || d.verb == "bath" || d.verb == "shower";
			if (d.verb == "party") {
				std::vector<double> confetti;
				for (const rooms::Doing &c : h.doings)
					if (c.verb == "confetti" && c.roomIndex == room) confetti.push_back(c.t0);
				double blown = 1e30;
				for (const rooms::Use &u : in.uses)
					if (u.verb == "blowout" && u.roomIndex == room && u.arrive >= 0.0) blown = std::min(blown, u.arrive + 1.0);
				out += rooms::partyText(r, d, confetti, blown, t);
			}
		}
		if (bath) out += rooms::bathText(r, h.doings, room, t);
	}
	// torches and shadow play: where each character is now
	for (const rooms::Act &a : h.acts) {
		if (a.verb != "torch" && a.verb != "shadowplay") continue;
		const Character *c = character(a.who);
		if (!c || t < a.t0 - 0.5 || t > a.t1 + 0.5) continue;
		const double on = within(t, a.t0, a.t1, 0.3);
		if (on <= 0.0) continue;
		const CharacterMoment m = momentOf(*c, t);
		const Vec3 feet = transformPoint(m.frame, {0.0, 0.0, 0.0}), forward = normalize(transformPoint(m.frame, {0.0, 0.0, 1.0}) - feet);
		if (a.verb == "torch") {
			out += rooms::torchText(torchHand(*c, m), forward + Vec3{0.0, -0.22, 0.0}, t >= a.t0 && t < a.t1 ? 1.0 : 0.0);
		} else {
			const double H = characterKinds().at(c->kind).height * c->scale;
			out += rooms::shadowLampText(rooms::shadowBulb(feet, forward, H), feet.y, on);
			if (!a.what.empty()) out += rooms::shadowAnimalText(a.what, transformPoint(m.frame, Vec3{0.0, characterKinds().at(c->kind).chest.y * 1.05, 0.36} * c->scale), forward, t, on);
		}
	}
	// lightning: each strike flashing in at every window
	double flash = 0.0;
	for (const skyfx::Lightning &l : spec.lightnings)
		for (const skyfx::Strike &s : l.strikes) flash = std::max(flash, skyfx::strokes(s, t - s.t));
	if (flash > 0.01)
		for (const rooms::Room &r : in.rooms) out += rooms::flashText(r, flash);
	// wet footprints: of whoever walks out of a bathroom after its bath or shower, drying in half a minute
	std::vector<rooms::Print> prints;
	for (const rooms::Doing &d : h.doings) {
		if (d.verb != "bath" && d.verb != "shower") continue;
		const double wet = d.verb == "shower" ? d.t1 : std::min(d.t1, d.t0 + 1e4);
		if (t < wet) continue;
		for (const Character &c : spec.characters)
			for (std::size_t i = 0; i < c.walks.size(); ++i) {
				const WalkLine &w = c.walks[i];
				if (w.points.empty() || w.t0 < wet - 1.0 || w.t0 > wet + 40.0 || w.t0 > t) continue;
				const rig::PathWalk path = pathOf(c, i);
				if (rooms::roomAt(in, pbrtOf(path.at(w.t0).at)) != d.roomIndex) continue;   // (it set off from the bathroom)
				const double end = std::min(t, w.t0 + path.length() / std::max(1e-6, path.speed));
				double leftAt = -1.0, lastStep = 0.0;
				for (double u = w.t0; u <= end; u += 0.03) {
					const rig::PathWalk::At a = path.at(u);
					if (leftAt < 0.0 && rooms::roomAt(in, pbrtOf(a.at)) != d.roomIndex) leftAt = a.dist;
					if (leftAt >= 0.0 && a.dist > leftAt + 3.5) break;   // (dry again a few steps out)
					if (std::floor(a.steps) <= std::floor(lastStep)) continue;
					lastStep = a.steps;
					const double side = static_cast<int>(std::floor(a.steps)) % 2 ? 1.0 : -1.0, yaw = a.yaw * kPi / 180.0;
					prints.push_back({a.at + Vec3{side * 0.09 * c.scale * std::cos(yaw), 0.0, -side * 0.09 * c.scale * std::sin(yaw)}, a.yaw, u});
				}
			}
	}
	if (!prints.empty()) out += rooms::printsText(prints, t, 30.0);
	return out.empty() ? out : "# what is done at home (film_homeworld.cpp)\n" + out;
}

// ---- the camera ----
camera_keyframes::Key DataFilm::trailKey(const Shot &shot, double t) const {
	camera_keyframes::Key key;
	key.fov = 52.0;
	const Character *c = character(shot.trail);
	if (!c) return key;
	Vec3 feet, forward, chest;
	subjectFrame(c->name, t, feet, forward, chest);
	const double H = sizeOf(*c), up = shot.trailUp > 0.0 ? shot.trailUp : std::clamp(0.8 * H + 0.3, 0.6, 1.9);
	Vec3 eye;
	const int room = rooms::roomAt(spec.indoors, pbrtOf(feet));
	if (shot.cutaway && room >= 0) {   // beside the house, outside its front wall at the walker's place along it, rising with its floor
		const rooms::Room &r = spec.indoors.rooms[static_cast<std::size_t>(room)];
		const Vec3 local = rooms::toLocal(r, pbrtOf(feet));
		eye = rooms::toPbrt(r, {local.x, local.y + up + 0.3, -0.5 * r.d - shot.trailBehind - 1.6});
		key.fov = 56.0;
	} else {   // where it was `behind` metres of its way ago (back along its walk in time), over the floor there; standing, behind its back
		Vec3 at = feet, last = feet;
		double gone = 0.0;
		for (double u = t - 0.05; u > t - 30.0 && gone < shot.trailBehind; u -= 0.05) {
			Vec3 p;
			momentOf(*c, u, &p);
			gone += std::hypot(p.x - last.x, p.z - last.z);
			last = p;
			at = p;
		}
		if (gone < shot.trailBehind) at = at - forward * (shot.trailBehind - gone);
		eye = pbrtOf(at + Vec3{0.0, up, 0.0});
	}
	const Vec3 look = pbrtOf(chest + forward * (0.3 * H));
	for (int i = 0; i < 3; ++i) key.pos[i] = (&eye.x)[i], key.target[i] = (&look.x)[i];
	if (!shot.cutaway && spec.indoors.any()) keepIndoors(key);   // (a standing walker's back may be against a wall)
	return key;
}

// ---- the sound ----
void DataFilm::homeSounds(audio::Mix &m, const std::function<void(double, int, Vec3, const std::vector<float> &, double, double)> &heard) const {
	const rooms::Indoors &in = spec.indoors;
	const rooms::Home &h = in.home;
	const double T = m.seconds();
	unsigned seed = 47003u;
	const auto roomOf = [&in](Vec3 p) { return rooms::roomAt(in, p); };
	for (const rooms::Doing &d : h.doings) {
		if (d.roomIndex < 0) continue;
		const rooms::Room &r = in.rooms[static_cast<std::size_t>(d.roomIndex)];
		const auto pieceAt = [&r](const char *kind, double y) { const rooms::Item *it = rooms::findItem(r, kind); return it ? rooms::toPbrt(r, {it->x, y, it->z}) : rooms::toPbrt(r, {0.0, y, 0.0}); };
		const double end = std::min({T, d.t1, d.t0 + 600.0});
		if (d.verb == "cook" && d.what == "pot") {   // the gas lit, the pot bubbling, its lid rattling
			const Vec3 at = pieceAt("stove", 0.95);
			if (d.t0 < T) heard(d.t0, d.roomIndex, at, audio::noiseBurst(0.5, 0.01, 0.4, 600.0, 2500.0, seed += 13u), 0.12, 6.0);
			for (double t = d.t0 + 3.5; t < end; t += 0.11 + 0.15 * audio::unit(seed += 13u)) heard(t, d.roomIndex, at, audio::knock(260.0 + 280.0 * audio::unit(seed + 1u), 0.05, seed), 0.05, 6.0);
			for (double t = d.t0 + 6.0; t < end; t += 0.19) heard(t, d.roomIndex, at, audio::knock(1900.0, 0.02, seed += 13u), 0.03, 6.0);
		} else if (d.verb == "cook" && d.what == "kettle") {   // its rumble growing, then its whistle
			const Vec3 at = pieceAt("stove", 1.05);
			for (double t = d.t0; t < std::min(end, d.t0 + 6.0); t += 0.4) heard(t, d.roomIndex, at, audio::noiseBurst(0.6, 0.1, 0.3, 80.0, 400.0, seed += 13u), 0.04 + 0.08 * (t - d.t0) / 6.0, 6.0);
			for (double t = d.t0 + 6.0; t < std::min({T, d.t1, d.t0 + 12.0}); t += 0.45) heard(t, d.roomIndex, at, audio::chirp(1950.0 + 40.0 * std::sin(t), 2050.0, 0.6, 9.0, 0.01, 1.0), 0.07, 8.0);
		} else if (d.verb == "cook" && d.what == "toast") {   // its lever down, and the pop and ding
			const Vec3 at = pieceAt("counter", 1.0);
			if (d.t0 < T) heard(d.t0, d.roomIndex, at, audio::knock(900.0, 0.05, seed += 13u), 0.12, 6.0);
			if (d.t0 + 5.0 < T) heard(d.t0 + 5.0, d.roomIndex, at, audio::knock(500.0, 0.08, seed += 13u), 0.2, 6.0), heard(d.t0 + 5.02, d.roomIndex, at, audio::bell(2350.0, 1.2), 0.05, 6.0);
		} else if (d.verb == "cook" && d.what == "oven") {   // its fan's hum; a ding when it is done
			const Vec3 at = pieceAt("stove", 0.4);
			for (double t = d.t0; t < end; t += 1.5) heard(t, d.roomIndex, at, audio::noiseBurst(1.7, 0.3, 0.3, 90.0, 250.0, seed += 13u), 0.04, 6.0);
			if (d.t1 < T) heard(d.t1, d.roomIndex, at, audio::bell(1760.0, 1.6), 0.08, 6.0);
		} else if (d.verb == "cook" && d.what == "meal") {   // knives and forks on plates
			const Vec3 at = pieceAt("table", 0.8);
			for (double t = d.t0 + 1.0; t < end; t += 0.6 + 1.4 * audio::unit(seed += 13u)) heard(t, d.roomIndex, at, audio::bell(3200.0 + 900.0 * audio::unit(seed + 2u), 0.25), 0.025, 6.0);
		} else if (d.verb == "bath" || d.verb == "shower") {   // the tap running (the shower's hiss), the plug pulled and gurgling away
			const Vec3 at = pieceAt("bath", d.verb == "shower" ? 1.6 : 0.6);
			const double runTo = d.verb == "shower" ? end : std::min(end, d.t0 + 10.0);
			for (double t = d.t0; t < runTo; t += 0.25) heard(t, d.roomIndex, at, audio::noiseBurst(0.3, 0.02, 0.05, d.verb == "shower" ? 3000.0 : 900.0, d.verb == "shower" ? 9000.0 : 3500.0, seed += 13u), 0.05, 6.0);
			if (d.verb == "bath" && d.t1 < T)
				for (int q = 0; q < 10; ++q) heard(d.t1 + 0.6 * q, d.roomIndex, at, audio::slide(180.0 + 60.0 * audio::unit(seed += 13u), 90.0, 0.4, 6.0), 0.07, 6.0);
		} else if (d.verb == "flicker") {   // a buzz as it falters
			const Vec3 at = rooms::toPbrt(r, {0.0, r.h - 0.4, 0.0});
			for (double t = d.t0; t < end; t += 0.1)
				if (hash01(static_cast<int>(std::floor(t * 10.0)), r.seed * 17) < 0.18) heard(t, d.roomIndex, at, audio::chirp(120.0, 120.0, 0.1, 50.0, 0.3, 1.0), 0.03, 6.0);
		} else if (d.verb == "confetti" && d.t0 < T) {   // a party popper
			heard(d.t0, d.roomIndex, rooms::toPbrt(r, {0.0, r.h - 0.3, 0.0}), audio::noiseBurst(0.15, 0.002, 0.1, 2000.0, 8000.0, seed += 13u), 0.25, 8.0);
		}
	}
	for (const rooms::Act &a : h.acts) {
		const Character *c = character(a.who);
		if (!c) continue;
		Vec3 at;
		if (a.verb == "torch")   // its click on and off
			for (const double t : {a.t0, a.t1})
				if (t < T) momentOf(*c, t, &at), heard(t, roomOf(pbrtOf(at)), pbrtOf(at + Vec3{0.0, 1.0, 0.0}), audio::knock(2600.0, 0.015, seed += 13u), 0.1, 4.0);
		if (a.verb == "dance" && c->extra)   // (a party's guests: their chatter while they dance)
			for (double t = a.t0; t < std::min(T, a.t1); t += 0.35 + 0.5 * audio::unit(seed += 13u)) {
				momentOf(*c, t, &at);
				heard(t, roomOf(pbrtOf(at)), pbrtOf(at + Vec3{0.0, 1.5, 0.0}), audio::chirp(170.0 + 140.0 * audio::unit(seed + 1u), 150.0 + 120.0 * audio::unit(seed + 2u), 0.2, 6.0, 0.05, 1.0), 0.03, 6.0);
			}
	}
	for (const rooms::Use &u : in.uses) {
		const Character *c = character(u.who);
		if (!c || u.arrive < 0.0 || u.arrive >= T) continue;
		const Vec3 at = u.seat + Vec3{0.0, 0.3, 0.0};
		if (u.verb == "nap" && c->kind == "cat")   // a purr, rising and falling with its breath
			for (double t = u.arrive + 1.0; t < std::min(T, u.t1); t += 1.3) heard(t, u.roomIndex, at, audio::noiseBurst(1.2, 0.4, 0.5, 25.0, 140.0, seed += 13u), 0.06, 4.0);
		if (u.verb == "knock" && c->kind == "cat" && u.arrive + 2.2 < T) heard(u.arrive + 2.2, u.roomIndex, at, audio::chirp(700.0, 520.0, 0.5, 5.0, 0.04, 1.0), 0.1, 6.0);   // (a meow, pleased with itself)
		if (u.verb == "blowout" && u.arrive + 1.0 < T) heard(u.arrive + 1.0, u.roomIndex, at + Vec3{0.0, 0.5, 0.0}, audio::noiseBurst(0.5, 0.05, 0.4, 300.0, 1800.0, seed += 13u), 0.15, 5.0);
		if (u.verb == "wait" && c->kind == "dog")   // a bark when the door it waits by opens
			for (const rooms::Door &d : in.doors)
				for (const auto &[t, amount] : d.open)
					if (amount > 0.3 && t > u.arrive && t < std::min(T, u.t1))
						for (int q = 0; q < 2; ++q) heard(t + 0.2 + 0.35 * q, u.roomIndex, at, audio::chirp(520.0, 340.0, 0.16, 0.0, 0.0, 1.0), 0.18, 8.0);
	}
	for (const Fallen &f : fallen) {   // a crash and the tinkle of pieces, or a thud and a roll
		const double land = f.t + std::sqrt(2.0 * std::max(0.05, f.from.y - f.to.y) / 9.81);
		if (land >= T) continue;
		const Vec3 at = pbrtOf(f.to);
		const int room = roomOf(at);
		if (f.breaks) {
			heard(land, room, at, audio::noiseBurst(0.25, 0.001, 0.2, 3000.0, 9000.0, seed += 13u), 0.35, 8.0);
			for (int q = 0; q < 6; ++q) heard(land + 0.05 + 0.08 * q, room, at, audio::bell(3500.0 + 2500.0 * audio::unit(seed += 13u), 0.3), 0.04, 8.0);
		} else {
			heard(land, room, at, audio::knock(180.0, 0.15, seed += 13u), 0.3, 8.0);
		}
	}
	for (const rooms::Mishap &mi : h.mishaps)
		if (mi.verb == "pile")   // a clatter as it goes
			for (int q = 0; q < 10 && mi.t + 0.4 + 0.07 * q < T; ++q) heard(mi.t + 0.4 + 0.07 * q, mi.roomIndex, mi.at, audio::knock(mi.of == "plates" ? 900.0 : 220.0 + 200.0 * audio::unit(seed += 13u), 0.1, seed), 0.15, 8.0);
	for (const rooms::Bump &b : h.bumps) {
		const Character *c = character(b.who);
		if (!c || b.t >= T) continue;
		Vec3 at;
		momentOf(*c, b.t, &at);
		const Vec3 p = pbrtOf(at);
		if (b.slip) {   // a squeak of a foot, and a thud onto its back
			heard(b.t, roomOf(p), p, audio::chirp(1300.0, 1900.0, 0.14, 0.0, 0.0, 1.0), 0.08, 6.0);
			if (b.t + 0.32 < T) heard(b.t + 0.32, roomOf(p), p, audio::knock(85.0, 0.25, seed += 13u), 0.4, 8.0);
		} else {
			heard(b.t, roomOf(p), p + Vec3{0.0, 1.5, 0.0}, audio::knock(300.0, 0.1, seed += 13u), 0.3, 6.0);
			heard(b.t + 0.03, roomOf(p), p + Vec3{0.0, 1.5, 0.0}, audio::boing(320.0, 0.5), 0.06, 6.0);
		}
	}
}

}  // namespace film_file
}  // namespace cartoon
