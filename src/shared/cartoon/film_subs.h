#pragma once
// film_subs.h -- under the sea, round 18: submarines, a glass-bubble sub and a remote robot.
//
//   sub round|bubble|robot NAME at X Y Z [to X Y Z]... [speed S] [from T] [toward A] [dive T]... [rise T]... [lights off] [colour R G B] [seed K]
//       round   a little yellow submarine 3.2 m long: a glass dome at its nose with its cabin inside (a seat, a panel of lit buttons, a warm lamp), a conning tower and
//               periscope, a propeller turning as fast as she goes, her wash of bubbles behind
//       bubble  a glass sphere two metres across on a frame of thrusters: the cabin all round, seen from everywhere
//       robot   a remote robot a metre long: a frame, a yellow float, thrusters, a camera eye with a glowing lens, a claw, and its tether up to the surface
//   Each goes from point to point at S m/s (from film time T; it turns to its way), with two searchlights ahead that light the water (on unless `lights off`); at each `dive T`
//   and `rise T` it blows its ballast - a burst of big bubbles from its vents. Heights above the seabed, pbrt's x z (film_parse_sea.h). `aboard WHO NAME` seats a
//   character in its cabin (dry: no bubbles as it talks); a shot can follow it by NAME.

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "film_deepsea.h"
#include "film_flow.h"
#include "film_sealife.h"
#include "film_undersea.h"
#include "film_underwater.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace subs {

struct Sub {
	std::string kind = "round", name;
	std::vector<Vec3> path;   // pbrt's: heights above the seabed until lifted
	double speed = 1.2, from = 0.0, toward = 0.0;
	bool lights = true;
	Vec3 colour{0.95, 0.74, 0.12};
	std::vector<double> dives, rises;
	int seed = 1;
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"round", "bubble", "robot"};
	return k;
}
inline double lengthOf(const std::string &kind) { return kind == "round" ? 3.2 : kind == "bubble" ? 2.2 : 1.1; }

namespace detail {
using deepsea::detail::add;
using sealife::detail::facingOf;
using sealife::detail::fin;
using sealife::detail::story;
inline Mesh ellipsoid(Vec3 c, Vec3 r, int nu = 20, int nv = 12) {
	Mesh m;
	add(m, uvSphere(1.0, nu, nv), mul(translate(c), scale(r.x, r.y, r.z)));
	return m;
}
inline Mesh boxAt(Vec3 c, Vec3 size) {
	Mesh m;
	add(m, box(size.x, size.y, size.z), translate(c));
	return m;
}
// A lathed shape along +z (profile: radius, z).
inline Mesh along(const std::vector<std::pair<double, double>> &profile, int sides = 24) {
	Mesh l = lathe(profile, sides), m;
	for (std::size_t i = 0; i < l.verts.size(); ++i) m.verts.push_back({l.verts[i].x, l.verts[i].z, l.verts[i].y}), m.normals.push_back({l.normals[i].x, l.normals[i].z, l.normals[i].y});
	for (const auto &t : l.tris) m.tris.push_back({t[0], t[2], t[1]});   // (y and z swapped: the winding turns over)
	return m;
}
}  // namespace detail

