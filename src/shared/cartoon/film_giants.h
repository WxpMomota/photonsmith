#pragma once
// film_giants.h -- the sea's giants for a film file (film_file.h `whale`, `dolphins`):
//
//   whale     a humpback swimming at the surface along a heading: every so often its back rolls up out of the water, it blows (a bushy spout of spray) and it dives,
//             its flukes lifting last; at `breach T` it launches itself out of the sea nearly upright, twists in the air and crashes back on its back in a burst of spray,
//             leaving a patch of foam;
//   dolphins  a pod porpoising along a heading: each leaping clear in low arcs every few seconds, a splash where it leaves the water and where it goes back in.
//
// A swimming body follows its path as a train follows its track: each part of it is where the part in front was a moment before, so it arches over a roll and bends
// through a leap. Under water (`underwater`) a breach and each dolphin going back in leave bubbles. The whale blows, crashes and (under water) sings; dolphins whistle
// and splash. Coordinates are pbrt's, as a film file's (the meshes are written in them); the giants move in nature's time (`slowmo` slows a breach).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_particles.h"
#include "film_soundscape.h"
#include "film_water.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace giants {

// A whale: where it starts (pbrt's x z), its heading (a compass bearing, as `wind`: 0 +z, 90 +x), how long it is, how fast it swims, when it is there, when it breaches.
struct Whale {
	double x = 0.0, z = -150.0, toward = 0.0, length = 14.0, speed = 2.0, t0 = 0.0, t1 = 1e30;
	std::vector<double> breaches;
	int seed = 1;
};
// A pod of dolphins: how many, where its middle starts, its heading, how fast it goes, when it is there.
struct Dolphins {
	int count = 6;
	double x = 0.0, z = -80.0, toward = 90.0, speed = 6.0, t0 = 0.0, t1 = 1e30;
	int seed = 1;
};

// ---- the bodies ----
// A body (its length 1, head towards +x, back up +y, its middle at the origin), in two colours: its dark back and its pale parts.
struct Body {
	Mesh dark, light;
};
namespace detail {
inline Mat4 frameAt(Vec3 c, double yawDegrees, double rollDegrees) { return invertAffine(mul(translate(c), rotate({0.0, 1.0, 0.0}, yawDegrees), rotate({1.0, 0.0, 0.0}, rollDegrees))); }
// The body's surface split by colour: `isLight(centre, normal)` for each triangle.
inline Body split(const Mesh &m, const std::function<bool(Vec3, Vec3)> &isLight) {
	Body b;
	for (Mesh *out : {&b.dark, &b.light}) out->verts = m.verts, out->normals = m.normals;
	for (const auto &tr : m.tris) {
		const Vec3 c = (m.verts[tr[0]] + m.verts[tr[1]] + m.verts[tr[2]]) * (1.0 / 3.0), n = normalize(m.normals[tr[0]] + m.normals[tr[1]] + m.normals[tr[2]]);
		(isLight(c, n) ? b.light : b.dark).tris.push_back(tr);
	}
	return b;
}
}  // namespace detail

