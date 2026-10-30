#pragma once
// film_undersea.h -- under the sea, round 14: how the water itself looks and moves, for films with `underwater` (film_underwater.h).
//
//   the surface from below   Snell's window: from under the water the whole sky is squeezed into a circle overhead (48.6 degrees from straight up); beyond it the
//                            surface turns into a mirror of what is under it (total internal reflection). windowText lays that mirror just under the sea's surface
//                            (film_water.h's thin water surface, which stays to light the depths) where the camera's line of sight meets the waves past the critical
//                            angle - each wave's own slope, so the window's edge ripples - and the mirror casts no shadows (pbrt_scene.h ray visibility), so the sun
//                            still comes down through it.
//   half above, half below   a camera at the surface (within a dome port's 12 cm of it) sees the air above and the water below, split where the waves stand on its dome:
//                            the line bobs with the waves going past (waterlineOf). The water then begins at a plane through the lens tilted to that line (a sphere 3 km
//                            across), the camera itself is in the air, the sea's surface keeps clear of the lens, and the lens look draws the line's meniscus and the
//                            drops running down the dome above it (lens_look.h `waterline`, `drops`).
//   depth and light          `underwater ... lamp L`: a diver's lamp on the camera (the reds the depth took come back near it, and the specks in the water glint in its
//                            beam: backscatter); `exposure auto` opens up as the camera goes down (depthExposure); `layer Y`: a thermocline or a halocline, the boundary
//                            between two waters, a sheet that bends the light a little and mirrors at a glancing look.
//   bubbles                  `bubbles stream|breath|net|ring ...`: each bubble rises at the speed its size gives it (small ones zigzag, the big ones are flattened caps
//                            that rock), grows as the pressure falls (Boyle's law), drifts with the current and is gone at the surface; air in water (a dielectric of eta
//                            1/1.333, so it mirrors silver at its edges), casting no shadows.
//   the current              `current SPEED toward A [surge P]` (underwater::Current): one flow for everything in the water - kelp and grass lean with it, the snow
//                            and the bubbles drift in it, the fish hold station against it; a surge swings it back and forth.
//
// Coordinates are pbrt's, as a film file's; meshes are made in story space (x mirrored) for emitMesh.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <regex>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_underwater.h"
#include "film_water.h"