// Where a sub is at nature's time t (pbrt's), and which way it is going (`fwd`, level): along its points at its speed from `from`, easing off at its last.
inline Vec3 subAt(const Sub &s, double t, Vec3 *fwd = nullptr) {
	if (s.path.empty()) return {};
	std::vector<double> at{0.0};
	for (std::size_t i = 1; i < s.path.size(); ++i) at.push_back(at.back() + length(s.path[i] - s.path[i - 1]));
	const double total = at.back();
	const double raw = std::max(0.0, (t - s.from) * s.speed);
	const double d = total > 0.0 ? std::min(total, raw < total - 1.5 ? raw : total - 1.5 + 1.5 * (1.0 - std::exp(-(raw - total + 1.5) / 1.5))) : 0.0;   // (it slows to a stop)
	const auto pointAt = [&](double u) {
		u = std::clamp(u, 0.0, total);
		std::size_t i = 1;
		while (i + 1 < s.path.size() && at[i] < u) ++i;
		if (s.path.size() == 1) return s.path[0];
		const double seg = std::max(1e-9, at[i] - at[i - 1]);
		return lerp(s.path[i - 1], s.path[i], std::clamp((u - at[i - 1]) / seg, 0.0, 1.0));
	};
	Vec3 p = pointAt(d);
	p.y += 0.05 * std::sin(0.8 * t + s.seed);   // (it rides a little)
	if (fwd) {   // (its way; one that stays where it is faces `toward`)
		Vec3 f = pointAt(d + 0.8) - pointAt(d - 0.8);
		f.y = 0.0;
		const double a = s.toward * kPi / 180.0;
		*fwd = length(f) > 1e-4 ? normalize(f) : Vec3{std::sin(a), 0.0, std::cos(a)};
	}
	return p;
}
// How fast it goes at t (m/s), for its propeller and its wash.
inline double paceAt(const Sub &s, double t) { return length(subAt(s, t + 0.25) - subAt(s, t - 0.25)) / 0.5; }
// Its frame at t (story space): +z its bow, +y up.
inline Mat4 frameOf(const Sub &s, double t) {
	Vec3 f;
	const Vec3 p = subAt(s, t, &f);
	return mul(translate(detail::story(p)), detail::facingOf(detail::story(f)), rotate({0, 0, 1}, 2.0 * std::sin(0.6 * t + s.seed)));
}
// Where a seat in its cabin is (story space), facing its bow.
inline Vec3 seatOf(const Sub &s, double t, int seat) {
	const double side = seat % 2 == 0 ? 0.0 : (seat % 4 == 1 ? 0.35 : -0.35);
	const Vec3 local = s.kind == "round" ? Vec3{side, -0.5, 0.1} : s.kind == "bubble" ? Vec3{seat == 0 ? 0.0 : (seat == 1 ? 0.45 : -0.45), -1.05, seat == 0 ? 0.15 : -0.1} : Vec3{0.0, 0.0, 0.0};
	return transformPoint(frameOf(s, t), local);
}