// A humpback: a long body tapering to its tail stock, a throat and jaw, a little hump for a dorsal fin, pectoral fins a third of its length, broad flukes.
inline const Body &whaleBody() {
	static const Body body = [] {
		Mat4 pec[2], fluke[2];
		for (int k = 0; k < 2; ++k) {
			const double s = k == 0 ? 1.0 : -1.0;
			pec[k] = detail::frameAt({0.08, -0.08, s * 0.24}, s * 28.0, s * 22.0);
			fluke[k] = detail::frameAt({-0.48, 0.0, s * 0.1}, -s * 25.0, 0.0);
		}
		const Mat4 hump = detail::frameAt({-0.17, 0.095, 0.0}, 0.0, 0.0);
		const auto f = [&](Vec3 p) {
			const Vec3 q{p.x, p.y * 1.12, p.z};   // (a little flattened)
			double d = sdf::roundCone(q, {0.44, -0.01, 0.0}, {0.12, 0.0, 0.0}, 0.06, 0.11);
			d = sdf::smin(d, sdf::roundCone(q, {0.12, 0.0, 0.0}, {-0.30, 0.01, 0.0}, 0.11, 0.035), 0.04);
			d = sdf::smin(d, sdf::roundCone(q, {-0.30, 0.01, 0.0}, {-0.46, 0.0, 0.0}, 0.035, 0.013), 0.03);
			d = sdf::smin(d, sdf::ellipsoid(p, Vec3{0.27, -0.05, 0.0}, {0.17, 0.065, 0.085}), 0.04);   // (the throat)
			d = sdf::smin(d, sdf::ellipsoid(p, hump, {0.05, 0.03, 0.012}), 0.02);
			for (int k = 0; k < 2; ++k) {
				d = sdf::smin(d, sdf::ellipsoid(p, pec[k], {0.05, 0.012, 0.17}), 0.02);
				d = sdf::smin(d, sdf::ellipsoid(p, fluke[k], {0.045, 0.01, 0.11}), 0.015);
			}
			return d;
		};
		const Mesh m = sdf::surfaceNets(f, {-0.62, -0.2, -0.46}, {0.53, 0.2, 0.46}, 1.0 / 90.0);
		return detail::split(m, [](Vec3 c, Vec3 n) {   // white pectorals, a pale grooved throat, pale under the flukes
			return (std::abs(c.z) > 0.13 && c.x > -0.25) || (n.y < -0.5 && c.x > -0.05) || (c.x < -0.42 && n.y < -0.5);
		});
	}();
	return body;
}
// A bottlenose dolphin: a body swelling behind its melon, a short beak, a swept dorsal fin, flippers, flukes.
inline const Body &dolphinBody() {
	static const Body body = [] {
		Mat4 flip[2], fluke[2];
		for (int k = 0; k < 2; ++k) {
			const double s = k == 0 ? 1.0 : -1.0;
			flip[k] = detail::frameAt({0.17, -0.075, s * 0.1}, s * 30.0, s * 30.0);
			fluke[k] = detail::frameAt({-0.46, 0.0, s * 0.075}, -s * 30.0, 0.0);
		}
		const Mat4 dorsal = invertAffine(mul(translate(-0.03, 0.12, 0.0), rotate({0.0, 0.0, 1.0}, -35.0)));
		const auto f = [&](Vec3 p) {
			double d = sdf::roundCone(p, {0.3, 0.0, 0.0}, {0.05, 0.0, 0.0}, 0.07, 0.1);
			d = sdf::smin(d, sdf::roundCone(p, {0.05, 0.0, 0.0}, {-0.35, 0.01, 0.0}, 0.1, 0.025), 0.03);
			d = sdf::smin(d, sdf::roundCone(p, {-0.35, 0.01, 0.0}, {-0.44, 0.0, 0.0}, 0.025, 0.012), 0.02);
			d = sdf::smin(d, sdf::sphere(p, {0.32, 0.02, 0.0}, 0.065), 0.03);   // (the melon)
			d = sdf::smin(d, sdf::capsule(p, {0.36, -0.02, 0.0}, {0.47, -0.03, 0.0}, 0.022), 0.03);   // (the beak)
			d = sdf::smin(d, sdf::ellipsoid(p, dorsal, {0.07, 0.075, 0.012}), 0.02);
			for (int k = 0; k < 2; ++k) {
				d = sdf::smin(d, sdf::ellipsoid(p, flip[k], {0.07, 0.01, 0.032}), 0.015);
				d = sdf::smin(d, sdf::ellipsoid(p, fluke[k], {0.035, 0.009, 0.08}), 0.012);
			}
			return d;
		};
		const Mesh m = sdf::surfaceNets(f, {-0.55, -0.15, -0.2}, {0.52, 0.22, 0.2}, 1.0 / 80.0);
		return detail::split(m, [](Vec3 c, Vec3 n) { return n.y < -0.3 && c.x > -0.3; });   // (a pale belly)
	}();
	return body;
}

