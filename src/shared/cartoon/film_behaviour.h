#pragma once
// film_behaviour.h -- round 16: set pieces of behaviour under the sea.
//
//   cleaningstation X Y Z [seed K]
//            a cleaning station: a big grouper hanging still over a coral head, head tilted up, mouth open and fins spread, while three small cleaner wrasse (blue, a
//            black stripe) dart over it, in at its mouth and out by its gills; other fish wait their turn circling nearby, and every ten seconds the next one comes in
//   tornado N at X Y Z [around NAME] [radius R] [height H] [seed K]
//            a barracuda tornado: a school spiralling in a tall column, all one way round, slowly rising and sinking - round a point, or round a diver (`around NAME`)
//   playmate X Y Z [with NAME] [seed K]
//            a playful sea lion: looping round a diver (`with NAME`, else round the point) a metre or three away, rolling as it turns, darting at their fins and away,
//            and every few seconds blowing a burst of bubbles at their face
//   damselfish N at X Y Z [radius R] [seed K]
//            territorial damselfish, each guarding its own spot near the coral: they dart at anything that comes within a metre and a half - the camera, a diver - and
//            back to their spot
//
// Coordinates: pbrt's in the film file (heights above the seabed, as round 14's lines); meshes in story space (x mirrored).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_deepsea.h"
#include "film_school.h"
#include "film_sealife.h"
#include "film_undersea.h"
#include "nature_lib.h"

