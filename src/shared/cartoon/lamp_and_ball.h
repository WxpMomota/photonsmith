#pragma once
// lamp_and_ball.h -- "Lamp and Ball", a 20 second cartoon: a small yellow desk lamp wakes up on a dark desk, notices a red ball rolling in, hops over, nudges it, watches it bounce off
// a stack of books and roll home, and puts its shade over it like a blanket. (It began as a Python script; the C++ is the film now.)
//
// The story is written in "story space": +x to the right of the picture, +y up, +z towards the camera. pbrt is left-handed, so every emitted x is negated (kMirror) and triangle windings
// are flipped; nothing else in this file has to think about it.

#include "cartoon_lib.h"

namespace cartoon {
namespace lamp_and_ball {

constexpr double kDuration = 20.0;
constexpr int kFps = 24;

constexpr double kBallR = 0.55;
constexpr double kGravity = 18.0;
constexpr double kBaseR = 0.80, kBaseH = 0.17;
constexpr double kL1 = 1.55, kL2 = 1.45;
constexpr double kShadeLen = 1.15, kShadeR0 = 0.30, kShadeR1 = 0.66;
constexpr double kBookX = 4.9;      // the bumper: the books' near face (the ball bounces off it)
constexpr double kNudgeT = 9.0;     // the head touches the ball
constexpr double kCoverT = 15.6;    // the shade comes down over the ball
constexpr double kRestX = -0.3;     // where the ball comes to rest, beside the lamp

struct Hop {
	double t0, dur, xa, xb, height;
};
inline const std::vector<Hop> &hops() {
	static const std::vector<Hop> h = {{6.25, 0.55, -3.4, -2.5, 0.75}, {7.05, 0.55, -2.5, -1.6, 0.75}, {7.85, 0.55, -1.6, -0.7, 0.75},
									   {10.2, 0.60, -0.7, -1.6, 0.9},  {11.1, 0.55, -1.6, -2.4, 0.7}};
	return h;
}

inline double lampX(double t) {
	double x = hops()[0].xa;
	for (const Hop &h : hops()) {
		if (t >= h.t0 + h.dur) {
			x = h.xb;
		} else if (t >= h.t0) {
			x = lerp(h.xa, h.xb, (t - h.t0) / h.dur);
			break;
		}
	}
	return x;
}

struct HopPose {
	double y, sy, sw;
};
// The lamp's whole body through the hops: anticipation squash, stretch in the air, landing squash with a wobble.
inline HopPose lampHop(double t) {
	for (const Hop &h : hops()) {
		const double pre = 0.22, post = 0.45;
		if (t >= h.t0 - pre && t < h.t0) {
			const double u = (t - (h.t0 - pre)) / pre, s = 1.0 - 0.20 * std::sin(u * kPi * 0.5);
			return {0.0, s, 1.0 / std::sqrt(s)};
		}
		if (t >= h.t0 && t < h.t0 + h.dur) {
			const double u = (t - h.t0) / h.dur, s = 1.0 + 0.12 * std::sin(u * kPi);
			return {4.0 * h.height * u * (1.0 - u), s, 1.0 / std::sqrt(s)};
		}
		if (t >= h.t0 + h.dur && t < h.t0 + h.dur + post) {
			const double u = (t - (h.t0 + h.dur)) / post, s = 1.0 - 0.18 * std::exp(-5.0 * u) * std::cos(u * 3.0 * kPi);
			return {0.0, s, 1.0 / std::sqrt(s)};
		}
	}
	return {0.0, 1.0, 1.0};
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The ball: a small physics pass, sampled at every frame
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct BallSample {
	Vec3 pos, vel;
	double spin;
};

inline std::vector<BallSample> simulateBall(double eWall) {
	const double dt = 1.0 / 480.0;
	const int steps = static_cast<int>(kDuration / dt) + 2;
	Vec3 pos = {9.5, kBallR, 0.35}, vel = {0.0, 0.0, 0.0};
	double spin = 0.0;
	bool nudged = false;
	std::vector<BallSample> out;
	out.reserve(static_cast<std::size_t>(steps));
	for (int k = 0; k < steps; ++k) {
		const double t = k * dt;
		if (t < 6.4) {   // rolls in from the right, slowing to a stop (scripted so that it stops exactly where the story wants it)
			const double u = clamp01((t - 4.0) / 2.4), e = 1.0 - std::pow(1.0 - u, 2.2);
			const double xNew = lerp(9.5, 2.9, e);
			vel = {(xNew - pos.x) / dt, 0.0, 0.0};
			pos = {xNew, kBallR, lerp(0.35, 0.05, e)};
			spin += (vel.x / kBallR) * dt * 180.0 / kPi;
			out.push_back({pos, vel, spin});
			continue;
		}
		if (t < kNudgeT + 0.04) {   // waits
			out.push_back({pos, {0.0, 0.0, 0.0}, spin});
			continue;
		}
		if (!nudged) {   // the nudge: up and away to the right
			vel = {3.4, 8.6, 0.0};
			nudged = true;
		}
		vel.y -= kGravity * dt;
		pos = pos + vel * dt;
		if (pos.x > kBookX - kBallR && vel.x > 0.0) {   // the books: bounce back
			pos.x = kBookX - kBallR;
			vel.x = -vel.x * eWall;
		}
		if (pos.y < kBallR) {
			pos.y = kBallR;
			if (vel.y < -1.0) {
				vel.y = -vel.y * 0.70;
				vel.x *= 0.94;
				vel.z *= 0.8;
			} else {
				vel.y = 0.0;
				const double f = 1.0 - 0.9 * dt;
				vel.x *= f;
				vel.z *= f;
			}
		}
		if (t > 12.2 && pos.y <= kBallR + 1e-6) {   // rolling home: a stronger drag so that it stops at the lamp
			const double f = 1.0 - 1.3 * dt;
			vel.x *= f;
			vel.z *= f;
		}
		spin += (vel.x / kBallR) * dt * 180.0 / kPi;
		out.push_back({pos, vel, spin});
	}
	return out;
}

// The ball must come to rest at kRestX, next to the lamp: bisect the books' bounce so that it does.
inline const std::vector<BallSample> &ball() {
	static const std::vector<BallSample> samples = [] {
		double lo = 0.2, hi = 3.0;
		std::vector<BallSample> best = simulateBall(1.0);
		for (int i = 0; i < 40; ++i) {
			const double mid = 0.5 * (lo + hi);
			best = simulateBall(mid);
			if (best.back().pos.x > kRestX) lo = mid; else hi = mid;
		}
		return best;
	}();
	return samples;
}
inline const BallSample &ballAt(double t) {
	const auto &b = ball();
	const int k = std::max(0, std::min(static_cast<int>(t * 480.0), static_cast<int>(b.size()) - 1));
	return b[static_cast<std::size_t>(k)];
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The lamp: a base, two arms (inverse kinematics puts the head where the story wants it), a head joint and a shade
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Arm2D {
	double j2x, j2y, j3x, j3y;
};
// Elbow position for a two-link arm (lengths kL1, kL2) in the x-y plane from j1 to as near target as it can reach; elbow up.
inline Arm2D armIk(double j1x, double j1y, double tx, double ty) {
	const double dx = tx - j1x, dy = ty - j1y;
	double dist = std::max(1e-4, std::hypot(dx, dy));
	dist = std::min(dist, (kL1 + kL2) * 0.999);
	const double ang = std::atan2(dy, dx);
	const double cosA = std::max(-1.0, std::min(1.0, (kL1 * kL1 + dist * dist - kL2 * kL2) / (2.0 * kL1 * dist)));
	const double e = ang + std::acos(cosA);
	return {j1x + kL1 * std::cos(e), j1y + kL1 * std::sin(e), j1x + dist * std::cos(ang), j1y + dist * std::sin(ang)};
}

struct Parts {
	Mesh base, sphere, arm, shadeOut, shadeIn, rim, ball;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		std::vector<std::pair<double, double>> prof = {{0.0, 0.0}, {kBaseR - 0.12, 0.0}};
		for (int i = 1; i < 7; ++i) {
			const double a = kPi * 0.5 * i / 6.0;
			prof.push_back({kBaseR - 0.12 + 0.12 * std::sin(a), kBaseH * 0.5 * (1.0 - std::cos(a))});
		}
		prof.push_back({kBaseR * 0.55, kBaseH});
		prof.push_back({0.0, kBaseH + 0.02});
		q.base = lathe(prof, 64);
		q.sphere = uvSphere(1.0, 32, 16);
		q.arm = lathe({{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}, 20);
		std::vector<std::pair<double, double>> outer = {{0.0, -0.20}, {0.16, -0.18}, {kShadeR0 * 0.9, -0.05}, {kShadeR0, 0.0}};
		const int n = 14;
		for (int i = 1; i <= n; ++i) {
			const double u = static_cast<double>(i) / n;
			outer.push_back({kShadeR0 + (kShadeR1 - kShadeR0) * std::pow(std::sin(u * kPi * 0.5), 1.3), kShadeLen * u});
		}
		q.shadeOut = lathe(outer, 72);
		std::vector<std::pair<double, double>> inner;
		for (std::size_t i = outer.size(); i-- > 3;) inner.push_back({outer[i].first > 0.2 ? outer[i].first - 0.035 : outer[i].first, outer[i].second});
		q.shadeIn = lathe(inner, 72);
		q.rim = lathe({{kShadeR1 - 0.035, kShadeLen}, {kShadeR1, kShadeLen}}, 72);
		q.ball = uvSphere(1.0, 64, 32);
		return q;
	}();
	return p;
}

struct HeadPlan {
	Vec3 j3;        // the head joint in lamp space (x forward, y up), z = 0
	Vec3 dir;       // the shade's axis
	double glow;
};

inline HeadPlan headPlan(double t, double baseX) {
	const BallSample &bs = ballAt(t);
	const double bx = bs.pos.x - baseX, by = bs.pos.y;   // the ball in lamp space
	const Vec3 rest = ballAt(kCoverT - 0.6).pos;
	const Vec3 sleepDir = normalize({0.35, -1.0, 0.0}), awakeDir = normalize({1.0, -0.12, 0.0});
	// wake up (2.3 - 3.5): lift with overshoot
	const double wake = Track1({{2.0, 0.0}, {3.4, 1.0}}, Ease::Back)(t);
	double j3x = lerp(1.55, 1.85, wake), j3y = lerp(1.25, 2.85, wake);
	Vec3 d = normalize(lerp(sleepDir, awakeDir, wake));
	// looks around (3.6 - 5.0): a slow sweep of the head
	const double look = Track1({{3.5, 0.0}, {3.95, 1.0}, {4.5, -1.0}, {5.0, 0.0}})(t);
	d = normalize({d.x, d.y, d.z + 0.45 * look * (t < 5.4 ? 1.0 : 0.0)});
	// tracks the ball from the moment it shows (4.6)
	const double trackW = Track1({{4.6, 0.0}, {5.3, 1.0}, {9.0, 1.0}, {9.3, 0.6}, {10.0, 1.0}, {15.0, 1.0}, {15.3, 0.0}})(t);
	const Vec3 aim = normalize({bx - j3x, by - j3y, bs.pos.z});
	if (t > 4.6) d = normalize(lerp(d, aim, trackW));
	// the double take (6.0 - 6.25): the head jerks back and up
	const double take = Track1::withEases({{5.95, 0.0, Ease::Smooth}, {6.1, 1.0, Ease::Out}, {6.3, 0.0, Ease::Smooth}})(t);
	j3x -= 0.25 * take;
	j3y += 0.35 * take;
	// wind-up and nudge (8.3 - 9.05)
	const double wind = Track1({{8.3, 0.0}, {8.85, 1.0}})(t);
	const double lunge = Track1::withEases({{8.85, 0.0, Ease::Smooth}, {kNudgeT, 1.0, Ease::In}, {9.35, 0.0, Ease::Out}})(t);
	if (t >= 8.3 && t < 9.4) {
		const double backX = j3x - 0.9, backY = j3y + 0.6;
		const double nudX = bx - 0.45 * d.x * kShadeLen, nudY = by + 0.35 - d.y * kShadeLen;
		j3x = lerp(j3x, backX, wind * (1.0 - lunge));
		j3y = lerp(j3y, backY, wind * (1.0 - lunge));
		if (lunge > 0.0) {
			j3x = lerp(j3x, nudX, lunge);
			j3y = lerp(j3y, nudY, lunge);
			d = normalize(lerp(d, normalize({bx - j3x, by - j3y, 0.0}), lunge));
		}
	}
	// the cover (14.6 - 15.9): lowers the shade straight down over the ball
	const double cov = Track1({{14.5, 0.0}, {kCoverT, 1.0}})(t);
	if (cov > 0.0) {
		j3x = lerp(j3x, rest.x - baseX, cov);
		j3y = lerp(j3y, 0.28 + kShadeLen, cov);
		d = normalize(lerp(d, Vec3{0.0, -1.0, 0.0}, cov));
	}
	// glow: flickers on at 2.3, pulses with the nudge, dims under the cover
	double glow = Track1::withEases({{0.0, 0.0, Ease::Smooth}, {2.25, 0.0, Ease::Smooth}, {2.3, 1.0, Ease::Linear}, {2.42, 0.0, Ease::Linear}, {2.55, 1.0, Ease::Linear},
												  {2.62, 0.2, Ease::Linear}, {2.8, 1.0, Ease::Linear}})(t);
	glow *= 1.0 + 0.5 * Track1({{8.95, 0.0}, {9.05, 1.0}, {9.4, 0.0}})(t);
	glow *= 1.0 - 0.62 * Track1({{16.2, 0.0}, {19.0, 1.0}})(t);
	return {{j3x, j3y, 0.0}, d, glow};
}

// A story-space matrix as the pbrt world's (mirrored) matrix.
inline const Mat4 &mirror() {
	static const Mat4 m = scale(-1.0, 1.0, 1.0);
	return m;
}
inline Vec3 worldPoint(Vec3 p) { return {-p.x, p.y, p.z}; }

inline std::string buildLamp(double t, std::string &spot) {
	const Parts &P = parts();
	const double baseX = lampX(t);
	const HopPose hp = lampHop(t);
	const Mat4 world = mul(translate(baseX, hp.y, 0.0), scale(hp.sw, hp.sy, hp.sw));
	const HeadPlan plan = headPlan(t, baseX);
	const double j1x = 0.0, j1y = kBaseH + 0.1;
	const Arm2D ik = armIk(j1x, j1y, plan.j3.x, plan.j3.y);
	const Vec3 j1 = {j1x, j1y, 0.0}, j2 = {ik.j2x, ik.j2y, 0.0}, j3 = {ik.j3x, ik.j3y, 0.0};
	const std::string bodyMat = coated({0.97, 0.72, 0.08}, 0.06), darkMat = coated({0.28, 0.28, 0.33}, 0.25), innerMat = diffuseMaterial({0.93, 0.91, 0.86});
	std::string out = attr(darkMat, emitMesh(P.base, world));
	const std::pair<Vec3, double> joints[3] = {{j1, 0.16}, {j2, 0.14}, {j3, 0.14}};
	for (const auto &jr : joints) out += attr(bodyMat, emitMesh(P.sphere, mul(world, translate(jr.first), scale(jr.second))));
	const std::tuple<Vec3, Vec3, double> arms[2] = {{j1, j2, 0.07}, {j2, j3, 0.065}};
	for (const auto &ab : arms) {
		const Vec3 v = std::get<1>(ab) - std::get<0>(ab);
		out += attr(bodyMat, emitMesh(P.arm, mul(world, alignY(std::get<0>(ab), v), scale(std::get<2>(ab), length(v), std::get<2>(ab)))));
	}
	const Mat4 head = mul(world, alignY(j3, plan.dir));
	out += attr(bodyMat, emitMesh(P.shadeOut, head));
	out += attr(innerMat, emitMesh(P.shadeIn, head) + emitMesh(P.rim, head));
	const Vec3 bulbLocal = {0.0, 0.40, 0.0};
	const Vec3 bulbC = transformPoint(head, bulbLocal);
	if (plan.glow > 0.01) {
		const Vec3 L = {30.0 * plan.glow, 25.0 * plan.glow, 17.0 * plan.glow};
		out += attr("Material \"diffuse\" \"rgb reflectance\" [ 1 1 1 ]", emitMesh(P.sphere, mul(head, translate(bulbLocal), scale(0.19))), &L);
	}
	// a spot light from the bulb along the shade's axis does the real lighting; its cone just fills the shade's mouth
	const Vec3 mouth = transformPoint(head, {0.0, kShadeLen, 0.0});
	const Vec3 axis = normalize(mouth - bulbC);
	spot.clear();
	if (plan.glow > 0.01)
		spot = "LightSource \"spot\" \"point3 from\" [ " + fmt3(worldPoint(bulbC)) + " ] \"point3 to\" [ " + fmt3(worldPoint(bulbC + axis)) +
			   " ] \"float coneangle\" [ 36 ] \"float conedeltaangle\" [ 14 ] \"rgb I\" [ " + fmt3({900.0 * plan.glow, 740.0 * plan.glow, 480.0 * plan.glow}) + " ]\n";
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The set, the ball, the camera
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string buildSet() {
	const Parts &P = parts();
	std::string out;
	out += attr(coated({0.42, 0.22, 0.12}, 0.10), emitMesh(box(34.0, 1.0, 18.0), translate(0.0, -0.5, -2.5)));
	out += attr(diffuseMaterial({0.16, 0.22, 0.36}), emitMesh(box(44.0, 22.0, 0.5), translate(0.0, 9.0, -8.0)));
	struct Book {
		double sx, sy, sz, ry;
		Vec3 col;
	};
	const Book books[4] = {{2.1, 0.40, 1.9, 8, {0.78, 0.15, 0.18}}, {1.9, 0.34, 1.75, -9, {0.15, 0.46, 0.72}}, {1.8, 0.30, 1.6, 14, {0.92, 0.77, 0.22}}, {1.6, 0.28, 1.5, -5, {0.30, 0.65, 0.40}}};
	double y = 0.0;
	for (const Book &b : books) {
		out += attr(coated(b.col, 0.2), emitMesh(roundedBox(b.sx, b.sy, b.sz, 0.06), mul(translate(kBookX + 1.05, y + b.sy / 2.0, 0.0), rotate({0, 1, 0}, b.ry))));
		y += b.sy;
	}
	// a window with the night sky in it (an area light), a frame, and a glass paperweight that throws the lamp's light into a caustic
	const Vec3 sky = {0.55, 0.70, 1.25};
	out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0.1 0.1 0.1 ]", emitMesh(box(5.2, 2.8, 0.05), translate(-1.2, 3.9, -7.7)), &sky);
	std::string frame;
	frame += emitMesh(box(5.6, 0.22, 0.2), translate(-1.2, 5.31, -7.65));
	frame += emitMesh(box(5.6, 0.22, 0.2), translate(-1.2, 2.49, -7.65));
	frame += emitMesh(box(0.22, 3.2, 0.2), translate(-3.89, 3.9, -7.65));
	frame += emitMesh(box(0.22, 3.2, 0.2), translate(1.49, 3.9, -7.65));
	frame += emitMesh(box(0.16, 2.8, 0.2), translate(-1.2, 3.9, -7.6));
	frame += emitMesh(box(5.2, 0.16, 0.2), translate(-1.2, 3.9, -7.6));
	out += attr(diffuseMaterial({0.20, 0.14, 0.10}), frame);
	out += attr("Material \"dielectric\" \"float eta\" [ 1.5 ]", emitMesh(P.ball, mul(translate(2.0, 0.5, -1.9), scale(0.5))));
	// a pencil and a mug at the back
	out += attr(coated({0.95, 0.80, 0.15}, 0.15), emitMesh(P.arm, mul(translate(-1.0, 0.07, -3.4), rotate({0, 0, 1}, -90), rotate({0, 1, 0}, 20), scale(0.07, 2.0, 0.07))));
	out += attr(coated({0.85, 0.85, 0.9}, 0.08), emitMesh(lathe({{0.0, 0.0}, {0.55, 0.0}, {0.58, 0.05}, {0.62, 1.0}, {0.56, 1.0}, {0.0, 0.1}}, 40), translate(-5.6, 0.0, -3.6)));
	return out;
}

inline std::string buildBall(double t) {
	const BallSample &bs = ballAt(t);
	Mat4 m = translate(bs.pos);
	const double speed = length(bs.vel);
	if (bs.pos.y > kBallR + 0.03 && speed > 1.0) {   // stretch along the velocity while it flies
		const double s = 1.0 + 0.16 * clamp01(speed / 12.0);
		const Mat4 f = alignY({0.0, 0.0, 0.0}, bs.vel);
		m = mul(m, f, scale(1.0 / std::sqrt(s), s, 1.0 / std::sqrt(s)), transposeRotation(f));
	}
	m = mul(m, rotate({0, 0, 1}, -bs.spin), scale(kBallR));
	return attr(coated({0.90, 0.10, 0.08}, 0.03), emitMesh(parts().ball, m));
}

// The camera drifts in while the lamp sleeps, then follows the action loosely and pulls back at the end.
inline std::string cameraText(double t) {
	const Vec3 eye = Track3::withEases({{0.0, {1.6, 3.4, 12.5}, Ease::Smooth}, {4.0, {0.9, 3.0, 11.2}, Ease::Smooth}, {9.0, {0.8, 3.1, 11.0}, Ease::Smooth},
													 {13.0, {-0.2, 3.0, 11.0}, Ease::Smooth}, {16.0, {-1.4, 2.7, 10.2}, Ease::Smooth}, {20.0, {-1.4, 3.0, 11.6}, Ease::Smooth}})(t);
	const Vec3 target = Track3::withEases({{0.0, {-1.2, 1.2, 0.0}, Ease::Smooth}, {4.0, {-0.8, 1.2, 0.0}, Ease::Smooth}, {9.0, {0.6, 1.1, 0.0}, Ease::Smooth},
														{13.0, {-0.2, 1.1, 0.0}, Ease::Smooth}, {16.0, {-1.9, 1.0, 0.0}, Ease::Smooth}, {20.0, {-1.9, 1.1, 0.0}, Ease::Smooth}})(t);
	char buf[400];
	std::snprintf(buf, sizeof(buf), "LookAt %s %s 0 1 0\nCamera \"perspective\" \"float fov\" [ 34 ] \"float lensradius\" [ 0.05 ] \"float focaldistance\" [ %.3f ]\n",
				  fmt3(worldPoint(eye)).c_str(), fmt3(worldPoint(target)).c_str(), length(eye - target));
	return buf;
}

// Everything up to and including WorldBegin: the camera at t, the picture's size, the sampler and the integrator. Cheap, and all the scene list
// needs to describe the film's still.
inline std::string headerAt(double t) { return sceneHeader(cameraText(t)); }

inline std::string sceneAt(double t) {
	std::string spot;
	const std::string lamp = buildLamp(t, spot);
	std::string out = headerAt(t);
	out += "LightSource \"infinite\" \"rgb L\" [ 0.05 0.065 0.11 ]\n";
	out += "AttributeBegin\n  AreaLightSource \"diffuse\" \"rgb L\" [ 0.9 0.95 1.35 ]\n  Shape \"trianglemesh\" \"integer indices\" [ 0 1 2 0 2 3 ] \"point3 P\" [ -9 9 -3  9 9 -3  9 9 5  -9 9 5 ]\nAttributeEnd\n";
	out += spot + buildSet() + lamp + buildBall(t);
	return out;
}

}  // namespace lamp_and_ball
}  // namespace cartoon
