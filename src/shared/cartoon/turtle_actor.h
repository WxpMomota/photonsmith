#pragma once
// turtle_actor.h -- a turtle for the cartoon films, drawn to be adorable (13 cm long): a big round head with a soft muzzle and chubby cheeks, large glossy eyes
// with two catchlights each, rosy cheeks, a small smile, a domed shell of pillowy hexagonal plates (each with a lighter growth ring) on a cream rim, a cream
// plastron, chubby legs with three round toes, and a little pointed tail. The head, the legs, the neck and the tail are sculpted (sdf_mesh.h): blended shapes,
// smooth as clay. The eyes look, blink and narrow, the brows frown or worry, the mouth smiles and opens; the neck pulls in or stretches out; the legs walk,
// paddle or reach for a point in the world (a hose, a rim). A film acts by filling in a `TurtlePose` for each instant and asking for `emitTurtle(pose)`.
//
// Units are metres (the films that use it are built at real size: a living room, a tank). The rig's own frame has +x forward, +y up and +z to the
// turtle's right; `pose.yaw` turns it about the vertical (0 faces +x, positive turns the front towards +z, as frontOf() in the films).

#include <array>

#include "cartoon_actor.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace turtle {

struct Look {
	Vec3 skin = {0.40, 0.66, 0.25};
	Vec3 shell = {0.05, 0.19, 0.09};       // between the plates (the seams)
	Vec3 plate = {0.15, 0.43, 0.17};
	Vec3 ring = {0.32, 0.60, 0.24};        // the growth ring in each plate
	Vec3 rim = {0.90, 0.74, 0.38};
	Vec3 plastron = {0.93, 0.82, 0.52};
	Vec3 blush = {0.95, 0.45, 0.50};
	Vec3 iris = {0.20, 0.11, 0.05};
	Vec3 brow = {0.13, 0.30, 0.10};
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
constexpr double kDomeHeight = 0.042;
constexpr double kPlastron = 0.011;     // the plastron's thickness; the dome sits on it
constexpr double kEyeR = 0.0088;        // a big eye: two fifths of the head's width

// The head, in its own frame (+x forward, +y up, +z right; the origin at the middle of the cranium): a round cranium, a soft muzzle and two chubby cheeks.
inline double headShape(Vec3 p) {
	double d = sdf::ellipsoid(p, Vec3{0.0, 0.006, 0.0}, Vec3{0.022, 0.0205, 0.0215});
	d = sdf::smin(d, sdf::ellipsoid(p, Vec3{0.0155, -0.0045, 0.0}, Vec3{0.0125, 0.0100, 0.0140}), 0.010);
	for (double side : {-1.0, 1.0}) d = sdf::smin(d, sdf::ellipsoid(p, Vec3{0.0075, -0.0050, side * 0.0122}, Vec3{0.0105, 0.0092, 0.0092}), 0.008);
	return d;
}
inline Vec3 headCentreLocal() { return {0.002, 0.002, 0.0}; }
// A point on the head's surface (head frame) seen from its middle in direction `dir`, and the surface's normal there.
inline Vec3 onHeadSurface(Vec3 dir, Vec3 *normal = nullptr) {
	const Vec3 p = sdf::radialHit(headShape, headCentreLocal(), normalize(dir), 0.06);
	if (normal) *normal = sdf::normalAt(headShape, p, 2e-4);
	return p;
}
// Each eye's centre in the head frame: sunk a little into the face, on the front of the head, looking forward and a little out (side -1 left, +1 right).
inline Vec3 eyeCentreLocal(double side) {
	static const std::array<Vec3, 2> e = [] {
		std::array<Vec3, 2> r;
		for (int s = 0; s < 2; ++s) {
			Vec3 n;
			const Vec3 p = onHeadSurface({0.80, 0.40, (s == 0 ? -1.0 : 1.0) * 0.46}, &n);
			r[static_cast<std::size_t>(s)] = p - n * (0.42 * kEyeR);
		}
		return r;
	}();
	return e[side < 0 ? 0 : 1];
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The parts that never change shape (built once, in their own frames)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A point on the dome's surface over (u, v) (u along the shell from tail -1 to head +1, v across from left -1 to right +1), raised `out` above it, in the
// shell's frame (the middle of the plastron's bottom at the origin).
inline Vec3 domePoint(double u, double v, double out = 0.0008) {
	const double r2 = u * u + v * v;
	const double s = std::sqrt(std::max(0.0, 1.0 - std::min(1.0, r2)));
	const double h = kPlastron + kDomeHeight * std::pow(s, 0.80);
	return {kHalfLength * u, h + out, kHalfWidth * v};
}

// A pillowy hexagonal plate on the dome, centred on (u0, v0) with half sizes (ru, rv) in (u, v), `height` high at its flat middle and rounded down to the dome at
// its edge.
inline Mesh plate(double u0, double v0, double ru, double rv, double height, double base) {
	Mesh m;
	const int rings = 5, around = 24;
	const auto hex = [](double a) {
		const double seg = std::fmod(a + 2.0 * kPi, kPi / 3.0) - kPi / 6.0;
		return lerp(std::cos(kPi / 6.0) / std::cos(seg), 1.0, 0.35);   // a hexagon with its corners softened
	};
	m.verts.push_back(domePoint(u0, v0, base + height));
	for (int r = 1; r <= rings; ++r) {
		const double s = static_cast<double>(r) / rings;
		const double h = height * std::sqrt(std::max(0.0, 1.0 - std::pow(s, 6.0)));
		for (int a = 0; a < around; ++a) {
			const double ang = 2.0 * kPi * a / around + kPi / 6.0, k = hex(ang) * s;
			m.verts.push_back(domePoint(u0 + ru * k * std::cos(ang), v0 + rv * k * std::sin(ang), base + h));
		}
	}
	for (int a = 0; a < around; ++a) m.tris.push_back({0, 1 + (a + 1) % around, 1 + a});
	for (int r = 1; r < rings; ++r)
		for (int a = 0; a < around; ++a) {
			const int i0 = 1 + (r - 1) * around, i1 = 1 + r * around, b = (a + 1) % around;
			m.tris.push_back({i0 + a, i0 + b, i1 + a});
			m.tris.push_back({i0 + b, i1 + b, i1 + a});
		}
	sdf::computeNormals(m);
	// (the triangles above wind clockwise seen from outside; the normals must point out of the dome)
	for (Vec3 &n : m.normals)
		if (n.y < 0.0) n = n * -1.0;
	return m;
}
inline void append(Mesh &into, const Mesh &m) {
	const int base = static_cast<int>(into.verts.size());
	into.verts.insert(into.verts.end(), m.verts.begin(), m.verts.end());
	into.normals.insert(into.normals.end(), m.normals.begin(), m.normals.end());
	for (auto t : m.tris) into.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
}

struct Parts {
	Mesh dome, rim, plastron, plates, rings, head, blush, nostrils, ball, eyeball, cap, disc, limbal, iris, glow, pupil;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		// the dome: a quarter ellipse turned about +y, full and round, closed at the bottom
		std::vector<std::pair<double, double>> prof;
		prof.push_back({0.0, 0.0});
		for (int i = 0; i <= 20; ++i) {
			const double a = 0.5 * kPi * i / 20.0;
			prof.push_back({std::cos(a), std::pow(std::sin(a), 0.80)});
		}
		prof.back().first = 0.0;
		q.dome = lathe(prof, 72);
		// the rim: a soft rounded band round the bottom edge, a little wider than the dome
		q.rim = lathe({{0.93, -0.02}, {1.03, 0.0}, {1.075, 0.06}, {1.07, 0.15}, {1.02, 0.22}, {0.95, 0.20}}, 72);
		q.plastron = uvSphere(1.0, 36, 12);
		// the plates: five down the spine, four on each side
		const double H = 0.0017;
		for (double u : {-0.60, -0.30, 0.0, 0.30, 0.60}) {
			append(q.plates, plate(u, 0.0, 0.148, 0.180, H, 0.0002));
			append(q.rings, plate(u, 0.0, 0.078, 0.094, 0.0004, 0.0002 + H));
		}
		for (double side : {-1.0, 1.0})
			for (double u : {-0.47, -0.16, 0.15, 0.46}) {
				append(q.plates, plate(u * 0.98, side * 0.53, 0.148, 0.165, H * 0.9, 0.0002));
				append(q.rings, plate(u * 0.98, side * 0.53, 0.076, 0.085, 0.0004, 0.0002 + H * 0.9));
			}
		q.head = sdf::radialMesh(headShape, headCentreLocal(), 0.06, 72, 48);
		for (double side : {-1.0, 1.0}) {
			append(q.blush, sdf::decal(headShape, headCentreLocal(), {0.50, -0.22, side * 0.80}, {1.0, 0.0, 0.0}, 0.0058, 0.0042, 0.00008, 0.06));
			append(q.nostrils, sdf::decal(headShape, headCentreLocal(), {1.0, 0.02, side * 0.16}, {0.0, 0.0, 1.0}, 0.0009, 0.0006, 0.00008, 0.06, 0.0, 3, 14));
		}
		q.ball = uvSphere(1.0, 28, 14);
		q.cap = actor::parts().lid;
		q.disc = uvSphere(1.0, 20, 8);
		q.eyeball = uvSphere(1.0, 48, 24);
		// the coloured parts of an eye: caps of spheres just outside the eyeball, round +y (a dark ring, the iris, a warm glow low in it, the pupil)
		const auto cap = [](double degrees, double r) {
			std::vector<std::pair<double, double>> prof;
			for (int i = 0; i <= 10; ++i) {
				const double a = degrees * kPi / 180.0 * (1.0 - i / 10.0);
				prof.push_back({r * std::sin(a), r * std::cos(a)});
			}
			return lathe(prof, 48);
		};
		q.limbal = cap(45.0, 1.0015);
		q.iris = cap(40.0, 1.0030);
		q.glow = cap(21.0, 1.0045);
		q.pupil = cap(19.0, 1.0060);
		return q;
	}();
	return p;
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
	if (front) return {{kHalfLength * 0.52, kPlastron * 0.9, side * kHalfWidth * 0.74}, {kHalfLength * 0.80, -0.012, side * kHalfWidth * 1.12}, side, true};
	return {{-kHalfLength * 0.58, kPlastron * 0.8, side * kHalfWidth * 0.70}, {-kHalfLength * 0.78, -0.012, side * kHalfWidth * 1.05}, side, false};
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
	const Vec3 baseLocal = {kHalfLength * 0.78, kPlastron + 0.013, 0.0};
	// the neck points forward and a little up, bent by part of the head's turn
	const Mat4 bend = mul(rotate({0, 1, 0}, -0.45 * p.headYaw), rotate({0, 0, 1}, -0.45 * p.headPitch + 14.0));
	const Vec3 dir = normalize(transformPoint(bend, Vec3{1.0, 0.0, 0.0}));
	const double length = 0.005 + 0.016 * out;
	const Vec3 topLocal = baseLocal + dir * length;
	Neck n;
	n.base = transformPoint(f.shell, baseLocal);
	n.top = transformPoint(f.shell, topLocal);
	n.head = mul(f.shell, translate(topLocal), bend, rotate({0, 1, 0}, -0.55 * p.headYaw), rotate({0, 0, 1}, -0.55 * p.headPitch - 14.0), rotate({1, 0, 0}, p.headRoll),
				 translate(0.017, 0.007, 0.0));
	return n;
}

// The eyes' centres (world), for a film that wants to aim a close-up at them.
inline Vec3 eyeOf(const Look &look, const TurtlePose &p, double side) {
	const Frames f = framesOf(look, p);
	return transformPoint(neckOf(f, p).head, eyeCentreLocal(side));
}
inline Vec3 headOf(const Look &look, const TurtlePose &p) {
	const Frames f = framesOf(look, p);
	return transformPoint(neckOf(f, p).head, {0.004, 0.0, 0.0});
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The turtle
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The sculpted skin that moves with the pose: the neck, four chubby legs with three round toes each, and the tail (world space).
inline std::string emitLimbs(const Frames &f, const TurtlePose &p, const Neck &n) {
	const double S = f.S;
	std::string out;
	const Vec3 shellUp = normalize(transformPoint(f.shell, {0.0, 1.0, 0.0}) - transformPoint(f.shell, {0.0, 0.0, 0.0}));
	// the neck: from inside the shell to the back of the head
	{
		const Vec3 a = n.base - (n.top - n.base) * 0.4, b = transformPoint(n.head, {-0.010, -0.005, 0.0});
		// (it widens into the back of the head, so that the head grows out of it)
		const Mat4 flare = invertAffine(mul(n.head, translate(-0.008, -0.008, 0.0)));
		const auto neck = [&](Vec3 q) { return sdf::smin(sdf::roundCone(q, a, b, 0.0150 * S, 0.0140 * S), sdf::ellipsoid(q, flare, Vec3{0.0095, 0.0085, 0.0115} * S), 0.005 * S); };
		Vec3 lo, hi;
		sdf::boundsOf({a, b, transformPoint(n.head, {-0.008, -0.008, 0.0})}, 0.019 * S, lo, hi);
		out += emitMesh(sdf::surfaceNets(neck, lo, hi, 0.0016 * S), identity());
	}
	for (int k = 0; k < 4; ++k) {
		const LegSite s = legSite(k);
		const Vec3 hip = transformPoint(f.shell, s.hip), foot = footOf(f, p, k);
		const Vec3 pole = transformPoint(f.shell, s.hip + Vec3{0.0, 0.02, s.side * 0.03}) - hip;
		const Limb leg = limbIk(hip, foot, 0.017 * S, 0.017 * S, pole);
		// the foot: a soft paddle along the leg's direction, flattened against the ground, three round toes in front
		const Vec3 along = normalize(leg.hand - leg.elbow);
		Vec3 fwd = along - shellUp * dot(along, shellUp);
		fwd = length(fwd) > 1e-6 ? normalize(fwd) : normalize(transformPoint(f.shell, {1.0, 0.0, 0.0}) - transformPoint(f.shell, {0.0, 0.0, 0.0}));
		const Vec3 side = normalize(cross(fwd, shellUp));
		const Mat4 frame = {fwd.x, shellUp.x, side.x, leg.hand.x, fwd.y, shellUp.y, side.y, leg.hand.y, fwd.z, shellUp.z, side.z, leg.hand.z, 0, 0, 0, 1};
		const Mat4 inv = invertAffine(mul(frame, translate(0.004 * S, -0.0015 * S, 0.0)));
		Vec3 toes[3];
		for (int c = 0; c < 3; ++c) toes[c] = transformPoint(frame, Vec3{0.0125, -0.0022, (c - 1) * 0.0047} * S);
		const auto limb = [&](Vec3 q) {
			double d = sdf::roundCone(q, hip, leg.elbow, 0.0096 * S, 0.0088 * S);
			d = sdf::smin(d, sdf::roundCone(q, leg.elbow, leg.hand, 0.0088 * S, 0.0078 * S), 0.004 * S);
			d = sdf::smin(d, sdf::ellipsoid(q, inv, Vec3{0.0105, 0.0058, 0.0092} * S), 0.005 * S);
			for (const Vec3 &t : toes) d = sdf::smin(d, sdf::sphere(q, t, 0.0031 * S), 0.0028 * S);
			return d;
		};
		Vec3 lo, hi;
		sdf::boundsOf({hip, leg.elbow, leg.hand, toes[0], toes[1], toes[2]}, 0.0125 * S, lo, hi);
		out += emitMesh(sdf::surfaceNets(limb, lo, hi, 0.0017 * S), identity());
	}
	{
		const double tailOut = 1.0 - 0.8 * clamp01(p.tuck);
		const Vec3 a = transformPoint(f.shell, {-kHalfLength * 0.82, kPlastron * 0.9, 0.0});
		const Vec3 m = transformPoint(f.shell, {-kHalfLength * (0.92 + 0.18 * tailOut), kPlastron * 0.55, 0.0});
		const Vec3 b = transformPoint(f.shell, {-kHalfLength * (0.94 + 0.34 * tailOut), kPlastron * 0.45, 0.004});
		const auto tail = [&](Vec3 q) { return sdf::smin(sdf::roundCone(q, a, m, 0.0058 * S, 0.0036 * S), sdf::roundCone(q, m, b, 0.0036 * S, 0.0013 * S), 0.002 * S); };
		Vec3 lo, hi;
		sdf::boundsOf({a, m, b}, 0.008 * S, lo, hi);
		out += emitMesh(sdf::surfaceNets(tail, lo, hi, 0.0011 * S), identity());
	}
	return out;
}

inline std::string emitTurtle(const TurtlePose &p, const Look &look = Look{}) {
	const Parts &P = parts();
	const Frames f = framesOf(look, p);
	std::string out;
	// (matte materials: a glossy coat over a whole character leaves a grain the denoiser cannot take out at the films' sample counts; the eyes get painted catchlights)
	const std::string skin = diffuseMaterial(look.skin);

	// ---- the shell ----
	out += attr(diffuseMaterial(look.shell), emitMesh(P.dome, mul(f.shell, translate(0.0, kPlastron, 0.0), scale(kHalfLength, kDomeHeight, kHalfWidth))));
	out += attr(diffuseMaterial(look.plate), emitMesh(P.plates, f.shell));
	out += attr(diffuseMaterial(look.ring), emitMesh(P.rings, f.shell));
	out += attr(diffuseMaterial(look.rim), emitMesh(P.rim, mul(f.shell, translate(0.0, kPlastron * 0.55, 0.0), scale(kHalfLength, 0.040, kHalfWidth))));
	out += attr(diffuseMaterial(look.plastron), emitMesh(P.plastron, mul(f.shell, translate(0.002, kPlastron * 0.5, 0.0), scale(kHalfLength * 0.93, kPlastron * 0.58, kHalfWidth * 0.88))));

	// ---- the skin: neck, legs, tail and the head ----
	const Neck n = neckOf(f, p);
	out += attr(skin, emitLimbs(f, p, n) + emitMesh(P.head, n.head));

	// ---- the face: rosy cheeks, nostrils, the smile and the open mouth, all lying on the skin ----
	{
		out += attr(diffuseMaterial(look.blush), emitMesh(P.blush, n.head));
		out += attr(diffuseMaterial({0.07, 0.10, 0.04}), emitMesh(P.nostrils, n.head));
		const double smile = std::max(-1.0, std::min(1.0, p.smile)), open = clamp01(p.mouthOpen);
		std::vector<Vec3> line;
		for (int k = 0; k <= 16; ++k) {
			const double u = -1.0 + 0.125 * k;
			Vec3 nrm;
			const Vec3 q = onHeadSurface({1.0, -0.42 + 0.15 * smile * u * u - 0.03 * smile - 0.06 * open * (1.0 - u * u), u * 0.46}, &nrm);
			line.push_back(q + nrm * 0.0003);
		}
		out += attr(diffuseMaterial({0.09, 0.12, 0.05}), emitMesh(sdf::tubeMesh(line, 0.00072, 0.45, 8), n.head));
		if (open > 0.05) {
			const Vec3 across = {0.0, 0.0, 1.0};
			out += attr(diffuseMaterial({0.24, 0.04, 0.06}),
						emitMesh(sdf::decal(headShape, headCentreLocal(), {1.0, -0.48 - 0.12 * open, 0.0}, across, 0.0042 + 0.0012 * open, 0.0016 + 0.0046 * open, 0.00012, 0.06, 0.55), n.head));
			out += attr(diffuseMaterial({0.92, 0.42, 0.46}),
						emitMesh(sdf::decal(headShape, headCentreLocal(), {1.0, -0.52 - 0.14 * open, 0.0}, across, 0.0024 + 0.0004 * open, 0.0008 + 0.0012 * open, 0.00020, 0.06), n.head));
		}
	}

	// ---- the eyes ----
	{
		const double wide = std::max(0.4, p.eyeWide);
		std::string whites, limbals, irises, glows, pupils, sparks, lids, brows;
		for (double side : {-1.0, 1.0}) {
			const Vec3 c = eyeCentreLocal(side);
			const double R = kEyeR * wide;
			whites += emitMesh(P.eyeball, mul(n.head, translate(c), scale(R)));
			Vec3 dir = normalize({1.0, 0.04, side * 0.30});   // forward and a little out
			if (p.lookWeight > 0.0) {
				const Vec3 t = transformPoint(invertAffine(n.head), p.look);
				Vec3 d = normalize(t - c);
				if (d.x < 0.05) d.x = 0.05;   // the eye cannot turn round into the head
				dir = normalize(lerp(dir, normalize(d), clamp01(p.lookWeight)));
			}
			// a dark ring, a big iris with a warm glow low in it, the pupil, and two catchlights: a big one up and out, a small one down and in
			const Mat4 eye = mul(n.head, alignY(c, dir), scale(R));
			limbals += emitMesh(P.limbal, eye);
			irises += emitMesh(P.iris, eye);
			glows += emitMesh(P.glow, mul(n.head, alignY(c, normalize(dir - Vec3{0.0, 0.30, 0.0})), scale(R)));
			pupils += emitMesh(P.pupil, eye);
			const Vec3 upv = {0.0, 1.0, 0.0}, outv = {0.0, 0.0, side};
			const Vec3 s1 = normalize(dir + upv * 0.50 + outv * 0.22), s2 = normalize(dir - upv * 0.42 - outv * 0.28);
			sparks += emitMesh(P.disc, mul(n.head, alignY(c + s1 * (R * 1.007), s1), scale(R * 0.22, R * 0.012, R * 0.22)));
			sparks += emitMesh(P.disc, mul(n.head, alignY(c + s2 * (R * 1.007), s2), scale(R * 0.10, R * 0.012, R * 0.10)));
			// lids: the upper one comes down with a blink or a squint, the lower one comes up with a squint
			const Vec3 outward = normalize(Vec3{0.25, 0.0, side});
			const double up = clamp01(std::max(p.blink, 0.62 * p.squint));
			const double low = clamp01(std::max(p.blink * 0.4, 0.55 * p.squint));
			const auto lid = [&](double amount, double sign) {
				const double psi = (180.0 - 180.0 * amount) * kPi / 180.0;
				Vec3 axis = outward * std::cos(psi) + Vec3{0.0, sign, 0.0} * std::sin(psi);
				const double tilt = sign > 0 ? 0.35 * (side < 0 ? p.browL : p.browR) : 0.0;   // a cross or worried tilt slants the upper lid
				axis = normalize(axis + Vec3{tilt * 0.6, 0.0, 0.0});
				return emitMesh(P.cap, mul(n.head, alignY(c, axis), scale(R * 1.04)));
			};
			if (up > 0.02) lids += lid(up, 1.0);
			if (low > 0.02) lids += lid(low, -1.0);
			// the brow: a short soft arc on the skin over the eye, its inner end up (worried) or down (cross)
			const double b = side < 0 ? p.browL : p.browR;
			const double h = R * 1.30 + 0.0018 * p.browRaise;
			std::vector<Vec3> arc;
			for (int k = 0; k <= 6; ++k) {
				const double u = k / 6.0;   // inner .. outer
				const Vec3 q = c + Vec3{lerp(0.0030, -0.0015, u), h + lerp(-0.0020 * b, 0.0012 * b, u) + 0.0007 * std::sin(kPi * u), side * lerp(-0.0035, 0.0040, u)};
				arc.push_back(onHeadSurface(q - headCentreLocal()) + normalize(q - headCentreLocal()) * 0.0006);
			}
			brows += emitMesh(sdf::tubeMesh(arc, 0.0011, 0.5, 8), n.head);
		}
		out += attr(diffuseMaterial({0.95, 0.95, 0.93}), whites);
		out += attr(diffuseMaterial(look.iris * 0.35), limbals);
		out += attr(diffuseMaterial(look.iris), irises);
		out += attr(diffuseMaterial(look.iris * 2.6), glows);
		out += attr(diffuseMaterial({0.015, 0.012, 0.01}), pupils);
		out += attr(diffuseMaterial({1.0, 1.0, 1.0}), sparks);
		if (!lids.empty()) out += attr(skin, lids);
		out += attr(diffuseMaterial(look.brow), brows);
	}
	return out;
}

}  // namespace turtle
}  // namespace cartoon
