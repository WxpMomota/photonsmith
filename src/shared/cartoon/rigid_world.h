#pragma once
// rigid_world.h -- a rigid-body world for a film's props, as Blender's Rigid Body World (Bullet) gives an animator: boxes and balls that fall, knock into each other and
// the ground, tumble, slide, bounce and come to rest - a stack of blocks pushed over, apples spilling across a table. Our own small solver from the usual ideas (sequential
// impulses with friction and restitution, position correction, sleeping), not Bullet's code.
//
// A film's `rigid BLOCK box W H D at X Y Z ...` or `rigid BLOCK sphere R at X Y Z ...` makes the scene's block named BLOCK one of the world's bodies (its shape and where its
// middle is: pbrt's coordinates, as `bounce` takes them); `start T velocity X Y Z spin X Y Z` throws or pushes it at T, `fixed` makes it a still obstacle the others hit. Every
// body rests where the scene has it, asleep, until something pushes it: so a stack stands until it is knocked, and the bodies all share one world.
//
// The whole world is worked out once from time 0 in steps of 1/480 s (and kept, per process): a body's place at any time is the same whichever frame asks first.

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "prop_physics.h"   // physics::Impact

namespace cartoon {
namespace rigid {

struct BodyDef {
	std::string block;           // the scene's block it moves
	bool sphere = false;         // a ball of `radius`, else a box of `half` extents
	Vec3 half{0.1, 0.1, 0.1};
	double radius = 0.1;
	Vec3 at;                     // its middle where the scene has it
	double turn = 0.0;           // the box turned about the vertical by this (degrees), as the scene has it
	double mass = 1.0;
	double start = -1.0;         // when it is pushed (velocity, spin); below 0: never
	Vec3 velocity, spin;         // m/s; degrees a second about each axis
	bool fixed = false;          // a still obstacle
	double bounce = 0.25, friction = 0.6;
};

struct Quat {
	double w = 1.0, x = 0.0, y = 0.0, z = 0.0;
};
inline Quat qmul(const Quat &a, const Quat &b) {
	return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
			a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
inline Quat qnorm(Quat q) {
	const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
	return n > 0.0 ? Quat{q.w / n, q.x / n, q.y / n, q.z / n} : Quat{};
}
inline Quat qconj(const Quat &q) { return {q.w, -q.x, -q.y, -q.z}; }
inline Vec3 qrot(const Quat &q, Vec3 v) {
	const Quat r = qmul(qmul(q, {0.0, v.x, v.y, v.z}), qconj(q));
	return {r.x, r.y, r.z};
}
inline Quat qaxis(Vec3 axis, double radians) {
	const Vec3 a = normalize(axis);
	const double s = std::sin(0.5 * radians);
	return {std::cos(0.5 * radians), a.x * s, a.y * s, a.z * s};
}

// Where a body is: its middle and its turn.
struct Pose {
	Vec3 at;
	Quat q;
};

namespace detail {

struct Body {
	const BodyDef *def = nullptr;
	Vec3 x, v, w;   // middle, velocity, angular velocity (world)
	Quat q;
	double invMass = 0.0;
	Vec3 invInertia;   // (the body's own axes)
	bool awake = false;
	double still = 0.0;   // how long it has been nearly still
	Vec3 toWorldInvI(Vec3 t) const {   // I^-1 t in world space
		const Vec3 l = qrot(qconj(q), t);
		return qrot(q, {l.x * invInertia.x, l.y * invInertia.y, l.z * invInertia.z});
	}
	Vec3 velocityAt(Vec3 p) const { return v + cross(w, p - x); }
};

struct Contact {
	int a, b;          // b = -1: the ground
	Vec3 p, n;         // the point, and the normal from b to a
	double depth;
	double target = 0.0, normalImpulse = 0.0, frictionImpulse[2] = {0.0, 0.0};
	Vec3 t1, t2;
};

inline void boxCorners(const Body &b, Vec3 out[8]) {
	const Vec3 h = b.def->half;
	for (int i = 0; i < 8; ++i) out[i] = b.x + qrot(b.q, {(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z});
}

// The contacts between body a and body b (b = -1: the ground plane at `ground`).
inline void collide(const std::vector<Body> &bodies, int ia, int ib, double ground, std::vector<Contact> &out) {
	const Body &A = bodies[static_cast<std::size_t>(ia)];
	if (ib < 0) {
		if (A.def->sphere) {
			const double d = A.def->radius - (A.x.y - ground);
			if (d > 0.0) out.push_back({ia, -1, A.x - Vec3{0.0, A.def->radius, 0.0}, {0.0, 1.0, 0.0}, d});
		} else {
			Vec3 c[8];
			boxCorners(A, c);
			for (const Vec3 &p : c)
				if (p.y < ground) out.push_back({ia, -1, p, {0.0, 1.0, 0.0}, ground - p.y});
		}
		return;
	}
	const Body &B = bodies[static_cast<std::size_t>(ib)];
	if (A.def->sphere && B.def->sphere) {
		const Vec3 d = A.x - B.x;
		const double dist = length(d), sum = A.def->radius + B.def->radius;
		if (dist < sum && dist > 1e-9) {
			const Vec3 n = d * (1.0 / dist);
			out.push_back({ia, ib, B.x + n * B.def->radius, n, sum - dist});
		}
		return;
	}
	if (A.def->sphere != B.def->sphere) {   // a ball and a box: the box's nearest point to the ball's middle
		const bool aSphere = A.def->sphere;
		const Body &S = aSphere ? A : B, &X = aSphere ? B : A;
		const Vec3 l = qrot(qconj(X.q), S.x - X.x), h = X.def->half;
		const Vec3 c{std::clamp(l.x, -h.x, h.x), std::clamp(l.y, -h.y, h.y), std::clamp(l.z, -h.z, h.z)};
		Vec3 nLocal;
		double depth;
		const Vec3 d = l - c;
		if (dot(d, d) > 1e-12) {
			const double dist = length(d);
			if (dist >= S.def->radius) return;
			nLocal = d * (1.0 / dist), depth = S.def->radius - dist;
		} else {   // (its middle inside the box: out through the nearest face)
			const double px = h.x - std::fabs(l.x), py = h.y - std::fabs(l.y), pz = h.z - std::fabs(l.z);
			if (px <= py && px <= pz) nLocal = {l.x < 0 ? -1.0 : 1.0, 0.0, 0.0}, depth = px + S.def->radius;
			else if (py <= pz) nLocal = {0.0, l.y < 0 ? -1.0 : 1.0, 0.0}, depth = py + S.def->radius;
			else nLocal = {0.0, 0.0, l.z < 0 ? -1.0 : 1.0}, depth = pz + S.def->radius;
		}
		const Vec3 n = qrot(X.q, nLocal), p = X.x + qrot(X.q, c);   // (from the box to the ball)
		out.push_back({ia, ib, p, aSphere ? n : n * -1.0, depth});
		return;
	}
	// two boxes: each one's corners inside the other, pushed out through the nearest face
	for (int pass = 0; pass < 2; ++pass) {
		const Body &P = pass == 0 ? A : B, &Q = pass == 0 ? B : A;
		Vec3 c[8];
		boxCorners(P, c);
		const Vec3 h = Q.def->half;
		for (const Vec3 &p : c) {
			const Vec3 l = qrot(qconj(Q.q), p - Q.x);
			const double px = h.x - std::fabs(l.x), py = h.y - std::fabs(l.y), pz = h.z - std::fabs(l.z);
			if (px <= 0.0 || py <= 0.0 || pz <= 0.0) continue;
			Vec3 nLocal;
			double depth;
			if (px <= py && px <= pz) nLocal = {l.x < 0 ? -1.0 : 1.0, 0.0, 0.0}, depth = px;
			else if (py <= pz) nLocal = {0.0, l.y < 0 ? -1.0 : 1.0, 0.0}, depth = py;
			else nLocal = {0.0, 0.0, l.z < 0 ? -1.0 : 1.0}, depth = pz;
			const Vec3 n = qrot(Q.q, nLocal);   // (out of Q, towards P)
			out.push_back({ia, ib, p, pass == 0 ? n : n * -1.0, depth});
		}
	}
}

inline bool nearby(const Body &a, const Body &b) {
	const auto reach = [](const Body &x) { return x.def->sphere ? x.def->radius : length(x.def->half); };
	return length(a.x - b.x) < reach(a) + reach(b);
}

}  // namespace detail

// The world worked out: every body's pose at each step from time 0, and the hard landings (for the soundtrack).
struct Run {
	double step = 1.0 / 480.0;              // the simulation's step
	int keepEvery = 4;                      // poses kept every this many steps (120 a second: a frame falls between two, as close as the eye can tell)
	std::size_t bodies = 0;
	std::vector<Pose> poses;                // [kept step * bodies + body]
	std::vector<physics::Impact> impacts;
	std::size_t kept() const { return bodies ? poses.size() / bodies : 0; }
};

// A moving collider the world cannot move: a ball that goes where it is told - a character's body or foot, sampled every `step` seconds from time 0 (pbrt's coordinates).
// The bodies it touches are pushed (and woken); it is not pushed back.
struct Kinematic {
	double radius = 0.1, step = 1.0 / 120.0;
	std::vector<Vec3> at;
	Vec3 placeAt(double t) const {
		if (at.empty()) return {0.0, -1e6, 0.0};
		const double s = std::clamp(t / step, 0.0, static_cast<double>(at.size() - 1));
		const std::size_t i = static_cast<std::size_t>(s), j = std::min(at.size() - 1, i + 1);
		return at[i] + (at[j] - at[i]) * (s - static_cast<double>(i));
	}
};

inline Run simulate(const std::vector<BodyDef> &defs, double seconds, double ground = 0.0, const std::vector<Kinematic> *movers = nullptr, double gravity = physics::kGravity) {
	using namespace detail;
	Run run;
	// the movers, as fixed balls at the end of the list whose place and velocity are set every step
	std::vector<BodyDef> moverDefs;
	if (movers)
		for (const Kinematic &k : *movers) {
			BodyDef d;
			d.block = "(mover)", d.sphere = true, d.radius = k.radius, d.fixed = true, d.friction = 0.8, d.bounce = 0.1;
			d.at = k.placeAt(0.0);
			moverDefs.push_back(d);
		}
	std::vector<Body> bodies(defs.size() + moverDefs.size());
	const std::size_t real = defs.size();
	for (std::size_t i = 0; i < bodies.size(); ++i) {
		const BodyDef &d = i < real ? defs[i] : moverDefs[i - real];
		Body &b = bodies[i];
		if (i >= real) b.awake = true;   // (a mover: always about, never moved by the world)
		b.def = &d;
		b.x = d.at;
		b.q = qaxis({0.0, 1.0, 0.0}, d.turn * kPi / 180.0);
		if (!d.fixed && d.mass > 0.0) {
			b.invMass = 1.0 / d.mass;
			if (d.sphere) {
				const double I = 0.4 * d.mass * d.radius * d.radius;
				b.invInertia = {1.0 / I, 1.0 / I, 1.0 / I};
			} else {
				const Vec3 s = d.half * 2.0;
				b.invInertia = {12.0 / (d.mass * (s.y * s.y + s.z * s.z)), 12.0 / (d.mass * (s.x * s.x + s.z * s.z)), 12.0 / (d.mass * (s.x * s.x + s.y * s.y))};
			}
		}
	}
	const int steps = static_cast<int>(std::ceil(std::max(0.0, seconds) / run.step)) + 2;
	const double dt = run.step;
	std::vector<char> pushed(defs.size(), 0);
	std::vector<double> lastImpact(bodies.size(), -1.0);
	run.bodies = real;
	run.poses.reserve(static_cast<std::size_t>(steps / run.keepEvery + 1) * real);
	for (int s = 0; s < steps; ++s) {
		const double time = s * dt;
		for (std::size_t i = real; i < bodies.size(); ++i) {   // the movers where they are told, at the speed they go
			const Kinematic &k = (*movers)[i - real];
			bodies[i].x = k.placeAt(time);
			bodies[i].v = (k.placeAt(time + dt) - k.placeAt(time - dt)) * (1.0 / (2.0 * dt));
		}
		if (s % run.keepEvery == 0)
			for (std::size_t i = 0; i < real; ++i) run.poses.push_back({bodies[i].x, bodies[i].q});
		// pushes, and gravity on what is awake
		for (std::size_t i = 0; i < bodies.size(); ++i) {
			Body &b = bodies[i];
			if (b.invMass <= 0.0) continue;
			if (!pushed[i] && b.def->start >= 0.0 && time >= b.def->start) {
				pushed[i] = 1, b.awake = true, b.still = 0.0;
				b.v = b.def->velocity;
				b.w = b.def->spin * (kPi / 180.0);
			}
			if (b.awake) b.v.y -= gravity * dt;   // (the film's: `gravity`)
		}
		// contacts: an awake body with the ground, with everything near it
		std::vector<Contact> contacts;
		for (std::size_t i = 0; i < bodies.size(); ++i) {
			if (!bodies[i].awake) continue;
			if (i < real) collide(bodies, static_cast<int>(i), -1, ground, contacts);   // (a mover is not stopped by the ground)
			for (std::size_t j = 0; j < bodies.size(); ++j) {
				if (j == i || (bodies[j].awake && j < i) || !nearby(bodies[i], bodies[j]) || (i >= real && j >= real)) continue;
				const std::size_t before = contacts.size();
				collide(bodies, static_cast<int>(i), static_cast<int>(j), ground, contacts);
				if (contacts.size() > before && !bodies[j].awake && bodies[j].invMass > 0.0) {   // (it wakes what it touches when it touches it hard enough)
					const Vec3 rel = bodies[i].velocityAt(contacts[before].p);
					if (dot(rel, rel) > 0.01) bodies[j].awake = true, bodies[j].still = 0.0;
				}
			}
		}
		// the contacts' targets (bounce off a hard landing, push out what overlaps) and their tangents
		for (Contact &c : contacts) {
			const Body &A = bodies[static_cast<std::size_t>(c.a)];
			const Vec3 vb = c.b >= 0 ? bodies[static_cast<std::size_t>(c.b)].velocityAt(c.p) : Vec3{};
			const double vn = dot(A.velocityAt(c.p) - vb, c.n);
			const double bounce = c.b >= 0 ? 0.5 * (A.def->bounce + bodies[static_cast<std::size_t>(c.b)].def->bounce) : A.def->bounce;
			c.target = (vn < -1.0 ? -bounce * vn : 0.0) + 0.2 / dt * std::max(0.0, c.depth - 0.002);
			if (vn < -1.0 && time - lastImpact[static_cast<std::size_t>(c.a)] > 0.15) {
				lastImpact[static_cast<std::size_t>(c.a)] = time;
				run.impacts.push_back({time, c.p, -vn});
			}
			c.t1 = normalize(std::fabs(c.n.y) < 0.9 ? cross(c.n, {0.0, 1.0, 0.0}) : cross(c.n, {1.0, 0.0, 0.0}));
			c.t2 = cross(c.n, c.t1);
		}
		// sequential impulses
		// (a sleeping body stays put this step, as a fixed one: what lands on a sleeping stack rests on it)
		const auto moves = [&](const Body &b) { return b.awake && b.invMass > 0.0; };
		const auto apply = [&](Contact &c, Vec3 dir, double lambda) {
			Body &A = bodies[static_cast<std::size_t>(c.a)];
			const Vec3 J = dir * lambda;
			if (moves(A)) {
				A.v = A.v + J * A.invMass;
				A.w = A.w + A.toWorldInvI(cross(c.p - A.x, J));
			}
			if (c.b >= 0 && moves(bodies[static_cast<std::size_t>(c.b)])) {
				Body &B = bodies[static_cast<std::size_t>(c.b)];
				B.v = B.v - J * B.invMass;
				B.w = B.w - B.toWorldInvI(cross(c.p - B.x, J));
			}
		};
		const auto mass = [&](const Contact &c, Vec3 dir) {
			const Body &A = bodies[static_cast<std::size_t>(c.a)];
			double k = 0.0;
			if (moves(A)) {
				const Vec3 ra = c.p - A.x;
				k += A.invMass + dot(cross(A.toWorldInvI(cross(ra, dir)), ra), dir);
			}
			if (c.b >= 0 && moves(bodies[static_cast<std::size_t>(c.b)])) {
				const Body &B = bodies[static_cast<std::size_t>(c.b)];
				const Vec3 rb = c.p - B.x;
				k += B.invMass + dot(cross(B.toWorldInvI(cross(rb, dir)), rb), dir);
			}
			return k;
		};
		for (int it = 0; it < 12; ++it)
			for (Contact &c : contacts) {
				const Body &A = bodies[static_cast<std::size_t>(c.a)];
				const auto relative = [&]() { return A.velocityAt(c.p) - (c.b >= 0 ? bodies[static_cast<std::size_t>(c.b)].velocityAt(c.p) : Vec3{}); };
				const double kn = mass(c, c.n);
				if (kn <= 0.0) continue;
				double dn = (c.target - dot(relative(), c.n)) / kn;
				const double total = std::max(0.0, c.normalImpulse + dn);
				dn = total - c.normalImpulse, c.normalImpulse = total;
				apply(c, c.n, dn);
				const double mu = c.b >= 0 ? std::sqrt(A.def->friction * bodies[static_cast<std::size_t>(c.b)].def->friction) : A.def->friction;
				for (int k = 0; k < 2; ++k) {
					const Vec3 t = k == 0 ? c.t1 : c.t2;
					const double kt = mass(c, t);
					if (kt <= 0.0) continue;
					double dt2 = -dot(relative(), t) / kt;
					const double f = std::clamp(c.frictionImpulse[k] + dt2, -mu * c.normalImpulse, mu * c.normalImpulse);
					dt2 = f - c.frictionImpulse[k], c.frictionImpulse[k] = f;
					apply(c, t, dt2);
				}
			}
		// move, and let what has come to rest sleep
		for (Body &b : bodies) {
			if (!b.awake || b.invMass <= 0.0) continue;
			b.w = b.w * (b.def->sphere ? 0.998 : 0.9995);   // (a ball's rolling resistance; air)
			b.x = b.x + b.v * dt;
			const Quat dq = qmul({0.0, b.w.x, b.w.y, b.w.z}, b.q);   // (q' = q + dt/2 (0, w) q)
			b.q = qnorm({b.q.w + 0.5 * dt * dq.w, b.q.x + 0.5 * dt * dq.x, b.q.y + 0.5 * dt * dq.y, b.q.z + 0.5 * dt * dq.z});
			b.still = (length(b.v) < 0.04 && length(b.w) < 0.12) ? b.still + dt : 0.0;
			if (b.still > 0.4) b.awake = false, b.v = {}, b.w = {};
		}
	}
	return run;
}

// The poses at time t of a run (between its kept steps: the middle's place in a straight line, the turn blended the short way round).
inline std::vector<Pose> posesAt(const Run &run, double t) {
	const std::size_t kept = run.kept();
	if (kept == 0) return {};
	const double s = std::clamp(t / (run.step * run.keepEvery), 0.0, static_cast<double>(kept - 1));
	const std::size_t i = static_cast<std::size_t>(s), j = std::min(kept - 1, i + 1);
	const double f = s - static_cast<double>(i);
	std::vector<Pose> out(run.bodies);
	for (std::size_t k = 0; k < run.bodies; ++k) {
		const Pose &a = run.poses[i * run.bodies + k], &b = run.poses[j * run.bodies + k];
		out[k].at = a.at + (b.at - a.at) * f;
		const double sign = a.q.w * b.q.w + a.q.x * b.q.x + a.q.y * b.q.y + a.q.z * b.q.z < 0.0 ? -1.0 : 1.0;   // (the turn between them the short way round)
		out[k].q = qnorm({a.q.w + (sign * b.q.w - a.q.w) * f, a.q.x + (sign * b.q.x - a.q.x) * f, a.q.y + (sign * b.q.y - a.q.y) * f, a.q.z + (sign * b.q.z - a.q.z) * f});
	}
	return out;
}

// A film's world, worked out once in this process (by what its bodies are) for `seconds`.
// movers (if given) are made only when the world is first worked out under moversKey (what they follow: a film's characters - the film's text says them).
inline std::shared_ptr<const Run> runFor(const std::vector<BodyDef> &defs, double seconds, const std::string &moversKey = std::string(),
										  const std::function<std::vector<Kinematic>()> &movers = nullptr, double gravity = physics::kGravity) {
	std::ostringstream key;
	key.precision(9);
	key << seconds << '#' << moversKey << "#g" << gravity;
	for (const BodyDef &d : defs)
		key << '|' << d.block << ' ' << d.sphere << ' ' << d.half.x << ' ' << d.half.y << ' ' << d.half.z << ' ' << d.radius << ' ' << d.at.x << ' ' << d.at.y << ' ' << d.at.z << ' '
			<< d.turn << ' ' << d.mass << ' ' << d.start << ' ' << d.velocity.x << ' ' << d.velocity.y << ' ' << d.velocity.z << ' ' << d.spin.x << ' ' << d.spin.y << ' ' << d.spin.z
			<< ' ' << d.fixed << ' ' << d.bounce << ' ' << d.friction;
	static std::mutex lock;
	static std::map<std::string, std::shared_ptr<const Run>> cache;
	const std::lock_guard<std::mutex> hold(lock);
	std::shared_ptr<const Run> &slot = cache[key.str()];
	if (!slot) {
		const std::vector<Kinematic> m = movers ? movers() : std::vector<Kinematic>();
		slot = std::make_shared<const Run>(simulate(defs, seconds, 0.0, m.empty() ? nullptr : &m, gravity));
	}
	return slot;
}

// The pbrt transform that moves body k's block from where the scene has it to its pose.
inline std::string transformOf(const BodyDef &d, const Pose &p, std::string (*num)(double)) {
	const Quat rel = qmul(p.q, qconj(qaxis({0.0, 1.0, 0.0}, d.turn * kPi / 180.0)));   // (its turn since the scene's)
	const double angle = 2.0 * std::acos(std::clamp(rel.w, -1.0, 1.0)), s = std::sqrt(std::max(0.0, 1.0 - rel.w * rel.w));
	std::string out = "  Translate " + num(p.at.x) + " " + num(p.at.y) + " " + num(p.at.z) + "\n";
	if (angle > 1e-7 && s > 1e-9) out += "  Rotate " + num(angle * 180.0 / kPi) + " " + num(rel.x / s) + " " + num(rel.y / s) + " " + num(rel.z / s) + "\n";
	return out + "  Translate " + num(-d.at.x) + " " + num(-d.at.y) + " " + num(-d.at.z) + "\n";
}

}  // namespace rigid
}  // namespace cartoon
