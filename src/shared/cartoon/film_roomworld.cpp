// film_roomworld.cpp -- a film's rooms in its frames and on its soundtrack (indoor round 1: film_rooms.h, film_roomlook.h, film_roomlife.h): DataFilm's members for them.
// Compiled once, in cpu_renderer.
//
//   what is still     each room's walls, floor, ceiling, stairs and furniture: meshes written once (film_roomlook.h), placed every frame; a `cutaway` shot leaves out the
//                     walls between the camera and the room it looks into
//   what moves        doors, curtains, the lamps (on while it is dark outside, or as `switch` says, off in a `powercut`), a fire, candles, a television, rain on the
//                     panes, dust in the sun through a window, the clutter, the fish (film_roomlife.h)
//   who uses them     `sit WHO on sofa`, `lie WHO in bed`, `open WHO fridge`, `lean WHO on counter`, `lookout WHO`: it walks there (finding its way round the furniture and
//                     through the doorways: the rooms are in what the way-finding reads), turns, and sits, lies back, opens it, leans, looks out; and gets up at `until`
//   the camera        one that moves by itself (`follow`, a move) stays inside the room it looks into, out of the furniture; `peek ROOM` looks in at a window
//   the light         a camera in a room opens up for a room's light (with `exposure auto`): by day a little, at night by the lamps more, by candlelight most
//   the sound         footsteps on each room's floor (wood, tiles, carpet, a rug), each room's echo, a fridge's hum, a clock, a fire, a television, doors, switches, rain
//                     on the roof - and what is in another room heard through the wall, muffled

#include "film_file.h"
#include "film_homelife.h"
#include "film_roomlife.h"
#include "film_roomlook.h"
#include "film_soundscape.h"

