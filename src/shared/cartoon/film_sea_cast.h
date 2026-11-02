#pragma once
// film_sea_cast.h -- round 18: more of the sea's characters that act, as `character NAME KIND at X Y Z` (the film file's character kinds: film_kinds.h).
//
//   mermaid, merman  a person above the waist and a fish below, 1.7 m from crown to fluke: she treads water upright, her tail waving slowly; swimming she lies level
//                    and drives with her tail, up and down as a dolphin does, her arms along her sides. Long hair that streams behind her and lifts in the water (or `hair`
//                    strands on her head); a merman's is short. Colours: skin, hair, tail, fins, top, eyes, lips
//   seaturtle        a green turtle 0.9 m long with a face: big eyes, a smile; her front flippers stroke as she swims and wave as hands. Colours: shell, skin, eyes
//   shark            a cartoon shark two metres long with a grin full of teeth, eyes that roll to what it looks at and brows that frown; its pectoral fins are its hands.
//                    Colours: body, belly, eyes, teeth
//   jellyfish        a bell 0.35 m across that pulses as it goes (faster swimming, bell first), a face on its bell, arms and tentacles that trail and stream behind it
//                    (film_flow.h); its rim glows. Colours: bell, glow, eyes
//   seahorse         a seahorse 0.3 m tall, upright, its little back fin a blur, its tail curled, a long snout that talks; it leans into its way as it goes. Colours:
//                    body, fins, eyes
//   puffer           a round pufferfish 0.25 m long with big worried eyes and quick little fins: `gesture NAME "puff" at T [for D]` blows it up into a spiky ball and lets it go
//                    down again. Colours: body, belly, spines, eyes
//
// Their poses are the sea's (film_sea_kinds.h): wave, point, cheer, shrug, nod, snap, puff. All are `aquatic`: they swim as they are, their walks under the water swims.
// Body space as every kind's: +x its left, +y up, +z forward, at scale 1.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "film_flow.h"
#include "film_sea_kinds.h"