// A sub at nature's time t as scene text: its hull, cabin, propeller or thrusters, lamps and searchlights; `level` the surface (the robot's tether goes up to it).
inline std::string subText(const Sub &s, double t, double level) {
	using namespace detail;
	if (s.path.empty()) return std::string();
	const Mat4 place = frameOf(s, t);
	const double pace = paceAt(s, t), spin = std::fmod(t * (1.0 + 6.0 * pace), 2.0 * kPi);
	const std::string metal = metalMaterial({0.62, 0.62, 0.6}, 0.25), dark = coated({0.08, 0.08, 0.09}, 0.4), glass = "Material \"thindielectric\" \"float eta\" [ 1.5 ]";
	Mesh hull, trim, glassM, props, cabin, panel, lamps;
	std::vector<std::pair<Vec3, Vec3>> beams;   // (searchlights: from, ahead, story-local)
	Vec3 lightAt{0.0, 0.2, 0.0};
	const auto propeller = [&](Vec3 hub, double r, double spinBy) {
		add(props, ellipsoid(hub, {0.06 * r / 0.3, 0.06 * r / 0.3, 0.1 * r / 0.3}, 10, 6));
		for (int b = 0; b < 3; ++b) {
			const double a = spinBy + 2.0 * kPi * b / 3.0;
			const Vec3 tip = hub + Vec3{r * std::cos(a), r * std::sin(a), 0.0}, lead = hub + Vec3{0.35 * r * std::cos(a + 0.6), 0.35 * r * std::sin(a + 0.6), 0.04};
			fin(props, hub, tip, lead);
		}
	};
	if (s.kind == "round") {
		add(hull, along({{0.0, -1.6}, {0.35, -1.5}, {0.62, -1.15}, {0.82, -0.5}, {0.86, 0.2}, {0.82, 0.7}, {0.76, 0.92}}, 32));   // (open at the nose: the dome is its window)
		add(glassM, along({{0.76, 0.92}, {0.66, 1.25}, {0.4, 1.52}, {0.0, 1.62}}, 32));
		{
			std::vector<Vec3> ring;
			for (int q = 0; q <= 32; ++q) ring.push_back({0.78 * std::cos(2.0 * kPi * q / 32), 0.78 * std::sin(2.0 * kPi * q / 32), 0.93});
			add(trim, sdf::tubeMesh(ring, 0.035, 1.0, 8));
		}
		add(hull, ellipsoid({0.0, 0.82, -0.25}, {0.28, 0.3, 0.55}));   // (its conning tower and periscope)
		add(trim, sdf::tubeMesh({{0.0, 1.0, -0.1}, {0.0, 1.45, -0.1}, {0.0, 1.47, 0.08}}, 0.03, 1.0, 8));
		for (const double side : {1.0, -1.0}) {
			add(trim, sdf::tubeMesh({{side * 0.4, -0.9, -0.9}, {side * 0.45, -0.95, 0.6}}, 0.05, 0.8, 8));   // (skids)
			fin(hull, {side * 0.5, 0.0, -1.2}, {side * 1.05, 0.0, -1.45}, {side * 0.3, 0.0, -1.5});   // (hydroplanes)
			add(lamps, ellipsoid({side * 0.62, -0.32, 0.88}, {0.09, 0.09, 0.05}, 12, 8));
			beams.push_back({{side * 0.62, -0.32, 0.95}, {side * 0.1, -0.25, 1.0}});
		}
		fin(hull, {0.0, 0.3, -1.3}, {0.0, 0.75, -1.65}, {0.0, 0.2, -1.55});   // (its rudder)
		propeller({0.0, 0.0, -1.66}, 0.38, spin);
		// the cabin, seen through the dome: a seat, a panel of lit buttons, a warm lamp
		add(cabin, boxAt({0.0, -0.62, 0.0}, {1.2, 0.08, 1.5}));
		add(cabin, boxAt({0.0, -0.45, -0.25}, {0.6, 0.25, 0.45}));
		add(cabin, boxAt({0.0, -0.1, -0.5}, {0.6, 0.6, 0.12}));
		add(panel, boxAt({0.0, -0.3, 0.62}, {0.9, 0.3, 0.1}));
		lightAt = {0.0, 0.35, 0.1};
	} else if (s.kind == "bubble") {
		add(glassM, ellipsoid({0.0, 0.0, 0.0}, {1.3, 1.3, 1.3}, 32, 20));
		std::vector<Vec3> ring;
		for (int q = 0; q <= 40; ++q) ring.push_back({1.29 * std::cos(2.0 * kPi * q / 40), -0.25, 1.29 * std::sin(2.0 * kPi * q / 40)});
		add(trim, sdf::tubeMesh(ring, 0.05, 1.0, 8));
		add(hull, boxAt({0.0, -1.4, 0.0}, {1.9, 0.25, 2.0}));
		for (const double sx : {1.0, -1.0})
			for (const double sz : {1.0, -1.0}) {
				add(hull, along({{0.0, -0.3}, {0.14, -0.25}, {0.14, 0.25}, {0.0, 0.3}}, 14), translate(sx * 1.0, -1.05, sz * 0.9));
				propeller({sx * 1.0, -1.05, sz * 0.9 - 0.32}, 0.11, spin * (sx * sz));
				add(trim, sdf::tubeMesh({{sx * 0.55, -1.1, sz * 0.6}, {sx * 0.72, -0.75, sz * 0.72}}, 0.04, 1.0, 6));
			}
		for (const double side : {1.0, -1.0}) {
			add(lamps, ellipsoid({side * 0.65, -1.05, 1.0}, {0.09, 0.09, 0.05}, 12, 8));
			beams.push_back({{side * 0.65, -1.05, 1.05}, {side * 0.08, -0.2, 1.0}});
		}
		add(cabin, boxAt({0.0, -1.1, 0.0}, {1.9, 0.08, 1.9}));
		add(cabin, boxAt({0.0, -0.95, -0.25}, {1.1, 0.25, 0.4}));
		add(panel, boxAt({0.0, -0.8, 0.75}, {0.7, 0.2, 0.08}));
		lightAt = {0.0, 0.6, 0.0};
	} else {   // the robot
		for (const double sx : {1.0, -1.0})
			for (const double sz : {1.0, -1.0}) {
				add(trim, sdf::tubeMesh({{sx * 0.5, -0.35, sz * 0.5}, {sx * 0.5, 0.25, sz * 0.5}}, 0.025, 1.0, 6));
				add(trim, sdf::tubeMesh({{sx * 0.5, -0.35, sz * 0.5}, {-sx * 0.5, -0.35, sz * 0.5}}, 0.025, 1.0, 6));
				add(trim, sdf::tubeMesh({{sx * 0.5, -0.35, sz * 0.5}, {sx * 0.5, -0.35, -sz * 0.5}}, 0.025, 1.0, 6));
			}
		add(hull, boxAt({0.0, 0.38, 0.0}, {1.1, 0.28, 1.15}));   // (its float, yellow)
		for (const double sx : {1.0, -1.0}) {
			add(trim, along({{0.0, -0.18}, {0.1, -0.15}, {0.1, 0.15}, {0.0, 0.18}}, 12), translate(sx * 0.62, 0.0, -0.4));
			propeller({sx * 0.62, 0.0, -0.6}, 0.09, spin * sx);
			add(lamps, ellipsoid({sx * 0.38, 0.12, 0.58}, {0.06, 0.06, 0.04}, 10, 8));
			beams.push_back({{sx * 0.38, 0.12, 0.62}, {sx * 0.05, -0.2, 1.0}});
		}
		add(panel, ellipsoid({0.0, -0.05, 0.52}, {0.13, 0.13, 0.08}, 14, 10));   // (its camera's lens, glowing)
		add(trim, ellipsoid({0.0, -0.05, 0.46}, {0.18, 0.18, 0.12}, 14, 10));
		const double grab = 0.5 + 0.5 * std::sin(0.7 * t + s.seed);   // (its claw, reaching and closing)
		const Vec3 wrist{0.25, -0.45 + 0.08 * grab, 0.7 + 0.15 * grab};
		add(trim, sdf::tubeMesh({{0.25, -0.3, 0.45}, {0.25, -0.42, 0.58}, wrist}, 0.03, 1.0, 6));
		for (const double side : {1.0, -1.0}) add(trim, sdf::tubeMesh({wrist, wrist + Vec3{side * (0.04 + 0.05 * grab), -0.05, 0.1}}, 0.015, 0.5, 5));
	}
	std::string out = "# a sub (" + s.kind + ", film_subs.h): " + s.name + "\n";
	out += attr(coated(s.colour, 0.2), emitMesh(hull, place)) + attr(metal, emitMesh(trim, place)) + attr(metal, emitMesh(props, place)) + attr(glass, emitMesh(glassM, place));
	if (!cabin.tris.empty()) out += attr(coated({0.3, 0.2, 0.14}, 0.5), emitMesh(cabin, place));
	const Vec3 panelGlow = s.kind == "robot" ? Vec3{0.3, 1.6, 2.2} : Vec3{0.6, 2.2, 1.0};
	if (!panel.tris.empty()) out += flow::glowAttr(emitMesh(panel, place), panelGlow);
	const Vec3 lampGlow = s.lights ? Vec3{14.0, 13.0, 10.0} : Vec3{0.2, 0.2, 0.2};
	out += flow::glowAttr(emitMesh(lamps, place), lampGlow);
	char buf[360];
	if (s.kind != "robot") {   // (the cabin's warm lamp)
		const Vec3 l = story(transformPoint(place, lightAt));
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ 0.9 0.65 0.35 ]\nAttributeEnd\n", l.x, l.y, l.z);
		out += buf;
	}
	if (s.lights)
		for (const auto &[from, ahead] : beams) {   // the searchlights: the water itself shows their beams
			const Vec3 a = story(transformPoint(place, from)), b = story(transformPoint(place, from + normalize(ahead) * 6.0));
			std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"spot\" \"point3 from\" [ %.4f %.4f %.4f ] \"point3 to\" [ %.4f %.4f %.4f ] \"float coneangle\" [ 18 ] \"float conedelta\" [ 6 ] \"rgb I\" [ 38 36 30 ]\nAttributeEnd\n",
						  a.x, a.y, a.z, b.x, b.y, b.z);
			out += buf;
		}
	if (s.kind == "robot") {   // its tether, slack, up to the surface
		const Vec3 top = transformPoint(place, {0.0, 0.52, -0.3});
		std::vector<Vec3> line;
		for (int q = 0; q <= 16; ++q) {
			const double u = q / 16.0;
			line.push_back({top.x + 2.0 * std::sin(kPi * u), lerp(top.y, level + 0.5, u), top.z + 1.5 * std::sin(kPi * u)});
		}
		out += attr(coated({0.9, 0.55, 0.1}, 0.4), emitMesh(sdf::tubeMesh(line, 0.012, 1.0, 5), identity()));
	}
	return out;
}

