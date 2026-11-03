// film_seaworld.cpp -- under the sea, round 18: the part of a frame, a character's moment and the sound the round added (film_flow.h, film_subs.h and those after it).

#include "film_file.h"

namespace cartoon {
namespace film_file {

namespace {
Vec3 pbrtOf(Vec3 p) { return {-p.x, p.y, p.z}; }   // (story space to pbrt's and back: x mirrored)
}  // namespace

std::string DataFilm::deepAt(const std::string &header, double t, double nt) const {
	if (!spec.seatoon.any() && spec.subs.empty() && !spec.air18.any() && spec.towns.empty() && spec.lanes.empty() && !spec.abyss.any() && !spec.above.any()) return std::string();
	std::string out;
	Vec3 eye, target;
	double fov = 40.0;
	film_subjects::cameraOf(header, eye, target, fov);
	const Vec3 eyeP = pbrtOf(eye), targetP = pbrtOf(target);
	std::vector<undersea::Bubble> bubbles;
	{   // (in any film: a cartoon's wipe, of round bubbles)
		std::vector<undersea::Bubble> wall;
		for (const double w : spec.seatoon.wipes) seatoon::wipeBubbles(w, t, eyeP, targetP, fov, wall);
		out += seatoon::roundBubbles(wall);
	}
	if (spec.hasUnderwater && !spec.seas.empty()) {
		const double level = spec.seas[0].level;
		const underwater::Current &current = spec.current;
		const auto bedAt = [this](double x, double z) { return seabedAt(x, z) + 0.04; };   // (on the caustics' decal, as the other sea lines stand)
		for (const subs::Sub &s : spec.subs) {
			out += subs::subText(s, nt, level);
			subs::bubbles(s, nt, level, current, bubbles);
		}
		for (const airpocket::Bell &b : spec.air18.bells) out += airpocket::bellText(b, nt, level, current, bubbles);
		for (const airpocket::Cave &v : spec.air18.caves) out += airpocket::caveText(v, nt, seabedAt(v.at.x, v.at.z));
		for (const airpocket::Big &b : spec.air18.bigs) out += airpocket::bigText(b, nt, level, current);
		const auto mouthOf = [&](const Character &c, double when, double &size) {   // (pbrt's: a little in front of its head)
			const CharacterKind &kind = characterKinds().at(c.kind);
			const CharacterMoment m = momentOf(c, when);
			size = kind.height * c.scale;
			const Mat4 body = mul(m.frame, scale(c.scale), kind.lean ? kind.lean(m) : identity());
			return pbrtOf(transformPoint(body, kind.head + Vec3{0.0, -0.04 * kind.height, 0.08 * kind.height}));
		};
		for (const airpocket::Ring &r : spec.air18.rings)
			if (const Character *c = character(r.who); c && t >= r.t) {
				double size = 1.0;
				undersea::Ring ring;
				ring.at = mouthOf(*c, r.t, size), ring.t = natureAt(r.t), ring.seed = seedOf(r.who);
				out += undersea::ringText(ring, nt, level, current);
			}
		for (const seatoon::Gulp &g : spec.seatoon.gulps)   // a gulp: a big bubble out of its mouth
			if (const Character *c = character(g.who); c && t >= g.t) {
				double size = 1.0;
				airpocket::Big b;
				b.at = mouthOf(*c, g.t, size), b.radius = std::clamp(0.06 * size, 0.02, 0.4), b.t = natureAt(g.t), b.wobble = 2.0, b.seed = seedOf(g.who);
				out += airpocket::bigText(b, nt, level, current);
			}
		for (const seatown::Town &w : spec.towns) out += seatown::townText(w, nt, bedAt);
		for (const fastlane::Lane &l : spec.lanes) out += fastlane::laneText(l, nt);
		for (const abyss::Kraken &k : spec.abyss.krakens) out += abyss::krakenText(k, nt);
		for (const abyss::Squid &q : spec.abyss.squids) out += abyss::squidText(q, nt);
		for (const abyss::Serpent &s : spec.abyss.serpents) out += abyss::serpentText(s, nt);
		for (const abyss::Eye &e : spec.abyss.eyes) out += abyss::eyeText(e, nt);
		for (const abyss::Reveal &r : spec.abyss.reveals) out += abyss::revealText(r, nt);
		for (const abyss::WhaleFall &w : spec.abyss.falls) out += abyss::whaleFallText(w, nt, bedAt);
		for (const fromabove::Hook &h : spec.above.hooks) out += fromabove::hookText(h, nt, level, seabedAt(h.x, h.z));
		const auto netDepth = [&](const fromabove::Net &n) {   // (down to its school's middle when it was cast, else two thirds of the way down)
			if (n.school >= 1 && static_cast<std::size_t>(n.school) <= spec.schools.size()) {
				const school::School &sc = schoolsSwum()[static_cast<std::size_t>(n.school) - 1];
				Vec3 mid{0, 0, 0};
				for (int i = 0; i < sc.count; ++i) mid = mid + school::fishAt(sc, i, n.lift < 1e29 ? n.lift : n.at + 20.0);
				return std::max(1.0, level - mid.y / std::max(1, sc.count) - 0.3 * n.size);
			}
			return 0.66 * (level - seabedAt(n.x, n.z));
		};
		for (const fromabove::Net &n : spec.above.nets) out += fromabove::netText(n, nt, level, netDepth(n));
		for (const fromabove::Anchor &a : spec.above.anchors) out += fromabove::anchorText(a, nt, level, seabedAt(a.x, a.z));
		for (const fromabove::Anchor &a : spec.above.anchors) {   // (its landing throws up silt)
			double landed = 1e30;
			fromabove::anchorY(a, nt, level, seabedAt(a.x, a.z), landed);
			if (nt >= landed) out += undersea::siltText({a.x, bedAt(a.x, a.z), a.z}, nt - landed, 0.9, static_cast<int>(a.at * 31.0));
		}
		for (const fromabove::Brine &b : spec.above.brines) out += fromabove::brineText(b, nt, bedAt);
		for (const fromabove::Lava &l : spec.above.lavas) out += fromabove::lavaText(l, nt, bedAt, level, current, bubbles);
		for (const fromabove::Brinicle &b : spec.above.brinicles) out += fromabove::brinicleText(b, nt, level - spec.iceCap.thickness, bedAt);
		for (const fromabove::Slump &s : spec.above.slumps) out += fromabove::slumpText(s, nt, bedAt);
		if (spec.seatoon.rays.on) {
			double elevation = 60.0, facing = 180.0;
			if (spec.hasSky) dressing::sunAt(spec.sky, dressing::hourAt(spec.sky, t, spec.storyEnd), elevation, facing);
			out += seatoon::raysText(spec.key.empty() ? std::string("film") : spec.key, spec.seatoon.rays, nt, eyeP, level, facing, elevation);
		}
		if (spec.seatoon.sparkle.on) {   // what moves through the dark water: the characters, the subs, the serpents
			std::vector<std::vector<Vec3>> movers;
			const double tick = std::floor(t * 10.0) / 10.0;   // (their places on the tenths a spark is born on: from t itself, a spark slid with its mover and jumped back ten times a second)
			for (const Character &c : spec.characters) {
				std::vector<Vec3> path;
				for (int k = 0; k < 20; ++k) path.push_back(pbrtOf(chestOf(c, std::max(0.0, tick - 0.1 * k))));
				movers.push_back(path);
			}
			for (const subs::Sub &s : spec.subs) {
				std::vector<Vec3> path;
				for (int k = 0; k < 20; ++k) path.push_back(subs::subAt(s, natureAt(std::max(0.0, tick - 0.1 * k))));
				movers.push_back(path);
			}
			for (const abyss::Serpent &s : spec.abyss.serpents) {
				std::vector<Vec3> path;
				for (int k = 0; k < 20; ++k) path.push_back(abyss::serpentHead(s, natureAt(std::max(0.0, tick - 0.1 * k))));
				movers.push_back(path);
			}
			seatoon::Sparkle s = spec.seatoon.sparkle;   // (only in dark water: under a sky, as the sun goes down)
			if (spec.hasSky) {
				double elevation = 60.0, facing = 180.0;
				dressing::sunAt(spec.sky, dressing::hourAt(spec.sky, t, spec.storyEnd), elevation, facing);
				s.amount *= smoothstep(12.0, -4.0, elevation);
			}
			if (s.amount > 0.01) out += seatoon::sparkleText(s, movers, t);
		}
	}
	out += undersea::bubblesText(bubbles);
	return out;
}

void DataFilm::deepMoment(const Character &c, double t, Vec3 &where, double &heading, double &going, double &swim, bool &airborne) const {
	if (spec.riders.count(c.name))   // in a sub's cabin: on its seat, facing its bow
		for (const Spec::Aboard &a : spec.aboards)
			if (a.who == c.name)
				for (const subs::Sub &b : spec.subs)
					if (b.name == a.boat) {
						const double nt = natureAt(t);
						Vec3 f;
						subs::subAt(b, nt, &f);
						where = subs::seatOf(b, nt, a.seat), heading = rig::headingOf(pbrtOf(f)), airborne = true, swim = 0.0, going = 0.0;
						return;
					}
	for (const fastlane::Chase &ch : spec.chases) {   // following another's way, a little behind it
		if (ch.who != c.name || t < ch.from - 1.0 || t > ch.to + 1.0) continue;
		const Character *other = character(ch.after);
		if (!other) continue;
		Vec3 at, before;
		const CharacterMoment om = momentOf(*other, t - ch.lag, &at);
		momentOf(*other, t - ch.lag - 0.15, &before);
		const double w = (ch.from < -1e29 ? 1.0 : smoothstep(ch.from - 1.0, ch.from, t)) * (ch.to > 1e29 ? 1.0 : 1.0 - smoothstep(ch.to, ch.to + 1.0, t));
		where = lerp(where, at, w);
		if (w > 0.5 && std::hypot(at.x - before.x, at.z - before.z) > 1e-4) heading = rig::headingOf(at - before);
		going = lerp(going, om.going, w), swim = std::max(swim, om.swim * w), airborne = airborne || (w > 0.5 && om.airborne);
	}
	for (const fastlane::Ride &r : spec.rides) {   // carried along a fast lane
		if (r.who != c.name || t < r.from) continue;
		for (const fastlane::Lane &l : spec.lanes) {
			if (l.name != r.lane) continue;
			Vec3 at, way;
			if (!fastlane::riderAt(l, r, t, pbrtOf(where), at, way)) continue;
			where = pbrtOf(at), heading = rig::headingOf(pbrtOf(way)), going = 1.0, airborne = true;
			if (swimming(c)) swim = 1.0;
		}
	}
}

void DataFilm::flowInto(const Character &c, double t, CharacterMoment &m) const {
	flow::Wake &w = m.wake;
	w.frames.clear();
	w.frames.push_back(m.frame);
	for (int k = 1; k < flow::kWakeSteps; ++k) w.frames.push_back(momentOf(c, t - k * flow::kWakeStep).frame);
	w.scale = m.scale, w.t = t;
	const Vec3 at = transformPoint(m.frame, {0.0, 0.0, 0.0});
	w.water = pbrtOf(spec.current.velocity(natureAt(t)));
	const CharacterKind &kind = characterKinds().at(c.kind);
	w.wet = m.swim > 0.0 || (!spec.seas.empty() && at.y + 0.6 * kind.height * c.scale < spec.seas[0].level);
	w.wind = dressing::gustAt(spec.gusts, at.x, at.z, t);
}

std::string DataFilm::wornAt(const Character &c, const CharacterKind &kind, double t, const CharacterMoment &m, const Mat4 &turn) const {
	std::string out;
	const Mat4 body = mul(m.frame, scale(m.scale)), lean = kind.lean ? kind.lean(m) : identity();
	const Mat4 local = mul(invertAffine(body), turn, body, lean);   // (the kind's lean and a swimmer's laying level, in its body's space)
	int k = 0;
	for (const flow::Garment &g : spec.garments)
		if (g.who == c.name) out += flow::garmentText(g, kind.head, kind.chest, kind.height, m.wake, local, m.seed + 17 * k++);
	for (const airpocket::Helmet &h : spec.air18.helmets) {
		if (h.who != c.name) continue;
		const Mat4 place = mul(turn, body, lean);
		const Vec3 head = transformPoint(place, kind.head), fwd = normalize(transformPoint(place, kind.head + Vec3{0.0, 0.0, 1.0}) - head + Vec3{1e-9, 0.0, 0.0});
		out += airpocket::helmetText(head + Vec3{0.0, 0.01 * kind.height * c.scale, 0.0}, h.size > 0.0 ? 0.5 * h.size : std::max(0.11 * kind.height, 0.6 * kind.radius) * c.scale, fwd);
	}
	for (const seatoon::Lines &l : spec.seatoon.lines) {
		if (l.who != c.name || t < l.t0 || t > l.t1) continue;
		std::vector<Vec3> path;
		for (int q = 0; q < 10; ++q) path.push_back(chestOf(c, std::max(0.0, t - 0.06 * q)));
		const double speed = length(path[0] - path[1]) / 0.06;
		out += seatoon::speedLinesText(path, speed, 2.0 * kind.radius * c.scale, m.seed);
	}
	return out;
}

bool DataFilm::deepSubject(const std::string &who, double t, Vec3 &feet, Vec3 &front, Vec3 &chest) const {
	const double nt = natureAt(t);
	for (const subs::Sub &s : spec.subs)
		if (s.name == who) {
			Vec3 f;
			const Vec3 at = subs::subAt(s, nt, &f);
			feet = pbrtOf(at), front = pbrtOf(f), chest = feet;
			return true;
		}
	for (const abyss::Serpent &s : spec.abyss.serpents)
		if (s.name == who) {
			Vec3 f;
			const Vec3 at = abyss::serpentHead(s, nt, &f);
			f.y = 0.0;
			feet = pbrtOf(at), front = pbrtOf(length(f) > 1e-6 ? normalize(f) : Vec3{0.0, 0.0, 1.0}), chest = feet;
			return true;
		}
	for (const fastlane::Lane &l : spec.lanes)   // (a lane: its middle, along it)
		if (l.name == who && l.line.size() >= 2) {
			const fastlane::Place p = fastlane::placeAt(l, 0.5 * l.total());
			feet = pbrtOf(p.at), front = pbrtOf(p.way), chest = feet;
			return true;
		}
	return false;
}

void DataFilm::seaShows(std::vector<school::School> &schools) const {
	const double level = spec.seas.empty() ? 0.0 : spec.seas[0].level;
	for (std::size_t i = 0; i < schools.size(); ++i) {
		shows::perform(schools[i], spec.shows, spec.songs, static_cast<int>(i));
		for (const fromabove::Net &n : spec.above.nets)
			if (n.school == static_cast<int>(i) + 1) {
				Vec3 mid{0, 0, 0};
				for (int f = 0; f < schools[i].count; ++f) mid = mid + school::fishAt(schools[i], f, n.lift < 1e29 ? n.lift : n.at + 20.0);
				fromabove::haul(schools[i], n, level, std::max(1.0, level - mid.y / std::max(1, schools[i].count) - 0.3 * n.size));
			}
	}
}

void DataFilm::deepSounds(audio::Mix &m) const {
	if (spec.subs.empty() && !spec.air18.any() && spec.towns.empty() && spec.lanes.empty() && spec.songs.empty() && !spec.abyss.any() && !spec.above.any() && spec.seatoon.wipes.empty() &&
		spec.seatoon.gulps.empty() && spec.shows.empty())
		return;
	namespace ss = soundscape;
	const ss::Ears ears = ss::earsOf(m.seconds(), [this](double t, Vec3 &eye, Vec3 &target) {
		double fov = 40.0;
		return film_subjects::cameraOf(headerAt(inCredits(t) ? std::max(0.0, spec.storyEnd - 1e-3) : t), eye, target, fov);
	});
	const double T = m.seconds(), level = spec.seas.empty() ? 0.0 : spec.seas[0].level;
	const auto bed = [this](double x, double z) { return seabedAt(x, z); };
	unsigned seed = 18001u;
	for (std::size_t i = 0; i < spec.subs.size(); ++i) {   // a sub: its motor's hum, a sonar ping now and then, its ballast blowing
		const subs::Sub &s = spec.subs[i];
		ss::lay(m, ears, 0.0, T, 0.5, [&s, this](double t) { return subs::subAt(s, natureAt(t)); }, [&s, this](double t) { return 0.25 + 0.5 * std::min(1.0, subs::paceAt(s, natureAt(t))); },
				ss::roar(70.0, 420.0, 1.0, 18101u + static_cast<unsigned>(i)), 25.0);
		for (double t = 3.0 + 2.0 * i; t < T; t += 7.0) {
			const Vec3 at = subs::subAt(s, natureAt(t));
			ss::place(m, ears, t, at, audio::chirp(1480.0, 1460.0, 0.5, 0.0, 0.0, 1.0), 0.18, 60.0);
			ss::place(m, ears, t + 0.6, at, audio::chirp(1480.0, 1460.0, 0.4, 0.0, 0.0, 1.0), 0.05, 60.0);   // (its echo)
		}
		std::vector<double> bursts = s.dives;
		bursts.insert(bursts.end(), s.rises.begin(), s.rises.end());
		for (const double when : bursts)   // (a dive's and a rise's times are nature's, as all of this but a character's own gulps and rings: heard at the film time they are seen)
			for (int k = 0; k < 14; ++k) ss::place(m, ears, filmTimeOf(when) + 0.1 * k, subs::subAt(s, when), ss::bubble(seed += 13u), 0.6, 30.0);
	}
	for (const airpocket::Big &b : spec.air18.bigs) ss::place(m, ears, filmTimeOf(b.t), b.at, audio::slide(150.0 / std::max(0.2, std::sqrt(b.radius)), 70.0 / std::max(0.2, std::sqrt(b.radius)), 0.6, 4.0), 0.5, 25.0);
	for (const seatoon::Gulp &g : spec.seatoon.gulps) {   // a gulp: a boing down, the bubble's bloop
		const Character *c = character(g.who);
		const Vec3 at = c ? pbrtOf(chestOf(*c, g.t)) : Vec3{0, 0, 0};
		ss::place(m, ears, g.t, at, audio::boing(180.0, 0.45), 0.6, 15.0);
		ss::place(m, ears, g.t + 0.25, at, audio::slide(260.0, 120.0, 0.4, 6.0), 0.5, 15.0);
	}
	for (const airpocket::Ring &r : spec.air18.rings) {
		const Character *c = character(r.who);
		ss::place(m, ears, r.t, c ? pbrtOf(chestOf(*c, r.t)) : Vec3{0, 0, 0}, audio::noiseBurst(0.35, 0.02, 0.3, 300.0, 1200.0, seed += 13u), 0.35, 15.0);
	}
	for (const double w : spec.seatoon.wipes) {   // a wipe: the rush of a wall of bubbles past the lens
		const Vec3 at = ears(w).eye;
		ss::place(m, ears, w - 0.7, at, audio::noiseBurst(1.5, 0.4, 1.0, 400.0, 3000.0, seed += 13u), 0.7, 5.0);
		for (int k = 0; k < 18; ++k) ss::place(m, ears, w - 0.5 + 0.06 * k, at, ss::bubble(seed += 13u), 0.4, 5.0);
	}
	for (const seatown::Town &w : spec.towns) {   // a town: the market's chatter, the signal's ding as it turns
		const Vec3 plaza{w.x, bed(w.x, w.z) + 1.5, w.z};
		for (double t = 0.4; t < T; t += 0.35 + 0.5 * audio::unit(seed += 13u)) ss::place(m, ears, t, plaza, audio::chirp(500.0 + 600.0 * audio::unit(seed + 1u), 400.0 + 700.0 * audio::unit(seed + 2u), 0.12, 20.0, 0.1, 1.0), 0.12, 20.0);
		for (double nt = 6.0, t = filmTimeOf(nt); t < T; nt += 6.0, t = filmTimeOf(nt))   // (the signal turns in nature's time, as its light does: its ding when it is seen to turn)
			ss::place(m, ears, t, plaza + Vec3{0.0, 1.0, 0.0}, audio::bell(std::fmod(nt, 12.0) < 1.0 ? 1320.0 : 990.0, 1.2), 0.15, 20.0);
	}
	for (std::size_t i = 0; i < spec.lanes.size(); ++i) {   // a fast lane: its roar, from where it passes nearest
		const fastlane::Lane &l = spec.lanes[i];
		ss::lay(m, ears, 0.0, T, 0.5, [&l, &ears](double t) {
			const Vec3 e = ears(t).eye;
			Vec3 best = l.line.empty() ? e : l.line[0];
			for (const Vec3 &p : l.line)
				if (length(p - e) < length(best - e)) best = p;
			return best;
		}, [](double) { return 0.6; }, ss::roar(120.0, 1800.0, 1.0, 18201u + static_cast<unsigned>(i)), 15.0);
	}
	for (const shows::Song &sg : spec.songs) {
		if (sg.school > 0 && static_cast<std::size_t>(sg.school) <= spec.schools.size()) {   // a fish choir: a slow chord of voices, phrase by phrase
			const school::School &sc = schoolsSwum()[static_cast<std::size_t>(sg.school) - 1];
			const double chord[4][3] = {{0, 4, 7}, {5, 9, 12}, {7, 11, 14}, {0, 4, 7}};
			for (int p = 0; p * 2.5 < sg.length - 0.5; ++p) {
				const double nt = sg.at + 2.5 * p;   // (nature's, as the school breathes it: shows::perform)
				Vec3 mid{0, 0, 0};
				int heard = 0;
				for (int f = 0; f < sc.count; f += std::max(1, sc.count / 40), ++heard) mid = mid + school::fishAt(sc, f, nt);
				mid = mid * (1.0 / std::max(1, heard));
				for (int v = 0; v < 3; ++v) {
					const double hz = 440.0 * std::pow(2.0, (sg.key + chord[p % 4][v] - 69.0) / 12.0);
					ss::place(m, ears, filmTimeOf(nt), mid, ss::voiced(hz, hz * 1.003, 2.6, 400.0 + 120.0 * v, 900.0, 0.08, 0.25, seed += 13u), 0.22, 40.0);
				}
			}
		} else {   // a whale's song from far off: long swoops and moans
			Vec3 from = sg.placed ? sg.from : Vec3{0.0, level - 30.0, 300.0};
			if (!sg.placed)
				for (const school::BigFish &b : spec.bigFish)
					if (b.kind == "humpbacks") {
						from = b.at + Vec3{0.0, bed(b.at.x, b.at.z), 0.0};
						break;
					}
			for (double t = sg.at; t < std::min(T, sg.at + sg.length); t += 3.5 + 2.5 * audio::unit(seed += 13u)) {
				const double f0 = 120.0 + 300.0 * audio::unit(seed + 1u), f1 = 70.0 + 380.0 * audio::unit(seed + 2u);
				ss::place(m, ears, t, from, audio::chirp(f0, f1, 1.8 + 1.6 * audio::unit(seed + 3u), 3.0, 0.03, 1.0), 0.4, 3000.0);
			}
		}
	}
	for (const shows::Show &sh : spec.shows)   // a school bursting: a silver whoosh
		if (sh.shape == "burst" && static_cast<std::size_t>(sh.school) <= spec.schools.size() && sh.school >= 1) {
			const school::School &sc = schoolsSwum()[static_cast<std::size_t>(sh.school) - 1];
			ss::place(m, ears, filmTimeOf(sh.at), school::fishAt(sc, 0, sh.at), audio::noiseBurst(0.9, 0.01, 0.8, 800.0, 5000.0, seed += 13u), 0.6, 30.0);
		}
	for (const abyss::Kraken &k : spec.abyss.krakens) {   // the giants: a kraken's groan, a serpent's rumble, an eye opening
		for (double t = filmTimeOf(k.rise >= 0.0 ? k.rise : 2.0); t < T; t += 9.0 + 4.0 * audio::unit(seed += 13u)) ss::place(m, ears, t, k.at, ss::voiced(48.0, 36.0, 3.5, 180.0, 420.0, 0.8, 0.3, seed), 1.0, 200.0);
	}
	for (const abyss::Serpent &s : spec.abyss.serpents)
		for (double t = filmTimeOf(s.from + 1.0); t < T; t += 6.0 + 3.0 * audio::unit(seed += 13u)) ss::place(m, ears, t, abyss::serpentHead(s, natureAt(t)), ss::voiced(70.0, 55.0, 2.0, 250.0, 600.0, 0.9, 0.5, seed), 0.7, 120.0);
	for (const abyss::Eye &e : spec.abyss.eyes) ss::place(m, ears, filmTimeOf(e.open), e.at, audio::slide(55.0, 42.0, 3.0, 1.0), 0.9, 120.0);
	for (const abyss::Squid &q : spec.abyss.squids)
		for (double t = std::max(0.0, filmTimeOf(q.from)); t < T; t += 3.0) ss::place(m, ears, t, q.at, audio::noiseBurst(0.8, 0.05, 0.7, 100.0, 600.0, seed += 13u), 0.5, 80.0);   // (each jet, three film seconds apart: stepped in nature's, a timelapse piled them up)
	for (const fromabove::Hook &h : spec.above.hooks) {   // the hook's plop as it goes in, the reel's whirr as it is snatched up
		ss::place(m, ears, filmTimeOf(h.at), {h.x, level, h.z}, audio::slide(700.0, 300.0, 0.15, 9.0), 0.4, 25.0);
		if (h.yank < 1e29) ss::place(m, ears, filmTimeOf(h.yank), {h.x, level, h.z}, audio::noiseBurst(1.2, 0.02, 1.0, 2000.0, 6000.0, seed += 13u), 0.35, 25.0);
	}
	for (const fromabove::Net &n : spec.above.nets) ss::place(m, ears, filmTimeOf(n.at), {n.x, level, n.z}, ss::crash(seed += 13u), 0.5, 40.0);
	const double natureEnd = natureAt(T);
	for (const fromabove::Anchor &a : spec.above.anchors) {   // the splash, the chain rattling out, the thud on the bottom
		double landed = 1e30;
		fromabove::anchorY(a, a.at + 100.0, level, bed(a.x, a.z), landed);
		ss::place(m, ears, filmTimeOf(a.at), {a.x, level, a.z}, ss::crash(seed += 13u), 0.8, 40.0);
		for (double t = filmTimeOf(a.at + 0.2), end = std::min(T, filmTimeOf(landed)); t < end; t += 0.09) ss::place(m, ears, t, {a.x, level - 1.0, a.z}, audio::knock(900.0 + 300.0 * audio::unit(seed += 13u), 0.05, seed), 0.25, 30.0);
		if (landed < natureEnd) ss::place(m, ears, filmTimeOf(landed), {a.x, bed(a.x, a.z), a.z}, audio::noiseBurst(1.0, 0.005, 0.9, 60.0, 400.0, seed += 13u), 1.0, 40.0);
	}
	for (const fromabove::Lava &l : spec.above.lavas)   // pillow lava hissing and cracking
		for (double t = 0.5; t < T; t += 0.6 + 0.8 * audio::unit(seed += 13u)) ss::place(m, ears, t, {l.x, bed(l.x, l.z) + 0.5, l.z}, audio::noiseBurst(0.5, 0.01, 0.4, 2000.0, 7000.0, seed), 0.25, 25.0);
	for (const fromabove::Slump &s : spec.above.slumps) ss::place(m, ears, filmTimeOf(s.at), {s.x, bed(s.x, s.z), s.z}, audio::noiseBurst(6.0, 0.6, 5.0, 40.0, 500.0, seed += 13u), 1.2, 150.0);
	for (const fromabove::Brinicle &b : spec.above.brinicles)
		for (double t = filmTimeOf(b.at), end = std::min(T, filmTimeOf(b.at + 20.0)); t < end; t += 0.7) ss::place(m, ears, t, {b.x, level - spec.iceCap.thickness - 2.0, b.z}, audio::knock(2400.0 + 800.0 * audio::unit(seed += 13u), 0.04, seed), 0.12, 20.0);
}

}  // namespace film_file
}  // namespace cartoon