// Where each part of a body is: for a place along it (metres from its middle, + towards the head), a point and its frame (forward, up; the side is their cross).
struct Frame {
	Vec3 at, forward, up;
};
// A body placed through `frameAt` (its length `L`) in its two colours, as scene text.
inline std::string bodyText(const Body &b, double L, const std::function<Frame(double)> &frameAt, Vec3 darkColour, Vec3 lightColour) {
	// the frames along the body, once (the vertices take the nearest two)
	constexpr int kN = 160;
	constexpr double kFrom = -0.65, kTo = 0.56;
	std::vector<Frame> fr;
	for (int i = 0; i <= kN; ++i) fr.push_back(frameAt((kFrom + (kTo - kFrom) * i / kN) * L));
	std::string out;
	for (int part = 0; part < 2; ++part) {
		const Mesh &src = part == 0 ? b.dark : b.light;
		if (src.tris.empty()) continue;
		Mesh m;
		m.tris = src.tris;
		m.verts.reserve(src.verts.size()), m.normals.reserve(src.verts.size());
		for (std::size_t v = 0; v < src.verts.size(); ++v) {
			const Vec3 p = src.verts[v], n = src.normals[v];
			const double u = std::clamp((p.x - kFrom) / (kTo - kFrom) * kN, 0.0, double(kN) - 1e-9);
			const int i = static_cast<int>(u);
			const double w = u - i;
			const Vec3 at = lerp(fr[i].at, fr[i + 1].at, w), F = normalize(lerp(fr[i].forward, fr[i + 1].forward, w)), U = normalize(lerp(fr[i].up, fr[i + 1].up, w)), S = cross(F, U);
			m.verts.push_back(at + U * (p.y * L) + S * (p.z * L));
			m.normals.push_back(normalize(F * n.x + U * n.y + S * n.z));
		}
		std::string shapes;
		m.appendPbrt(shapes, identity(), false);
		out += attr(coated(part == 0 ? darkColour : lightColour, 0.06), shapes);
	}
	return out;
}

// ---- the whale's swim ----
namespace detail {
inline Vec3 heading(double degrees) { return {std::sin(degrees * kPi / 180.0), 0.0, std::cos(degrees * kPi / 180.0)}; }
// The whale's dive cycle: how far along its way (horizontally, metres) between one roll at the surface and the next.
inline double cycle(const Whale &w) { return std::max(3.0 * w.length, w.speed * 16.0); }
inline double cycleStart(const Whale &w) { return cycle(w) * hash01(w.seed, 501); }
// The height of the whale's way where it has come `h` metres along: deep, then a roll up through the surface - its back out, gently up, steeply down (its flukes lift as
// they go over the top) - and deep again. `level`: the sea's.
inline double swimY(const Whale &w, double h, double level) {
	const double L = w.length, deep = level - 0.35 * L, top = level + 0.1 * L, C = cycle(w);
	const double u = std::fmod(std::fmod(h + cycleStart(w), C) + C, C) / (1.6 * L);
	if (u >= 1.0) return deep;
	return deep + (top - deep) * (u < 0.6 ? smoothstep(0.0, 0.6, u) : 1.0 - smoothstep(0.6, 1.0, u));
}
}  // namespace detail

