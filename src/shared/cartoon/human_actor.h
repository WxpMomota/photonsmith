#pragma once
// human_actor.h -- a cartoon person for the films (an adult, about 1.8 m), sculpted rather than assembled (sdf_mesh.h): a head with a round cranium, a soft jaw
// and chin, full cheeks, a round nose and ears, eyes set in their sockets with big irises, lids and brows, a mouth that smiles or opens; a swept hairstyle;
// a knitted sweater whose sleeves grow out of its shoulders, with a ribbed collar, cuffs and hem; trousers; sneakers with white soles; four-fingered hands
// with a thumb that close round what they hold. Two arms reach for a point in the world, two legs walk, the body bends at the hips and crouches at the knees.
// A film fills in a `HumanPose` for each instant and asks for `emitHuman(pose)`, as for the turtle; props in the hands (keys, grocery bags) are part of the
// pose.
//
// Units are metres. The rig's own frame has +x forward, +y up and +z to the person's right; `pose.yaw` turns them about the vertical (0 faces +x).

#include <array>

#include "cartoon_actor.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace human {

struct Outfit {
	Vec3 skin = {0.80, 0.56, 0.42};
	Vec3 hair = {0.17, 0.09, 0.05};
	Vec3 sweater = {0.78, 0.50, 0.15};
	Vec3 trousers = {0.19, 0.26, 0.42};
	Vec3 shoes = {0.70, 0.13, 0.11};
	Vec3 sole = {0.93, 0.92, 0.88};
	Vec3 eyes = {0.20, 0.11, 0.05};
	Vec3 lips = {0.58, 0.28, 0.26};
	Vec3 blush = {0.88, 0.50, 0.45};
};

struct HumanPose {
	Vec3 pos = {0.0, 0.0, 0.0};   // on the floor, between the feet
	double yaw = 0.0;
	double lean = 0.0;            // forward bend at the hips, degrees
	double crouch = 0.0;          // 0 standing .. 1 squatting
	double walkPhase = 0.0, walkAmount = 0.0;
	double headYaw = 0.0, headPitch = 0.0;   // degrees (positive pitch looks down)
	Vec3 look = {0.0, 0.0, 0.0};
	double lookWeight = 0.0;
	double blink = 0.0, eyeWide = 1.0, browRaise = 0.0, browTilt = 0.0;   // browTilt positive = worried
	double smile = 0.3, mouthOpen = 0.0;
	Vec3 handL = {0, 0, 0}, handR = {0, 0, 0};
	double handWeightL = 0.0, handWeightR = 0.0;
	bool keys = false;            // car keys in the right hand
	bool bags = false;            // a grocery bag in each hand
	bool feetHeld = false;        // the feet at `heldFoot` (world: left, right) instead of where the walk puts them - a foot held where it came down (rig::FootLock)
	Vec3 heldFoot[2];
};