namespace cartoon {
namespace undersea {

constexpr double kCritical = 0.75019;   // (sin of the critical angle, water to air: 1 / 1.333)
constexpr double kDome = 0.12;          // (a dome port's radius: how near the surface a camera is half above it)

struct Stream {   // a stream of bubbles from a vent, a crack, a sunken tank
	Vec3 at;
	double rate = 6.0, size = 1.0, t0 = -1e9, t1 = 1e9;
	int seed = 1;
};
struct Breath {   // a diver breathing out: a burst of bubbles from the regulator every `every` seconds
	Vec3 at;
	double every = 4.0;
	int seed = 1;
};
struct Net {   // a humpback's bubble net: a spiral curtain blown from `depth` under the surface round (x, z), from `at` for 10 s
	double x = 0.0, z = 0.0, radius = 8.0, depth = 12.0, at = 0.0;
	int seed = 1;
};
struct Ring {   // a bubble ring: a vortex of air rising and widening, blown at `t`
	Vec3 at;
	double t = 0.0;
	int seed = 1;
};
struct Layer {   // a thermocline or halocline at height y: `strength` 1 a halocline's clear shimmer
	double y = -10.0, strength = 1.0;
};
struct Sea {
	std::vector<Stream> streams;
	std::vector<Breath> breaths;
	std::vector<Net> nets;
	std::vector<Ring> rings;
	std::vector<Layer> layers;
	bool any() const { return !streams.empty() || !breaths.empty() || !nets.empty() || !rings.empty() || !layers.empty(); }
};

namespace detail {
inline Vec3 story(Vec3 p) { return {-p.x, p.y, p.z}; }
// A shape list that casts no shadows (ray visibility): each mesh's Shape line marked.
inline std::string noShadow(std::string shapes) {
	for (std::size_t at = 0; (at = shapes.find("Shape \"trianglemesh\"", at)) != std::string::npos; at += 20) shapes.insert(at + 20, " \"bool shadow\" false");
	return shapes;
}
}  // namespace detail

// ---- the surface from below ----
// The sea's height at pbrt's (x, z) at t (the wave there; near enough where it was at rest).
inline double surfaceAt(const water::Sea &s, double x, double z, double t) {
	Vec3 p, n;
	double fold = 0.0;
	water::seaAt(s, x, z, t, 0.0, p, n, fold);
	return p.y;
}
// The mirror beyond Snell's window, seen from pbrt's `eye` under the surface: rings round the point over the eye out to far away, each point of the waves (its own slope)
// a mirror where the line from the eye meets it past the critical angle; a centimetre under the water's own surface.
inline std::string windowText(const water::Sea &s, double t, const Vec3 &eye) {
	const double depth = std::max(0.05, s.level - eye.y);
	const double edge = depth * kCritical / std::sqrt(1.0 - kCritical * kCritical);   // (where the window's edge is on calm water)
	const double r0 = 0.55 * edge, r1 = std::max(500.0, 60.0 * depth);
	const int around = 120, rings = 50;
	Mesh m, out;
	std::vector<double> mirror;   // (how far past the critical angle the line of sight meets each point: the mirror where it is positive)
	for (int j = 0; j <= rings; ++j) {
		// (dense across the window's edge, where it ripples, then spreading out)
		const double u = j / double(rings);
		const double r = u < 0.4 ? r0 + (2.2 * edge - r0) * (u / 0.4) : 2.2 * edge * std::pow(r1 / (2.2 * edge), (u - 0.4) / 0.6);
		const double spacing = std::max(0.02, r * 2.0 * kPi / around);
		for (int i = 0; i <= around; ++i) {
			const double a = 2.0 * kPi * i / around;
			const double x = eye.x + r * std::cos(a), z = eye.z + r * std::sin(a);
			Vec3 p, n;
			double fold = 0.0;
			if (r < 2500.0) water::seaAt(s, x, z, t, std::exp(-r / (20.0 + 4.0 * depth)), p, n, fold, spacing);
			else p = {x, s.level, z}, n = {0.0, 1.0, 0.0};
			p.y -= 0.012;
			const Vec3 d = normalize(p - eye);
			const double c = std::clamp(dot(d, n), -1.0, 1.0);
			mirror.push_back(c < 0.0 ? 1.0 : std::sqrt(1.0 - c * c) - kCritical);
			m.verts.push_back(detail::story(p));
			m.normals.push_back(detail::story(n * -1.0));   // (facing down, into the water)
		}
	}
	const int w = around + 1;
	for (int j = 0; j < rings; ++j)
		for (int i = 0; i < around; ++i) {
			const int a = j * w + i;
			for (const std::array<int, 3> &tri : {std::array<int, 3>{a, a + 1, a + w}, std::array<int, 3>{a + 1, a + w + 1, a + w}}) {
				// the triangle cut where the line of sight crosses the critical angle (between its points, linearly), so the window's edge is a smooth curve
				std::vector<Vec3> ps, ns;
				for (int k = 0; k < 3; ++k) {
					const std::size_t q0 = static_cast<std::size_t>(tri[k]), q1 = static_cast<std::size_t>(tri[(k + 1) % 3]);
					const double f0 = mirror[q0], f1 = mirror[q1];
					if (f0 >= 0.0) ps.push_back(m.verts[q0]), ns.push_back(m.normals[q0]);
					if ((f0 >= 0.0) != (f1 >= 0.0)) {
						const double u = f0 / (f0 - f1);
						ps.push_back(lerp(m.verts[q0], m.verts[q1], u)), ns.push_back(normalize(lerp(m.normals[q0], m.normals[q1], u)));
					}
				}
				if (ps.size() < 3) continue;
				const int base = static_cast<int>(out.verts.size());
				out.verts.insert(out.verts.end(), ps.begin(), ps.end());
				out.normals.insert(out.normals.end(), ns.begin(), ns.end());
				for (int k = 1; k + 1 < static_cast<int>(ps.size()); ++k) out.tris.push_back({base, base + k, base + k + 1});
			}
		}
	if (out.tris.empty()) return std::string();
	return "# the mirror beyond Snell's window (film_undersea.h)\n" + attr(metalMaterial({0.96, 0.97, 0.97}, 0.015), detail::noShadow(emitMesh(out, identity())));
}

// Where a camera is with the water: under it, above it, or at it with the line across its dome port at `elevation` (radians: the rays below it are in the water).
struct Waterline {
	enum Where { Under, At, Above } where = Under;
	double elevation = 0.0, surface = 0.0;
};
inline Waterline waterlineOf(const water::Sea &s, double t, const Vec3 &eye) {
	Waterline w;
	w.surface = surfaceAt(s, eye.x, eye.z, t);
	const double dy = w.surface - eye.y;
	w.where = dy > kDome ? Waterline::Under : dy < -kDome ? Waterline::Above : Waterline::At;
	w.elevation = std::asin(std::clamp(dy / kDome, -1.0, 1.0));
	return w;
}
// A camera put within 0.6 m of the surface - or any, in a shot with `waterline` (`atSurface`) - floats on it, as a camera held at the surface is: the frame header with
// its LookAt (eye and target together) moved to nine tenths of the wave at the eye, so the surface stays near its dome and the line across it rocks with the waves. Any other
// header as it was.
inline std::string floated(const std::string &header, const water::Sea &s, double t, bool atSurface = false) {
	static const std::regex look(R"(LookAt\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+))");
	std::smatch m;
	if (!std::regex_search(header, m, look)) return header;
	double v[6];
	for (int k = 0; k < 6; ++k) v[k] = std::atof(m[k + 1].str().c_str());
	if (!atSurface && std::abs(v[1] - s.level) > 0.6) return header;
	const double dy = 0.9 * (surfaceAt(s, v[0], v[2], t) - s.level) + (atSurface ? s.level - v[1] : 0.0);
	char buf[200];
	std::snprintf(buf, sizeof buf, "LookAt %.6f %.6f %.6f %.6f %.6f %.6f", v[0], v[1] + dy, v[2], v[3], v[4] + dy, v[5]);
	return header.substr(0, static_cast<std::size_t>(m.position(0))) + buf + header.substr(static_cast<std::size_t>(m.position(0) + m.length(0)));
}
// The water's boundary for a camera at the waterline: a sphere 3 km across whose top is the plane through the lens holding its view's horizontal and the line's elevation
// (rays below it go into the water at once), as story-space centre for underwater::boundary.
inline Vec3 waterlineCentre(const Vec3 &eye, const Vec3 &target, double elevation, double radius) {
	Vec3 f{target.x - eye.x, 0.0, target.z - eye.z};
	f = length(f) > 1e-9 ? normalize(f) : Vec3{0.0, 0.0, 1.0};
	const Vec3 n = normalize(Vec3{0.0, std::cos(elevation), 0.0} - f * std::sin(elevation));
	const Vec3 c = eye - n * (radius + 0.002);
	return {-c.x, c.y, c.z};
}
// The look line's words for the waterline: its row on the screen (from the view's pitch and a film's vertical field of view `fov`) and the drops on the dome above it.
inline std::string waterlineLook(const Vec3 &eye, const Vec3 &target, double fov, double elevation, double t) {
	const Vec3 d = normalize(target - eye);
	const double pitch = std::asin(std::clamp(d.y, -1.0, 1.0));
	const double row = 0.5 - 0.5 * std::tan(elevation - pitch) / std::tan(0.5 * fov * kPi / 180.0);
	char buf[120];
	std::snprintf(buf, sizeof buf, " waterline %.4f drops 0.6 drops-time %.3f", std::clamp(row, -0.5, 1.5), t);
	return buf;
}

// ---- depth and light ----
// How much a camera `depth` metres down opens up under `exposure auto`: most of what the water took from the light on its way down (the green, as the eye judges) - but
// with a lamp on the camera only a little (deep down, the lamp is what it is exposed for).
inline double depthExposure(const underwater::Water &w, double depth) {
	const double k = underwater::absorption(w).y + 0.5 * underwater::scattering(w).y;
	return std::min(w.lamp > 0.0 ? 2.5 : 40.0, std::exp(0.8 * k * std::clamp(depth, 0.0, 400.0)));
}
// A diver's lamp on the camera (pbrt's eye and target): a warm spot a little to the side of the lens, as a diver holds one (backscatter is less there).
// `blue` (round 15): a blue lamp, under which fluorescent corals glow.
inline std::string lampText(double lamp, const Vec3 &eye, const Vec3 &target, bool blue = false) {
	if (lamp <= 0.0) return std::string();
	const Vec3 f = normalize(target - eye), right = normalize(cross(Vec3{0.0, 1.0, 0.0}, f) + Vec3{1e-9, 0, 0});
	const Vec3 at = eye + right * 0.35 + Vec3{0.0, 0.15, 0.0}, to = target;
	const double I = 12.0 * lamp;
	char buf[320];
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"spot\" \"point3 from\" [ %.4f %.4f %.4f ] \"point3 to\" [ %.4f %.4f %.4f ] \"float coneangle\" [ 24 ] \"float conedelta\" [ 9 ] \"rgb I\" [ %.3f %.3f %.3f ]\nAttributeEnd\n",
				  at.x, at.y, at.z, to.x, to.y, to.z, blue ? 0.25 * I : I, blue ? 0.45 * I : 0.93 * I, blue ? 1.1 * I : 0.82 * I);
	return std::string("# a diver's lamp (film_undersea.h)\n") + buf;
}
// The specks in the water close to the lens that a lamp catches (story-space eye): a fixed fine pattern in 2 m tiles, drifting with the water.
inline std::string backscatterText(double t, const Vec3 &eyeStory, const Vec3 &drift) {
	Mesh m;
	const Mesh fleck = uvSphere(1.0, 4, 3);
	const double tile = 2.0;
	const Vec3 at = eyeStory - Vec3{-drift.x, drift.y, drift.z};   // (the pattern moves with the water: look it up where the water came from)
	const int tx = static_cast<int>(std::floor(at.x / tile)), ty = static_cast<int>(std::floor(at.y / tile)), tz = static_cast<int>(std::floor(at.z / tile));
	for (int a = -1; a <= 1; ++a)
		for (int b = -1; b <= 1; ++b)
			for (int c = -1; c <= 1; ++c)
				for (int k = 0; k < 60; ++k) {
					const int cell = ((tx + a) * 73856093) ^ ((ty + b) * 19349663) ^ ((tz + c) * 83492791);
					Vec3 p{(tx + a + hash01(k, cell + 1)) * tile, (ty + b + hash01(k, cell + 2)) * tile, (tz + c + hash01(k, cell + 3)) * tile};
					p = p + Vec3{-drift.x, drift.y, drift.z} + Vec3{0.05 * std::sin(0.7 * t + k), 0.04 * std::sin(0.5 * t + 2 * k), 0.05 * std::cos(0.6 * t + k)};
					const double d = length(p - eyeStory);
					if (d > 2.6 || d < 0.45) continue;
					const double r = 0.0006 + 0.001 * hash01(k, cell + 4);
					const int base0 = static_cast<int>(m.verts.size());
					for (std::size_t i = 0; i < fleck.verts.size(); ++i) m.verts.push_back(p + fleck.verts[i] * r), m.normals.push_back(fleck.normals[i]);
					for (const auto &tr : fleck.tris) m.tris.push_back({tr[0] + base0, tr[1] + base0, tr[2] + base0});
				}
	return m.tris.empty() ? std::string() : "# backscatter (film_undersea.h)\n" + attr(diffuseMaterial({0.85, 0.85, 0.82}), emitMesh(m, identity()));
}
// A thermocline or halocline round pbrt's `eye`: a slowly heaving sheet that bends the light a little (eta 1 + 0.012 strength) - clear seen straight through, a
// mirror at a glancing look - and casts no shadows.
inline std::string layerText(const Layer &l, double t, const Vec3 &eye) {
	Mesh m;
	const int n = 90;
	const double half = 70.0, step = 2.0 * half / n;
	const auto h = [&](double x, double z) {
		return l.y + 0.35 * std::sin(0.9 * x + 0.3 * z + 0.25 * t) + 0.2 * std::sin(-0.4 * x + 1.3 * z - 0.18 * t) + 0.06 * std::sin(3.1 * x - 2.2 * z + 0.6 * t);
	};
	const double x0 = std::floor((eye.x - half) / step) * step, z0 = std::floor((eye.z - half) / step) * step;
	for (int j = 0; j <= n; ++j)
		for (int i = 0; i <= n; ++i) {
			const double x = x0 + i * step, z = z0 + j * step, e = 0.05;
			m.verts.push_back({-x, h(x, z), z});
			m.normals.push_back(normalize(Vec3{(h(x + e, z) - h(x - e, z)) / (2.0 * e), 1.0, -(h(x, z + e) - h(x, z - e)) / (2.0 * e)}));
		}
	for (int j = 0; j < n; ++j)
		for (int i = 0; i < n; ++i) {
			const int a = j * (n + 1) + i;
			m.tris.push_back({a, a + 1, a + n + 1});
			m.tris.push_back({a + 1, a + n + 2, a + n + 1});
		}
	char mat[120];
	std::snprintf(mat, sizeof mat, "Material \"dielectric\" \"float eta\" [ %.4f ]", 1.0 + 0.012 * std::clamp(l.strength, 0.1, 5.0));
	return "# a halocline (film_undersea.h)\n" + attr(mat, detail::noShadow(emitMesh(m, identity())));
}

// ---- bubbles ----
struct Bubble {
	Vec3 p;            // pbrt's
	double r = 0.002;  // radius (metres)
	double phase = 0.0;
};
// How fast a bubble of radius r rises: the small ones slowly, a few millimetres at about 0.25 m/s, the big caps by their size (Davies and Taylor).
inline double riseSpeed(double r) {
	if (r < 0.0007) return 0.08 + 220.0 * r;
	if (r < 0.006) return 0.23 + 3.0 * (r - 0.0007);
	return 0.64 * std::sqrt(9.81 * 2.0 * r);
}
// A bubble let go at pbrt's p0 with radius r0 `age` seconds ago, in sea `s` and current `c` (at time t): where it is, how big, or false once it has reached the surface.
inline bool bubbleAt(Vec3 p0, double r0, double age, double t, int seed, double level, const underwater::Current &c, Bubble &out) {
	if (age < 0.0) return false;
	const double v = riseSpeed(r0);
	const double y = p0.y + v * age;
	if (y > level - 0.01) return false;
	const double d0 = std::max(0.0, level - p0.y), d = std::max(0.0, level - y);
	out.r = r0 * std::cbrt((10.3 + d0) / (10.3 + d));   // (Boyle: the pressure falls a bar every 10.3 m)
	const double zig = out.r < 0.0007 ? 0.2 * out.r : out.r < 0.006 ? 2.5 * out.r : 0.6 * out.r;   // (a few millimetres bubble zigzags; a cap rocks)
	const double w = 2.0 * kPi * (out.r < 0.006 ? 4.0 : 2.0), ph = 6.2832 * hash01(seed, 5);
	const Vec3 drift = c.drift(t) - c.drift(t - age);
	out.p = {p0.x + zig * std::sin(w * age + ph) + drift.x, y, p0.z + zig * std::cos(0.8 * w * age + ph) + drift.z};
	out.phase = w * age + ph;
	return true;
}
// A big bubble's shape: a few millimetres an oblate ball that wobbles, bigger a spherical cap (flat underneath, its rim rocking).
inline Mesh bubbleMesh(double r, double phase) {
	if (r < 0.006) {
		const double sq = 1.25 + 0.15 * std::sin(phase);
		Mesh m = uvSphere(1.0, 8, 6);
		for (Vec3 &v : m.verts) v = Vec3{v.x * r * std::sqrt(sq), v.y * r / sq, v.z * r * std::sqrt(sq)};
		for (Vec3 &n : m.normals) n = normalize(Vec3{n.x / std::sqrt(sq), n.y * sq, n.z / std::sqrt(sq)});
		return m;
	}
	const double R = 1.55 * r, rim = 52.0 * kPi / 180.0;
	std::vector<std::pair<double, double>> profile;
	for (int k = 0; k <= 4; ++k) {   // (the base: a shallow dimple up into the cap)
		const double u = k / 4.0;
		profile.push_back({R * std::sin(rim) * u, R * std::cos(rim) + 0.12 * R * (1.0 - u * u)});
	}
	for (int k = 1; k <= 10; ++k) {
		const double a = rim * (1.0 - k / 10.0);
		profile.push_back({R * std::sin(a), R * std::cos(a)});
	}
	Mesh m = lathe(profile, 12);
	for (std::size_t i = 0; i < m.verts.size(); ++i) {   // (the rim's ripple, and the cap set down so its middle is the bubble's)
		Vec3 &v = m.verts[i];
		const double ang = std::atan2(v.z, v.x), edge = smoothstep(0.6 * R, R, std::hypot(v.x, v.z));
		v.y += edge * 0.12 * R * std::sin(3.0 * ang + phase) - 0.85 * R;
	}
	return m;
}
// The bubbles as scene text: the small as spheres, the big as meshes, all air in water casting no shadows.
inline std::string bubblesText(const std::vector<Bubble> &bubbles) {
	if (bubbles.empty()) return std::string();
	std::string out = "# bubbles (film_undersea.h)\nAttributeBegin\n  Material \"dielectric\" \"float eta\" [ 0.7502 ]\n";
	Mesh big;
	char buf[200];
	for (const Bubble &b : bubbles) {
		if (b.r < 0.0025) {
			std::snprintf(buf, sizeof buf, "  AttributeBegin\n    Translate %.4f %.4f %.4f\n    Shape \"sphere\" \"float radius\" [ %.5f ] \"bool shadow\" false\n  AttributeEnd\n", b.p.x, b.p.y, b.p.z, b.r);
			out += buf;
			continue;
		}
		const Mesh one = bubbleMesh(b.r, b.phase);
		const int base = static_cast<int>(big.verts.size());
		for (std::size_t i = 0; i < one.verts.size(); ++i) big.verts.push_back(one.verts[i] + detail::story(b.p)), big.normals.push_back(one.normals[i]);
		for (const auto &tr : one.tris) big.tris.push_back({tr[0] + base, tr[1] + base, tr[2] + base});
	}
	if (!big.tris.empty()) out += detail::noShadow(emitMesh(big, identity()));
	return out + "AttributeEnd\n";
}
// A stream's bubbles at t: one let go every 1/rate seconds (a little irregularly), of a millimetre or two times `size`.
inline void streamBubbles(const Stream &s, double t, double level, const underwater::Current &c, std::vector<Bubble> &out) {
	const double rate = std::clamp(s.rate, 0.05, 200.0), life = std::max(0.0, level - s.at.y) / riseSpeed(0.0015 * s.size) + 1.0;
	const long k0 = static_cast<long>(std::floor((std::max(t - life, s.t0)) * rate)), k1 = static_cast<long>(std::floor(std::min(t, s.t1) * rate));
	for (long k = std::max(k0, k1 - 4000); k <= k1; ++k) {
		const int key = static_cast<int>(k % 1000003) + s.seed * 7919;
		const double born = (k + 0.8 * hash01(key, 1)) / rate;
		if (born < s.t0 || born > s.t1) continue;
		const double r0 = s.size * (0.0015 + 0.0035 * std::pow(hash01(key, 2), 2.0));
		Bubble b;
		if (bubbleAt(s.at + Vec3{0.02 * (hash01(key, 3) - 0.5), 0.0, 0.02 * (hash01(key, 4) - 0.5)}, r0, t - born, t, key, level, c, b)) out.push_back(b);
	}
}
// A diver's breath at t: every `every` seconds a burst over 1.2 s - a few big caps and a cloud of small bubbles - from the regulator.
inline void breathBubbles(const Breath &br, double t, double level, const underwater::Current &c, std::vector<Bubble> &out) {
	const double every = std::max(1.5, br.every), life = std::max(0.0, level - br.at.y) / 0.22 + 1.0;
	for (long n = static_cast<long>(std::floor((t - life) / every)); n <= static_cast<long>(std::floor(t / every)); ++n) {
		const double start = n * every + 0.4 * every;   // (breathing in first: the burst comes after)
		for (int k = 0; k < 40; ++k) {
			const int key = static_cast<int>(n * 131 + k) + br.seed * 104729;
			const double born = start + 1.2 * hash01(key, 1);
			const double r0 = k < 4 ? 0.01 + 0.012 * hash01(key, 2) : 0.0018 + 0.004 * hash01(key, 2);
			Bubble b;
			if (bubbleAt(br.at + Vec3{0.04 * (hash01(key, 3) - 0.5), 0.0, 0.04 * (hash01(key, 4) - 0.5)}, r0, t - born, t, key, level, c, b)) out.push_back(b);
		}
	}
}
// A bubble net at t: the whale swims a rising spiral round (x, z) for 10 s from `at`, a curtain of small bubbles let out behind it all the way.
inline void netBubbles(const Net &nt, double t, double level, const underwater::Current &c, std::vector<Bubble> &out) {
	const double y0 = level - nt.depth;
	for (int k = 0; k < 220; ++k) {   // (points along the spiral, each letting out a burst over 1.5 s)
		const double u = k / 220.0, at = nt.at + 10.0 * u;
		const double a = 2.0 * kPi * 1.25 * u, r = nt.radius * (1.0 - 0.35 * u);
		const Vec3 p0{nt.x + r * std::cos(a), y0 + 2.0 * u, nt.z + r * std::sin(a)};
		for (int q = 0; q < 16; ++q) {
			const int key = k * 37 + q + nt.seed * 7027;
			const double born = at + 1.5 * hash01(key, 1);
			Bubble b;
			if (bubbleAt(p0 + Vec3{0.3 * (hash01(key, 3) - 0.5), 0.0, 0.3 * (hash01(key, 4) - 0.5)}, 0.0012 + 0.0025 * hash01(key, 2), t - born, t, key, level, c, b)) out.push_back(b);
		}
	}
}
// A bubble ring at t: a torus of air rising at 0.4 m/s, widening and thinning, wobbling; gone at the surface or after 12 s.
inline std::string ringText(const Ring &r, double t, double level, const underwater::Current &c) {
	const double age = t - r.t;
	if (age < 0.0 || age > 12.0) return std::string();
	const Vec3 drift = c.drift(t) - c.drift(r.t);
	const Vec3 at = r.at + Vec3{drift.x, 0.4 * age, drift.z};
	if (at.y > level - 0.1) return std::string();
	const double R = 0.08 + 0.07 * age, tube = std::max(0.004, 0.018 - 0.0011 * age);
	Mesh m = torus(R, tube, 48, 10);
	for (Vec3 &v : m.verts) v.y += 0.02 * R * std::sin(3.0 * std::atan2(v.z, v.x) + 2.0 * age + r.seed);
	return "# a bubble ring (film_undersea.h)\n" + attr("Material \"dielectric\" \"float eta\" [ 0.7502 ]", detail::noShadow(emitMesh(m, translate(detail::story(at)))));
}
// Bubbles left behind along a path (a penguin's, a seal's): air shed from its coat every 1/rate seconds while it moves, rising from where it was.
inline void trailBubbles(const std::function<Vec3(double)> &path, double t, double rate, double size, int seed, double level, const underwater::Current &c, std::vector<Bubble> &out) {
	for (int k = 0; k < static_cast<int>(rate * 4.0); ++k) {   // (the last 4 s)
		const double age = (k + hash01(k, seed)) / rate;
		const Vec3 p0 = path(t - age);
		Bubble b;
		if (bubbleAt(p0, size * (0.0015 + 0.003 * hash01(k, seed + 1)), age, t, seed * 31 + k, level, c, b)) out.push_back(b);
	}
}
// All of a sea's bubbles and layers at t (pbrt's eye for the layers).
inline std::string text(const Sea &sea, double t, double level, const underwater::Current &c, const Vec3 &eye) {
	std::vector<Bubble> bubbles;
	for (const Stream &s : sea.streams) streamBubbles(s, t, level, c, bubbles);
	for (const Breath &b : sea.breaths) breathBubbles(b, t, level, c, bubbles);
	for (const Net &n : sea.nets) netBubbles(n, t, level, c, bubbles);
	std::string out = bubblesText(bubbles);
	for (const Ring &r : sea.rings) out += ringText(r, t, level, c);
	for (const Layer &l : sea.layers) out += layerText(l, t, eye);
	return out;
}
// The film's bubbles and layers with their heights made pbrt's (on a terrain heights are above the ground there).
inline Sea lifted(Sea sea, const std::function<double(double, double)> &groundAt) {
	for (Stream &s : sea.streams) s.at.y += groundAt(s.at.x, s.at.z);
	for (Breath &b : sea.breaths) b.at.y += groundAt(b.at.x, b.at.z);
	for (Ring &r : sea.rings) r.at.y += groundAt(r.at.x, r.at.z);
	return sea;
}

}  // namespace undersea
}  // namespace cartoon