// How long a breach is in the air, for a whale `L` long: from its head breaking the surface (at the breach's time) to its middle crashing back down; and its launch speed.
inline double breachSpeed(double L) { return std::sqrt(2.0 * 9.81 * 0.85 * L); }
inline double breachCrash(double L) {
	const double v0 = breachSpeed(L);
	return (v0 + std::sqrt(std::max(0.0, v0 * v0 - 4.0 * 4.905 * 0.47 * L))) / 9.81;
}
// The breach under way at nature's time `nt`, if any: its time (nature's), else a negative number.
inline double breachNow(const Whale &w, const std::vector<double> &breachNt, double nt, double before = 0.4, double after = 0.0) {
	for (double b : breachNt)
		if (nt >= b - before && nt <= b + breachCrash(w.length) + after) return b;
	return -1.0;
}
// Where the whale's middle is along its heading at nature's time `nt` (`ntStart`: its start's nature time).
inline Vec3 whaleMiddle(const Whale &w, double nt, double ntStart) {
	const Vec3 u = detail::heading(w.toward);
	return Vec3{w.x, 0.0, w.z} + u * (w.speed * (nt - ntStart));
}
// The whale at `nt` as its frames along its body: rolling along its way, or flung up in a breach (rigid, pitched up and twisting onto its back as it falls).
inline std::function<Frame(double)> whaleFrames(const Whale &w, double nt, double ntStart, const std::vector<double> &breachNt, double level) {
	const Vec3 u = detail::heading(w.toward), up{0.0, 1.0, 0.0}, mid = whaleMiddle(w, nt, ntStart);
	const double L = w.length, h0 = w.speed * (nt - ntStart);
	const double b = breachNow(w, breachNt, nt);
	if (b >= 0.0) {
		const double tau = nt - b, v0 = breachSpeed(L), air = breachCrash(L), s = clamp01(tau / air);
		const double y = level - 0.47 * L + v0 * tau - 4.905 * tau * tau;
		const double pitch = (80.0 - 70.0 * smoothstep(0.45, 1.0, s)) * kPi / 180.0, roll = 160.0 * smoothstep(0.15, 0.85, s) * kPi / 180.0;
		const Vec3 F = u * std::cos(pitch) + up * std::sin(pitch), N0 = up * std::cos(pitch) - u * std::sin(pitch), S0 = cross(F, N0);
		const Vec3 N = N0 * std::cos(roll) + S0 * std::sin(roll), c{mid.x, y, mid.z};
		return [c, F, N](double xi) { return Frame{c + F * xi, F, N}; };
	}
	// (deep and level for a while round a breach: it comes up from the deep and goes back down there)
	const bool calm = breachNow(w, breachNt, nt, 6.0, 8.0) >= 0.0;
	const double beat = 0.02 * L, phase = 2.0 * kPi * nt / 3.0;   // (a slow beat of the flukes)
	return [=](double xi) {
		// the way, by its length: from the middle `xi` metres along it (the way rises and falls, so a part's place is found by walking it)
		const auto yAt = [&](double h) { return calm ? level - 0.35 * L : detail::swimY(w, h, level); };
		const double dir = xi >= 0.0 ? 1.0 : -1.0;
		double h = h0, left = std::abs(xi);
		while (left > 1e-9) {   // (steps of L/120 along the slope)
			const double a = std::min(L / 120.0, left), slope = (yAt(h + dir * a) - yAt(h)) / a;
			h += dir * a / std::sqrt(1.0 + slope * slope);
			left -= a;
		}
		const double slope = (yAt(h + 0.01 * L) - yAt(h - 0.01 * L)) / (0.02 * L);
		const Vec3 F = normalize(u + up * slope), N = normalize(up - u * slope);
		const double tail = std::max(0.0, -xi / L);   // (the beat grows towards the flukes)
		const Vec3 at = Vec3{w.x, 0.0, w.z} + u * h + up * yAt(h) + N * (beat * tail * tail * 4.0 * std::sin(phase - 4.0 * tail));
		return Frame{at, F, N};
	};
}
// When the whale blows (nature's times) up to `nt`: as its blowhole comes over the top of each roll (not round a breach).
inline std::vector<double> blowTimes(const Whale &w, double ntStart, double ntEnd, const std::vector<double> &breachNt) {
	std::vector<double> out;
	const double C = detail::cycle(w), L = w.length;
	if (w.speed <= 0.0) return out;
	for (int k = -1; k < 2000; ++k) {
		const double h = 0.8 * L - 0.3 * L - detail::cycleStart(w) + k * C;   // (the blowhole, 0.3 L ahead of the middle, at 0.8 L into the roll)
		const double t = ntStart + h / w.speed;
		if (t > ntEnd) break;
		if (t >= ntStart && breachNow(w, breachNt, t, 6.0, 8.0) < 0.0) out.push_back(t);
	}
	return out;
}