constexpr double kHip = 0.92, kThigh = 0.45, kShin = 0.44, kTorso = 0.55, kHeadR = 0.122;
constexpr double kEyeR = 0.028;   // big, friendly eyes

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The head and the hair, in the head's frame (+x forward, +y up, +z right; the origin in the middle of the head). Drawn for appeal: a big round head, a soft
// round lower face with full cheeks, eyes set at half height, a button nose, small round ears, and a rounded hairstyle with a fringe swept to one side.
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline double headShape(Vec3 p) {
	double d = sdf::ellipsoid(p, Vec3{-0.004, 0.030, 0.0}, Vec3{0.126, 0.134, 0.120});                       // the cranium
	d = sdf::smin(d, sdf::ellipsoid(p, Vec3{0.028, -0.048, 0.0}, Vec3{0.104, 0.090, 0.108}), 0.070);          // the lower face, soft and round
	for (double side : {-1.0, 1.0}) {
		d = sdf::smin(d, sdf::ellipsoid(p, Vec3{0.080, -0.046, side * 0.058}, Vec3{0.044, 0.038, 0.040}), 0.040);   // the cheeks
		d = sdf::smin(d, sdf::ellipsoid(p, Vec3{-0.014, -0.006, side * 0.123}, Vec3{0.024, 0.036, 0.017}), 0.008);  // the ears
	}
	d = sdf::smin(d, sdf::sphere(p, Vec3{0.143, -0.020, 0.0}, 0.0215), 0.016);                                    // a button nose
	return d;
}
inline double hairMass(Vec3 p) {
	double d = sdf::ellipsoid(p, Vec3{-0.012, 0.040, 0.0}, Vec3{0.137, 0.146, 0.132});
	// the hairline: across the top of the forehead, just above the ears, down to the nape
	const double line = p.x > 0.0 ? 0.062 + 0.40 * p.x : 0.062 + 1.30 * p.x;
	d = sdf::smax(d, line - p.y, 0.020);
	// a fringe swept across the forehead from the left, a round full top, a full back, two little tufts at the crown
	static const Mat4 fringe = mul(rotate({1, 0, 0}, 22.0), translate(Vec3{-0.100, -0.118, 0.020}));
	d = sdf::smin(d, sdf::ellipsoid(p, fringe, Vec3{0.046, 0.040, 0.104}), 0.035);
	d = sdf::smin(d, sdf::ellipsoid(p, Vec3{0.000, 0.150, 0.0}, Vec3{0.112, 0.048, 0.108}), 0.040);
	d = sdf::smin(d, sdf::ellipsoid(p, Vec3{-0.070, 0.080, 0.0}, Vec3{0.080, 0.088, 0.114}), 0.040);
	// locks at the fringe's edge, pointing down and across the forehead
	for (int k = 0; k < 4; ++k) {
		const double z = -0.070 + 0.034 * k;
		d = sdf::smin(d, sdf::roundCone(p, {0.112, 0.112 + 0.010 * k, z}, {0.128, 0.082 + 0.012 * k - 0.004 * (k == 0), z + 0.018}, 0.016, 0.004), 0.012);
	}
	d = sdf::smin(d, sdf::ellipsoid(p, Vec3{-0.060, 0.182, 0.020}, Vec3{0.030, 0.026, 0.022}), 0.020);
	d = sdf::smin(d, sdf::ellipsoid(p, Vec3{-0.085, 0.172, -0.012}, Vec3{0.026, 0.022, 0.020}), 0.018);
	return d;
}
// The hair with its locks drawn in: shallow grooves combed back over the top and down the sides, following the hair's own surface.
inline double hairShape(Vec3 p) {
	static const std::vector<std::vector<Vec3>> grooves = [] {
		std::vector<std::vector<Vec3>> g;
		const Vec3 centre = {-0.012, 0.030, 0.0};
		for (double phi : {-58.0, -34.0, -12.0, 10.0, 32.0, 56.0}) {
			std::vector<Vec3> line;
			const double f = phi * kPi / 180.0;
			for (int k = 0; k <= 12; ++k) {
				const double th = (42.0 + 104.0 * k / 12.0) * kPi / 180.0;   // from behind the fringe over the top to the back
				const Vec3 dir = normalize({std::cos(th), std::sin(th) * std::cos(f), std::sin(th) * std::sin(f) + 0.12 * std::sin(2.0 * th)});
				line.push_back(sdf::radialHit(hairMass, centre, dir, 0.3) + dir * 0.0085);
			}
			g.push_back(line);
		}
		return g;
	}();
	double d = hairMass(p);
	for (const auto &line : grooves)
		for (std::size_t i = 1; i < line.size(); ++i) {   // (each groove fades out at its ends)
			// a wide shallow channel: a fat rod lying just outside the hair, so that only the bottom of its curve cuts in (deepest mid-way, nothing at the ends)
			const auto r = [&](std::size_t k) { return 0.0085 + 0.0022 * std::sin(kPi * static_cast<double>(k) / static_cast<double>(line.size() - 1)); };
			d = sdf::smax(d, -sdf::roundCone(p, line[i - 1], line[i], r(i - 1), r(i)), 0.005);
		}
	return d;
}
// Each eye's centre (head frame): set into the face at half height, a little apart, sunk a little so that the face holds it.
inline Vec3 eyeCentreLocal(double side) {
	static const std::array<Vec3, 2> e = [] {
		std::array<Vec3, 2> r;
		for (int s = 0; s < 2; ++s) {
			const Vec3 dir = normalize({0.86, 0.10, (s == 0 ? -1.0 : 1.0) * 0.44});
			const Vec3 q = sdf::radialHit(headShape, {0.0, 0.0, 0.0}, dir, 0.3);
			r[static_cast<std::size_t>(s)] = q - sdf::normalAt(headShape, q, 4e-4) * (0.45 * kEyeR);
		}
		return r;
	}();
	return e[side < 0 ? 0 : 1];
}
// A point on the face seen from the head's middle in direction `dir`, and the normal there.
inline Vec3 onFace(Vec3 dir, Vec3 *normal = nullptr) {
	const Vec3 p = sdf::radialHit(headShape, {0.0, 0.0, 0.0}, normalize(dir), 0.3);
	if (normal) *normal = sdf::normalAt(headShape, p, 4e-4);
	return p;
}