// What it breathes out (pbrt's bubbles): the wash behind its propeller as it goes, and a burst from its vents at each `dive` and `rise`.
inline void bubbles(const Sub &s, double t, double level, const underwater::Current &c, std::vector<undersea::Bubble> &out) {
	for (int k = 0; k < 60; ++k) {   // (born every 0.05 s over the last 3 s, from where its propeller was)
		const double born = std::floor(t * 20.0) / 20.0 - 0.05 * k;
		if (born < s.from) break;
		const double pace = paceAt(s, born);
		if (pace < 0.15) continue;
		const Vec3 prop = transformPoint(frameOf(s, born), {0.0, s.kind == "robot" ? 0.0 : -0.1, -0.55 * lengthOf(s.kind)});
		for (int q = 0; q < 3; ++q) {
			const int key = static_cast<int>(born * 20.0) * 5 + q + s.seed * 977;
			undersea::Bubble b;
			const Vec3 jitter{0.25 * (hash01(key, 1) - 0.5), 0.25 * (hash01(key, 2) - 0.5), 0.25 * (hash01(key, 3) - 0.5)};
			if (undersea::bubbleAt(Vec3{-prop.x, prop.y, prop.z} + jitter, (0.0015 + 0.004 * hash01(key, 4)) * std::min(1.5, pace), t - born, t, key, level, c, b)) out.push_back(b);
		}
	}
	std::vector<double> bursts = s.dives;
	bursts.insert(bursts.end(), s.rises.begin(), s.rises.end());
	for (const double when : bursts) {
		if (t < when || t > when + 14.0) continue;
		for (int q = 0; q < 140; ++q) {
			const int key = static_cast<int>(when * 31.0) * 1000 + q + s.seed * 7;
			const double side = q % 2 == 0 ? 1.0 : -1.0, delay = 1.2 * hash01(key, 5);
			const Vec3 vent = transformPoint(frameOf(s, when + delay), {side * 0.5 * lengthOf(s.kind) / 3.2, 0.5, -0.2 + 0.4 * hash01(key, 6)});
			undersea::Bubble b;
			if (undersea::bubbleAt(Vec3{-vent.x, vent.y, vent.z}, 0.004 + 0.012 * hash01(key, 7), t - when - delay, t, key, level, c, b)) out.push_back(b);
		}
	}
}

// The film's subs with their heights made pbrt's (the seabed's under each point, plus its height above it).
inline std::vector<Sub> lifted(std::vector<Sub> subs, const std::function<double(double, double)> &groundAt) {
	for (Sub &s : subs)
		for (Vec3 &p : s.path) p.y += groundAt(p.x, p.z);
	return subs;
}

}  // namespace subs
}  // namespace cartoon