// ---- dolphins ----
namespace detail {
// A dolphin's leap cycle: its period (seconds) and when its first leap starts, of its own.
inline double leapPeriod(const Dolphins &d, int i) { return 2.6 + 1.2 * hash01(d.seed * 131 + i, 601); }
inline double leapOffset(const Dolphins &d, int i) { return leapPeriod(d, i) * hash01(d.seed * 131 + i, 603); }
constexpr double kLeap = 0.95;   // (seconds in the air)
}  // namespace detail
// A dolphin's place in the pod (across and along, metres) and how high its way is `h` metres along (its time on that way: h / speed): a little under the surface, and a low arc clear
// of it every leap.
inline Vec3 dolphinSlot(const Dolphins &d, int i) { return {(hash01(d.seed * 131 + i, 605) - 0.5) * (2.0 + 1.6 * d.count), 0.0, (hash01(d.seed * 131 + i, 607) - 0.5) * (3.0 + 1.5 * d.count)}; }
inline double dolphinY(const Dolphins &d, int i, double ntSince, double level) {
	const double P = detail::leapPeriod(d, i), ph = std::fmod(std::fmod(ntSince + detail::leapOffset(d, i), P) + P, P);
	if (ph < detail::kLeap) {   // in the air: up out of the water and back, its middle 1 m above the surface at the top
		const double v0 = 4.905 * detail::kLeap;
		return level - 0.25 + v0 * ph - 4.905 * ph * ph;   // (from just under the surface to just under it)
	}
	const double under = (ph - detail::kLeap) / (P - detail::kLeap);
	return level - 0.25 - 0.9 * std::sin(kPi * under);   // (a dip between leaps)
}
// When dolphin i's leaps (nature's time since the pod's start) leave the water and go back in, up to `since`.
inline void leapTimes(const Dolphins &d, int i, double since, std::vector<double> &out, std::vector<double> &in) {
	const double P = detail::leapPeriod(d, i);
	for (int k = 0; k < 10000; ++k) {
		const double t = k * P - detail::leapOffset(d, i);
		if (t > since) break;
		if (t >= 0.0) out.push_back(t + 0.05);
		if (t + detail::kLeap >= 0.0 && t + detail::kLeap <= since) in.push_back(t + detail::kLeap - 0.05);
	}
}
// Dolphin i at `since` seconds (nature's) into the pod's swim, as frames along its body (its length `L`).
inline std::function<Frame(double)> dolphinFrames(const Dolphins &d, int i, double since, double level, double L) {
	const Vec3 u = detail::heading(d.toward), side{u.z, 0.0, -u.x}, up{0.0, 1.0, 0.0}, slot = dolphinSlot(d, i);
	const Vec3 base = Vec3{d.x, 0.0, d.z} + side * slot.x + u * slot.z;
	const double sp = std::max(0.1, d.speed);
	return [=](double xi) {
		const double tt = since + xi / sp;   // (each part where the part in front was a moment before)
		const double y = dolphinY(d, i, tt, level), slope = (dolphinY(d, i, tt + 0.02, level) - dolphinY(d, i, tt - 0.02, level)) / (0.04 * sp);
		const Vec3 F = normalize(u + up * slope), N = normalize(up - u * slope);
		return Frame{base + u * (sp * tt) + up * y, F, N};
	};
}

