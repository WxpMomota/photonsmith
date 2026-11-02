// film_waterworld.cpp -- what touches the water, what is on it and what is heard from it: the part of a frame the water rounds added (film_stir.h and those after it).

#include "film_file.h"

namespace cartoon {
namespace film_file {

bool DataFilm::wildSubject(const std::string &who, double t, Vec3 &feet, Vec3 &front, Vec3 &chest) const {
	if (deepSubject(who, t, feet, front, chest)) return true;   // (round 18: a sub, a serpent)
	for (const craft::Craft &c : spec.crafts)   // a boat by name
		if (c.name == who) {
			const craft::Shape h = craft::shapeOf(c.kind, c.length, c.colour);
			const craft::Riding r = craft::ridingAt(c, h, afloat(), natureAt(t));
			feet = story(r.at.x, r.at.y, r.at.z), front = story(r.ahead.x, 0.0, r.ahead.z), chest = feet + Vec3{0.0, h.free + 0.4, 0.0};
			return true;
		}
	for (const adrift::Thing &th : spec.adrifts)   // something the water carries, by name
		if (th.name == who && !th.ways.empty()) {
			const adrift::Pose p = adrift::poseOf(th, 0, natureAt(t), spec.rivers, spec.seas.empty() ? nullptr : &spec.seas[0]);
			feet = story(p.at.x, p.at.y, p.at.z), front = story(p.ahead.x, 0.0, p.ahead.z), chest = feet + Vec3{0.0, 0.05, 0.0};
			return true;
		}
	{   // under the sea (round 15): a diver by name, or schoolN, swimmersN, bigfishN (the Nth line of that word: its first animal, or a school's middle)
		const double nt = natureAt(t);
		const auto lift = [&](Vec3 p) { return Vec3{p.x, p.y + seabedAt(p.x, p.z), p.z}; };
		Vec3 at, way{0, 0, 1};
		bool found = false;
		for (const diver::Diver &d : spec.divers)
			if (d.name == who) {
				diver::Diver l = d;
				for (Vec3 &p : l.path) p = lift(p);
				at = diver::diverAt(l, nt, &way), found = true;
			}
		double n = 0.0;
		const auto numbered = [&](const char *word) {
			const std::size_t w = std::char_traits<char>::length(word);
			return who.size() > w && who.compare(0, w, word) == 0 && detail::number(who.substr(w), n) && n >= 1.0 && n == std::floor(n);
		};
		if (!found && numbered("school") && static_cast<std::size_t>(n) <= spec.schools.size()) {
			const school::School &sc = schoolsSwum()[static_cast<std::size_t>(n) - 1];
			Vec3 sum{0, 0, 0}, vel{0, 0, 0}, v;
			for (int i = 0; i < sc.count; ++i) sum = sum + school::fishAt(sc, i, nt, &v), vel = vel + v;
			at = sum * (1.0 / std::max(1, sc.count)), way = vel, found = true;
		} else if (!found && numbered("swimmers") && static_cast<std::size_t>(n) <= spec.deep.swimmers.size()) {
			deepsea::Swimmers g = spec.deep.swimmers[static_cast<std::size_t>(n) - 1];
			if (g.kind != "otters") g.at = lift(g.at);
			double roll = 0.0;
			at = deepsea::swimmerAt(g, 0, nt, way, roll, spec.seas.empty() ? 0.0 : spec.seas[0].level), found = true;
		} else if (!found && numbered("bigfish") && static_cast<std::size_t>(n) <= spec.bigFish.size()) {
			school::BigFish b = spec.bigFish[static_cast<std::size_t>(n) - 1];
			b.at = lift(b.at);
			at = school::bigAt(b, 0, nt, way), found = true;
		}
		if (found) {
			way.y = 0.0;
			way = length(way) > 1e-9 ? normalize(way) : Vec3{0, 0, 1};
			feet = story(at.x, at.y, at.z), front = story(way.x, 0.0, way.z), chest = feet;
			return true;
		}
	}
	const bool herd = who.rfind("herd", 0) == 0, flock = who.rfind("flock", 0) == 0;
	if (!herd && !flock) return false;
	double n = 0.0;
	if (!detail::number(who.substr(herd ? 4 : 5), n) || n < 1.0 || n != std::floor(n)) return false;
	const std::size_t i = static_cast<std::size_t>(n) - 1;
	const double nt = natureAt(t);
	const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
	Vec3 sum{0, 0, 0}, way{0, 0, 0};
	double lift = 1.0;
	int count = 0;
	if (herd) {
		if (i >= spec.herds.size()) return false;
		const wildlife::Herd &h = spec.herds[i];
		const animals::Species *sp = animals::species(h.kind);
		if (!sp) return false;
		for (int a = 0; a < std::min(h.count, 12); ++a, ++count) {   // (the herd's leading animals: the bunch the camera is on)
			const wildlife::Animal an = wildlife::animalAt(h, *sp, a, nt, ground.get());
			sum = sum + an.at, way = an.dir;
		}
		lift = 0.85 * sp->length;
	} else {
		if (i >= spec.flocks.size()) return false;
		const wildlife::Flock &f = spec.flocks[i];
		if (f.kind == "eagles") {
			for (int a = 0; a < f.count; ++a, ++count) {
				Vec3 fwd;
				sum = sum + wildlife::eagleAt(f, a, nt, &fwd);
				way = way + fwd;
			}
		} else if ((f.kind == "starlings" || f.kind == "fish") && !f.pos.empty()) {
			const std::size_t k = std::min(f.pos.size() - 1, static_cast<std::size_t>(std::max(0.0, (t - f.t0) / f.dt)));
			for (std::size_t a = 0; a < f.pos[k].size(); ++a, ++count) sum = sum + f.pos[k][a], way = way + f.vel[k][a];
		} else if (f.kind == "geese") {
			const double speed = f.speed > 0.0 ? f.speed : 15.0;
			way = normalize(f.to - f.from + Vec3{1e-9, 0, 0});
			sum = f.from + way * std::min(length(f.to - f.from), (nt - f.t0) * speed), count = 1;
		} else {   // butterflies: over their patch
			sum = Vec3{0.5 * (f.x0 + f.x1), f.at.y + (ground ? ground->heightAt(0.5 * (f.x0 + f.x1), 0.5 * (f.z0 + f.z1)) : 0.0) + 0.8, 0.5 * (f.z0 + f.z1)}, count = 1;
			way = {0, 0, 1};
		}
		lift = 0.0;
	}
	if (count == 0) return false;
	const Vec3 mid = sum * (1.0 / count);
	way.y = 0.0;
	if (length(way) < 1e-9) way = {0, 0, 1};
	way = normalize(way);
	feet = story(mid.x, mid.y, mid.z);
	front = story(way.x, 0.0, way.z);
	chest = feet + Vec3{0.0, lift, 0.0};
	return true;
}

stir::Field DataFilm::stirredAt(double t, double nt, const Vec3 &eye) const {
	stir::Field f;
	if (spec.hasUnderwater) return f;
	f.eyeX = -eye.x, f.eyeZ = eye.z;
	for (const particles::Weather &w : spec.weather) {   // rain: a ring from each drop near the camera
		if (w.kind != "rain" || t < w.t0 || t > w.t1) continue;
		const double level = spec.storm.on ? spec.storm.levelAt(t) / spec.storm.strength : 1.0;
		f.rain = std::max(f.rain, std::min(1.0, w.amount * level / 2.0) * smoothstep(w.t0, w.t0 + 1.5, t) * (1.0 - smoothstep(w.t1 - 1.5, w.t1, t)));
	}
	const std::function<double(double)> nature = [this](double ft) { return natureAt(ft); };
	for (const stir::Plop &p : spec.plops) f.rings.push_back(stir::ringOf(p.x, p.z, natureAt(p.at), p.size, 0.22 * p.size, true));
	for (const stir::Skip &k : spec.skips) stir::addSkip(f, k, nature);
	for (const craft::Craft &c : spec.crafts) craft::stirred(f, c, afloat(), nt);
	for (const adrift::Thing &th : spec.adrifts) adrift::stirred(f, th, nt, spec.rivers, spec.seas.empty() ? nullptr : &spec.seas[0]);
	for (const pondlife::Life &l : spec.lifes) pondlife::stirred(f, l, nt);
	if (!spec.seas.empty()) {   // who is in the water: rings from their legs as they wade, a wake as they swim
		const double level = spec.seas[0].level;
		for (const Character &c : spec.characters) {
			const auto kind = characterKinds().find(c.kind);
			if (c.extra || kind == characterKinds().end() || kind->second.aquatic) continue;
			const double H = kind->second.height * c.scale;
			Vec3 last;
			bool had = false, wasIn = false;
			for (double u = std::max(0.0, std::floor((t - 6.0) * 5.0) / 5.0); u <= t; u += 0.2) {
				Vec3 at;
				momentOf(c, u, &at);
				const double depth = level - at.y;
				const bool in = depth > 0.04 * H;
				const double way = had ? std::hypot(at.x - last.x, at.z - last.z) / 0.2 : 0.0;
				if (in && wasIn && way > 0.1) f.rings.push_back(stir::ringOf(-at.x, at.z, natureAt(u), 0.05 + 0.05 * H, 0.012 * std::min(1.0, way) * H));
				if (in && !wasIn && had) {
					f.rings.push_back(stir::ringOf(-at.x, at.z, natureAt(u), 0.16 * H, 0.05 * H, true));
					f.froth.push_back({-at.x, at.z, natureAt(u), 0.4 * H, 2.0});
				}
				last = at, had = true, wasIn = in;
			}
		}
	}
	f.trim(nt);
	return f;
}

std::string DataFilm::waterAt(const std::string &header, double t, double nt) const {
	std::string out;
	Vec3 eye, target;
	double fov = 40.0;
	film_subjects::cameraOf(header, eye, target, fov);
	const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
	const double aspect = 16.0 / 9.0, hfov = 2.0 * std::atan(std::tan(fov * kPi / 360.0) * aspect) * 180.0 / kPi;   // (a film's fov is its shorter side's)
	const stir::Field stirred = stirredAt(t, nt, eye);
	const bool painted = spec.toon.on && !spec.paint.off && !spec.hasUnderwater;   // (water drawn: film_toonwater.h)
	for (water::Sea sea : spec.seas) {
		if (painted) paintSea(sea);
		sea.glow = sea.glow * (1.0 / std::max(1.0, exposureAt(t)));   // (living light as bright on the screen however far the night camera has opened up)
		if (spec.hasUnderwater && undersea::waterlineOf(sea, nt, {-eye.x, eye.y, eye.z}).where == undersea::Waterline::At) sea.holeX = -eye.x, sea.holeZ = eye.z, sea.holeR = 1.5;   // (clear of a lens at the waterline)
		out += water::seaText(sea, nt, eye, target, hfov, ground.get(), spec.swell.on ? &spec.swell : nullptr, spec.hasUnderwater || painted ? nullptr : &stirred);
	}
	if (spec.swell.on && ground) out += surf::shallowsCausticText(spec.key.empty() ? std::string("film") : spec.key, nt, -eye.x, eye.z, *ground, spec.seas[0].level, spec.tracedCaustics);
	for (const water::River &r : spec.rivers) out += stream::text(spec.key.empty() ? std::string("film") : spec.key, r, nt, ground.get(), eye, target, hfov, spec.hasUnderwater ? nullptr : &stirred, painted ? &spec.paint.mid : nullptr);
	for (const water::Fall &f : spec.falls) out += f.frozen ? (ground ? std::string() : water::cliffText(f)) + ice::frozenFallText(f) : water::fallText(f, nt, ground.get());
	out += onTheWater(stirred, t, nt, eye, target, hfov);
	return out;
}

void DataFilm::paintSea(water::Sea &sea) const {
	toonwater::Paint p = spec.paint;
	if (!p.coloured && sea.kind == "lake") p.deep = {0.05, 0.3, 0.32}, p.mid = {0.12, 0.46, 0.42}, p.shallow = {0.45, 0.72, 0.56};
	sea.foam = true;
	if (spec.hasTerrain) {
		const std::shared_ptr<const terrain::Field> g = terrain::fieldOf(spec.terrain);
		const double size = (g->n - 1) * g->cell;
		sea.paintX = g->x0, sea.paintZ = g->z0, sea.paintSize = size;
		const std::string tex = "toon_sea";
		sea.painted = "Texture \"" + tex + "\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + toonwater::bandsPicture(spec.key.empty() ? std::string("film") : spec.key, *g, sea.level, p) + "\" ]\n  Material \"diffuse\" \"texture reflectance\" [ \"" + tex + "\" ]";
	} else {
		sea.painted = diffuseMaterial(p.mid);
	}
}

std::string DataFilm::onTheWater(const stir::Field &stirred, double t, double nt, const Vec3 &eye, const Vec3 &target, double hfov) const {
	std::string out;
	const std::string film = spec.key.empty() ? std::string("film") : spec.key;
	const double level = spec.seas.empty() ? (spec.rivers.empty() || spec.rivers[0].path.empty() ? 0.0 : spec.rivers[0].path[0].y) : spec.seas[0].level + spec.seas[0].riseAt(nt);
	const bool painted = spec.toon.on && !spec.paint.off && !spec.hasUnderwater;   // (as waterAt draws it)
	const auto surfaceAt = [&](double x, double z) { return spec.seas.empty() && !spec.rivers.empty() ? stream::heightAt(spec.rivers[0], x, z, nt) : level; };
	if (painted) {
		double elevation = 40.0, facing = 0.0;
		if (spec.hasSky) dressing::sunAt(spec.sky, dressing::hourAt(spec.sky, t, spec.storyEnd), elevation, facing);
		const Vec3 ahead = normalize(Vec3{-(target.x - eye.x), 0.0, target.z - eye.z} + Vec3{1e-9, 0.0, 0.0});
		out += toonwater::text(spec.paint, stirred, nt, {-eye.x, eye.y, eye.z}, ahead, hfov, level, spec.seas.empty() ? nullptr : &spec.seas[0], facing, elevation, surfaceAt);
	} else {
		out += stir::splashText(stirred, nt, surfaceAt);
	}
	if (spec.hasTerrain && !spec.seas.empty() && spec.seas[0].tidePeriod > 0.0) out += tideline::wetBandText(film, *terrain::fieldOf(spec.terrain), spec.seas[0].tideLow, spec.seas[0].tideHigh);
	const std::shared_ptr<const terrain::Field> land = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
	// (with no land under it: a bar of sand just over the still water, not half a metre under it, where a sandcastle was gone in its first 14 seconds)
	const double still = spec.seas.empty() ? level : spec.seas[0].level;
	const auto groundOf = [&](double x, double z) { return land && land->inside(x, z) ? land->heightAt(x, z) : still + 0.05; };
	for (const tideline::Castle &c : spec.castles) {
		const double y = groundOf(c.x, c.z);
		double since = 0.0;   // (how long the water has been up to it, all the film: a castle the tide has had does not stand again when it goes out)
		for (double u = 0.0; u < nt && !spec.seas.empty() && since < 14.0; u += 0.5)
			if (spec.seas[0].level + spec.seas[0].riseAt(u) > y - 0.02) since += 0.5;
		if (since < 14.0) out += tideline::castleText(c, y, tideline::slumped(since), nt);
	}
	for (const tideline::Pool &p : spec.pools) out += tideline::poolText(p, groundOf(p.x, p.z), level);
	double sunUp = 40.0, sunFacing = 0.0;   // (after dark a lighthouse's light turns and the lamps on a pier or a quay glow)
	if (spec.hasSky) dressing::sunAt(spec.sky, dressing::hourAt(spec.sky, t, spec.storyEnd), sunUp, sunFacing);
	const double dark = 1.0 - smoothstep(-6.0, 6.0, sunUp);
	for (const waterside::Place &p : spec.places) {
		double y = level;
		if (p.river >= 0 && p.river < static_cast<int>(spec.rivers.size())) y = stream::heightAt(spec.rivers[static_cast<std::size_t>(p.river)], p.x, p.z, nt);
		else if (spec.seas.empty() && !spec.rivers.empty()) y = stream::heightAt(spec.rivers[0], p.x, p.z, nt);
		const double onWater = y;   // (a mill's wheel stands in the water, whatever its house stands on)
		if (p.kind == "lighthouse" || p.kind == "harbour" || p.kind == "watermill") y = std::max(y, groundOf(p.x, p.z));
		out += waterside::staticText(p, y, nt, dark);
		if (p.kind == "watermill") {
			const double flow = p.river >= 0 && p.river < static_cast<int>(spec.rivers.size()) ? spec.rivers[static_cast<std::size_t>(p.river)].speed : 1.2;
			out += waterside::detail::millWheel(p, onWater, nt, flow);
		}
		if (p.kind == "lighthouse") out += waterside::lanternText(p, y, nt, dark);
	}
	for (const stir::Skip &k : spec.skips) out += stir::skipText(k, t, level);
	for (const adrift::Thing &th : spec.adrifts) out += adrift::text(th, nt, spec.rivers, spec.seas.empty() ? nullptr : &spec.seas[0]);
	for (const craft::Craft &c : spec.crafts) {
		bool rowed = false;
		std::vector<double> paddlers;
		const craft::Shape h = craft::shapeOf(c.kind, c.length, c.colour);
		for (const Spec::Aboard &a : spec.aboards)
			if (a.boat == c.name && a.pulls) {
				if (c.kind == "rowboat") rowed = true;
				else paddlers.push_back(craft::seatOf(h, c.kind, a.seat, true).at.z);
			}
		out += craft::craftText(c, afloat(), nt, rowed, paddlers);
	}
	const auto levelAt = [&](double x, double z) { return spec.seas.empty() && !spec.rivers.empty() ? stream::heightAt(spec.rivers[0], x, z, nt) : level; };
	if (spec.bank.on) {   // what grows by the water, parted by what goes through it
		std::vector<bankside::Mover> movers;
		for (const craft::Craft &c : spec.crafts) {
			const craft::Shape h = craft::shapeOf(c.kind, c.length, c.colour);
			const craft::Riding r = craft::ridingAt(c, h, afloat(), nt);
			for (double u = -0.4; u <= 0.41; u += 0.4) movers.push_back({r.at.x + r.ahead.x * u * h.length, r.at.z + r.ahead.z * u * h.length, 0.75 * h.beam + 0.3});
		}
		if (!spec.seas.empty())
			for (const Character &c : spec.characters) {
				Vec3 at;
				momentOf(c, t, &at);
				if (at.y < level) movers.push_back({-at.x, at.z, 0.7 * c.scale});
			}
		const Vec3 eyeP{-eye.x, eye.y, eye.z}, ahead = normalize(Vec3{-(target.x - eye.x), 0.0, target.z - eye.z} + Vec3{1e-9, 0.0, 0.0});
		const double wind = spec.seas.empty() ? 0.0 : spec.seas[0].wind * kPi / 180.0;
		const auto windAt = [&](double x, double z) {   // (pbrt's, as bankside wants it: the gusts are story space's, asked and answered mirrored)
			const Vec3 g = dressing::gustAt(spec.gusts, -x, z, nt);
			return Vec3{-g.x, 0.0, g.z} + Vec3{0.5 * std::sin(wind), 0.0, 0.5 * std::cos(wind)};
		};
		out += bankside::text(film, spec.bank, nt, eyeP, ahead, hfov, &stirred, movers, windAt, levelAt);
		if (spec.hasTerrain && !spec.seas.empty()) out += bankside::mudText(film, spec.bank, *terrain::fieldOf(spec.terrain), spec.seas[0].level);   // (along a sea's still line: a river's height is not one contour)
	}
	if (spec.waterMist.on) {
		const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
		const auto wet = [&](double x, double z) {
			if (!spec.seas.empty()) return !ground || !ground->inside(x, z) || ground->heightAt(x, z) < level;
			for (const water::River &r : spec.rivers)
				for (std::size_t i = 0; i < r.path.size(); i += 4)
					if (std::hypot(r.path[i].x - x, r.path[i].z - z) < 0.6 * r.width + 2.0) return true;
			return false;
		};
		out += bankside::mistText(film, spec.waterMist, t, spec.seas.empty() ? 0.0 : spec.seas[0].wind, {-eye.x, eye.y, eye.z}, wet, level);
	}
	for (const pondlife::Life &l : spec.lifes) out += pondlife::text(l, nt, levelAt);
	return out;
}

bool DataFilm::boatSeat(const Character &c, double t, Mat4 &frame, craft::Seat &seat, const craft::Craft *&boat, bool &pulls) const {
	for (const Spec::Aboard &a : spec.aboards) {
		if (a.who != c.name) continue;
		for (const craft::Craft &k : spec.crafts) {
			if (k.name != a.boat) continue;
			const craft::Shape h = craft::shapeOf(k.kind, k.length, k.colour);
			frame = craft::frameOf(craft::ridingAt(k, h, afloat(), natureAt(t)));
			seat = craft::seatOf(h, k.kind, a.seat, a.pulls && k.kind == "rowboat");
			boat = &k, pulls = a.pulls;
			return true;
		}
	}
	return false;
}

void DataFilm::waterMoment(const Character &c, double t, Vec3 &where, double &heading, double going, double &swim, bool &airborne, double &wet) const {
	if ((spec.hasUnderwater && spec.crafts.empty()) || (spec.aboards.empty() && spec.tumbles.empty() && spec.seas.empty())) return;
	const auto kind = characterKinds().find(c.kind);
	if (kind == characterKinds().end() || kind->second.aquatic) return;
	const double H = kind->second.height * c.scale, level = spec.seas.empty() ? 0.0 : spec.seas[0].level + spec.seas[0].riseAt(natureAt(t));
	const auto bob = [&] { return 0.012 * H * std::sin(2.0 * t + seedOf(c.name)); };
	const Spec::Tumble *fell = nullptr, *out = nullptr;   // (the last of each that has begun)
	for (const Spec::Tumble &f : spec.tumbles)
		if (f.who == c.name && t >= f.at) (f.out ? out : fell) = &f;
	const auto seatPlace = [&](double when, Vec3 &at, double &turned) {   // (where it was at `when`, in a boat or not)
		Mat4 frame;
		craft::Seat seat;
		const craft::Craft *boat = nullptr;
		bool pulls = false;
		if (!boatSeat(c, when, frame, seat, boat, pulls)) return false;
		Vec3 place = seat.at;
		if (pulls && boat->kind == "rowboat") place.z += 0.07 * std::sin(craft::strokeAt(natureAt(when)) + 1.57);   // (it leans into each stroke)
		at = transformPoint(frame, place);
		turned = rig::headingOf(transformPoint(frame, place + Vec3{0.0, 0.0, seat.faces}) - at);
		return true;
	};
	if (spec.hasUnderwater) {   // (round 18: a boat over an underwater film - who is in it sits in it; the rest is for a film above the water)
		Vec3 place;
		double turned = heading;
		if (seatPlace(t, place, turned)) where = place, heading = turned;
		return;
	}
	const bool fellLast = fell && (!out || fell->at > out->at);   // (a fall after it climbed out: in again, from where it climbed out to)
	if (fell) {   // over the side: in an arc, under, up again, treading water
		Vec3 from = where;
		double turned = heading;
		if (out && fellLast) from = story(out->x, out->y, out->z);
		else seatPlace(fell->at, from, turned);
		const Vec3 to = fell->placed ? story(fell->x, level, fell->z) : from + Vec3{std::sin(turned * kPi / 180.0 + 1.57), 0.0, std::cos(turned * kPi / 180.0 + 1.57)} * (0.9 * H);
		const double u = t - fell->at, flight = 0.55, treading = level - 0.72 * H;
		Vec3 p{lerp(from.x, to.x, std::min(1.0, u / flight)), 0.0, lerp(from.z, to.z, std::min(1.0, u / flight))};
		if (u < flight) p.y = lerp(from.y, level - 0.3 * H, u / flight) + 4.0 * 0.5 * H * (u / flight) * (1.0 - u / flight);
		else if (u < flight + 0.8) p.y = lerp(level - 0.3 * H, level - 1.25 * H, smoothstep(0.0, 0.8, u - flight));
		else p.y = lerp(level - 1.25 * H, treading, smoothstep(0.0, 1.1, u - flight - 0.8)) + bob();
		where = p, heading = turned, airborne = true, wet = 1.0;
		if (!out || fellLast) return;
	}
	if (out) {   // out of the water: heaved up onto the bank, and it stays there
		const Vec3 to = story(out->x, out->y, out->z);
		const double u = std::min(1.0, (t - out->at) / 1.1), w = smoothstep(0.0, 1.0, u);
		where = {lerp(where.x, to.x, w), lerp(where.y, to.y, w) + 0.25 * H * std::sin(kPi * u), lerp(where.z, to.z, w)};
		airborne = u < 1.0;
		wet = 1.0 - smoothstep(4.0, 30.0, t - out->at);
		return;
	}
	Vec3 place;
	double turned = heading;
	if (seatPlace(t, place, turned)) {
		where = place, heading = turned;
		return;
	}
	if (spec.seas.empty()) return;
	const double depth = level - where.y;
	if (depth <= 0.0) return;
	wet = smoothstep(0.1 * H, 0.5 * H, depth);
	if (depth > 0.66 * H) {   // out of its depth: it swims at the surface, or treads water
		where.y = level - lerp(0.72, 0.42, going) * H + bob();
		swim = going, airborne = true;
	}
}

void DataFilm::waterHands(const Character &c, double t, CharacterMoment &m) const {
	if (spec.aboards.empty()) return;
	Mat4 frame;
	craft::Seat seat;
	const craft::Craft *boat = nullptr;
	bool pulls = false;
	if (!boatSeat(c, t, frame, seat, boat, pulls) || !pulls) return;
	const craft::Shape h = craft::shapeOf(boat->kind, boat->length, boat->colour);
	const double nt = natureAt(t), way = craft::ridingAt(*boat, h, afloat(), nt).way;
	if (boat->kind == "rowboat") {
		for (int side = -1; side <= 1; side += 2) {
			const craft::Oar o = craft::oarAt(h, side, craft::strokeAt(nt), way > 0.05);
			m.hands[side < 0 ? 1 : 0] = {transformPoint(frame, o.handle), 1.0};   // (facing astern: her port oar in its right hand)
		}
	} else if (boat->kind == "canoe") {
		const craft::Paddle p = craft::paddleAt(h, seat.at.z, way > 0.05 ? craft::strokeAt(nt + 0.3 * seat.at.z) : 0.0);
		m.hands[1] = {transformPoint(frame, p.top), 1.0};
		m.hands[0] = {transformPoint(frame, p.lower), 1.0};
	}
}

void DataFilm::waterSounds(audio::Mix &m) const {
	bool changing = false;
	for (const water::River &r : spec.rivers) changing = changing || !r.bores.empty();
	if (!spec.seas.empty()) changing = changing || !spec.seas[0].bends.empty();
	if (spec.plops.empty() && spec.skips.empty() && spec.crafts.empty() && spec.lifes.empty() && !changing) return;
	namespace ss = soundscape;
	const ss::Ears ears = ss::earsOf(m.seconds(), [this](double t, Vec3 &eye, Vec3 &target) {
		double fov = 40.0;
		return film_subjects::cameraOf(headerAt(inCredits(t) ? std::max(0.0, spec.storyEnd - 1e-3) : t), eye, target, fov);
	});
	const double level = spec.seas.empty() ? 0.0 : spec.seas[0].level;
	const auto plop = [&](double t, double x, double z, double size, unsigned seed) {   // a plop: the note of the bubble it traps, under the burst of the touch
		const double loud = std::min(1.2, 0.25 + 3.0 * size);
		ss::place(m, ears, t, {x, level, z}, audio::noiseBurst(0.09 + 0.5 * size, 0.002, 0.08 + 0.4 * size, 500.0, 4200.0, seed), 0.8 * loud, 30.0);
		ss::place(m, ears, t + 0.02, {x, level, z}, audio::slide(420.0 * std::pow(0.05 / std::max(0.01, size), 0.5), 900.0 * std::pow(0.05 / std::max(0.01, size), 0.5), 0.12, 9.0), 0.5 * loud, 30.0);
	};
	unsigned seed = 7001u;
	for (const stir::Plop &p : spec.plops) plop(p.at, p.x, p.z, p.size, seed += 13u);
	for (const stir::Skip &k : spec.skips) {
		const std::vector<stir::Hop> hops = stir::hopsOf(k);
		for (std::size_t n = 0; n < hops.size(); ++n) plop(hops[n].t, hops[n].x, hops[n].z, n + 1 == hops.size() ? 0.05 : 0.015, seed += 13u);
	}
	const double T = m.seconds(), natureEnd = natureAt(T);
	// (a whirlpool's, a great wave's, a bore's and the animals' times are nature's, as the water they stir goes by: each is heard at the film time it is seen)
	if (!spec.seas.empty())   // a whirlpool's roar, a great wave coming
		for (const water::Sea::Bend &b : spec.seas[0].bends) {
			if (b.kind == 0) {
				for (double t = std::max(0.0, b.t0); t < std::min(natureEnd, b.t1); t += 1.6) ss::place(m, ears, filmTimeOf(t), {b.x, level, b.z}, audio::noiseBurst(2.2, 0.4, 0.8, 120.0, 900.0, seed += 13u), 0.9, 2.5 * b.radius);
			} else {
				const double a = b.facing * kPi / 180.0;
				for (double t = std::max(0.0, b.t0); t < std::min(natureEnd, b.t0 + 40.0); t += 1.2) {
					const double d = b.speed * (t - b.t0);
					ss::place(m, ears, filmTimeOf(t), {b.x + std::sin(a) * d, level, b.z + std::cos(a) * d}, audio::noiseBurst(1.8, 0.3, 0.8, 90.0, 1600.0, seed += 13u), 1.3, 300.0);
				}
			}
		}
	for (const water::River &r : spec.rivers)
		for (const water::River::Bore &b : r.bores)
			for (double t = b.t; t < std::min(natureEnd, b.t + 16.0); t += 1.0) {
				const Vec3 at = stream::placeAt(r, b.from + (r.speed + std::sqrt(19.6 * b.height)) * (t - b.t)).at;
				ss::place(m, ears, filmTimeOf(t), at, audio::noiseBurst(1.5, 0.2, 0.7, 150.0, 2400.0, seed += 13u), 1.0, 120.0);
			}
	for (const pondlife::Life &l : spec.lifes) {   // what the water's animals do, and their voices
		for (const pondlife::Touch &q : l.touches) {
			if (q.kind == 1) ss::place(m, ears, filmTimeOf(q.t), {q.x, level, q.z}, audio::noiseBurst(0.09, 0.001, 0.08, 900.0, 5000.0, seed += 13u), 1.2, 60.0);
			plop(filmTimeOf(q.t), q.x, q.z, q.size, seed += 13u);
		}
		const auto voice = [&](double t, Vec3 at, const std::vector<float> &sound, double gain, double within) { at.y = level + 0.2; ss::place(m, ears, t, at, sound, gain, within); };
		if (l.kind == "ducks")
			for (double t = 2.0 + 4.0 * hash01(l.seed, 21), g = 0; t < T; t += 3.0 + 9.0 * hash01(l.seed, static_cast<int>(g) + 22), ++g) {
				const Vec3 at = pondlife::detail::wanderAt(l, 0, natureAt(t));   // (where the drake swims then: in nature's time, as `text` draws him)
				for (int q = 0; q < 2 + static_cast<int>(g) % 3; ++q) voice(t + 0.17 * q, at, audio::chirp(380.0, 240.0, 0.13, 26.0, 0.07, 0.9), 0.4, 25.0);
			}
		if (l.kind == "frogs")
			for (double t = 1.0 + 3.0 * hash01(l.seed, 21), g = 0; t < T; t += 1.5 + 4.0 * hash01(l.seed, static_cast<int>(g) + 22), ++g)
				for (int q = 0; q < 2; ++q) voice(t + 0.24 * q, {l.x, 0.0, l.z}, audio::chirp(170.0, 115.0, 0.2, 30.0, 0.12, 1.0), 0.35, 25.0);
		if (l.kind == "kingfisher")
			for (double t = 2.0, g = 0; t < T; t += 6.0 + 8.0 * hash01(l.seed, static_cast<int>(g) + 22), ++g) voice(t, {l.x, 0.0, l.z}, audio::chirp(3100.0, 3600.0, 0.09, 0.0, 0.0, 1.0), 0.14, 35.0);
	}
	for (const craft::Craft &c : spec.crafts) {   // what a boat sounds like, from where she is
		const craft::Shape h = craft::shapeOf(c.kind, c.length, c.colour);
		const double U = c.speed > 0.0 ? c.speed : h.speed;
		const auto at = [&](double t) { Vec3 p = craft::placeAt(c, craft::alongAt(c, U, natureAt(t))); p.y = level + 0.3; return p; };
		const bool rowed = std::any_of(spec.aboards.begin(), spec.aboards.end(), [&c](const Spec::Aboard &a) { return a.boat == c.name && a.pulls; });
		if (rowed || c.path.size() >= 2) {
			for (double t = 0.25 / craft::kStrokeRate; t < T; t += 1.0 / craft::kStrokeRate) {   // a dip and a creak at each stroke
				if (c.path.size() < 2 || craft::alongAt(c, U, natureAt(t + 0.1)) - craft::alongAt(c, U, natureAt(t)) < 0.005) continue;
				if (c.kind == "rowboat" || c.kind == "canoe") {
					ss::place(m, ears, t, at(t), audio::noiseBurst(0.3, 0.03, 0.24, 500.0, 2400.0, seed += 13u), 0.32, 18.0);
					if (c.kind == "rowboat") ss::place(m, ears, t + 0.6, at(t), audio::chirp(190.0, 150.0, 0.16, 0.0, 0.02, 0.1), 0.05, 12.0);
				}
			}
		}
		if (c.kind == "fishing" || c.kind == "ferry")   // an engine: a low beat, a second at a time, louder near
			for (double t = 0.0; t < T; t += 1.0) ss::place(m, ears, t, at(t), audio::noiseBurst(1.1, 0.05, 0.05, 40.0, 180.0, seed += 13u), 0.5, c.kind == "ferry" ? 120.0 : 60.0);
		if (c.kind == "sailboat")
			for (double t = 0.0; t < T; t += 2.0) ss::place(m, ears, t, at(t), audio::noiseBurst(2.2, 0.5, 0.8, 300.0, 1500.0, seed += 13u), 0.16, 30.0);
	}
}

}  // namespace film_file
}  // namespace cartoon