struct Parts {
	Mesh ball, cap, ring, head, hair, blush, eyeball, limbal, iris, glow, pupil, disc;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		q.ball = uvSphere(1.0, 20, 10);
		q.cap = actor::parts().lid;
		q.ring = torus(1.0, 0.18, 40, 10);
		q.head = sdf::radialMesh(headShape, {0.0, 0.0, 0.0}, 0.3, 72, 54);
		q.hair = sdf::surfaceNets(hairShape, {-0.17, -0.11, -0.16}, {0.17, 0.215, 0.16}, 0.0048);
		for (double side : {-1.0, 1.0}) {
			const Mesh b = sdf::decal(headShape, {0.0, 0.0, 0.0}, {0.70, -0.36, side * 0.60}, {1.0, 0.2, 0.0}, 0.024, 0.015, 0.0002, 0.3);
			const int base = static_cast<int>(q.blush.verts.size());
			q.blush.verts.insert(q.blush.verts.end(), b.verts.begin(), b.verts.end());
			q.blush.normals.insert(q.blush.normals.end(), b.normals.begin(), b.normals.end());
			for (auto t : b.tris) q.blush.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
		}
		q.eyeball = uvSphere(1.0, 40, 20);
		const auto cap = [](double degrees, double r) {
			std::vector<std::pair<double, double>> prof;
			for (int i = 0; i <= 10; ++i) {
				const double a = degrees * kPi / 180.0 * (1.0 - i / 10.0);
				prof.push_back({r * std::sin(a), r * std::cos(a)});
			}
			return lathe(prof, 40);
		};
		q.limbal = cap(41.0, 1.0015);
		q.iris = cap(37.0, 1.0030);
		q.glow = cap(19.0, 1.0045);
		q.pupil = cap(17.0, 1.0060);
		q.disc = uvSphere(1.0, 16, 6);
		return q;
	}();
	return p;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The rig
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Frames {
	Mat4 root, pelvis, chest, head;
	double hipY;
};
inline Frames framesOf(const HumanPose &p) {
	Frames f;
	const double bob = 0.025 * p.walkAmount * std::abs(std::cos(p.walkPhase));
	f.hipY = kHip * (1.0 - 0.42 * clamp01(p.crouch)) - bob;
	f.root = mul(translate(p.pos), rotate({0, 1, 0}, -p.yaw));
	const double twist = 5.0 * p.walkAmount * std::sin(p.walkPhase);
	f.pelvis = mul(f.root, translate(0.0, f.hipY, 0.0), rotate({0, 1, 0}, twist));
	f.chest = mul(f.pelvis, rotate({0, 0, 1}, -p.lean), rotate({0, 1, 0}, -2.0 * twist));
	f.head = mul(f.chest, translate(0.0, kTorso + 0.05, 0.0), rotate({0, 1, 0}, -p.headYaw), rotate({0, 0, 1}, -p.headPitch), translate(0.0, kHeadR * 0.95, 0.0));
	return f;
}

// Where a foot is (world) for the walk: a stride forwards and back, lifted as it swings through.
inline Vec3 footOf(const Frames &f, const HumanPose &p, double side) {
	if (p.feetHeld) return p.heldFoot[side < 0 ? 0 : 1];
	const double ph = p.walkPhase + (side < 0 ? 0.0 : kPi);
	const double fwd = 0.30 * p.walkAmount * std::sin(ph), up = 0.11 * p.walkAmount * std::max(0.0, std::cos(ph));
	return transformPoint(f.root, {fwd + 0.02, 0.055 + up, side * (0.11 + 0.05 * clamp01(p.crouch))});
}