// ---- spray, foam, bubbles ----
// A burst of spray `age` seconds after it went up from `at`: `count` drops thrown up and out at about `speed`, `size` across (white: spray is white); a blow's (`blow`) go
// up in a bushy column and hang, drifting.
inline std::string sprayText(Vec3 at, double age, int count, double speed, double size, int seed, bool blow = false, const Vec3 *glow = nullptr) {
	if (age < 0.0 || age > 6.0) return std::string();
	std::string out;
	char buf[160];
	for (int n = 0; n < count; ++n) {
		const double a = 2.0 * kPi * hash01(seed * 7919 + n, 1), up = blow ? 0.9 + 0.1 * hash01(seed * 7919 + n, 2) : 0.45 + 0.55 * hash01(seed * 7919 + n, 2);
		const double v = speed * (0.4 + 0.8 * hash01(seed * 7919 + n, 3)), drag = blow ? 1.4 : 0.35, g = blow ? 2.5 : 9.81;
		const double life = blow ? 1.8 + 1.8 * hash01(seed * 7919 + n, 4) : 6.0;
		if (age > life) continue;
		const Vec3 vel{v * std::cos(a) * std::sqrt(1.0 - up * up) * 1.4, v * up, v * std::sin(a) * std::sqrt(1.0 - up * up) * 1.4};
		const double k = (1.0 - std::exp(-drag * age)) / drag;
		const Vec3 p = at + vel * k + Vec3{0.0, -0.5 * g * age * age, 0.0};
		if (p.y < at.y - 0.05) continue;   // (back in the sea)
		const double r = size * (0.3 + 0.7 * hash01(seed * 7919 + n, 5)) * (blow ? 1.0 + age : 1.0);
		std::snprintf(buf, sizeof buf, "  AttributeBegin\n    Translate %.3f %.3f %.3f\n    Shape \"sphere\" \"float radius\" [ %.4f ]\n  AttributeEnd\n", p.x, p.y, p.z, r);
		out += buf;
	}
	if (out.empty()) return out;
	const std::string light = glow ? "  AreaLightSource \"diffuse\" \"rgb L\" [ " + fmt3(*glow) + " ]\n" : std::string();
	return "# spray (film_giants.h)\nAttributeBegin\n" + light + "  Material \"diffuse\" \"rgb reflectance\" [ 0.82 0.85 0.87 ]\n" + out + "AttributeEnd\n";
}
// A patch of foam `age` seconds after a crash at `at`, spreading to `radius` and breaking up over half a minute: flecks lying on the sea's surface.
inline std::string foamText(const water::Sea &sea, Vec3 at, double age, double radius, double nt, int seed) {
	if (age < 0.0 || age > 30.0) return std::string();
	const int count = static_cast<int>(2400.0 * (1.0 - age / 30.0));
	const double R = radius * (0.45 + 0.55 * (1.0 - std::exp(-age / 4.0)));
	particles::detail::MeshText mesh;
	for (int n = 0; n < count; ++n) {
		const double a = 2.0 * kPi * hash01(seed * 6007 + n, 1), r = R * std::pow(hash01(seed * 6007 + n, 2), 0.8);   // (thinning out to its edge)
		Vec3 p, nn;
		double fold = 0.0;
		water::seaAt(sea, at.x + r * std::cos(a), at.z + r * std::sin(a), nt, 0.0, p, nn, fold);
		const double s = radius * (0.004 + 0.012 * hash01(seed * 6007 + n, 3)) * (1.0 - 0.5 * r / std::max(1e-6, R));
		Vec3 e1 = normalize(cross(nn, Vec3{std::cos(3.0 * a), 0.0, std::sin(3.0 * a)}));
		const Vec3 e2 = cross(nn, e1), c = p + nn * 0.03;
		mesh.quad(c - e1 * s - e2 * s, c + e1 * s - e2 * s, c + e1 * s + e2 * s, c - e1 * s + e2 * s);
	}
	const std::string light = sea.glowing() ? "AreaLightSource \"diffuse\" \"rgb L\" [ " + fmt3(sea.glow) + " ]\n  " : std::string();
	return mesh.text(light + "Material \"diffuse\" \"rgb reflectance\" [ 0.85 0.87 0.88 ]");
}
// Bubbles rising `age` seconds after something went into the sea at `at` (under water: `level` the surface): `count` of them, up to `size` across.
inline std::string bubbleText(Vec3 at, double age, double level, int count, double depth, double size, int seed) {
	if (age < 0.0 || age > 10.0) return std::string();
	std::string out;
	char buf[160];
	for (int n = 0; n < count; ++n) {
		const double a = 2.0 * kPi * hash01(seed * 4001 + n, 1), r = depth * 0.4 * std::sqrt(hash01(seed * 4001 + n, 2));
		const double y = level - depth * hash01(seed * 4001 + n, 3) + (0.5 + 0.7 * hash01(seed * 4001 + n, 4)) * age;
		if (y > level - 0.02) continue;
		const double wob = 0.08 * std::sin(6.0 * age + 6.28 * hash01(seed * 4001 + n, 5));
		std::snprintf(buf, sizeof buf, "  AttributeBegin\n    Translate %.3f %.3f %.3f\n    Shape \"sphere\" \"float radius\" [ %.4f ]\n  AttributeEnd\n", at.x + r * std::cos(a) + wob, y,
					  at.z + r * std::sin(a), size * (0.15 + 0.85 * hash01(seed * 4001 + n, 6)));
		out += buf;
	}
	return out.empty() ? out : "# bubbles (film_giants.h)\nAttributeBegin\n  Material \"dielectric\" \"float eta\" [ 0.75 ]\n" + out + "AttributeEnd\n";
}