namespace cartoon {
namespace behaviour {

struct Station {
	Vec3 at;
	int seed = 1;
};
struct Tornado {
	Vec3 at;
	int count = 120, seed = 1;
	double radius = 3.0, height = 6.0;
	std::string around;   // (a diver's name, or "")
};
struct Playmate {
	Vec3 at;
	std::string with;
	int seed = 1;
};
struct Damsels {
	Vec3 at;
	int count = 5, seed = 1;
	double radius = 1.5;
};
struct World {
	std::vector<Station> stations;
	std::vector<Tornado> tornadoes;
	std::vector<Playmate> playmates;
	std::vector<Damsels> damsels;
	bool any() const { return !stations.empty() || !tornadoes.empty() || !playmates.empty() || !damsels.empty(); }
};

namespace detail {
using sealife::detail::facingOf;
using sealife::detail::story;
inline Mat4 placed(Vec3 at, Vec3 fwd, double roll = 0.0, double s = 1.0) { return mul(translate(story(at)), facingOf(story(fwd)), rotate({0, 0, 1}, roll), scale(s)); }
inline int poseAt(double t, double beats, int salt) { return static_cast<int>(std::floor((t * beats + hash01(salt, 9)) * sealife::detail::kPoses)) % sealife::detail::kPoses; }
}  // namespace detail

// ---- the cleaning station ----
inline std::string stationText(const Station &st, double t, const std::function<double(double, double)> &groundAt) {
	std::string out = "# a cleaning station (film_behaviour.h)\n";
	const Vec3 rock{st.at.x, groundAt(st.at.x, st.at.z), st.at.z};
	out += attr(coated({0.45, 0.4, 0.33}, 0.7), emitMesh(nature::rock(0.9, st.seed), translate(detail::story(rock))));
	// the clients: one at a time over the rock, the rest circling nearby; each turn ten seconds
	const int clients = 4, turn = static_cast<int>(std::floor(t / 10.0));
	const double inTurn = std::fmod(t, 10.0);
	Mesh grouper, mouth;
	for (int c = 0; c < clients; ++c) {
		const bool cleaned = (turn % clients) == c;
		const double a = 0.25 * t + 2.0 * kPi * c / clients;
		Vec3 at = st.at + Vec3{2.2 * std::cos(a), 0.5 + 0.3 * std::sin(0.7 * t + c), 2.2 * std::sin(a)};
		Vec3 fwd{-std::sin(a), 0.0, std::cos(a)};
		double gape = 0.0;
		if (cleaned) {   // (swims in over the rock, hangs there tilted, mouth open, then swims off)
			const double in = smoothstep(0.0, 1.5, inTurn) * (1.0 - smoothstep(8.5, 10.0, inTurn));
			at = lerp(at, st.at + Vec3{0.0, 0.9, 0.0}, in);
			fwd = normalize(lerp(fwd, Vec3{std::cos(st.seed * 1.0), 0.35, std::sin(st.seed * 1.0)}, in));
			gape = in;
		}
		const double L = cleaned ? 0.8 : 0.45 + 0.2 * hash01(c, st.seed);
		const int pose = detail::poseAt(t, cleaned ? 0.5 : 2.0, c + st.seed);
		Mesh body = sealife::detail::fish(pose, L, 1.4);
		for (const double side : {-1.0, 1.0}) sealife::detail::fin(body, {side * 0.07 * L, -0.02 * L, 0.12 * L}, {side * (0.15 + 0.1 * gape) * L, -0.08 * L, 0.0}, {side * 0.07 * L, -0.03 * L, 0.0});
		deepsea::detail::add(grouper, body, detail::placed(at, fwd));
		if (gape > 0.05) deepsea::detail::add(mouth, uvSphere(1.0, 10, 6), mul(detail::placed(at, fwd), translate(0.0, -0.02 * L, 0.47 * L), scale(0.05 * L, 0.05 * L * gape, 0.02 * L)));
		if (!cleaned) continue;
		for (int w = 0; w < 3; ++w) {   // the cleaner wrasse working over it: quick loops along its flanks, in at the mouth, out at the gills
			const double ph = std::fmod(t * (0.35 + 0.1 * w) + w / 3.0, 1.0);
			const Vec3 local = ph < 0.3 ? Vec3{(w % 2 ? 1.0 : -1.0) * 0.12 * L, 0.05 * L * std::sin(20.0 * ph), (0.4 - 1.6 * ph) * L}
							   : ph < 0.5 ? Vec3{0.0, -0.01 * L, (0.47 - 0.3 * (ph - 0.3) * 5.0) * L}
										  : Vec3{(w % 2 ? -1.0 : 1.0) * 0.13 * L, 0.03 * L, (0.2 - 0.8 * (ph - 0.5)) * L};
			const Mat4 frame = detail::placed(at, fwd);
			const Vec3 p = transformPoint(frame, local), q = transformPoint(frame, local + Vec3{0.0, 0.0, ph < 0.5 ? -0.05 : 0.05});
			const Vec3 pp{-p.x, p.y, p.z}, dir = normalize(Vec3{-(q.x - p.x), q.y - p.y, q.z - p.z} + Vec3{1e-9, 0, 0});
			const std::string wrasse = attr(coated({0.15, 0.35, 0.85}, 0.25), emitMesh(sealife::detail::fish(detail::poseAt(t, 6.0, w), 0.08, 0.8), detail::placed(pp, dir))) +
									   attr(coated({0.03, 0.03, 0.05}, 0.25), emitMesh(sealife::detail::bands(detail::poseAt(t, 6.0, w), 0.08, 0.8, {0.0}), detail::placed(pp, dir)));
			out += wrasse;
		}
	}
	return out + attr(coated({0.42, 0.33, 0.24}, 0.3), emitMesh(grouper, identity())) + attr(diffuseMaterial({0.05, 0.02, 0.02}), emitMesh(mouth, identity()));
}

// ---- the barracuda tornado ----
inline std::string tornadoText(const Tornado &tn, int index, double t, const std::function<Vec3(double)> &centreAt) {
	const std::string base = "tornado_" + std::to_string(index) + "_";
	std::string out = "# a barracuda tornado (film_behaviour.h)\n";
	for (int k = 0; k < sealife::detail::kPoses; ++k)
		out += nature::objectBegin(base + std::to_string(k)) + attr(school::detail::fishMaterial("barracuda"), emitMesh(school::detail::fishOf("barracuda", k, 1.0), identity())) + nature::objectEnd();
	const Vec3 c = centreAt(t);
	for (int i = 0; i < tn.count; ++i) {
		const int s = tn.seed * 389 + i;
		const double R = tn.radius * (0.75 + 0.5 * hash01(s, 1)), w = 0.75 / R, a = w * t + 2.0 * kPi * hash01(s, 2);
		const double y = tn.height * (hash01(s, 3) - 0.5) + 0.8 * std::sin(0.15 * t + 6.0 * hash01(s, 4));
		const Vec3 p = c + Vec3{R * std::cos(a), y, R * std::sin(a)};
		const Vec3 fwd{-std::sin(a), 0.05 * std::cos(0.15 * t), std::cos(a)};
		out += nature::placeObject(base + std::to_string(detail::poseAt(t, 1.5, s)), detail::placed(p, fwd, -18.0, 0.8 + 0.4 * hash01(s, 5)));
	}
	return out;
}

// ---- the playful sea lion ----
inline Vec3 playmateAt(const Playmate &pm, double t, const std::function<Vec3(double)> &friendAt, Vec3 &fwd, double &roll) {
	const Vec3 f = friendAt(t);
	const double a = 1.1 * t + pm.seed, R = 1.6 + 1.0 * std::sin(0.31 * t + pm.seed);
	const double dart = std::max(0.0, std::sin(0.45 * t + pm.seed * 2.0));   // (now and then in at the fins)
	Vec3 p = f + Vec3{R * std::cos(a), 0.6 * std::sin(0.8 * a), R * std::sin(a)};
	p = lerp(p, f + Vec3{0.0, -0.3, 0.0} - Vec3{std::cos(a), 0.0, std::sin(a)} * 0.4, 0.6 * dart * dart);
	fwd = normalize(Vec3{-std::sin(a), 0.48 * std::cos(0.8 * a), std::cos(a)});
	roll = 120.0 * std::sin(0.6 * t + pm.seed);
	return p;
}
inline std::string playmateText(const Playmate &pm, double t, double level, const underwater::Current &c, const std::function<Vec3(double)> &friendAt) {
	Vec3 fwd;
	double roll = 0.0;
	const Vec3 at = playmateAt(pm, t, friendAt, fwd, roll);
	const double L = 1.9, stroke = std::sin(2.0 * kPi * t / 1.2 + pm.seed);
	Mesh body = deepsea::torpedo(L, 0.62, 0.6, 0.06 * std::sin(2.0 * kPi * t / 1.0), 0.01);
	for (const double side : {-1.0, 1.0}) {
		const Vec3 root{side * 0.12 * L, 0.0, 0.18 * L};
		sealife::detail::fin(body, root, root + Vec3{side * 0.38 * L * std::cos(0.6 * stroke), 0.38 * L * std::sin(0.6 * stroke), -0.12 * L}, root + Vec3{0.0, 0.0, -0.16 * L});
		sealife::detail::fin(body, {side * 0.05 * L, 0.0, -0.42 * L}, {side * 0.12 * L, 0.02 * L * stroke, -0.62 * L}, {side * 0.02 * L, 0.0, -0.58 * L});
	}
	std::string out = "# a playful sea lion (film_behaviour.h)\n" + attr(coated({0.28, 0.2, 0.13}, 0.18), emitMesh(body, detail::placed(at, fwd, roll)));
	std::vector<undersea::Bubble> bubbles;   // the bursts it blows at its friend's face, every 7 s
	for (long n = static_cast<long>(std::floor((t - 30.0) / 7.0)); n <= static_cast<long>(std::floor(t / 7.0)); ++n) {
		const double when = n * 7.0 + 3.0;
		if (when > t || when < 0.0) continue;
		Vec3 f0;
		double r0 = 0.0;
		const Vec3 from = playmateAt(pm, when, friendAt, f0, r0) + f0 * (0.5 * L);
		for (int k = 0; k < 30; ++k) {
			const int key = static_cast<int>(n) * 97 + k + pm.seed * 7;
			undersea::Bubble b;
			if (undersea::bubbleAt(from + Vec3{0.1 * (hash01(key, 1) - 0.5), 0.0, 0.1 * (hash01(key, 2) - 0.5)}, k < 3 ? 0.012 : 0.002 + 0.004 * hash01(key, 3), t - when - 0.4 * hash01(key, 4), t, key,
								   level, c, b))
				bubbles.push_back(b);
		}
	}
	return out + undersea::bubblesText(bubbles);
}

// ---- territorial damselfish ----
inline std::string damselText(const Damsels &d, double t, const std::function<std::vector<Vec3>(double)> &intruders) {
	std::string out = "# damselfish (film_behaviour.h)\n";
	Mesh blue, yellow;
	const std::vector<Vec3> others = intruders(t);
	for (int i = 0; i < d.count; ++i) {
		const int s = d.seed * 211 + i;
		const double a = 2.0 * kPi * hash01(s, 1), r = d.radius * std::sqrt(hash01(s, 2));
		const Vec3 home = d.at + Vec3{r * std::cos(a), 0.3 * hash01(s, 3), r * std::sin(a)};
		Vec3 at = home + Vec3{0.08 * std::sin(1.7 * t + s), 0.05 * std::sin(1.3 * t + 2.0 * s), 0.08 * std::cos(1.5 * t + s)};
		Vec3 fwd{std::cos(1.1 * t + s), 0.0, -std::sin(1.1 * t + s)};
		for (const Vec3 &o : others) {   // darting at an intruder within 1.5 m of its spot, stopping short of it, and back
			const double dist = length(o - home);
			if (dist > 1.5) continue;
			const double charge = smoothstep(1.5, 0.6, dist) * (0.6 + 0.4 * std::sin(6.0 * t + s));
			at = lerp(at, home + normalize(o - home) * std::max(0.0, dist - 0.3), charge);
			fwd = normalize(o - home + Vec3{1e-9, 0, 0});
		}
		const int pose = detail::poseAt(t, 5.0, s);
		const Mat4 frame = detail::placed(at, fwd);
		deepsea::detail::add(blue, sealife::detail::fish(pose, 0.09, 1.5), frame);
		deepsea::detail::add(yellow, sealife::detail::bands(pose, 0.09, 1.5, {-0.32}), frame);
	}
	return out + attr(coated({0.1, 0.25, 0.85}, 0.25), emitMesh(blue, identity())) + attr(coated({0.95, 0.8, 0.1}, 0.25), emitMesh(yellow, identity()));
}

// The film's set pieces with their heights made pbrt's (above the seabed there).
inline World lifted(World w, const std::function<double(double, double)> &groundAt) {
	for (Station &s : w.stations) s.at.y += groundAt(s.at.x, s.at.z);
	for (Tornado &s : w.tornadoes) s.at.y += groundAt(s.at.x, s.at.z);
	for (Playmate &s : w.playmates) s.at.y += groundAt(s.at.x, s.at.z);
	for (Damsels &s : w.damsels) s.at.y += groundAt(s.at.x, s.at.z);
	return w;
}

}  // namespace behaviour
}  // namespace cartoon
