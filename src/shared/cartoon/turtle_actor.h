#pragma once
// turtle_actor.h -- a turtle for the cartoon films (a red-eared slider, 13 cm long): a domed shell with its scutes, seams and yellow markings, a plastron
// underneath, a head on a neck that pulls in or stretches out, cartoon eyes that look, blink and narrow, a brow ridge over each that frowns or worries, the
// red patch behind each eye and the yellow stripes down the neck, a beak that smiles and opens, four clawed legs that walk, paddle or reach for a point
// in the world (a hose, a rim), and a short tail. A film acts by filling in a `TurtlePose` for each instant and asking for `emitTurtle(pose)`, as for Pip
// (cartoon_actor.h).
//
// Units are metres (the films that use it are built at real size: a living room, a tank). The rig's own frame has +x forward, +y up and +z to the
// turtle's right; `pose.yaw` turns it about the vertical (0 faces +x, positive turns the front towards +z, as frontOf() in the films).

#include "cartoon_actor.h"

namespace cartoon {
namespace turtle {

struct Look {
	Vec3 skin = {0.24, 0.33, 0.13};
	Vec3 stripe = {0.86, 0.78, 0.30};
	Vec3 shell = {0.22, 0.30, 0.12};
	Vec3 seam = {0.07, 0.09, 0.04};
	Vec3 marking = {0.70, 0.66, 0.26};
	Vec3 plastron = {0.82, 0.74, 0.36};
	Vec3 ear = {0.82, 0.14, 0.07};
	double scale = 1.0;
};

struct TurtlePose {
	Vec3 pos = {0.0, 0.0, 0.0};   // the point under the middle of the shell, on what it stands on
	double yaw = 0.0, pitch = 0.0, roll = 0.0;   // degrees; pitch positive lifts the nose
	double lift = 0.012;          // the plastron's bottom above `pos` (0: lying on its belly)
	double squash = 1.0;          // cartoon squash of the whole turtle (1 = none)
	double neck = 1.0;            // 0 pulled into the shell, 1 out, 1.7 stretched as far as it goes
	double headYaw = 0.0, headPitch = 0.0, headRoll = 0.0;   // degrees, relative to the neck
	Vec3 look = {0.0, 0.0, 0.0};
	double lookWeight = 0.0;
	double blink = 0.0;           // 0 open, 1 shut
	double squint = 0.0;          // 0 open, 1 the eyes narrowed to slits (the lids meet from above and below)
	double eyeWide = 1.0;
	double browL = 0.0, browR = 0.0;   // -1 worried (inner ends up) .. 1 cross (inner ends down)
	double browRaise = 0.0;            // both brows up (surprise) or down
	double smile = 0.15, mouthOpen = 0.0;
	double walkPhase = 0.0, walkAmount = 0.0;   // diagonal pairs of legs, one cycle per 2 pi
	double swimPhase = 0.0, swimAmount = 0.0;   // front legs sweep, hind legs kick
	Vec3 frontL = {0, 0, 0}, frontR = {0, 0, 0}, hindL = {0, 0, 0}, hindR = {0, 0, 0};   // world targets for the feet...
	double frontWeightL = 0.0, frontWeightR = 0.0, hindWeightL = 0.0, hindWeightR = 0.0;   // ...and how much they follow them
	double tuck = 0.0;            // 0 .. 1: legs and tail pulled into the shell
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Sizes (metres, at scale 1)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr double kHalfLength = 0.065;   // the shell
constexpr double kHalfWidth = 0.052;
constexpr double kDomeHeight = 0.040;
constexpr double kPlastron = 0.011;     // the plastron's thickness; the dome sits on it

struct Parts {
	Mesh dome, rimRing, ball, cap;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		// the dome: a quarter ellipse turned about +y, a little flat on top and full at the sides, closed at the bottom
		std::vector<std::pair<double, double>> prof;
		for (int i = 0; i <= 14; ++i) {
			const double a = 0.5 * kPi * i / 14.0;
			prof.push_back({std::pow(std::cos(a), 0.85), std::pow(std::sin(a), 1.15)});
		}
		prof.back().first = 0.0;
		prof.insert(prof.begin(), {0.0, 0.0});
		q.dome = lathe(prof, 56);
		// the marginal scutes: a flat band round the bottom edge, a little wider than the dome
		q.rimRing = lathe({{0.94, 0.0}, {1.02, 0.04}, {1.035, 0.10}, {1.0, 0.16}, {0.95, 0.12}}, 56);
		q.ball = uvSphere(1.0, 16, 8);   // (13 cm of turtle: more would not show, and every frame is parsed)
		q.cap = actor::parts().lid;
		return q;
	}();
	return p;
}

// A point on the dome's surface over (u, v) (u along the shell from tail -1 to head +1, v across from left -1 to right +1), raised `out` above it, in the
// shell's frame (the middle of the plastron's bottom at the origin).
inline Vec3 domePoint(double u, double v, double out = 0.0008) {
	const double r2 = u * u + v * v;
	const double s = std::sqrt(std::max(0.0, 1.0 - std::min(1.0, r2)));
	const double h = kPlastron + kDomeHeight * std::pow(s, 1.0 / 1.15 * 1.0);
	return {kHalfLength * u, h + out, kHalfWidth * v};
}

// The dome's seams and markings, as rods along (u, v) polylines.
inline void addPolyline(std::string &out, const Mat4 &m, const std::vector<std::pair<double, double>> &pts, double r, double outset = 0.0009) {
	for (std::size_t i = 1; i < pts.size(); ++i)
		out += bar(transformPoint(m, domePoint(pts[i - 1].first, pts[i - 1].second, outset)), transformPoint(m, domePoint(pts[i].first, pts[i].second, outset)), r, 3);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Frames {
	Mat4 root;    // on the ground, turned
	Mat4 shell;   // the shell's frame: lifted, pitched, rolled, squashed
	double S;
};
inline Frames framesOf(const Look &look, const TurtlePose &p) {
	Frames f;
	f.S = look.scale;
	const double bob = 0.0025 * p.walkAmount * std::abs(std::sin(p.walkPhase));
	const double sway = 3.0 * p.walkAmount * std::sin(p.walkPhase) + 6.0 * p.swimAmount * std::sin(p.swimPhase);
	f.root = mul(translate(p.pos), rotate({0, 1, 0}, -p.yaw));
	const double sy = std::max(0.3, p.squash), sxz = 1.0 / std::sqrt(sy);
	f.shell = mul(f.root, translate(0.0, (p.lift + bob) * f.S, 0.0), rotate({0, 0, 1}, p.pitch), rotate({1, 0, 0}, p.roll + sway), scale(f.S * sxz, f.S * sy, f.S * sxz));
	return f;
}

// Where a leg's hip or shoulder is on the shell, and where its foot rests (shell frame).
struct LegSite {
	Vec3 hip, rest;
	double side;    // -1 left, +1 right
	bool front;
};
inline LegSite legSite(int k) {
	const bool front = k < 2;
	const double side = (k % 2 == 0) ? -1.0 : 1.0;
	if (front) return {{kHalfLength * 0.52, kPlastron * 0.9, side * kHalfWidth * 0.78}, {kHalfLength * 0.80, -0.012, side * kHalfWidth * 1.12}, side, true};
	return {{-kHalfLength * 0.58, kPlastron * 0.8, side * kHalfWidth * 0.72}, {-kHalfLength * 0.78, -0.012, side * kHalfWidth * 1.05}, side, false};
}

// The foot of leg k for a pose (world): resting, walking, paddling, pulled in, or reaching for its own target.
inline Vec3 footOf(const Frames &f, const TurtlePose &p, int k) {
	const LegSite s = legSite(k);
	Vec3 local = s.rest;
	// diagonal pairs: front-left with hind-right
	const double ph = p.walkPhase + ((k == 0 || k == 3) ? 0.0 : kPi);
	local = local + Vec3{0.014 * p.walkAmount * std::sin(ph) * (s.front ? 1.0 : 0.9), 0.009 * p.walkAmount * std::max(0.0, std::cos(ph)), 0.0};
	// swimming: the front legs sweep forwards and back in a wide arc, the hind legs kick
	const double sp = p.swimPhase + (s.side < 0 ? 0.0 : kPi * 0.15);
	if (p.swimAmount > 0.0) {
		const Vec3 sweep = s.front ? Vec3{0.020 * std::cos(sp), 0.006 + 0.004 * std::sin(sp), s.side * (0.010 + 0.012 * std::sin(sp))}
								   : Vec3{-0.010 - 0.014 * std::cos(sp + kPi), 0.008, s.side * 0.008 * std::sin(sp)};
		local = lerp(local, s.rest + sweep + Vec3{0.0, 0.016, 0.0}, clamp01(p.swimAmount));
	}
	// pulled in: under the shell's edge
	local = lerp(local, s.hip + Vec3{0.0, -0.004, -s.side * 0.012}, clamp01(p.tuck));
	Vec3 world = transformPoint(f.shell, local);
	const Vec3 *target[4] = {&p.frontL, &p.frontR, &p.hindL, &p.hindR};
	const double w[4] = {p.frontWeightL, p.frontWeightR, p.hindWeightL, p.hindWeightR};
	if (w[k] > 0.0) world = lerp(world, *target[k], clamp01(w[k]));
	return world;
}

// The head's frame (world): at the end of the neck, turned by headYaw/headPitch/headRoll. The neck itself bends with part of the turn.
struct Neck {
	Vec3 base, top;
	Mat4 head;
};
inline Neck neckOf(const Frames &f, const TurtlePose &p) {
	const double out = std::max(0.0, p.neck) * (1.0 - 0.8 * clamp01(p.tuck));
	const Vec3 baseLocal = {kHalfLength * 0.80, kPlastron + 0.012, 0.0};
	// the neck points forward and a little up, bent by part of the head's turn
	const Mat4 bend = mul(rotate({0, 1, 0}, -0.45 * p.headYaw), rotate({0, 0, 1}, -0.45 * p.headPitch + 12.0));
	const Vec3 dir = normalize(transformPoint(bend, Vec3{1.0, 0.0, 0.0}));
	const double length = 0.010 + 0.024 * out;
	const Vec3 topLocal = baseLocal + dir * length;
	Neck n;
	n.base = transformPoint(f.shell, baseLocal);
	n.top = transformPoint(f.shell, topLocal);
	n.head = mul(f.shell, translate(topLocal), bend, rotate({0, 1, 0}, -0.55 * p.headYaw), rotate({0, 0, 1}, -0.55 * p.headPitch - 12.0), rotate({1, 0, 0}, p.headRoll),
				 translate(0.014, 0.004, 0.0));
	return n;
}

// The eyes' centres (world), for a film that wants to aim a close-up at them.
inline Vec3 eyeOf(const Look &look, const TurtlePose &p, double side) {
	const Frames f = framesOf(look, p);
	return transformPoint(neckOf(f, p).head, {0.006, 0.0085, side * 0.0115});
}
inline Vec3 headOf(const Look &look, const TurtlePose &p) {
	const Frames f = framesOf(look, p);
	return transformPoint(neckOf(f, p).head, {0.004, 0.0, 0.0});
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The turtle
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string emitTurtle(const TurtlePose &p, const Look &look = Look{}) {
	const Parts &P = parts();
	const Frames f = framesOf(look, p);
	std::string out;
	const std::string skin = coated(look.skin, 0.32), stripe = coated(look.stripe, 0.35), shellMat = coated(look.shell, 0.12), seam = diffuseMaterial(look.seam);
	const std::string marking = coated(look.marking, 0.3), plastron = coated(look.plastron, 0.3), ear = coated(look.ear, 0.25);

	// ---- the shell ----
	out += attr(shellMat, emitMesh(P.dome, mul(f.shell, translate(0.0, kPlastron, 0.0), scale(kHalfLength, kDomeHeight, kHalfWidth))));
	out += attr(coated(lerp(look.shell, look.marking, 0.35), 0.15), emitMesh(P.rimRing, mul(f.shell, translate(0.0, kPlastron * 0.55, 0.0), scale(kHalfLength, 0.040, kHalfWidth))));
	out += attr(plastron, emitMesh(P.ball, mul(f.shell, translate(0.003, kPlastron * 0.5, 0.0), scale(kHalfLength * 0.92, kPlastron * 0.55, kHalfWidth * 0.86))));
	// seams: the five scutes down the spine, the four on each side, the edge
	std::string seams;
	for (double u : {-0.80, -0.47, -0.14, 0.19, 0.52, 0.82}) addPolyline(seams, f.shell, {{u, -0.30}, {u + 0.03, 0.0}, {u, 0.30}}, 0.0011);
	for (double side : {-1.0, 1.0}) {
		std::vector<std::pair<double, double>> spine;
		for (int k = 0; k <= 12; ++k) {
			const double u = -0.80 + 1.62 * k / 12.0;
			spine.push_back({u, side * (0.30 + 0.035 * std::cos(kPi * (u + 0.80) / 0.33))});
		}
		addPolyline(seams, f.shell, spine, 0.0011);
		for (double u : {-0.47, -0.08, 0.32}) addPolyline(seams, f.shell, {{u, side * 0.31}, {u - 0.05, side * 0.62}, {u - 0.10, side * 0.90}}, 0.0010);
	}
	std::vector<std::pair<double, double>> edge;
	for (int k = 0; k <= 40; ++k) {
		const double a = 2.0 * kPi * k / 40.0;
		edge.push_back({0.93 * std::cos(a), 0.93 * std::sin(a)});
	}
	addPolyline(seams, f.shell, edge, 0.0010, 0.0005);
	out += attr(seam, seams);
	// the yellow markings: a curl in each side scute and a thin line down each spine scute
	std::string marks;
	for (double side : {-1.0, 1.0})
		for (double u : {-0.62, -0.27, 0.12, 0.50}) {
			std::vector<std::pair<double, double>> curl;
			for (int k = 0; k <= 8; ++k) {
				const double a = kPi * (0.2 + 1.2 * k / 8.0);
				curl.push_back({u + 0.07 * std::cos(a), side * (0.58 + 0.10 * std::sin(a))});
			}
			addPolyline(marks, f.shell, curl, 0.0007, 0.0012);
		}
	for (double u : {-0.63, -0.30, 0.02, 0.35, 0.66}) addPolyline(marks, f.shell, {{u - 0.06, 0.0}, {u + 0.06, 0.0}}, 0.0007, 0.0012);
	out += attr(marking, marks);
	// dark blotches on the plastron, seen when it climbs or flips
	std::string blotches;
	for (int k = 0; k < 6; ++k)
		blotches += emitMesh(P.ball, mul(f.shell, translate(-0.035 + 0.014 * k, 0.0005, (k % 2 ? 1.0 : -1.0) * 0.012), scale(0.006, 0.0012, 0.005)));
	out += attr(diffuseMaterial({0.25, 0.18, 0.08}), blotches);

	// ---- the neck and the head ----
	const Neck n = neckOf(f, p);
	const double S = f.S;
	std::string skinParts, stripes, claws;
	skinParts += bar(n.base - (n.top - n.base) * 0.3, n.top, 0.0115 * S, 10);
	// head: a rounded wedge (a big ball and a smaller snout ball)
	skinParts += emitMesh(P.ball, mul(n.head, scale(0.019, 0.014, 0.0155)));
	skinParts += emitMesh(P.ball, mul(n.head, translate(0.011, -0.001, 0.0), scale(0.012, 0.0095, 0.0105)));
	// the lower jaw, opening downwards about the back of the mouth
	const double open = clamp01(p.mouthOpen);
	const Mat4 jaw = mul(n.head, translate(-0.006, -0.006, 0.0), rotate({0, 0, 1}, 28.0 * open), translate(0.006, 0.006, 0.0));
	skinParts += emitMesh(P.ball, mul(jaw, translate(0.006, -0.0075, 0.0), scale(0.016, 0.0055, 0.0125)));
	// legs and tail
	for (int k = 0; k < 4; ++k) {
		const LegSite s = legSite(k);
		const Vec3 hip = transformPoint(f.shell, s.hip), foot = footOf(f, p, k);
		const Vec3 pole = transformPoint(f.shell, s.hip + Vec3{0.0, 0.02, s.side * 0.03}) - hip;
		const Limb leg = limbIk(hip, foot, 0.017 * S, 0.017 * S, pole);
		skinParts += bar(hip, leg.elbow, 0.0078 * S, 8) + bar(leg.elbow, leg.hand, 0.0068 * S, 8);
		// the foot: a flat paddle along the leg's direction, with four claws
		const Vec3 along = normalize(leg.hand - leg.elbow);
		const Vec3 flat = normalize(Vec3{along.x, along.y * 0.3, along.z});
		const Mat4 footFrame = alignY(leg.hand, flat);
		skinParts += emitMesh(P.ball, mul(footFrame, translate(0.0, 0.006, 0.0), scale(0.0085, 0.010, 0.0035)));
		for (int c = 0; c < 4; ++c) {
			const double off = (c - 1.5) * 0.0032;
			const Vec3 a = transformPoint(footFrame, {off, 0.014, 0.0}), b = transformPoint(footFrame, {off * 1.25, 0.0185, 0.0});
			claws += bar(a, b, 0.0009, 4);
		}
		// a yellow line down the outside of each leg
		stripes += bar(lerp(hip, leg.elbow, 0.2) + Vec3{0.0, 0.004, 0.0}, lerp(leg.elbow, leg.hand, 0.7) + Vec3{0.0, 0.003, 0.0}, 0.0016, 5);
	}
	{
		const double tailOut = 1.0 - 0.8 * clamp01(p.tuck);
		const Vec3 a = transformPoint(f.shell, {-kHalfLength * 0.9, kPlastron * 0.8, 0.0}), b = transformPoint(f.shell, {-kHalfLength * (0.9 + 0.30 * tailOut), kPlastron * 0.3, 0.0});
		skinParts += bar(a, b, 0.0045 * S, 6);
	}
	out += attr(skin, skinParts);
	// yellow stripes down the neck and along the head
	for (double side : {-1.0, 1.0}) {
		for (double h : {-0.004, 0.003}) {
			const Vec3 a = n.base + Vec3{0.0, h, 0.0} + normalize(cross(n.top - n.base, Vec3{0.0, 1.0, 0.0})) * (side * 0.011);
			const Vec3 b = transformPoint(n.head, {-0.010, h * 0.8, side * 0.0135});
			stripes += bar(a, b, 0.0012, 5);
			stripes += bar(b, transformPoint(n.head, {0.012, h * 0.6 - 0.002, side * 0.0095}), 0.0010, 5);
		}
	}
	stripes += bar(transformPoint(n.head, {-0.010, 0.0135, 0.0}), transformPoint(n.head, {0.012, 0.0105, 0.0}), 0.0010, 5);   // down the top of the head
	out += attr(stripe, stripes);
	out += attr(coated({0.12, 0.10, 0.06}, 0.3), claws);
	// the red patch behind each eye
	std::string ears;
	for (double side : {-1.0, 1.0}) ears += emitMesh(P.ball, mul(n.head, translate(-0.008, 0.004, side * 0.0145), rotate({0, 1, 0}, side * 10.0), scale(0.0085, 0.0040, 0.0022)));
	out += attr(ear, ears);
	// the mouth: the beak's edge, curving up at the corners for a smile, and the dark inside when it opens
	{
		std::string lip;
		const double smile = std::max(-1.0, std::min(1.0, p.smile));
		Vec3 prev{};
		for (int k = 0; k <= 8; ++k) {
			const double u = -1.0 + 0.25 * k;   // -1 left corner .. +1 right corner
			const double x = 0.021 - 0.014 * u * u, y = -0.0045 + 0.0025 * smile * u * u - 0.0012 * smile;
			const Vec3 q = transformPoint(n.head, {x, y, u * 0.0108});
			if (k > 0) lip += bar(prev, q, 0.0007, 4);
			prev = q;
		}
		out += attr(diffuseMaterial({0.06, 0.05, 0.03}), lip);
		if (open > 0.05) out += attr(diffuseMaterial({0.35, 0.10, 0.10}), emitMesh(P.ball, mul(n.head, translate(0.008, -0.0065, 0.0), scale(0.010, 0.003 + 0.004 * open, 0.008))));
		// the nostrils
		std::string nose;
		for (double side : {-1.0, 1.0}) nose += emitMesh(P.ball, mul(n.head, translate(0.0218, 0.0018, side * 0.0022), scale(0.0009)));
		out += attr(diffuseMaterial({0.03, 0.03, 0.02}), nose);
	}
	// ---- the eyes ----
	{
		const double wide = std::max(0.4, p.eyeWide);
		std::string whites, pupils, highlights, lids, brows;
		for (double side : {-1.0, 1.0}) {
			const Vec3 c = {0.006, 0.0085, side * 0.0115};
			const double R = 0.0058 * wide;
			whites += emitMesh(P.ball, mul(n.head, translate(c), scale(R)));
			Vec3 dir = {0.35, 0.05, side * 0.94};   // turtles look out to the sides
			dir = normalize(dir);
			if (p.lookWeight > 0.0) {
				const Vec3 t = transformPoint(invertAffine(n.head), p.look);
				Vec3 d = normalize(t - c);
				if (d.z * side < 0.15) d.z = side * 0.15;   // the eye cannot see through the head
				dir = normalize(lerp(dir, normalize(d), clamp01(p.lookWeight)));
			}
			const Vec3 pc = c + dir * (R * 0.60);
			pupils += emitMesh(P.ball, mul(n.head, translate(pc), scale(R * 0.56)));
			highlights += emitMesh(P.ball, mul(n.head, translate(pc + normalize(dir + Vec3{0.3, 0.6, 0.0}) * (R * 0.32) + dir * (R * 0.2)), scale(R * 0.15)));
			// lids: the upper one comes down with a blink or a squint, the lower one comes up with a squint
			const Vec3 outward = normalize(Vec3{0.25, 0.0, side});
			const double up = clamp01(std::max(p.blink, 0.62 * p.squint));
			const double low = clamp01(std::max(p.blink * 0.4, 0.55 * p.squint));
			const auto lid = [&](double amount, double sign) {
				const double psi = (180.0 - 180.0 * amount) * kPi / 180.0;
				Vec3 axis = outward * std::cos(psi) + Vec3{0.0, sign, 0.0} * std::sin(psi);
				// a cross or worried tilt slants the upper lid
				const double tilt = sign > 0 ? 0.35 * (side < 0 ? p.browL : p.browR) : 0.0;
				axis = normalize(axis + Vec3{tilt * 0.6, 0.0, 0.0});
				return emitMesh(P.cap, mul(n.head, alignY(c, axis), scale(R * 1.02)));
			};
			if (up > 0.02) lids += lid(up, 1.0);
			if (low > 0.02) lids += lid(low, -1.0);
			// the brow ridge: a short bar over the eye, its inner end up (worried) or down (cross)
			const double b = side < 0 ? p.browL : p.browR;
			const double h = R * 1.35 + 0.0016 * p.browRaise;
			const Vec3 inner = c + Vec3{0.004, h - 0.0018 * b, -side * 0.003}, outer = c + Vec3{-0.003, h + 0.0012 * b, side * 0.0015};
			brows += bar(transformPoint(n.head, inner), transformPoint(n.head, outer), 0.0011, 5);
		}
		out += attr(coated({0.93, 0.92, 0.80}, 0.08), whites);
		out += attr(coated({0.04, 0.04, 0.03}, 0.04), pupils);
		out += attr(coated({0.98, 0.98, 0.95}, 0.05), highlights);
		if (!lids.empty()) out += attr(skin, lids);
		out += attr(coated(look.skin * 0.55, 0.4), brows);
	}
	return out;
}

}  // namespace turtle
}  // namespace cartoon