// ---- a frame and its sound ----
// Where the whale's head breaks the surface in breach `b` (nature's time), and where its middle crashes back down (pbrt's, at the sea's level).
inline Vec3 breachOut(const Whale &w, double b, double ntStart, double level) {
	const Vec3 m = whaleMiddle(w, b, ntStart) + detail::heading(w.toward) * (0.08 * w.length);
	return {m.x, level, m.z};
}
inline Vec3 breachIn(const Whale &w, double b, double ntStart, double level) {
	const Vec3 m = whaleMiddle(w, b + breachCrash(w.length), ntStart);
	return {m.x, level, m.z};
}
// Where a whale's blowhole is at nature's time `bt`.
inline Vec3 blowhole(const Whale &w, double bt, double ntStart, const std::vector<double> &breachNt, double level) {
	const Frame f = whaleFrames(w, bt, ntStart, breachNt, level)(0.3 * w.length);
	return f.at + f.up * (0.1 * w.length);
}
constexpr double kDolphinLength = 2.6;

// The giants at film time t (nature's `nt`), as scene text: the whales and their spray, foam and bubbles; the dolphins and their splashes. `natureAt`: nature's time at a film time.
inline std::string giantsText(const std::vector<Whale> &whales, const std::vector<Dolphins> &pods, const water::Sea &sea, bool underwater, double t, double nt,
							  const std::function<double(double)> &natureAt) {
	std::string out;
	const double level = sea.level;
	for (std::size_t i = 0; i < whales.size(); ++i) {
		const Whale &w = whales[i];
		if (t < w.t0 || t > w.t1) continue;
		const double ns = natureAt(w.t0), sc = w.length / 14.0;
		const int seed = 7 + static_cast<int>(i) * 97;
		std::vector<double> b;
		for (double x : w.breaches) b.push_back(natureAt(x));
		out += bodyText(whaleBody(), w.length, whaleFrames(w, nt, ns, b, level), {0.05, 0.055, 0.06}, {0.75, 0.76, 0.75});
		for (std::size_t k = 0; k < b.size(); ++k) {   // a breach: spray as it bursts out, a crash of spray as it lands, its foam, bubbles under water
			const double crash = b[k] + breachCrash(w.length);
			const Vec3 in = breachIn(w, b[k], ns, level);
			out += sprayText(breachOut(w, b[k], ns, level), nt - b[k], 900, 8.0 * std::sqrt(sc), 0.06 * sc, seed + static_cast<int>(k), false, sea.glowing());
			out += sprayText(in, nt - crash, 2600, 13.0 * std::sqrt(sc), 0.07 * sc, seed + 50 + static_cast<int>(k), false, sea.glowing());
			out += foamText(sea, in, nt - crash, 0.9 * w.length, nt, seed + static_cast<int>(k));
			if (underwater) out += bubbleText(in, nt - crash, level, 400, 0.4 * w.length, 0.12 * sc, seed + static_cast<int>(k));
		}
		for (double bt : blowTimes(w, ns, nt, b))   // its blows: a bushy spout of spray hanging over it
			if (nt - bt < 4.0) out += sprayText(blowhole(w, bt, ns, b, level), nt - bt, 260, 9.0 * std::sqrt(sc), 0.06 * sc, seed + 300 + static_cast<int>(bt * 10.0), true);
	}
	for (std::size_t p = 0; p < pods.size(); ++p) {
		const Dolphins &d = pods[p];
		if (t < d.t0 || t > d.t1) continue;
		const double since = nt - natureAt(d.t0);
		for (int i = 0; i < d.count; ++i) {
			out += bodyText(dolphinBody(), kDolphinLength, dolphinFrames(d, i, since, level, kDolphinLength), {0.2, 0.22, 0.25}, {0.62, 0.64, 0.66});
			std::vector<double> outs, ins;
			leapTimes(d, i, since, outs, ins);
			const int seed = 900 + static_cast<int>(p) * 211 + i * 17;
			for (const std::vector<double> *list : {&outs, &ins})
				for (double x : *list) {
					if (since - x > 1.6) continue;
					const Vec3 at = dolphinFrames(d, i, x, level, kDolphinLength)(0.0).at;
					out += sprayText({at.x, level, at.z}, since - x, 45, 3.5, 0.03, seed + static_cast<int>(x * 10.0), false, sea.glowing());
					if (underwater && list == &ins) out += bubbleText({at.x, level, at.z}, since - x, level, 60, 1.2, 0.04, seed + static_cast<int>(x * 10.0));
				}
		}
	}
	return out;
}

