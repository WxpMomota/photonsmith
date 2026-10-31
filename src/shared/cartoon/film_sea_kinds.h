#pragma once
// film_sea_kinds.h -- round 17: cartoon sea characters that act, as `character NAME fish|crab|octopus at X Y Z` (the film file's character kinds: film_kinds.h).
//
//   fish      a round cartoon fish half a metre long: big eyes that look at what it looks at and blink, a mouth that says its lines, brows and a smile for its feelings,
//             a tail that beats faster the faster it swims, pectoral fins for hands (a wave, a point, a cheer, a shrug)
//   crab      a crab a third of a metre across on six legs: it scuttles sideways (its body turns across its way as it walks), eyes on stalks, a mouth under them, two claws
//             for hands that snap as it talks
//   octopus   an octopus with a big round head and eight arms that ripple as it walks on them and trail behind as it swims; its two front arms are its hands
//
// Their poses (`gesture NAME "wave" ...`): wave, point, cheer, shrug, nod, and the crab's snap. A fish and an octopus swim level as they are (CharacterKind::aquatic: castAt
// does not lay them flat as it lays a person); a crab walks on the seabed (CharacterKind::crawls: it does not swim). Their parts take colours on the character line:
// body, belly, fins, eyes, lips (fish); shell, claws, legs, eyes (crab); skin, spots, suckers, eyes (octopus).
//
// Body space as every kind's: +x its left, +y up, +z forward, at scale 1 (CharacterMoment::frame carries it into the world).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "film_deepsea.h"
#include "film_kinds.h"
#include "film_sealife.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace film_kinds {
namespace sea {

// A sea character's pose: its arms or fins (each 0 at rest .. 1 raised), what they do, its head, its face and its eyes.
struct SeaPose {
	double raiseL = 0.0, raiseR = 0.0;   // fins, claws or front arms lifted
	double forwardR = 0.0;               // the right one held out ahead (a point)
	double waveR = 0.0;                  // the right one waving, -1..1
	double spread = 0.0;                 // both held out to the sides (a shrug)
	double snap = 0.0;                   // a crab's claws open, 0..1
	double nod = 0.0, tilt = 0.0;        // the head (or whole body) nodding down, tilted (degrees)
	double mouthOpen = 0.0, smile = 0.0, browTilt = 0.0, browRaise = 0.0, eyeWide = 0.0, lids = 0.0, blink = 0.0;
	Vec3 look;
	double lookWeight = 0.0;
};

inline const rig::PoseLibrary<SeaPose> &poses() {
	static const rig::PoseLibrary<SeaPose> library = {
		{"wave", [](SeaPose &p, double w, double since) { p.raiseR += 0.9 * w, p.waveR += w * std::sin(2.0 * kPi * 2.2 * since), p.smile += 0.3 * w; }},
		{"point", [](SeaPose &p, double w, double) { p.forwardR += w, p.raiseR += 0.35 * w; }},
		{"cheer", [](SeaPose &p, double w, double since) { p.raiseL += w, p.raiseR += w, p.smile += 0.6 * w, p.eyeWide += 0.2 * w, p.nod -= 6.0 * w * std::sin(2.0 * kPi * 2.0 * since); }},
		{"shrug", [](SeaPose &p, double w, double) { p.spread += w, p.tilt += 12.0 * w, p.browRaise += 0.6 * w, p.smile -= 0.15 * w; }},
		{"nod", [](SeaPose &p, double w, double since) { p.nod += 14.0 * w * std::max(0.0, std::sin(2.0 * kPi * 1.8 * since)); }},
		{"snap", [](SeaPose &p, double w, double since) { p.snap = std::max(p.snap, w * (0.5 + 0.5 * std::sin(2.0 * kPi * 3.0 * since))), p.raiseL += 0.5 * w, p.raiseR += 0.5 * w; }},
	};
	return library;
}

namespace detail {
using deepsea::detail::add;
inline Mat4 at(Vec3 p) { return translate(p); }
inline Mesh ellipsoid(Vec3 c, Vec3 r, int nu = 20, int nv = 12) {
	Mesh m;
	add(m, uvSphere(1.0, nu, nv), mul(translate(c), scale(r.x, r.y, r.z)));
	return m;
}
// The face and the moment's pose that every sea kind shares: the feeling, the lines it says, a blink, where it looks, its idle eyes, and its gestures on top.
inline SeaPose posed(const CharacterMoment &m, Vec3 headLocal) {
	SeaPose p;
	p.smile = m.face.smile, p.browTilt = m.face.browTilt, p.browRaise = m.face.browRaise, p.eyeWide = m.face.eyeWide, p.lids = m.face.lids, p.mouthOpen = m.face.mouthOpen;
	speakWith(m, p.mouthOpen, p.smile);
	p.blink = rig::blink(m.t, m.seed);
	double yaw = 0.0, pitch = 0.0;
	lookWith(m, headLocal, 50.0, yaw, pitch, p.look, p.lookWeight);
	if (m.idle) idleEyes(m, idleAt(m.t, m.seed), headLocal, p.look, p.lookWeight);
	return m.gestures && !m.gestures->empty() ? rig::act(p, *m.gestures, m.t, poses()) : p;
}
// An eye at `c` (body space, scale 1) of radius r: white, its pupil turned towards what it looks at (or ahead), its lid closing with a blink.
inline void eye(Mesh &white, Mesh &pupil, Mesh &lid, const CharacterMoment &m, const SeaPose &p, Vec3 c, double r, Vec3 ahead) {
	add(white, ellipsoid(c, {r, r * (1.0 + 0.25 * p.eyeWide), r}));
	Vec3 dir = ahead;
	if (p.lookWeight > 0.0) {
		const Mat4 toBody = invertAffine(mul(m.frame, scale(m.scale)));
		const Vec3 target = transformPoint(toBody, p.look);
		dir = normalize(lerp(ahead, normalize(target - c + Vec3{0.0, 0.0, 1e-6}), std::min(1.0, p.lookWeight)));
	}
	add(pupil, ellipsoid(c + dir * (0.82 * r), {0.42 * r, 0.42 * r, 0.42 * r}, 12, 8));
	const double closed = std::clamp(std::max(p.blink, p.lids), 0.0, 1.0);
	if (closed > 0.02) add(lid, ellipsoid(c + Vec3{0.0, r * (1.0 - closed), 0.0}, {1.06 * r, 1.06 * r * closed + 0.002, 1.06 * r}, 14, 8));
}
// A brow bar over an eye at `c`, tilted by the feeling (inner end down for anger, up for worry) and raised.
inline void brow(Mesh &m, Vec3 c, double r, double side, const SeaPose &p) {
	const double lift = r * (1.25 + 0.35 * p.browRaise + 0.25 * p.eyeWide);
	const Vec3 inner = c + Vec3{-side * 0.7 * r, lift - 0.35 * r * p.browTilt * -1.0, 0.25 * r}, outer = c + Vec3{side * 0.8 * r, lift + 0.15 * r * p.browTilt, 0.25 * r};
	add(m, sdf::tubeMesh({inner, outer}, 0.18 * r, 0.8, 6));
}
inline std::string emitted(const CharacterMoment &m, const std::vector<std::pair<std::string, const Mesh *>> &parts, const Mat4 &local = identity()) {
	const Mat4 place = mul(m.frame, scale(m.scale), local);
	std::string out;
	for (const auto &[material, mesh] : parts)
		if (!mesh->tris.empty()) out += attr(material, emitMesh(*mesh, place));
	return out;
}
}  // namespace detail

// ---- the fish ----
inline CharacterKind fishKind() {
	CharacterKind k;
	k.aquatic = true;
	k.chest = {0.0, 0.22, 0.02};
	k.head = {0.0, 0.25, 0.16};
	k.carry = {0.12, 0.2, 0.12};
	k.radius = 0.2, k.height = 0.45;
	k.scalable = true;
	k.poses = namesOf(poses());
	k.voice = {420.0, 1.3, 0.07};
	k.parts = {"body", "belly", "fins", "eyes", "lips"};
	k.text = [](const CharacterMoment &m) {
		using namespace detail;
		const SeaPose p = posed(m, {0.0, 0.25, 0.16});
		Vec3 bodyC{0.9, 0.42, 0.12}, bellyC{0.98, 0.9, 0.7}, finC{0.95, 0.62, 0.18}, irisC{0.04, 0.04, 0.05}, lipC{0.85, 0.3, 0.25};
		colourFrom(m, "body", bodyC), colourFrom(m, "belly", bellyC), colourFrom(m, "fins", finC), colourFrom(m, "eyes", irisC), colourFrom(m, "lips", lipC);
		const double beat = 2.0 * kPi * m.t * (1.3 + 2.2 * m.going), wag = std::sin(beat) * (0.25 + 0.45 * m.going);
		const double bob = 0.01 * std::sin(2.0 * kPi * m.t / 1.6 + m.seed);
		const Mat4 lean = mul(translate(0.0, 0.22 + bob, 0.0), rotate({0, 0, 1}, p.tilt), rotate({1, 0, 0}, p.nod), translate(0.0, -0.22, 0.0));
		Mesh body, belly, fins, white, pupil, lid, brows, mouth, lips;
		add(body, ellipsoid({0.0, 0.22, 0.0}, {0.13, 0.17, 0.24}, 28, 16));
		add(belly, ellipsoid({0.0, 0.17, 0.03}, {0.105, 0.11, 0.19}, 20, 12));
		// the tail: two lobes behind, beating side to side
		const Vec3 root{0.0, 0.22, -0.22}, tip = root + Vec3{0.17 * std::sin(wag), 0.0, -0.17 * std::cos(wag)};
		sealife::detail::fin(fins, root, tip + Vec3{0.0, 0.12, 0.0}, tip + Vec3{0.0, 0.02, 0.0});
		sealife::detail::fin(fins, root, tip + Vec3{0.0, -0.1, 0.0}, tip + Vec3{0.0, -0.01, 0.0});
		sealife::detail::fin(fins, {0.0, 0.38, 0.06}, {0.0, 0.47, -0.08}, {0.0, 0.37, -0.12});   // (its dorsal fin)
		// the pectoral fins: its hands - raised, held forward, waving, spread
		for (const double side : {1.0, -1.0}) {
			const bool right = side < 0.0;
			const double raise = right ? p.raiseR : p.raiseL, flap = 0.25 * std::sin(beat * 0.5 + side) * (0.4 + m.going);
			const Vec3 base{side * 0.12, 0.18, 0.06};
			Vec3 dir{side * (0.55 + 0.6 * p.spread), -0.55 + 1.3 * raise + flap, -0.45};
			if (right) dir = dir + Vec3{0.0, 0.25 * p.waveR, 0.9 * p.forwardR + 0.2 * p.waveR};
			dir = normalize(dir) * 0.13;
			const Vec3 up{0.0, 0.04, 0.0};
			sealife::detail::fin(fins, base + up, base + dir + up * 0.5, base - up + dir * 0.4);
		}
		// the face: big eyes on the front of its head, brows, a mouth that opens with its lines and smiles
		for (const double side : {1.0, -1.0}) {
			const Vec3 c{side * 0.07, 0.29, 0.17};
			eye(white, pupil, lid, m, p, c, 0.055, normalize(Vec3{side * 0.35, 0.0, 1.0}));
			brow(brows, c, 0.055, side, p);
		}
		const double open = std::clamp(p.mouthOpen, 0.0, 1.0), smile = std::clamp(p.smile, -1.0, 1.0);
		add(mouth, ellipsoid({0.0, 0.17 + 0.012 * smile, 0.225}, {0.045 + 0.012 * smile, 0.006 + 0.04 * open, 0.02}, 14, 8));
		add(lips, sdf::tubeMesh({{-0.05, 0.18 + 0.02 * smile, 0.215}, {0.0, 0.18 - 0.01 * smile + 0.02 * open, 0.232}, {0.05, 0.18 + 0.02 * smile, 0.215}}, 0.012, 0.9, 6));
		return emitted(m, {{coated(bodyC, 0.25), &body}, {coated(bellyC, 0.3), &belly}, {coated(finC, 0.3), &fins}, {coated({0.96, 0.96, 0.95}, 0.08), &white},
						   {coated(irisC, 0.05), &pupil}, {coated(bodyC, 0.25), &lid}, {coated({0.08, 0.05, 0.04}, 0.4), &brows}, {diffuseMaterial({0.12, 0.02, 0.03}), &mouth},
						   {coated(lipC, 0.2), &lips}},
					   lean);
	};
	k.handAt = [](const CharacterMoment &m, int s) { return transformPoint(m.frame, Vec3{(s == 0 ? 1.0 : -1.0) * 0.2, 0.18, 0.06} * m.scale); };
	return k;
}

// ---- the crab ----
inline CharacterKind crabKind() {
	CharacterKind k;
	k.crawls = true;
	k.chest = {0.0, 0.1, 0.0};
	k.head = {0.0, 0.14, 0.1};
	k.carry = {0.14, 0.1, 0.16};
	k.radius = 0.2, k.height = 0.2;
	k.stride = 0.12;
	k.scalable = true;
	k.poses = namesOf(poses());
	k.voice = {300.0, 1.5, 0.05};
	k.parts = {"shell", "claws", "legs", "eyes"};
	k.text = [](const CharacterMoment &m) {
		using namespace detail;
		SeaPose p = posed(m, {0.0, 0.14, 0.1});
		p.snap = std::max(p.snap, 0.6 * std::clamp(p.mouthOpen, 0.0, 1.0));   // (its claws click as it talks)
		Vec3 shellC{0.82, 0.22, 0.12}, clawC{0.9, 0.3, 0.15}, legC{0.75, 0.2, 0.1}, irisC{0.04, 0.04, 0.05};
		colourFrom(m, "shell", shellC), colourFrom(m, "claws", clawC), colourFrom(m, "legs", legC), colourFrom(m, "eyes", irisC);
		// it scuttles sideways: walking, its body turns across its way
		const double across = 90.0 * smoothstep(0.0, 0.6, m.going), steps = m.steps * 2.0 * kPi;
		const Mat4 turn = mul(rotate({0, 1, 0}, across), translate(0.0, 0.006 * std::abs(std::sin(steps * 2.0)) * m.going, 0.0), rotate({1, 0, 0}, p.nod * 0.6), rotate({0, 0, 1}, p.tilt));
		Mesh shell, claws, legs, white, pupil, lid, brows, mouth;
		add(shell, ellipsoid({0.0, 0.1, 0.0}, {0.16, 0.06, 0.12}, 26, 12));
		// six legs, three a side, walking in the alternate pattern of a crab's gait
		for (int s = 0; s < 6; ++s) {
			const double side = s < 3 ? 1.0 : -1.0, along = 0.07 - 0.07 * (s % 3), phase = steps + (s % 2 == (s < 3 ? 0 : 1) ? 0.0 : kPi);
			const double lift = 0.03 * std::max(0.0, std::sin(phase)) * m.going, swing = 0.03 * std::cos(phase) * m.going;
			const Vec3 hip{side * 0.13, 0.09, along}, knee{side * 0.24, 0.13 + lift, along + swing}, foot{side * 0.29, 0.0 + lift, along + 1.3 * swing};
			add(legs, sdf::tubeMesh({hip, knee, foot}, 0.014, 0.6, 6));
		}
		// the claws: raised, held out, opening and closing
		for (const double side : {1.0, -1.0}) {
			const bool right = side < 0.0;
			const double raise = right ? p.raiseR : p.raiseL, wave = right ? 0.04 * p.waveR : 0.0;
			const Vec3 shoulder{side * 0.12, 0.1, 0.09};
			const Vec3 elbow = shoulder + Vec3{side * (0.08 + 0.06 * p.spread), 0.03 + 0.08 * raise, 0.06 + (right ? 0.06 * p.forwardR : 0.0)};
			const Vec3 hand = elbow + Vec3{-side * 0.03 + wave, 0.02 + 0.1 * raise, 0.08 + (right ? 0.08 * p.forwardR : 0.0)};
			add(claws, sdf::tubeMesh({shoulder, elbow, hand}, 0.02, 0.8, 8));
			add(claws, ellipsoid(hand, {0.045, 0.035, 0.06}, 16, 10));
			const double open = 0.25 + 0.6 * p.snap;
			add(claws, sdf::tubeMesh({hand + Vec3{0.0, 0.012, 0.04}, hand + Vec3{0.0, 0.012 + 0.05 * open, 0.1}}, 0.012, 0.4, 6));
			add(claws, sdf::tubeMesh({hand + Vec3{0.0, -0.012, 0.04}, hand + Vec3{0.0, -0.012 - 0.02 * open, 0.1}}, 0.012, 0.4, 6));
		}
		// eyes on stalks, a mouth under them
		for (const double side : {1.0, -1.0}) {
			const Vec3 c{side * 0.045, 0.2 + 0.01 * p.eyeWide, 0.1};
			add(legs, sdf::tubeMesh({{side * 0.035, 0.13, 0.09}, c - Vec3{0.0, 0.02, 0.0}}, 0.008, 0.9, 6));
			eye(white, pupil, lid, m, p, c, 0.026, {0.0, 0.0, 1.0});
			brow(brows, c, 0.026, side, p);
		}
		const double open = std::clamp(p.mouthOpen, 0.0, 1.0), smile = std::clamp(p.smile, -1.0, 1.0);
		add(mouth, ellipsoid({0.0, 0.1 + 0.008 * smile, 0.12}, {0.03 + 0.006 * smile, 0.004 + 0.022 * open, 0.012}, 12, 8));
		return emitted(m, {{coated(shellC, 0.2), &shell}, {coated(clawC, 0.2), &claws}, {coated(legC, 0.3), &legs}, {coated({0.96, 0.96, 0.95}, 0.08), &white},
						   {coated(irisC, 0.05), &pupil}, {coated(shellC, 0.2), &lid}, {coated({0.08, 0.04, 0.03}, 0.4), &brows}, {diffuseMaterial({0.12, 0.02, 0.03}), &mouth}},
					   turn);
	};
	k.handAt = [](const CharacterMoment &m, int s) { return transformPoint(m.frame, Vec3{(s == 0 ? 1.0 : -1.0) * 0.2, 0.15, 0.2} * m.scale); };
	return k;
}

// ---- the octopus ----
inline CharacterKind octopusKind() {
	CharacterKind k;
	k.aquatic = true;
	k.chest = {0.0, 0.3, 0.0};
	k.head = {0.0, 0.42, 0.1};
	k.carry = {0.25, 0.25, 0.25};
	k.radius = 0.35, k.height = 0.6;
	k.stride = 0.3;
	k.scalable = true;
	k.poses = namesOf(poses());
	k.voice = {220.0, 1.1, 0.06};
	k.parts = {"skin", "spots", "suckers", "eyes"};
	k.text = [](const CharacterMoment &m) {
		using namespace detail;
		const SeaPose p = posed(m, {0.0, 0.42, 0.1});
		Vec3 skinC{0.65, 0.25, 0.55}, spotC{0.85, 0.45, 0.7}, suckerC{0.95, 0.8, 0.82}, irisC{0.04, 0.04, 0.05};
		colourFrom(m, "skin", skinC), colourFrom(m, "spots", spotC), colourFrom(m, "suckers", suckerC), colourFrom(m, "eyes", irisC);
		const double breathe = 1.0 + 0.04 * std::sin(2.0 * kPi * m.t / 2.4 + m.seed);
		const Mat4 lean = mul(translate(0.0, 0.3, 0.0), rotate({0, 0, 1}, p.tilt), rotate({1, 0, 0}, p.nod), translate(0.0, -0.3, 0.0));
		Mesh skin, spots, suckers, white, pupil, lid, brows, mouth;
		add(skin, ellipsoid({0.0, 0.42, -0.04}, {0.17 * breathe, 0.2 * breathe, 0.19 * breathe}, 28, 16));
		for (int s = 0; s < 7; ++s) {
			const double a = 2.0 * kPi * hash01(s, m.seed + 3), b = 0.3 + 0.9 * hash01(s, m.seed + 5);
			add(spots, ellipsoid({0.17 * std::sin(a) * std::sin(b), 0.42 + 0.19 * std::cos(b), -0.04 + 0.17 * std::cos(a) * std::sin(b)}, {0.02, 0.02, 0.02}, 8, 6));
		}
		// eight arms from under the head: walking, they ripple in turn; swimming, they trail behind; still, they curl; the front two are its hands
		const double swim = m.swim * m.going, walk = (1.0 - m.swim) * m.going;
		for (int i = 0; i < 8; ++i) {
			const double a = 2.0 * kPi * (i + 0.5) / 8.0, ph = 2.0 * kPi * m.t * 0.9 + i * 0.8 + m.steps * kPi;
			const bool hand = i == 0 || i == 7, right = i == 7;
			const double raise = hand ? (right ? p.raiseR : p.raiseL) : 0.0;
			std::vector<Vec3> line;
			for (int j = 0; j <= 8; ++j) {
				const double u = j / 8.0, reach = 0.08 + 0.32 * u;
				Vec3 q{std::sin(a) * reach, 0.28 - 0.26 * u * (1.0 - swim) + 0.1 * std::sin(ph + 4.0 * u) * u * (0.4 + walk), std::cos(a) * reach};
				q = lerp(q, Vec3{std::sin(a) * 0.1 * (1.0 - u), 0.3 + 0.04 * std::sin(ph + 3.0 * u), -0.12 - 0.42 * u}, swim);   // (trailing behind as it swims)
				if (raise > 0.0) q = lerp(q, Vec3{std::sin(a) * (0.1 + 0.25 * u + 0.15 * p.spread * u), 0.3 + 0.35 * u + (right ? 0.08 * p.waveR * u : 0.0), 0.12 + (right ? 0.25 * p.forwardR * u : 0.0)}, std::min(1.0, raise));
				q.y += 0.03 * u * u * std::sin(ph * 1.7 + u * 6.0) * (1.0 - swim);   // (the tip curling)
				line.push_back(q);
			}
			add(skin, sdf::tubeMesh(line, 0.035, 0.18, 8));
			for (int j = 2; j <= 7; j += 2) add(suckers, ellipsoid(line[static_cast<std::size_t>(j)] + Vec3{0.0, -0.022 * (1.0 - 0.1 * j), 0.0}, {0.012, 0.006, 0.012}, 8, 5));
		}
		for (const double side : {1.0, -1.0}) {
			const Vec3 c{side * 0.075, 0.45, 0.13};
			eye(white, pupil, lid, m, p, c, 0.06, normalize(Vec3{side * 0.3, 0.0, 1.0}));
			brow(brows, c, 0.06, side, p);
		}
		const double open = std::clamp(p.mouthOpen, 0.0, 1.0), smile = std::clamp(p.smile, -1.0, 1.0);
		add(mouth, ellipsoid({0.0, 0.33 + 0.012 * smile, 0.15}, {0.04 + 0.01 * smile, 0.005 + 0.035 * open, 0.02}, 14, 8));
		return emitted(m, {{coated(skinC, 0.35), &skin}, {coated(spotC, 0.35), &spots}, {coated(suckerC, 0.4), &suckers}, {coated({0.96, 0.96, 0.95}, 0.08), &white},
						   {coated(irisC, 0.05), &pupil}, {coated(skinC, 0.35), &lid}, {coated({0.12, 0.04, 0.08}, 0.4), &brows}, {diffuseMaterial({0.12, 0.02, 0.03}), &mouth}},
					   lean);
	};
	k.handAt = [](const CharacterMoment &m, int s) { return transformPoint(m.frame, Vec3{(s == 0 ? 1.0 : -1.0) * 0.18, 0.4, 0.3} * m.scale); };
	return k;
}

inline void install() {
	auto &kinds = film_file::characterKinds();
	kinds["fish"] = fishKind();
	kinds["crab"] = crabKind();
	kinds["octopus"] = octopusKind();
}

}  // namespace sea
}  // namespace film_kinds
}  // namespace cartoon