namespace cartoon {
namespace film_kinds {
namespace seacast {

using sea::SeaPose;
using sea::detail::brow;
using sea::detail::ellipsoid;
using sea::detail::emitted;
using sea::detail::eye;
using sea::detail::posed;
using deepsea::detail::add;

namespace detail {
// A two-segment arm from `shoulder` to the hand the pose puts it at (body space): reaching down at rest, raised, held forward, spread, waving; `streamline` 0..1 laid
// along the side (swimming fast). `side` +1 its left, -1 its right.
inline Vec3 handFor(Vec3 shoulder, double side, const SeaPose &p, double reach, double streamline, double t) {
	const bool right = side < 0.0;
	const double raise = right ? p.raiseR : p.raiseL;
	Vec3 dir{side * (0.25 + 0.75 * p.spread), -1.0 + 1.9 * raise, 0.12};
	if (right) dir = dir + Vec3{0.25 * p.waveR, 0.0, 1.6 * p.forwardR};
	dir = lerp(normalize(dir), normalize(Vec3{side * 0.15, -1.0, -0.12}), streamline * (1.0 - std::min(1.0, raise + p.forwardR)));
	return shoulder + normalize(dir) * reach + Vec3{0.0, 0.015 * std::sin(1.3 * t + side), 0.0};
}
inline void arm(Mesh &skin, Mesh &hands, Vec3 shoulder, Vec3 hand, double side, double r) {
	const Vec3 mid = (shoulder + hand) * 0.5 + Vec3{side * 0.05, -0.02, -0.05};   // (the elbow out and back a little)
	add(skin, sdf::tubeMesh({shoulder, mid, hand}, r, 0.75, 8));
	add(hands, ellipsoid(hand, {0.032, 0.04, 0.025}, 10, 8));
}
// A fluke (two lobes across the tail) at `at`, going `dir`, spread across `across`: into `m`.
inline void fluke(Mesh &m, Vec3 at, Vec3 dir, Vec3 across, double size) {
	for (const double side : {1.0, -1.0}) {
		const Vec3 tip = at + dir * (0.75 * size) + across * (side * size), notch = at + dir * (0.4 * size);
		sealife::detail::fin(m, at, tip, notch);
		sealife::detail::fin(m, at - across * (side * 0.08 * size), at + dir * (0.3 * size) + across * (side * 0.6 * size), tip);
	}
}
// Spines over a ball of radius r about c, `out` long (a pufferfish's).
inline void spines(Mesh &m, Vec3 c, Vec3 r, double out, int seed) {
	for (int k = 0; k < 70; ++k) {
		const double u = 2.0 * hash01(k, seed) - 1.0, a = 2.0 * kPi * hash01(k, seed + 1), q = std::sqrt(1.0 - u * u);
		const Vec3 n{q * std::cos(a), u, q * std::sin(a)};
		if (n.z > 0.75) continue;   // (none on its face)
		const Vec3 base = c + Vec3{n.x * r.x, n.y * r.y, n.z * r.z};
		add(m, sdf::tubeMesh({base - n * 0.004, base + n * out}, 0.006, 0.15, 5));
	}
}
}  // namespace detail

// ---- the mermaid (and the merman) ----
// Her lean at a moment: laid level about her middle as she swims, a little forward as she treads water.
inline Mat4 mermaidLean(const CharacterMoment &m) {
	const double level = (m.swim > 0.0 ? 78.0 * smoothstep(0.0, 0.7, m.going) : 0.0) + 6.0 * std::sin(0.5 * m.t + m.seed);
	return mul(translate(0.0, 1.05, 0.0), rotate({1, 0, 0}, level), translate(0.0, -1.05, 0.0));
}
inline CharacterKind merKind(bool merman) {
	CharacterKind k;
	k.aquatic = true, k.flows = true;
	k.chest = {0.0, 1.2, 0.0};
	k.head = {0.0, 1.55, 0.02};
	k.carry = {0.18, 1.05, 0.25};
	k.radius = 0.3, k.height = 1.7;
	k.stride = 0.9;
	k.scalable = true;
	k.poses = namesOf(sea::poses());
	k.voice = merman ? speech::Voice{150.0, 1.0, 0.04} : speech::Voice{260.0, 1.05, 0.05};
	k.parts = {"skin", "hair", "tail", "fins", "top", "eyes", "lips"};
	k.lean = mermaidLean;
	k.text = [merman](const CharacterMoment &m) {
		using namespace detail;
		const SeaPose p = posed(m, {0.0, 1.55, 0.02});
		Vec3 skinC{0.93, 0.74, 0.62}, hairC = merman ? Vec3{0.12, 0.22, 0.32} : Vec3{0.85, 0.22, 0.14}, tailC{0.1, 0.62, 0.55}, finC{0.35, 0.82, 0.7}, topC{0.82, 0.45, 0.78}, irisC{0.12, 0.35, 0.6}, lipC{0.85, 0.35, 0.38};
		colourFrom(m, "skin", skinC), colourFrom(m, "hair", hairC), colourFrom(m, "tail", tailC), colourFrom(m, "fins", finC), colourFrom(m, "top", topC), colourFrom(m, "eyes", irisC), colourFrom(m, "lips", lipC);
		const Mat4 lean = mermaidLean(m);
		const double swimming = m.swim > 0.0 ? smoothstep(0.0, 0.7, m.going) : 0.0;
		Mesh skin, hands, tail, fins, top, hair, white, pupil, lid, brows, mouth, lips;
		// the head and the body above the waist
		add(skin, ellipsoid({0.0, 1.55, 0.02}, {0.1, 0.12, 0.11}, 24, 16));
		add(skin, sdf::tubeMesh({{0.0, 1.39, 0.0}, {0.0, 1.47, 0.01}}, 0.045, 1.0, 10));
		add(skin, ellipsoid({0.0, 1.25, 0.0}, {merman ? 0.18 : 0.15, 0.18, 0.1}, 22, 14));
		add(skin, ellipsoid({0.0, 1.09, 0.0}, {0.13, 0.1, 0.095}, 18, 10));
		add(skin, ellipsoid({0.0, 1.53, 0.12}, {0.016, 0.022, 0.018}, 8, 6));   // (the nose)
		for (const double side : {1.0, -1.0}) add(skin, ellipsoid({side * 0.098, 1.55, 0.0}, {0.015, 0.03, 0.02}, 8, 6));   // (the ears)
		if (!merman)
			for (const double side : {1.0, -1.0}) add(top, ellipsoid({side * 0.066, 1.27, 0.075}, {0.065, 0.055, 0.035}, 14, 10));   // (her shell top)
		// her arms: hanging, raised, waving; along her sides as she swims, a slow stroke as she treads water
		for (const double side : {1.0, -1.0}) {
			const Vec3 shoulder{side * 0.165, 1.37, 0.0};
			Vec3 hand = handFor(shoulder, side, p, 0.48, swimming, m.t);
			if (swimming < 0.5 && p.raiseL + p.raiseR + p.forwardR + p.spread < 0.1) hand = hand + Vec3{side * 0.1 * (1.0 + std::sin(2.0 * kPi * m.t / 2.4)), 0.12, 0.08 * std::cos(2.0 * kPi * m.t / 2.4)};
			arm(skin, hands, shoulder, hand, side, merman ? 0.045 : 0.038);
		}
		// the tail: from her hips down to the fluke, waving - a slow sway while she treads water, a strong kick as she swims (up and down, once she lies level)
		const double beat = 2.0 * kPi * m.t * (0.45 + 1.3 * swimming), amp = 0.35 + 0.9 * swimming;
		std::vector<Vec3> spine;
		std::vector<double> radii;
		for (int i = 0; i <= 14; ++i) {
			const double u = i / 14.0;
			spine.push_back({0.0, 1.02 - 0.88 * u, 0.04 * std::sin(kPi * u) + amp * (0.02 + 0.14 * u * u) * std::sin(beat - 3.2 * u)});
			radii.push_back(lerp(0.135, 0.032, std::pow(u, 0.9)) * (1.0 + 0.12 * std::sin(kPi * std::min(1.0, u * 2.5))));
		}
		add(tail, flow::tube(spine, radii, 14, 1.25, {1.0, 0.0, 0.0}));
		const Vec3 end = spine.back(), dir = normalize(spine.back() - spine[spine.size() - 2]);
		fluke(fins, end, dir, {1.0, 0.0, 0.0}, merman ? 0.3 : 0.26);
		for (const double side : {1.0, -1.0}) sealife::detail::fin(fins, spine[4] + Vec3{side * 0.09, 0.0, 0.0}, spine[6] + Vec3{side * 0.2, -0.05, 0.0}, spine[6] + Vec3{side * 0.08, 0.0, 0.0});
		fins.uvs.clear();
		// her hair: a cap, and locks streaming from the back of her head (unless she has `hair` strands)
		if (!m.strandHair) {
			add(hair, ellipsoid({0.0, 1.585, 0.0}, {0.108, 0.105, 0.115}, 22, 12));
			const flow::Wake &w = m.wake;
			const int locks = merman ? 7 : 15;
			for (int q = 0; q < locks; ++q) {
				const double a = kPi * (0.15 + 0.7 * q / (locks - 1.0)), sideX = std::cos(a);   // (round the back of her head, ear to ear)
				const Vec3 root{0.1 * sideX, 1.6 + 0.03 * std::sin(a), -0.08 * std::sin(a) - 0.01};
				const Vec3 rest = m.swim > 0.0 || w.wet ? Vec3{0.35 * sideX, 0.1 + 0.5 * (hash01(q, m.seed + 9) - 0.5), -1.0} : Vec3{0.25 * sideX, -1.0, -0.3};   // (in the water: back behind her, a little spread and lifted)
				const double len = (merman ? 0.18 : 0.55) * (0.85 + 0.3 * hash01(q, m.seed + 7));
				const std::vector<Vec3> line = flow::chain(w, lean, root, rest, len, 8, merman ? 0.03 : 0.07, m.seed + q);
				std::vector<double> r;
				for (std::size_t i = 0; i < line.size(); ++i) r.push_back(lerp(merman ? 0.03 : 0.036, 0.014, static_cast<double>(i) / (line.size() - 1)));
				add(hair, flow::tube(line, r, 6, 2.2, {1.0, 0.0, 0.0}));
			}
		}
		// her face
		for (const double side : {1.0, -1.0}) {
			const Vec3 c{side * 0.042, 1.575, 0.098};
			eye(white, pupil, lid, m, p, c, 0.026, normalize(Vec3{side * 0.15, 0.0, 1.0}));
			brow(brows, c, 0.026, side, p);
		}
		const double open = std::clamp(p.mouthOpen, 0.0, 1.0), smile = std::clamp(p.smile, -1.0, 1.0);
		add(mouth, ellipsoid({0.0, 1.488 + 0.004 * smile, 0.108}, {0.024 + 0.006 * smile, 0.003 + 0.018 * open, 0.01}, 12, 8));
		add(lips, sdf::tubeMesh({{-0.026, 1.494 + 0.008 * smile, 0.103}, {0.0, 1.492 - 0.004 * smile + 0.01 * open, 0.112}, {0.026, 1.494 + 0.008 * smile, 0.103}}, 0.006, 0.9, 6));
		return emitted(m, {{coated(skinC, 0.45), &skin}, {coated(skinC, 0.45), &hands}, {coated(tailC, 0.18), &tail}, {coated(finC, 0.25), &fins}, {coated(topC, 0.15), &top},
						   {coated(hairC, 0.5), &hair}, {coated({0.96, 0.96, 0.95}, 0.08), &white}, {coated(irisC, 0.05), &pupil}, {coated(skinC, 0.45), &lid},
						   {coated(hairC * 0.6, 0.5), &brows}, {diffuseMaterial({0.25, 0.04, 0.05}), &mouth}, {coated(lipC, 0.3), &lips}},
					   lean);
	};
	k.handAt = [](const CharacterMoment &m, int s) {
		const SeaPose p = posed(m, {0.0, 1.55, 0.02});
		const double side = s == 0 ? 1.0 : -1.0;
		return transformPoint(mul(m.frame, scale(m.scale), mermaidLean(m)), detail::handFor({side * 0.165, 1.37, 0.0}, side, p, 0.48, m.swim > 0.0 ? smoothstep(0.0, 0.7, m.going) : 0.0, m.t));
	};
	k.headFrame = [](const CharacterMoment &m) {   // (+x forward, +y up, +z its right: for `hair` strands)
		const Mat4 body = mul(m.frame, scale(m.scale), mermaidLean(m));
		const Vec3 at = transformPoint(body, {0.0, 1.55, 0.02});
		const Vec3 fwd = normalize(transformPoint(body, {0.0, 1.55, 1.02}) - at), up = normalize(transformPoint(body, {0.0, 2.55, 0.02}) - at), right = normalize(cross(fwd, up));
		return Mat4{fwd.x, up.x, right.x, at.x, fwd.y, up.y, right.y, at.y, fwd.z, up.z, right.z, at.z, 0, 0, 0, 1};
	};
	k.scalpCentre = {0.0, 0.02, 0.0};
	k.scalpRadii = {0.11, 0.12, 0.1};
	return k;
}

// ---- the sea turtle ----
inline CharacterKind seaTurtleKind() {
	CharacterKind k;
	k.aquatic = true;
	k.chest = {0.0, 0.16, 0.05};
	k.head = {0.0, 0.18, 0.52};
	k.carry = {0.3, 0.12, 0.3};
	k.radius = 0.5, k.height = 0.35;
	k.stride = 0.7;
	k.scalable = true;
	k.poses = namesOf(sea::poses());
	k.voice = {170.0, 0.85, 0.04};
	k.parts = {"shell", "skin", "eyes"};
	k.text = [](const CharacterMoment &m) {
		using namespace detail;
		const SeaPose p = posed(m, {0.0, 0.18, 0.52});
		Vec3 shellC{0.36, 0.42, 0.16}, skinC{0.55, 0.62, 0.35}, irisC{0.08, 0.06, 0.04};
		colourFrom(m, "shell", shellC), colourFrom(m, "skin", skinC), colourFrom(m, "eyes", irisC);
		const double L = 0.9, stroke = std::sin(2.0 * kPi * m.t / (m.going > 0.3 ? 1.9 : 3.4) + m.seed);
		const Mat4 lean = mul(translate(0.0, 0.15, 0.0), rotate({0, 0, 1}, p.tilt - 6.0 * stroke * m.going), rotate({1, 0, 0}, 0.4 * p.nod), translate(0.0, -0.15, 0.0));
		Mesh shell = lathe({{0.0, -0.5}, {0.3, -0.42}, {0.45, -0.15}, {0.48, 0.1}, {0.38, 0.35}, {0.0, 0.48}}, 22);
		for (Vec3 &v : shell.verts) v = Vec3{v.x * L, v.z * L * 0.32 + 0.15, v.y * L};
		for (Vec3 &n : shell.normals) n = normalize(Vec3{n.x, n.z / 0.32, n.y});
		for (auto &tr : shell.tris) std::swap(tr[1], tr[2]);
		shell.uvs.clear();
		Mesh plates, skin, white, pupil, lid, brows, mouth;
		for (int q = 0; q < 9; ++q) {   // (the plates on its shell, a shade lighter)
			const double a = 2.0 * kPi * q / 6.0, r = q < 6 ? 0.2 : 0.0, z = q < 6 ? 0.0 : 0.22 * (q - 7);
			add(plates, ellipsoid({r * std::sin(a) * L * 0.6, 0.15 + 0.135 * L * (q < 6 ? 0.85 : 1.0), r * std::cos(a) * L * 0.6 + z * L}, {0.09, 0.012, 0.09}, 12, 6));
		}
		const double look = 0.06 * p.nod;
		add(skin, ellipsoid({0.0, 0.17 - look, 0.53}, {0.095, 0.085, 0.12}, 18, 12));   // (its head, on a neck)
		add(skin, sdf::tubeMesh({{0.0, 0.15, 0.32}, {0.0, 0.16, 0.45}}, 0.06, 1.0, 10));
		for (const double side : {1.0, -1.0}) {   // front flippers (its hands: stroking as it swims) and rear ones
			const bool right = side < 0.0;
			const double raise = right ? p.raiseR : p.raiseL;
			const double up = 0.85 * stroke * (0.3 + 0.7 * m.going) + 1.3 * raise + (right ? 0.3 * p.waveR : 0.0);
			const Vec3 root{side * 0.27 * L, 0.13, 0.22 * L}, tip = root + Vec3{side * 0.55 * L * std::cos(up) * (1.0 - 0.4 * (right ? p.forwardR : 0.0)), 0.55 * L * std::sin(up), -0.2 * L + (right ? 0.4 * L * p.forwardR : 0.0) + 0.06 * std::cos(2.0 * kPi * m.t / 1.9)};
			sealife::detail::fin(skin, root, tip, root + Vec3{0.0, 0.0, -0.2 * L});
			sealife::detail::fin(skin, root + Vec3{0.0, 0.015, 0.0}, tip + Vec3{0.0, 0.015, 0.0}, root + Vec3{side * 0.05, 0.0, -0.18 * L});
			sealife::detail::fin(skin, {side * 0.22 * L, 0.13, -0.35 * L}, {side * 0.4 * L, 0.13 + 0.03 * stroke, -0.5 * L}, {side * 0.18 * L, 0.13, -0.46 * L});
		}
		skin.uvs.clear();
		for (const double side : {1.0, -1.0}) {
			const Vec3 c{side * 0.06, 0.21 - look, 0.6};
			eye(white, pupil, lid, m, p, c, 0.032, normalize(Vec3{side * 0.4, 0.0, 1.0}));
			brow(brows, c, 0.032, side, p);
		}
		const double open = std::clamp(p.mouthOpen, 0.0, 1.0), smile = std::clamp(p.smile, -1.0, 1.0);
		add(mouth, ellipsoid({0.0, 0.135 - look + 0.006 * smile, 0.635}, {0.05 + 0.01 * smile, 0.004 + 0.022 * open, 0.02}, 12, 8));
		return emitted(m, {{coated(shellC, 0.35), &shell}, {coated(shellC * 1.35, 0.4), &plates}, {coated(skinC, 0.5), &skin}, {coated({0.96, 0.96, 0.95}, 0.08), &white},
						   {coated(irisC, 0.05), &pupil}, {coated(skinC, 0.5), &lid}, {coated(skinC * 0.4, 0.5), &brows}, {diffuseMaterial({0.12, 0.05, 0.04}), &mouth}},
					   lean);
	};
	k.handAt = [](const CharacterMoment &m, int s) { return transformPoint(m.frame, Vec3{(s == 0 ? 1.0 : -1.0) * 0.55, 0.18, 0.1} * m.scale); };
	return k;
}

// ---- the shark ----
inline CharacterKind sharkKind() {
	CharacterKind k;
	k.aquatic = true;
	k.chest = {0.0, 0.4, 0.3};
	k.head = {0.0, 0.48, 0.7};
	k.carry = {0.4, 0.3, 0.5};
	k.radius = 0.6, k.height = 0.8;
	k.stride = 1.2;
	k.scalable = true;
	k.poses = namesOf(sea::poses());
	k.voice = {105.0, 0.9, 0.06};
	k.parts = {"body", "belly", "eyes", "teeth"};
	k.text = [](const CharacterMoment &m) {
		using namespace detail;
		const SeaPose p = posed(m, {0.0, 0.48, 0.7});
		Vec3 bodyC{0.38, 0.47, 0.58}, bellyC{0.9, 0.9, 0.86}, irisC{0.05, 0.05, 0.06}, toothC{0.97, 0.96, 0.92};
		colourFrom(m, "body", bodyC), colourFrom(m, "belly", bellyC), colourFrom(m, "eyes", irisC), colourFrom(m, "teeth", toothC);
		const double len = 2.0, sway = (0.08 + 0.18 * m.going) * std::sin(2.0 * kPi * m.t * (0.5 + 1.0 * m.going) + m.seed);
		const Mat4 lean = mul(translate(0.0, 0.4, 0.0), rotate({0, 0, 1}, p.tilt), rotate({1, 0, 0}, 0.5 * p.nod), translate(0.0, -0.4, 0.0));
		Mesh body = sealife::detail::body(len, 0.62, 0.72, sway, {{0.0, -0.5}, {0.05, -0.4}, {0.13, -0.15}, {0.16, 0.08}, {0.14, 0.28}, {0.09, 0.42}, {0.0, 0.5}});
		for (Vec3 &v : body.verts) v.y += 0.4;
		const double tx = sway * 0.3 * len;
		Mesh fins, belly, gills, white, pupil, lid, brows, mouth, teeth;
		sealife::detail::fin(fins, {0.0, 0.62, 0.14}, {0.0, 0.98, -0.12}, {0.0, 0.62, -0.26});   // (its dorsal fin)
		sealife::detail::fin(fins, {tx * 0.8, 0.4, -0.9}, {tx * 1.25, 0.95, -1.38}, {tx, 0.4, -1.12});   // (its tail, taller above than below)
		sealife::detail::fin(fins, {tx * 0.8, 0.4, -0.9}, {tx, 0.4, -1.12}, {tx * 1.1, 0.12, -1.2});
		for (const double side : {1.0, -1.0}) {   // its pectoral fins: its hands
			const bool right = side < 0.0;
			const double raise = right ? p.raiseR : p.raiseL, wave = right ? 0.25 * p.waveR : 0.0;
			const Vec3 root{side * 0.17, 0.3, 0.25};
			const Vec3 tip = root + Vec3{side * (0.45 + 0.2 * p.spread), -0.18 + 0.6 * raise + wave, -0.12 + (right ? 0.45 * p.forwardR : 0.0)};
			sealife::detail::fin(fins, root, tip, root + Vec3{0.0, 0.0, -0.22});
		}
		fins.uvs.clear(), body.uvs.clear();
		add(belly, ellipsoid({0.0, 0.31, 0.15}, {0.21, 0.13, 0.62}, 22, 12));
		for (const double side : {1.0, -1.0})
			for (int g = 0; g < 3; ++g) add(gills, sdf::tubeMesh({{side * 0.2, 0.47, 0.42 - 0.06 * g}, {side * 0.215, 0.36, 0.44 - 0.06 * g}}, 0.008, 0.6, 5));
		// big eyes on the front of its head, brows that frown, and a wide grin of teeth
		for (const double side : {1.0, -1.0}) {
			const Vec3 c{side * 0.13, 0.55, 0.66};
			eye(white, pupil, lid, m, p, c, 0.065, normalize(Vec3{side * 0.45, 0.1, 1.0}));
			brow(brows, c, 0.065, side, p);
		}
		const double open = std::clamp(p.mouthOpen, 0.0, 1.0), smile = std::clamp(p.smile + 0.4, -1.0, 1.0);
		const double gape = 0.025 + 0.08 * open;
		add(mouth, ellipsoid({0.0, 0.33, 0.8}, {0.17, gape, 0.06}, 18, 8));
		for (int q = -6; q <= 6; ++q) {   // (two rows of teeth along the curve of its grin)
			const double x = 0.026 * q, curve = 0.03 * smile * (q / 6.0) * (q / 6.0);
			const Vec3 top{x, 0.33 + gape * 0.85 + curve, 0.81 + 0.035 * (1.0 - std::abs(q) / 6.0)}, bottom{x, 0.33 - gape * 0.85 + curve, 0.8 + 0.03 * (1.0 - std::abs(q) / 6.0)};
			add(teeth, sdf::tubeMesh({top, top + Vec3{0.0, -0.028, 0.004}}, 0.01, 0.2, 5));
			if (open > 0.15) add(teeth, sdf::tubeMesh({bottom, bottom + Vec3{0.0, 0.024, 0.004}}, 0.009, 0.2, 5));
		}
		return emitted(m, {{coated(bodyC, 0.3), &body}, {coated(bodyC, 0.3), &fins}, {coated(bellyC, 0.35), &belly}, {coated(bodyC * 0.45, 0.4), &gills},
						   {coated({0.96, 0.96, 0.95}, 0.08), &white}, {coated(irisC, 0.05), &pupil}, {coated(bodyC, 0.3), &lid}, {coated(bodyC * 0.3, 0.4), &brows},
						   {diffuseMaterial({0.25, 0.04, 0.06}), &mouth}, {coated(toothC, 0.1), &teeth}},
					   lean);
	};
	k.handAt = [](const CharacterMoment &m, int s) { return transformPoint(m.frame, Vec3{(s == 0 ? 1.0 : -1.0) * 0.6, 0.25, 0.15} * m.scale); };
	return k;
}

// ---- the jellyfish ----
// Its lean: bell first as it swims, gently rocking as it hangs.
inline Mat4 jellyLean(const CharacterMoment &m) {
	const double tip = (m.swim > 0.0 ? 65.0 * smoothstep(0.0, 0.8, m.going) : 0.0) + 5.0 * std::sin(0.4 * m.t + m.seed);
	return mul(translate(0.0, 0.62, 0.0), rotate({1, 0, 0}, tip), translate(0.0, -0.62, 0.0));
}
inline CharacterKind jellyfishKind() {
	CharacterKind k;
	k.aquatic = true, k.flows = true;
	k.chest = {0.0, 0.62, 0.0};
	k.head = {0.0, 0.7, 0.12};
	k.carry = {0.2, 0.5, 0.2};
	k.radius = 0.25, k.height = 0.9;
	k.stride = 0.5;
	k.scalable = true;
	k.poses = namesOf(sea::poses());
	k.voice = {520.0, 1.2, 0.08};
	k.parts = {"bell", "glow", "eyes"};
	k.lean = jellyLean;
	k.text = [](const CharacterMoment &m) {
		using namespace detail;
		const SeaPose p = posed(m, {0.0, 0.7, 0.12});
		Vec3 bellC{0.82, 0.6, 0.92}, glowC{0.9, 0.55, 1.0}, irisC{0.15, 0.05, 0.25};
		colourFrom(m, "bell", bellC), colourFrom(m, "glow", glowC), colourFrom(m, "eyes", irisC);
		const Mat4 lean = jellyLean(m);
		const double period = m.going > 0.3 ? 0.9 : 1.8, ph = std::fmod(m.t / period + 0.1 * m.seed, 1.0);
		const double squeeze = ph < 0.3 ? std::sin(kPi * ph / 0.3) : 0.0, wide = 1.0 - 0.25 * squeeze, tall = 1.0 + 0.18 * squeeze;
		const double R = 0.18;
		Mesh bell = lathe({{0.0, 0.62 * tall}, {0.45 * wide, 0.55 * tall}, {0.85 * wide, 0.32 * tall}, {1.0 * wide, 0.05}, {0.92 * wide, -0.02}, {0.8 * wide, 0.06}, {0.0, 0.25 * tall}}, 28);
		for (Vec3 &v : bell.verts) v = Vec3{v.x * R, 0.55 + v.y * R, v.z * R};
		bell.uvs.clear();
		Mesh rim, arms, tentacles, white, pupil, lid, brows, mouth;
		std::vector<Vec3> ring;
		for (int q = 0; q <= 28; ++q) ring.push_back({std::cos(2.0 * kPi * q / 28) * 0.96 * wide * R, 0.56, std::sin(2.0 * kPi * q / 28) * 0.96 * wide * R});
		add(rim, sdf::tubeMesh(ring, 0.008, 1.0, 6));
		const flow::Wake &w = m.wake;
		for (int q = 0; q < 12; ++q) {   // the tentacles from its rim, trailing and streaming; the front two lift for its hands
			const double a = 2.0 * kPi * (q + 0.5) / 12.0;
			const Vec3 root{std::sin(a) * 0.9 * wide * R, 0.56, std::cos(a) * 0.9 * wide * R};
			const bool front = q == 0 || q == 11, right = q == 11;
			const double raise = front ? (right ? p.raiseR : p.raiseL) : 0.0;
			Vec3 rest{0.25 * std::sin(a), -1.0, 0.25 * std::cos(a)};
			if (raise > 0.0) rest = lerp(rest, Vec3{0.4 * std::sin(a) * (1.0 + p.spread), 0.6, 0.6 + (right ? 0.8 * p.forwardR : 0.0)}, std::min(1.0, raise));
			const std::vector<Vec3> line = flow::chain(w, lean, root, rest, (front ? 0.3 : 0.5) * (0.8 + 0.4 * hash01(q, m.seed)), 10, 0.1 + (right ? 0.2 * std::abs(p.waveR) : 0.0), m.seed + q);
			std::vector<double> r(line.size());
			for (std::size_t i = 0; i < line.size(); ++i) r[i] = lerp(0.006, 0.0025, static_cast<double>(i) / (line.size() - 1));
			add(tentacles, flow::tube(line, r, 4));
		}
		for (int q = 0; q < 4; ++q) {   // the frilly arms from under its middle
			const double a = 2.0 * kPi * (q + 0.25) / 4.0;
			const std::vector<Vec3> line = flow::chain(w, lean, {0.02 * std::sin(a), 0.58, 0.02 * std::cos(a)}, {0.1 * std::sin(a), -1.0, 0.1 * std::cos(a)}, 0.32, 10, 0.06, m.seed + 40 + q);
			std::vector<double> r(line.size());
			for (std::size_t i = 0; i < line.size(); ++i) r[i] = 0.016 * (1.0 - 0.6 * i / (line.size() - 1.0)) * (1.0 + 0.35 * std::sin(1.9 * i + q));
			add(arms, flow::tube(line, r, 7, 1.8));
		}
		for (const double side : {1.0, -1.0}) {   // a face on the front of its bell
			const Vec3 c{side * 0.06, 0.6 + 0.4 * R * tall, 0.7 * R * wide + 0.02};
			eye(white, pupil, lid, m, p, c, 0.03, normalize(Vec3{side * 0.2, 0.1, 1.0}));
			brow(brows, c, 0.03, side, p);
		}
		const double open = std::clamp(p.mouthOpen, 0.0, 1.0), smile = std::clamp(p.smile, -1.0, 1.0);
		add(mouth, ellipsoid({0.0, 0.6 + 0.1 * R + 0.004 * smile, 0.9 * R * wide + 0.015}, {0.025 + 0.006 * smile, 0.003 + 0.016 * open, 0.01}, 12, 8));
		const Vec3 L = glowC * 0.9;
		char glow[200];
		std::snprintf(glow, sizeof glow, "AreaLightSource \"diffuse\" \"rgb L\" [ %.4f %.4f %.4f ] \"bool twosided\" true \"bool sampled\" false\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", L.x, L.y, L.z);
		return emitted(m, {{translucentMaterial(bellC * 0.45, bellC * 0.75), &bell}, {glow, &rim}, {translucentMaterial(bellC * 0.4, bellC * 0.7), &tentacles},
						   {translucentMaterial(bellC * 0.55, bellC * 0.6), &arms}, {coated({0.96, 0.96, 0.95}, 0.08), &white}, {coated(irisC, 0.05), &pupil},
						   {translucentMaterial(bellC * 0.45, bellC * 0.75), &lid}, {coated(irisC, 0.4), &brows}, {diffuseMaterial({0.2, 0.05, 0.12}), &mouth}},
					   lean);
	};
	k.handAt = [](const CharacterMoment &m, int s) { return transformPoint(mul(m.frame, scale(m.scale), jellyLean(m)), Vec3{(s == 0 ? 1.0 : -1.0) * 0.08, 0.66, 0.3}); };
	return k;
}

// ---- the seahorse ----
inline CharacterKind seahorseKind() {
	CharacterKind k;
	k.aquatic = true;
	k.chest = {0.0, 0.17, 0.03};
	k.head = {0.0, 0.27, 0.03};
	k.carry = {0.05, 0.18, 0.08};
	k.radius = 0.08, k.height = 0.32;
	k.stride = 0.15;
	k.scalable = true;
	k.poses = namesOf(sea::poses());
	k.voice = {620.0, 1.4, 0.05};
	k.parts = {"body", "fins", "eyes"};
	k.text = [](const CharacterMoment &m) {
		using namespace detail;
		const SeaPose p = posed(m, {0.0, 0.27, 0.03});
		Vec3 bodyC{0.95, 0.62, 0.15}, finC{0.98, 0.85, 0.5}, irisC{0.05, 0.04, 0.06};
		colourFrom(m, "body", bodyC), colourFrom(m, "fins", finC), colourFrom(m, "eyes", irisC);
		const double bob = 0.01 * std::sin(2.0 * kPi * m.t / 1.4 + m.seed);
		const Mat4 lean = mul(translate(0.0, 0.17 + bob, 0.0), rotate({1, 0, 0}, 22.0 * m.going + p.nod), rotate({0, 0, 1}, p.tilt), translate(0.0, -0.17, 0.0));
		Mesh body, fins, white, pupil, lid, brows, mouth;
		// its body: an S from the crown down the belly to the curled tail
		std::vector<Vec3> spine{{0.0, 0.29, 0.0}, {0.0, 0.25, -0.02}, {0.0, 0.2, 0.0}, {0.0, 0.15, 0.035}, {0.0, 0.1, 0.03}, {0.0, 0.065, 0.0}};
		std::vector<double> radii{0.022, 0.026, 0.03, 0.036, 0.03, 0.02};
		const double curl = 0.6 + 0.15 * std::sin(1.1 * m.t + m.seed);
		for (int q = 1; q <= 14; ++q) {   // (the tail's spiral, tighter towards its tip)
			const double u = q / 14.0, a = curl * 2.2 * kPi * u, r = 0.045 * (1.0 - 0.7 * u);
			spine.push_back({0.0, 0.03 + r * std::cos(a) - 0.01 * u, -0.01 - r * std::sin(a) + 0.025 * u});
			radii.push_back(0.016 * (1.0 - 0.75 * u) + 0.002);
		}
		add(body, flow::tube(spine, radii, 10, 0.8));
		add(body, sdf::tubeMesh({{0.0, 0.265, 0.02}, {0.0, 0.258, 0.07}, {0.0, 0.252, 0.095}}, 0.011, 0.9, 8));   // (its snout)
		for (int q = 0; q < 4; ++q) add(body, sdf::tubeMesh({{0.0, 0.29 - 0.012 * q, -0.012 - 0.006 * q}, {0.0, 0.31 - 0.012 * q, -0.02 - 0.008 * q}}, 0.004, 0.3, 5));   // (its crown)
		const double flutter = std::sin(2.0 * kPi * 9.0 * m.t);   // (its back fin, a blur)
		sealife::detail::fin(fins, {0.0, 0.17, -0.025}, {0.012 * flutter, 0.15, -0.065}, {0.0, 0.11, -0.03});
		for (const double side : {1.0, -1.0}) {   // the little fins behind its head: its hands
			const bool right = side < 0.0;
			const double raise = right ? p.raiseR : p.raiseL;
			const Vec3 root{side * 0.02, 0.24, -0.005};
			sealife::detail::fin(fins, root, root + Vec3{side * 0.035, 0.01 + 0.04 * raise + 0.008 * flutter, -0.015 + (right ? 0.04 * p.forwardR : 0.0)}, root + Vec3{0.0, -0.02, -0.01});
		}
		for (const double side : {1.0, -1.0}) {
			const Vec3 c{side * 0.02, 0.272, 0.022};
			eye(white, pupil, lid, m, p, c, 0.011, normalize(Vec3{side * 0.8, 0.0, 0.6}));
			brow(brows, c, 0.011, side, p);
		}
		const double open = std::clamp(p.mouthOpen, 0.0, 1.0);
		add(mouth, ellipsoid({0.0, 0.252, 0.103}, {0.007, 0.002 + 0.006 * open, 0.004}, 8, 6));
		fins.uvs.clear();
		return emitted(m, {{coated(bodyC, 0.35), &body}, {translucentMaterial(finC * 0.6, finC * 0.35), &fins}, {coated({0.96, 0.96, 0.95}, 0.08), &white},
						   {coated(irisC, 0.05), &pupil}, {coated(bodyC, 0.35), &lid}, {coated(bodyC * 0.4, 0.4), &brows}, {diffuseMaterial({0.15, 0.04, 0.04}), &mouth}},
					   lean);
	};
	k.handAt = [](const CharacterMoment &m, int s) { return transformPoint(m.frame, Vec3{(s == 0 ? 1.0 : -1.0) * 0.05, 0.25, 0.0} * m.scale); };
	return k;
}

// ---- the pufferfish ----
inline CharacterKind pufferKind() {
	CharacterKind k;
	k.aquatic = true;
	k.chest = {0.0, 0.13, 0.0};
	k.head = {0.0, 0.15, 0.08};
	k.carry = {0.1, 0.12, 0.12};
	k.radius = 0.15, k.height = 0.25;
	k.stride = 0.2;
	k.scalable = true;
	k.poses = namesOf(sea::poses());
	k.voice = {380.0, 1.5, 0.07};
	k.parts = {"body", "belly", "spines", "eyes"};
	k.text = [](const CharacterMoment &m) {
		using namespace detail;
		const SeaPose p = posed(m, {0.0, 0.15, 0.08});
		Vec3 bodyC{0.78, 0.68, 0.38}, bellyC{0.95, 0.93, 0.85}, spineC{0.55, 0.45, 0.25}, irisC{0.05, 0.08, 0.05};
		colourFrom(m, "body", bodyC), colourFrom(m, "belly", bellyC), colourFrom(m, "spines", spineC), colourFrom(m, "eyes", irisC);
		const double puff = std::clamp(p.puff, 0.0, 1.0), wobble = puff > 0.0 ? 0.04 * puff * std::sin(2.0 * kPi * 3.0 * m.t) : 0.0;
		const double r = 0.1 * (1.0 + 1.1 * puff) * (1.0 + wobble);
		const Vec3 c{0.0, 0.13, 0.0}, radii{r * (0.95 + 0.1 * puff), r * (0.85 + 0.15 * puff), r * (1.25 - 0.25 * puff)};
		const Mat4 lean = mul(translate(0.0, 0.13, 0.0), rotate({0, 0, 1}, p.tilt), rotate({1, 0, 0}, p.nod), translate(0.0, -0.13, 0.0));
		Mesh body, belly, spikes, fins, white, pupil, lid, brows, mouth, lips;
		add(body, ellipsoid(c, radii, 26, 16));
		add(belly, ellipsoid(c + Vec3{0.0, -0.3 * radii.y, 0.05 * radii.z}, radii * 0.82, 20, 12));
		for (int q = 0; q < 9; ++q) add(spikes, ellipsoid(c + Vec3{0.75 * radii.x * std::cos(0.7 * q), 0.6 * radii.y, 0.75 * radii.z * std::sin(0.7 * q) - 0.2 * radii.z}, {0.012, 0.004, 0.012}, 6, 4));   // (spots)
		if (puff > 0.05) spines(spikes, c, radii, 0.05 * puff, m.seed);
		const double flap = std::sin(2.0 * kPi * (4.0 + 3.0 * m.going) * m.t);
		const double back = radii.z;
		sealife::detail::fin(fins, c + Vec3{0.0, 0.0, -0.9 * back}, c + Vec3{0.03 * flap, 0.06, -back - 0.07}, c + Vec3{0.03 * flap, -0.05, -back - 0.07});   // (its tail)
		for (const double side : {1.0, -1.0}) {   // its quick little fins: its hands
			const bool right = side < 0.0;
			const double raise = right ? p.raiseR : p.raiseL;
			const Vec3 root = c + Vec3{side * 0.92 * radii.x, -0.1 * radii.y, 0.1 * radii.z};
			sealife::detail::fin(fins, root, root + Vec3{side * 0.05, 0.02 + 0.06 * raise + 0.015 * flap, -0.03 + (right ? 0.05 * p.forwardR : 0.0)}, root + Vec3{0.0, -0.03, -0.02});
		}
		fins.uvs.clear();
		for (const double side : {1.0, -1.0}) {
			const Vec3 e = c + Vec3{side * 0.5 * radii.x, 0.42 * radii.y, 0.78 * radii.z};
			eye(white, pupil, lid, m, p, e, 0.032 + 0.006 * puff, normalize(Vec3{side * 0.3, 0.0, 1.0}));
			brow(brows, e, 0.032, side, p);
		}
		const double open = std::clamp(p.mouthOpen, 0.0, 1.0);
		const Vec3 lipAt = c + Vec3{0.0, -0.12 * radii.y, 0.99 * radii.z};
		add(mouth, ellipsoid(lipAt, {0.016, 0.004 + 0.014 * open, 0.008}, 10, 6));
		add(lips, sdf::tubeMesh({lipAt + Vec3{-0.02, 0.0, -0.004}, lipAt + Vec3{0.0, 0.006, 0.004}, lipAt + Vec3{0.02, 0.0, -0.004}}, 0.007, 0.9, 6));
		return emitted(m, {{coated(bodyC, 0.3), &body}, {coated(bellyC, 0.35), &belly}, {coated(spineC, 0.3), &spikes}, {translucentMaterial(bodyC * 0.6, bodyC * 0.3), &fins},
						   {coated({0.96, 0.96, 0.95}, 0.08), &white}, {coated(irisC, 0.05), &pupil}, {coated(bodyC, 0.3), &lid}, {coated(bodyC * 0.35, 0.4), &brows},
						   {diffuseMaterial({0.2, 0.05, 0.05}), &mouth}, {coated(bodyC * 1.1, 0.3), &lips}},
					   lean);
	};
	k.handAt = [](const CharacterMoment &m, int s) { return transformPoint(m.frame, Vec3{(s == 0 ? 1.0 : -1.0) * 0.14, 0.12, 0.02} * m.scale); };
	return k;
}

inline void install() {
	auto &kinds = film_file::characterKinds();
	kinds["mermaid"] = merKind(false);
	kinds["merman"] = merKind(true);
	kinds["seaturtle"] = seaTurtleKind();
	kinds["shark"] = sharkKind();
	kinds["jellyfish"] = jellyfishKind();
	kinds["seahorse"] = seahorseKind();
	kinds["puffer"] = pufferKind();
}

}  // namespace seacast
}  // namespace film_kinds
}  // namespace cartoon