// The giants' sound: a whale's blows and the crash of a breach; under water its song; dolphins' splashes and whistles. `filmAt`: the film time at a nature's time.
inline void giantsSound(audio::Mix &m, const soundscape::Ears &ears, const std::vector<Whale> &whales, const std::vector<Dolphins> &pods, double level, bool underwater,
						const std::function<double(double)> &natureAt, const std::function<double(double)> &filmAt) {
	namespace ss = soundscape;
	const double T = m.seconds();
	for (std::size_t i = 0; i < whales.size(); ++i) {
		const Whale &w = whales[i];
		const double ns = natureAt(w.t0), sc = w.length / 14.0, end = std::min(T, w.t1);
		const unsigned seed = 1301u + static_cast<unsigned>(i) * 31u;
		std::vector<double> b;
		for (double x : w.breaches) b.push_back(natureAt(x));
		for (std::size_t k = 0; k < b.size(); ++k) {
			const unsigned s = seed + static_cast<unsigned>(k) * 7u;
			ss::place(m, ears, filmAt(b[k]), breachOut(w, b[k], ns, level), ss::wash(s), 0.7 * sc, 60.0);
			const double crash = filmAt(b[k] + breachCrash(w.length));
			const Vec3 in = breachIn(w, b[k], ns, level);
			ss::place(m, ears, crash, in, ss::crash(s + 1u), 1.0 * sc, 80.0);
			ss::place(m, ears, crash, in, audio::noiseBurst(2.5, 0.02, 2.2, 60.0, 900.0, s + 2u), 1.4 * sc, 80.0);   // (the boom of it)
		}
		for (double bt : blowTimes(w, ns, natureAt(end), b))
			ss::place(m, ears, filmAt(bt), blowhole(w, bt, ns, b, level), audio::noiseBurst(1.4, 0.05, 1.2, 500.0, 3500.0, seed + static_cast<unsigned>(bt * 10.0)), 0.6 * sc, 40.0);
		if (underwater)   // its song: long low swoops, heard far off under water
			for (double t = std::max(0.0, w.t0) + 2.0; t < end; t += 7.0) {
				const unsigned s = seed + static_cast<unsigned>(t * 3.0);
				const double f0 = 180.0 + 220.0 * audio::unit(s), f1 = 90.0 + 300.0 * audio::unit(s + 1u);
				ss::place(m, ears, t, whaleMiddle(w, natureAt(t), ns) + Vec3{0.0, level - 0.35 * w.length, 0.0}, audio::chirp(f0, f1, 2.2 + 1.2 * audio::unit(s + 2u), 3.0, 0.03, 1.0), 0.25, 2000.0);
			}
	}
	for (std::size_t p = 0; p < pods.size(); ++p) {
		const Dolphins &d = pods[p];
		const double n0 = natureAt(d.t0), end = std::min(T, d.t1);
		for (int i = 0; i < d.count; ++i) {
			std::vector<double> outs, ins;
			leapTimes(d, i, natureAt(end) - n0, outs, ins);
			for (const std::vector<double> *list : {&outs, &ins})
				for (double x : *list) {
					const Vec3 at = dolphinFrames(d, i, x, level, kDolphinLength)(0.0).at;
					ss::place(m, ears, filmAt(n0 + x), {at.x, level, at.z}, audio::noiseBurst(0.35, 0.004, 0.3, 1800.0, 7000.0, 1501u + static_cast<unsigned>(p * 97 + i * 13) + static_cast<unsigned>(x * 10.0)), 0.25, 15.0);
				}
		}
		for (double t = std::max(0.0, d.t0) + 0.5; t < end; t += 1.1 + 1.2 * audio::unit(1601u + static_cast<unsigned>(t * 10.0))) {   // whistles, now one, now another
			const unsigned s = 1607u + static_cast<unsigned>(p * 31) + static_cast<unsigned>(t * 10.0);
			const int i = static_cast<int>(audio::unit(s) * d.count) % std::max(1, d.count);
			const Vec3 at = dolphinFrames(d, i, natureAt(t) - n0, level, kDolphinLength)(0.0).at;
			ss::place(m, ears, t, at, audio::chirp(6000.0 + 3000.0 * audio::unit(s + 1u), 8000.0 + 4000.0 * audio::unit(s + 2u), 0.35 + 0.3 * audio::unit(s + 3u), 9.0, 0.04, 0.6), 0.12, 25.0);
		}
	}
}

}  // namespace giants
}  // namespace cartoon