struct Arm {
	Vec3 shoulder, elbow, hand;
};
inline Arm armOf(const Frames &f, const HumanPose &p, double side) {
	const Vec3 shoulder = transformPoint(f.chest, {0.0, kTorso - 0.07, side * 0.195});
	// at rest the arm hangs and swings against the legs while walking
	const double swing = -0.20 * p.walkAmount * std::sin(p.walkPhase + (side < 0 ? 0.0 : kPi));
	const Vec3 rest = transformPoint(f.chest, {0.06 + swing, kTorso - 0.62, side * 0.27});
	const double w = clamp01(side < 0 ? p.handWeightL : p.handWeightR);
	Vec3 target = lerp(rest, side < 0 ? p.handL : p.handR, w);
	const double reach = 0.57;
	if (length(target - shoulder) > reach) target = shoulder + normalize(target - shoulder) * reach;
	const Limb l = limbIk(shoulder, target, 0.30, 0.28, transformPoint(f.chest, {-0.6, -0.5, side * 0.5}) - transformPoint(f.chest, {0.0, 0.0, 0.0}));
	return {shoulder, l.elbow, l.hand};
}

inline Vec3 headCentre(const HumanPose &p) { return transformPoint(framesOf(p).head, {0.0, 0.0, 0.0}); }
inline Vec3 handOf(const HumanPose &p, double side) { return armOf(framesOf(p), p, side).hand; }