namespace cartoon {
namespace film_file {

namespace {
// The pieces of room `room` someone opens (their doors drawn on their own, as they swing).
std::vector<int> openedIn(const Spec &spec, int room) {
	std::vector<int> out;
	for (const rooms::Use &u : spec.indoors.uses)
		if (u.verb == "open" && u.roomIndex == room && std::find(out.begin(), out.end(), u.itemIndex) == out.end()) out.push_back(u.itemIndex);
	return out;
}
// How far into a use t is: in over its first 0.6 s after it gets there, out over 0.6 s from `until` (an `open` lets go after three seconds unless it says).
double useWeight(const rooms::Use &u, double t) {
	if (u.arrive < 0.0 || t < u.arrive) return 0.0;
	const double end = u.t1 < 1e29 ? u.t1 : u.verb == "open" ? u.arrive + 3.0 : 1e30;
	return smoothstep(u.arrive, u.arrive + 0.6, t) * (1.0 - smoothstep(end, end + 0.6, t));
}
double angleBetween(double from, double to, double u) {
	double d = to - from;
	while (d > 180.0) d -= 360.0;
	while (d < -180.0) d += 360.0;
	return from + d * u;
}
std::vector<float> muffled(std::vector<float> s) {   // (through a wall: its highs gone)
	const double k = 1.0 - std::exp(-2.0 * kPi * 600.0 / audio::kRate);
	for (int pass = 0; pass < 2; ++pass) {
		double y = 0.0;
		for (float &v : s) y += k * (v - y), v = static_cast<float>(y);
	}
	return s;
}
}  // namespace

double DataFilm::darkOutside(double t) const {
	if (!spec.hasSky) return 1.0;   // (a film with no sky: a room's own light)
	double el = 0.0, facing = 0.0;
	dressing::sunAt(spec.sky, dressing::hourAt(spec.sky, t, spec.storyEnd), el, facing);
	return 1.0 - smoothstep(-3.0, 7.0, el);
}

std::string DataFilm::roomsStatic() const {
	const std::string film = spec.key.empty() ? std::string("film") : spec.key;
	std::string out;
	for (std::size_t i = 0; i < spec.indoors.rooms.size(); ++i) out += rooms::staticText(film, spec.indoors.rooms[i], openedIn(spec, static_cast<int>(i)));
	return out;
}

std::string DataFilm::roomsAt(const std::string &header, double t, double nt) const {
	const std::string film = spec.key.empty() ? std::string("film") : spec.key;
	const rooms::Indoors &in = spec.indoors;
	Vec3 eye, target;
	double fov = 40.0;
	film_subjects::cameraOf(header, eye, target, fov);
	const Vec3 eyeP{-eye.x, eye.y, eye.z};
	const bool cut = !spec.shots.empty() && spec.shots[shotAt(t)].cutaway;
	const double dark = darkOutside(t);
	Vec3 toSun{0.0, -1.0, 0.0};
	if (spec.hasSky) {
		double el = 0.0, facing = 0.0;
		dressing::sunAt(spec.sky, dressing::hourAt(spec.sky, t, spec.storyEnd), el, facing);
		const double e = el * kPi / 180.0, f = facing * kPi / 180.0;
		toSun = {std::cos(e) * std::sin(f), std::sin(e), std::cos(e) * std::cos(f)};
	}
	double rain = 0.0;
	for (const particles::Weather &w : spec.weather)
		if (w.kind == "rain" && t >= w.t0 && t <= w.t1) rain = std::max(rain, std::min(1.0, w.amount) * smoothstep(w.t0, w.t0 + 2.0, t) * (1.0 - smoothstep(w.t1 - 2.0, w.t1, t)));
	std::string out = "# rooms (film_roomworld.cpp)\n";
	std::set<std::string> written;
	for (std::size_t i = 0; i < in.rooms.size(); ++i) {
		const rooms::Room &r = in.rooms[i];
		int leaveOut = 0;
		if (cut) {   // (a dolls' house: each wall the camera is outside of, between it and the room, not drawn)
			const Vec3 e = rooms::toLocal(r, eyeP);
			if (e.z < -0.5 * r.d) leaveOut |= 1;
			if (e.z > 0.5 * r.d) leaveOut |= 2;
			if (e.x < -0.5 * r.w) leaveOut |= 4;
			if (e.x > 0.5 * r.w) leaveOut |= 8;
		}
		const std::vector<int> opened = openedIn(spec, static_cast<int>(i));
		out += rooms::staticText(film, r, opened, leaveOut, rooms::steamOf(in.home.doings, static_cast<int>(i), t));   // (a mirror steamed up: round 2)
		std::vector<std::pair<int, double>> openness;
		for (const int item : opened) {
			double v = 0.0;
			for (const rooms::Use &u : in.uses)
				if (u.verb == "open" && u.roomIndex == static_cast<int>(i) && u.itemIndex == item) v = std::max(v, useWeight(u, t));
			openness.push_back({item, v});
		}
		out += rooms::doorsText(in, static_cast<int>(i), t) + rooms::itemDoorsText(r, openness) + rooms::curtainsText(r, t) + rooms::lampsText(in, r, t, dark);
		out += rooms::windowLightText(r, 1.0 - dark, rain) + rooms::rainOnGlassText(r, t, rain) +rooms::clutterText(r, rooms::clutterOf(r), t, written) + rooms::aquariumText(r, nt);
		if (dark < 0.9 && rain < 0.3) {   // (the sun in at its windows, where the curtains are not drawn)
			double closed = 0.0, blow = 0.0;
			rooms::curtainsAt(r, t, closed, blow);
			if (closed < 0.6) out += rooms::motesText(r, nt, toSun);
		}
	}
	return out + homeAt(header, t, nt);   // (what is done there: round 2, film_homeworld.cpp)
}

std::string DataFilm::placeUses() {
	for (const rooms::Use &u : spec.indoors.uses) {
		Character *c = nullptr;
		for (Character &each : spec.characters)
			if (each.name == u.who) c = &each;
		if (!c) continue;
		WalkLine w;
		w.line = u.line, w.t0 = u.t0, w.speed = u.speed;
		w.targets.push_back({u.stand.x, u.stand.z});
		w.floors.push_back(u.stand.y);   // (its room's floor: upstairs, by the stairs - film_ways.cpp)
		const auto at = std::find_if(c->walks.begin(), c->walks.end(), [&u](const WalkLine &o) { return o.t0 > u.t0; });
		if (at != c->walks.begin() && std::prev(at)->t0 == u.t0) return std::to_string(u.line) + ": " + u.who + " already walks at " + std::to_string(u.t0) + " s";
		c->walks.insert(at, w);
	}
	placeHome();   // (round 2's: a carry's walks, a pet's after its owner - film_homeworld.cpp)
	return std::string();
}

void DataFilm::timeUses() {
	for (rooms::Use &u : spec.indoors.uses) {
		Character *c = nullptr;
		for (Character &each : spec.characters)
			if (each.name == u.who) c = &each;
		if (!c) continue;
		for (std::size_t i = 0; i < c->walks.size(); ++i) {
			const WalkLine &w = c->walks[i];
			if (w.t0 != u.t0 || w.targets.size() != 1 || w.targets[0].first != u.stand.x || w.targets[0].second != u.stand.z) continue;
			const rig::PathWalk path = pathOf(*c, i);
			u.arrive = w.t0 + (w.points.empty() ? 0.0 : path.length() / std::max(1e-6, path.speed));
		}
		if (u.arrive < 0.0) u.arrive = u.t0;
		if (u.verb == "sit") c->sits.push_back({u.arrive + 0.45, u.t1, u.seatHeight});
		if (u.verb == "open") {   // (a reach for its handle as it opens)
			const auto &poses = characterKinds().at(c->kind).poses;
			const double end = u.t1 < 1e29 ? u.t1 : u.arrive + 3.0;
			if (std::find(poses.begin(), poses.end(), "reach") != poses.end()) c->gestures.push_back({"reach", u.arrive, u.arrive + 0.4, std::max(u.arrive + 0.5, end - 0.4), end + 0.2, -1.0});
		}
	}
	timeHome();   // (round 2's: picks and puts, knocks, falls, slips: film_homeworld.cpp)
}

void DataFilm::roomMoment(const Character &c, double t, Vec3 &where, double &heading) const {
	for (const rooms::Use &u : spec.indoors.uses) {
		if (u.who != c.name || u.verb == "go") continue;   // (a `go` is only its walk)
		const double w = useWeight(u, t);
		if (w <= 0.0) continue;
		const Vec3 seat = story(u.seat.x, where.y, u.seat.z);
		where = Vec3{lerp(where.x, seat.x, w), where.y, lerp(where.z, seat.z, w)};
		heading = angleBetween(heading, -u.faceTo, std::min(1.0, 1.6 * w));   // (it turns round as it settles)
	}
}

void DataFilm::roomPose(const Character &c, double t, CharacterMoment &m) const {
	for (const rooms::Use &u : spec.indoors.uses) {
		if (u.who != c.name || (u.verb != "lie" && u.verb != "lean")) continue;
		const double w = useWeight(u, t);
		if (w <= 0.0) continue;
		const double H = characterKinds().at(c.kind).height * c.scale;
		if (u.verb == "lie") {   // (up onto it, its back above the seat by its own thickness; then castAt lays it back, its head up the bed)
			m.frame = mul(translate(0.0, (u.seatHeight + 0.09 * H) * w, 0.0), m.frame);
			m.lie = std::max(m.lie, smoothstep(0.2, 1.0, w));
		} else {
			m.lean = std::max(m.lean, w);
		}
	}
	homePose(c, t, m);   // (round 2's: a nap, a torch, a dance, a slip - film_homeworld.cpp)
}

camera_keyframes::Key DataFilm::peekKey(const Shot &shot) const {
	camera_keyframes::Key key;
	key.fov = 44.0;
	for (const rooms::Room &r : spec.indoors.rooms) {
		if (r.name != shot.peek) continue;
		std::vector<const rooms::Opening *> windows;
		for (const rooms::Opening &o : r.openings)
			if (!o.door) windows.push_back(&o);
		if (windows.empty()) break;
		const rooms::Opening &o = *windows[static_cast<std::size_t>(std::clamp(shot.peekWindow, 0, static_cast<int>(windows.size()) - 1))];
		const Vec3 at = rooms::wallPoint(r, o.wall, o.along, o.sill + 0.55 * o.height), inward = rooms::wallInward(o.wall);
		const Vec3 eye = rooms::toPbrt(r, at - inward * (rooms::kWall + shot.peekFrom)), look = rooms::toPbrt(r, at + inward * (0.6 * (o.wall < 2 ? r.d : r.w)) + Vec3{0.0, -0.45, 0.0});
		for (int i = 0; i < 3; ++i) key.pos[i] = (&eye.x)[i], key.target[i] = (&look.x)[i];
	}
	return key;
}

void DataFilm::keepIndoors(camera_keyframes::Key &key) const {
	const Vec3 target{key.target[0], key.target[1], key.target[2]};
	const int i = rooms::roomAt(spec.indoors, target);
	if (i < 0) return;
	const rooms::Room &r = spec.indoors.rooms[static_cast<std::size_t>(i)];
	Vec3 e = rooms::toLocal(r, {key.pos[0], key.pos[1], key.pos[2]});
	const double mx = 0.5 * r.w - rooms::kWall - 0.25, mz = 0.5 * r.d - rooms::kWall - 0.25;
	e.x = std::clamp(e.x, -mx, mx), e.z = std::clamp(e.z, -mz, mz), e.y = std::clamp(e.y, 0.3, r.h - 0.25);   // (inside its walls, under its ceiling)
	for (const rooms::Item &it : r.items) {   // (and out of its furniture: to the nearest side of what it is in)
		if (it.flat || e.y > it.h + 0.1) continue;
		const rooms::detail::Rect f = rooms::detail::footprint(it, 0.15);
		if (e.x <= f.x0 || e.x >= f.x1 || e.z <= f.z0 || e.z >= f.z1) continue;
		const double dx0 = e.x - f.x0, dx1 = f.x1 - e.x, dz0 = e.z - f.z0, dz1 = f.z1 - e.z, least = std::min({dx0, dx1, dz0, dz1});
		if (least == dx0) e.x = f.x0;
		else if (least == dx1) e.x = f.x1;
		else if (least == dz0) e.z = f.z0;
		else e.z = f.z1;
	}
	const Vec3 p = rooms::toPbrt(r, e);
	key.pos[0] = p.x, key.pos[1] = p.y, key.pos[2] = p.z;
}

double DataFilm::indoorExposure(double t) const {
	Vec3 eye, target;
	double fov = 40.0;
	if (!film_subjects::cameraOf(headerAt(t), eye, target, fov)) return 0.0;
	const int i = rooms::roomAt(spec.indoors, {-eye.x, eye.y, eye.z});
	if (i < 0) return 0.0;
	const rooms::Room &r = spec.indoors.rooms[static_cast<std::size_t>(i)];
	const double dark = darkOutside(t);
	for (const rooms::Doing &d : spec.indoors.home.doings)   // (shadow play: a bare bulb lights it, the lamps turned down - opened up as for candles, the bulb burnt the shadow out)
		if (d.verb == "dim" && d.roomIndex == i && t >= d.t0 && t < d.t1) return lerp(1.8, 0.9, dark);
	double lamps = 0.0;   // (how much of its light is a lamp's, lit)
	for (const rooms::Lamp &l : r.lamps)
		if (l.electric) lamps = std::max(lamps, rooms::lampOn(spec.indoors, r, l, t, dark));
	const double day = 1.8, lamplit = 1.6, candlelit = 3.0;   // (by candlelight it is dim still)
	return lerp(day, lerp(candlelit, lamplit, lamps), dark);
}

int DataFilm::floorSurfaceAt(Vec3 story) const {
	const Vec3 p{-story.x, story.y, story.z};
	const int i = rooms::roomAt(spec.indoors, p);
	if (i < 0) return -1;
	const rooms::Room &r = spec.indoors.rooms[static_cast<std::size_t>(i)];
	const Vec3 local = rooms::toLocal(r, p);
	for (const rooms::Item &it : r.items) {   // (a rug under it)
		if (it.kind != "rug" && it.kind != "mat") continue;   // (a bathroom's mirror is flat too, on its wall)
		const rooms::detail::Rect f = rooms::detail::footprint(it);
		if (local.x > f.x0 && local.x < f.x1 && local.z > f.z0 && local.z < f.z1) return static_cast<int>(sfx::Surface::Carpet);
	}
	const std::string kind = !r.look.floor.empty() ? r.look.floor : r.kind == "bathroom" || r.kind == "kitchen" ? "tiles" : r.kind == "bedroom" ? "carpet" : "wood";
	return static_cast<int>(kind == "carpet" ? sfx::Surface::Carpet : kind == "tiles" || kind == "checks" ? sfx::Surface::Tile : sfx::Surface::Wood);
}

double DataFilm::roomSendAt(Vec3 story) const {
	const int i = rooms::roomAt(spec.indoors, {-story.x, story.y, story.z});
	if (i < 0) return -1.0;
	const rooms::Room &r = spec.indoors.rooms[static_cast<std::size_t>(i)];
	const int floor = floorSurfaceAt(story);
	// (small and soft is dry; hard and big rings: a tiled bathroom most, a carpeted bedroom least)
	const double hard = floor == static_cast<int>(sfx::Surface::Tile) ? 1.0 : floor == static_cast<int>(sfx::Surface::Carpet) ? 0.25 : 0.55;
	const double size = std::clamp(std::cbrt(r.w * r.d * r.h) / 4.0, 0.5, 1.6);
	return std::clamp(0.08 + 0.35 * hard * size + (r.kind == "bathroom" ? 0.2 : 0.0), 0.06, 0.7);
}

void DataFilm::roomSounds(audio::Mix &m) const {
	namespace ss = soundscape;
	const ss::Ears ears = ss::earsOf(m.seconds(), [this](double t, Vec3 &eye, Vec3 &target) {
		double fov = 40.0;
		return film_subjects::cameraOf(headerAt(inCredits(t) ? std::max(0.0, spec.storyEnd - 1e-3) : t), eye, target, fov);
	});
	const rooms::Indoors &in = spec.indoors;
	const double T = m.seconds();
	unsigned seed = 31001u;
	// a sound from room `room` at pbrt's `p`: through the wall, muffled and quieter, when the ears are in another room
	const auto heard = [&](double t, int room, Vec3 p, const std::vector<float> &sound, double gain, double within) {
		const int earsIn = rooms::roomAt(in, ears(t).eye);
		if (earsIn == room || earsIn < 0) ss::place(m, ears, t, p, sound, gain, within);
		else ss::place(m, ears, t, p, muffled(sound), 0.45 * gain, within);
	};
	for (std::size_t i = 0; i < in.rooms.size(); ++i) {
		const rooms::Room &r = in.rooms[i];
		const int room = static_cast<int>(i);
		for (const rooms::Item &it : r.items) {
			const Vec3 at = rooms::toPbrt(r, {it.x, 0.8, it.z});
			if (it.kind == "fridge")   // its hum, now and then
				for (double t = 0.5; t < T; t += 1.4) heard(t, room, at, audio::noiseBurst(1.6, 0.4, 0.5, 60.0, 180.0, seed += 13u), 0.06, 8.0);
			if (it.kind == "bookshelf" || it.kind == "fireplace")   // a clock ticking on it
				for (double t = 0.2; t < T; t += 1.0) heard(t, room, at + Vec3{0.0, 0.6, 0.0}, audio::knock(2400.0, 0.02, seed += 13u), 0.035, 6.0);
		}
		for (std::size_t k = 0; k < r.lamps.size(); ++k) {
			const rooms::Lamp &l = r.lamps[k];
			const Vec3 at = rooms::toPbrt(r, l.at);
			if (l.kind == "fire")   // its crackle
				for (double t = 0.1; t < T; t += 0.12 + 0.4 * audio::unit(seed += 13u))
					if (rooms::lampOn(in, r, l, t, darkOutside(t)) > 0.5) heard(t, room, at, audio::noiseBurst(0.04, 0.001, 0.03, 1800.0, 6000.0, seed), 0.12 * audio::unit(seed + 1u), 8.0);
			if (l.kind == "tv")   // voices in it, a murmur of a programme
				for (double t = 0.3; t < T; t += 0.25 + 0.3 * audio::unit(seed += 13u))
					if (rooms::lampOn(in, r, l, t, darkOutside(t)) > 0.5) heard(t, room, at, audio::chirp(180.0 + 160.0 * audio::unit(seed + 1u), 160.0 + 140.0 * audio::unit(seed + 2u), 0.18, 6.0, 0.05, 1.0), 0.05, 8.0);
		}
		for (const rooms::Switch &sw : in.switches)   // a switch's click
			if (sw.room == r.name)
				for (const double t : {sw.on, sw.off})
					if (t < T) heard(t, room, rooms::toPbrt(r, {0.0, 1.15, -0.5 * r.d}), audio::knock(3200.0, 0.012, seed += 13u), 0.12, 6.0);
		for (const auto &[at, how] : r.curtains)   // the curtains' swish
			if (at >= 0.0 && at < T) heard(at, room, rooms::toPbrt(r, {0.0, 1.6, 0.5 * r.d - 0.2}), audio::noiseBurst(0.9, 0.2, 0.6, 2500.0, 5000.0, seed += 13u), 0.1, 8.0);
	}
	for (const rooms::Door &d : in.doors) {   // a door's creak as it swings, its knock as it shuts
		const rooms::Room &r = in.rooms[static_cast<std::size_t>(d.roomIndex)];
		const rooms::Opening &o = r.openings[static_cast<std::size_t>(d.opening)];
		const Vec3 at = rooms::toPbrt(r, rooms::wallPoint(r, o.wall, o.along, 1.0));
		double was = 0.0;
		for (const auto &[t, amount] : d.open) {
			if (t >= T) break;
			if (std::abs(amount - was) > 0.05) heard(t, d.roomIndex, at, audio::slide(420.0, 300.0, 0.6, 14.0), 0.06, 10.0);
			if (amount < 0.05 && was > 0.05) heard(t + 0.85, d.roomIndex, at, audio::knock(140.0, 0.12, seed += 13u), 0.3, 12.0);
			was = amount;
		}
	}
	for (const particles::Weather &w : spec.weather) {   // rain on the roof and the panes, heard from inside
		if (w.kind != "rain") continue;
		for (double t = std::max(0.0, w.t0); t < std::min(T, w.t1); t += 0.05) {
			const int earsIn = rooms::roomAt(in, ears(t).eye);
			if (earsIn < 0) continue;
			const rooms::Room &r = in.rooms[static_cast<std::size_t>(earsIn)];
			ss::place(m, ears, t, rooms::toPbrt(r, {0.0, r.h + 0.5, 0.0}), audio::noiseBurst(0.03, 0.001, 0.02, 1500.0, 5000.0, seed += 13u), 0.05 * std::min(1.0, w.amount), 10.0);
		}
	}
	homeSounds(m, heard);   // (round 2's: film_homeworld.cpp)
}

}  // namespace film_file
}  // namespace cartoon