// A paper grocery bag standing on `base` (world), turned by `yaw`, with what sticks out of the top: a loaf, a leek, apples.
inline std::string groceryBag(Vec3 base, double yaw, int salt) {
	std::string out;
	const Mat4 m = mul(translate(base), rotate({0, 1, 0}, -yaw));
	out += attr(diffuseMaterial({0.66, 0.50, 0.32}), emitMesh(roundedBox(0.26, 0.34, 0.17, 0.012, 2), mul(m, translate(0.0, 0.17, 0.0))));
	out += attr(diffuseMaterial({0.55, 0.40, 0.25}), emitMesh(roundedBox(0.262, 0.03, 0.172, 0.01, 2), mul(m, translate(0.0, 0.325, 0.0))));   // the folded rim
	out += attr(coated({0.80, 0.56, 0.28}, 0.6), emitMesh(capsule(0.035, 0.30, 12, 4), mul(m, translate(-0.05, 0.22, -0.03), rotate({0, 0, 1}, -14.0 - 6.0 * salt))));   // a baguette
	out += attr(coated({0.30, 0.55, 0.18}, 0.5), emitMesh(capsule(0.016, 0.26, 10, 3), mul(m, translate(0.06, 0.25, 0.04), rotate({1, 0, 0}, 10.0))));   // a leek
	for (int k = 0; k < 3; ++k)
		out += attr(coated(k == 1 ? Vec3{0.85, 0.55, 0.12} : Vec3{0.75, 0.10, 0.08}, 0.25), emitMesh(parts().ball, mul(m, translate(-0.06 + 0.06 * k, 0.33, 0.04 - 0.03 * k), scale(0.038))));
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The person
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A hand in its frame (+y along the fingers, +x out of the back of the hand... the thumb towards `side`), the fingers curled by `grip` (0 open, 1 a fist round a handle).
inline Mesh handMesh(const Mat4 &frame, double side, double grip) {
	const Mat4 inv = invertAffine(frame);
	const auto shape = [&](Vec3 w) {
		const Vec3 q = transformPoint(inv, w);
		double d = sdf::roundBox(q, translate(Vec3{0.0, -0.046, 0.0}), {0.017, 0.044, 0.041}, 0.016);   // the palm
		for (int k = 0; k < 4; ++k) {
			const double z = side * (-0.028 + 0.0185 * k), len = k == 0 || k == 3 ? 0.036 : 0.042;
			const Vec3 a = {0.0, 0.082, z}, b = {-0.020 * grip, 0.082 + len * (1.0 - 0.45 * grip), z * 1.05}, c = {-0.034 * grip, 0.082 + len * (1.0 - 0.9 * grip), z * 1.08};
			d = sdf::smin(d, sdf::smin(sdf::capsule(q, a, b, 0.0098), sdf::capsule(q, b, c, 0.0092), 0.004), 0.010);
		}
		d = sdf::smin(d, sdf::roundCone(q, {-0.006, 0.030, side * 0.030}, {-0.020 - 0.010 * grip, 0.068, side * (0.046 - 0.012 * grip)}, 0.0125, 0.0100), 0.012);   // the thumb
		return d;
	};
	Vec3 lo, hi;
	sdf::boundsOf({transformPoint(frame, {0.0, 0.0, 0.0}), transformPoint(frame, {0.0, 0.13, 0.0})}, 0.07, lo, hi);
	return sdf::surfaceNets(shape, lo, hi, 0.0050);
}

inline std::string emitHuman(const HumanPose &p, const Outfit &o = Outfit{}) {
	const Parts &P = parts();
	const Frames f = framesOf(p);
	std::string out;
	const std::string skin = diffuseMaterial(o.skin), hair = diffuseMaterial(o.hair), sweater = diffuseMaterial(o.sweater), rib = diffuseMaterial(o.sweater * 0.82);
	const Arm arms[2] = {armOf(f, p, -1.0), armOf(f, p, 1.0)};

	// ---- legs, hips and shoes ----
	{
		Vec3 hip[2], knee[2], ankle[2];
		for (int s = 0; s < 2; ++s) {
			const double side = s == 0 ? -1.0 : 1.0;
			hip[s] = transformPoint(f.pelvis, {0.0, -0.02, side * 0.095});
			const Limb l = limbIk(hip[s], footOf(f, p, side), kThigh, kShin, transformPoint(f.root, {1.0, 0.0, side * 0.1}) - transformPoint(f.root, {0.0, 0.0, 0.0}));
			knee[s] = l.elbow;
			ankle[s] = l.hand;
		}
		const Mat4 pelvisInv = invertAffine(f.pelvis);
		const auto trousers = [&](Vec3 q) {
			double d = sdf::ellipsoid(q, pelvisInv, {0.128, 0.13, 0.180});
			for (int s = 0; s < 2; ++s) {
				const Vec3 cuff = ankle[s] + Vec3{0.0, 0.035, 0.0};
				d = sdf::smin(d, sdf::smin(sdf::roundCone(q, hip[s], knee[s], 0.080, 0.062), sdf::roundCone(q, knee[s], cuff, 0.062, 0.056), 0.025), 0.05);
			}
			return d;
		};
		Vec3 lo, hi;
		sdf::boundsOf({transformPoint(f.pelvis, {0.0, 0.14, 0.0}), hip[0], hip[1], knee[0], knee[1], ankle[0], ankle[1]}, 0.20, lo, hi);
		out += attr(diffuseMaterial(o.trousers), emitMesh(sdf::surfaceNets(trousers, lo, hi, 0.0175), identity()));
		// sneakers: a rounded upper and a white sole, pointing the way the person faces
		std::string uppers, soles;
		for (int s = 0; s < 2; ++s) {
			const Mat4 shoe = mul(translate(ankle[s]), rotate({0, 1, 0}, -p.yaw), translate(0.045, -0.050, 0.0));
			const Mat4 inv = invertAffine(shoe);
			const auto upper = [&](Vec3 q) {
				return sdf::smin(sdf::ellipsoid(q, mul(translate(Vec3{-0.004, -0.048, 0.0}), inv), {0.128, 0.058, 0.060}),
								 sdf::ellipsoid(q, mul(translate(Vec3{0.045, -0.085, 0.0}), inv), {0.056, 0.056, 0.051}), 0.03);
			};
			const auto sole = [&](Vec3 q) { return sdf::roundBox(q, mul(translate(Vec3{0.0, -0.012, 0.0}), inv), {0.127, 0.012, 0.058}, 0.010); };
			Vec3 lo2, hi2;
			sdf::boundsOf({transformPoint(shoe, {-0.14, 0.0, 0.0}), transformPoint(shoe, {0.14, 0.12, 0.0})}, 0.07, lo2, hi2);
			uppers += emitMesh(sdf::surfaceNets(upper, lo2, hi2, 0.0080), identity());
			soles += emitMesh(sdf::surfaceNets(sole, lo2, hi2, 0.0080), identity());
		}
		out += attr(diffuseMaterial(o.shoes), uppers);
		out += attr(diffuseMaterial(o.sole), soles);
	}

	// ---- the sweater: the body, the shoulders and the sleeves in one piece ----
	{
		const Mat4 chestInv = invertAffine(f.chest);
		Vec3 wrist[2];
		for (int s = 0; s < 2; ++s) wrist[s] = lerp(arms[s].elbow, arms[s].hand, 0.84);
		const auto knit = [&](Vec3 q) {
			const Vec3 c = transformPoint(chestInv, q);
			double d = sdf::ellipsoid(c, Vec3{0.0, 0.36, 0.0}, Vec3{0.122, 0.205, 0.185});
			d = sdf::smin(d, sdf::ellipsoid(c, Vec3{0.010, 0.15, 0.0}, Vec3{0.124, 0.18, 0.170}), 0.06);
			d = sdf::smin(d, sdf::ellipsoid(c, Vec3{0.0, 0.03, 0.0}, Vec3{0.118, 0.09, 0.165}), 0.05);
			for (int s = 0; s < 2; ++s) {
				const Vec3 top = lerp(arms[s].shoulder, transformPoint(f.chest, {0.0, kTorso - 0.10, 0.0}), 0.18);   // (the sleeve starts a little in from the shoulder, so it does not stand up like a pad)
				const double sleeve = sdf::smin(sdf::roundCone(q, top, arms[s].elbow, 0.054, 0.048), sdf::roundCone(q, arms[s].elbow, wrist[s], 0.048, 0.043), 0.025);
				d = sdf::smin(d, sleeve, 0.075);
			}
			return d;
		};
		Vec3 lo, hi;
		sdf::boundsOf({transformPoint(f.chest, {0.0, -0.05, 0.0}), transformPoint(f.chest, {0.0, kTorso + 0.04, 0.0}), arms[0].shoulder, arms[1].shoulder, arms[0].elbow,
					   arms[1].elbow, wrist[0], wrist[1]},
					  0.17, lo, hi);
		out += attr(sweater, emitMesh(sdf::surfaceNets(knit, lo, hi, 0.0165), identity()));
		std::string ribs = emitMesh(P.ring, mul(f.chest, translate(0.0, -0.005, 0.0), scale(0.120, 0.07, 0.168)));   // the ribbed hem
		ribs += emitMesh(P.ring, mul(f.chest, translate(0.0, kTorso - 0.012, 0.0), scale(0.074, 0.07, 0.080)));          // the collar
		for (int s = 0; s < 2; ++s) ribs += emitMesh(P.ring, mul(alignY(wrist[s], normalize(arms[s].hand - arms[s].elbow)), scale(0.043, 0.09, 0.043)));   // the cuffs
		out += attr(rib, ribs);
	}

	// ---- the neck, the hands and the props ----
	std::string skinParts;
	{
		const Vec3 a = transformPoint(f.chest, {0.0, kTorso - 0.03, 0.0}), b = transformPoint(f.head, {-0.015, -0.100, 0.0});
		// (it flares into the underside of the jaw, so that the head grows out of it rather than sitting on a post)
		const Mat4 jaw = invertAffine(mul(f.head, translate(-0.010, -0.085, 0.0)));
		const auto neck = [&](Vec3 q) { return sdf::smin(sdf::roundCone(q, a, b, 0.064, 0.058), sdf::ellipsoid(q, jaw, {0.070, 0.040, 0.075}), 0.035); };
		Vec3 lo, hi;
		sdf::boundsOf({a, b}, 0.09, lo, hi);
		skinParts += emitMesh(sdf::surfaceNets(neck, lo, hi, 0.011), identity());
	}
	for (int s = 0; s < 2; ++s) {
		const double side = s == 0 ? -1.0 : 1.0;
		const Arm &a = arms[s];
		const Vec3 wristDir = normalize(a.hand - a.elbow);
		const Mat4 hand = alignY(lerp(a.elbow, a.hand, 0.90), wristDir, transformPoint(f.chest, {1.0, 0.0, 0.0}) - transformPoint(f.chest, {0.0, 0.0, 0.0}));
		const double grip = p.bags || (side > 0 && p.keys) ? 0.85 : 0.25;
		skinParts += emitMesh(handMesh(hand, side, grip), identity());
		if (side > 0 && p.keys) {
			std::string metal = emitMesh(P.ring, mul(hand, translate(0.0, 0.10, 0.0), rotate({1, 0, 0}, 90.0), scale(0.018, 0.02, 0.018)));
			metal += bar(transformPoint(hand, {0.0, 0.11, 0.0}), transformPoint(hand, {0.01, 0.16, 0.012}), 0.004, 6);
			metal += bar(transformPoint(hand, {0.0, 0.11, 0.0}), transformPoint(hand, {-0.012, 0.155, -0.01}), 0.004, 6);
			out += attr(metalMaterial({0.85, 0.80, 0.62}, 0.15), metal);
			out += attr(coated({0.08, 0.08, 0.09}, 0.3), emitMesh(roundedBox(0.03, 0.045, 0.012, 0.005, 2), mul(hand, translate(0.0, 0.13, 0.025))));   // the fob
		}
		if (p.bags) out += groceryBag(a.hand - Vec3{0.0, 0.33, 0.0}, p.yaw + side * 8.0, s);
	}

	// ---- the head ----
	skinParts += emitMesh(P.head, f.head);
	out += attr(skin, skinParts);
	out += attr(hair, emitMesh(P.hair, f.head));
	out += attr(diffuseMaterial(o.blush), emitMesh(P.blush, f.head));
	// eyes in their sockets: the white, a dark ring, the iris with a warm glow low in it, the pupil, two catchlights; lids that rest a little lowered
	{
		std::string whites, limbals, irises, glows, pupils, sparks, lids, lashes, brows;
		const double wide = std::max(0.5, p.eyeWide);
		for (double side : {-1.0, 1.0}) {
			const Vec3 c = eyeCentreLocal(side);
			const double R = kEyeR * std::min(1.12, wide);
			whites += emitMesh(P.eyeball, mul(f.head, translate(c), scale(R)));
			Vec3 dir = {1.0, 0.0, side * 0.06};
			if (p.lookWeight > 0.0) {
				Vec3 d = normalize(transformPoint(invertAffine(f.head), p.look) - c);
				d.x = std::max(d.x, 0.5);
				dir = normalize(lerp(normalize(dir), normalize(d), clamp01(p.lookWeight)));
			}
			dir = normalize(dir);
			const Mat4 eye = mul(f.head, alignY(c, dir), scale(R));
			limbals += emitMesh(P.limbal, eye);
			irises += emitMesh(P.iris, eye);
			glows += emitMesh(P.glow, mul(f.head, alignY(c, normalize(dir - Vec3{0.0, 0.25, 0.0})), scale(R)));
			pupils += emitMesh(P.pupil, eye);
			const Vec3 s1 = normalize(dir + Vec3{0.0, 0.40, side * 0.18}), s2 = normalize(dir + Vec3{0.0, -0.30, -side * 0.22});
			sparks += emitMesh(P.disc, mul(f.head, alignY(c + s1 * (R * 1.007), s1), scale(R * 0.17, R * 0.01, R * 0.17)));
			sparks += emitMesh(P.disc, mul(f.head, alignY(c + s2 * (R * 1.007), s2), scale(R * 0.08, R * 0.01, R * 0.08)));
			// the upper lid rests a fifth of the way down (wide eyes lift it), the lower one a little up
			const double up = clamp01(std::max(clamp01(p.blink), 0.30 - 0.40 * (wide - 1.0) + 0.10 * p.browTilt));
			const double low = clamp01(std::max(0.35 * clamp01(p.blink), 0.10));
			const auto lid = [&](double amount, double sign) {
				const double psi = (180.0 - 180.0 * amount) * kPi / 180.0;
				return emitMesh(P.cap, mul(f.head, alignY(c, Vec3{std::cos(psi), sign * std::sin(psi), 0.0}), scale(R * 1.03)));
			};
			lids += lid(up, 1.0);
			lids += lid(low, -1.0);
			// the lash line along the upper lid's edge, heavier towards the outer corner, with a small flick
			{
				const double psi = (180.0 - 180.0 * up) * kPi / 180.0;
				const Vec3 a = {std::cos(psi), std::sin(psi), 0.0}, u = {0.0, 0.0, side}, w = {a.y, -a.x, 0.0};
				std::vector<Vec3> lash;
				for (int k = 0; k <= 12; ++k) {
					const double tt = kPi * (0.12 + 0.80 * k / 12.0);
					lash.push_back(c + (u * std::cos(kPi - tt) + w * std::sin(tt)) * (R * 1.045));
				}
				lash.push_back(lash.back() + (u * 0.55 + Vec3{0.0, 0.45, 0.0} + w * 0.2) * (R * 0.22));
				lashes += emitMesh(sdf::tubeMesh(lash, R * 0.055, 0.35, 6), f.head);
			}
			// the brow: a thick arc on the brow ridge, raised in surprise, its inner end up when worried
			std::vector<Vec3> arc;
			for (int k = 0; k <= 8; ++k) {
				const double u = k / 8.0;   // inner .. outer
				const double y = 0.068 + 0.012 * p.browRaise + 0.008 * std::sin(kPi * u) + lerp(0.010 * p.browTilt, -0.005 * p.browTilt, u);
				const Vec3 dirB = {1.0, y / 0.115, side * lerp(0.16, 0.66, u)};
				Vec3 nrm;
				const Vec3 q = onFace(dirB, &nrm);
				arc.push_back(q + nrm * 0.0035);
			}
			brows += emitMesh(sdf::tubeMesh(arc, 0.0068, 0.50, 8), f.head);
		}
		out += attr(diffuseMaterial({0.95, 0.94, 0.92}), whites);
		out += attr(diffuseMaterial(o.eyes * 0.4), limbals);
		out += attr(diffuseMaterial(o.eyes), irises);
		out += attr(diffuseMaterial(o.eyes * 2.0), glows);
		out += attr(diffuseMaterial({0.015, 0.012, 0.01}), pupils);
		out += attr(diffuseMaterial({1.0, 1.0, 1.0}), sparks);
		out += attr(skin, lids);
		out += attr(diffuseMaterial({0.06, 0.035, 0.025}), lashes);
		out += attr(hair, brows);
	}
	// the mouth: a line whose corners rise with a smile, and a dark mouth with teeth when it opens
	{
		const double smile = std::max(-1.0, std::min(1.0, p.smile)), open = clamp01(p.mouthOpen);
		std::vector<Vec3> line;
		for (int k = 0; k <= 16; ++k) {
			const double u = -1.0 + 0.125 * k;
			Vec3 nrm;
			const Vec3 q = onFace({1.0, -0.50 + 0.17 * smile * u * u - 0.05 * smile - 0.04 * open * (1.0 - u * u), u * 0.30}, &nrm);
			line.push_back(q + nrm * 0.0022);
		}
		out += attr(diffuseMaterial(o.lips * 0.55), emitMesh(sdf::tubeMesh(line, 0.0030, 0.40, 8), f.head));
		if (open > 0.04) {
			const Vec3 across = {0.0, 0.0, 1.0};
			out += attr(diffuseMaterial({0.20, 0.05, 0.06}),
						emitMesh(sdf::decal(headShape, {0.0, 0.0, 0.0}, {1.0, -0.55 - 0.08 * open, 0.0}, across, 0.022 + 0.004 * open, 0.004 + 0.018 * open, 0.0008, 0.3, 0.5), f.head));
			out += attr(diffuseMaterial({0.96, 0.95, 0.90}),
						emitMesh(sdf::decal(headShape, {0.0, 0.0, 0.0}, {1.0, -0.525, 0.0}, across, 0.017, 0.0040 * std::min(1.0, 3.0 * open), 0.0012, 0.3), f.head));
		}
	}
	return out;
}

}  // namespace human
}  // namespace cartoon
