#pragma once
// film_screens.h -- holograms and screens (film_file.h `hologram`, `screen`; round 11):
//
//   hologram   a picture made of light standing over a projector: a globe (its land bright, its seas faint), a craft (any kind of film_voyage.h's), the orbits of a
//              system (moons going round) or a map (a hilly land in contour lines) - cut into thin glowing slices that climb slowly (the scanlines; between them it is
//              see-through), turning, flickering now and then and slipping sideways for a moment (a glitch);
//   screen     a flat screen in a frame showing colour bars, static, a picture file, a radar (its beam sweeping round, the film's own craft as blips that glow as the
//              beam passes and fade) or a trace (a heartbeat line running across).
//
// What glows in either is no light to aim at ("bool sampled" false: it costs a frame almost nothing); a hologram's projector lens is one small light that tints what is near.
// Coordinates are pbrt's. Times are nature's.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_clouds.h"
#include "film_sky.h"
#include "film_voyage.h"

namespace cartoon {
namespace screens {

struct Hologram {
	std::string name, kind = "globe";   // globe, ship, orbits or map
	Vec3 at;                            // the projector's foot
	double size = 1.0;                  // how big the picture is (a globe's diameter)
	Vec3 colour{0.3, 0.85, 1.0};
	double spin = 20.0;                 // degrees a second
	double flicker = 1.0;               // 0 steady .. 1 (and more: shakier)
	std::string ship = "shuttle";       // (ship) which craft
	int seed = 1;
};
struct Screen {
	std::string name, show = "bars";    // bars, static, picture, radar or trace
	std::string file;                   // (picture) the picture, made absolute when the film loads
	Vec3 at;                            // its middle
	double width = 1.6, height = 0.9, facing = 0.0, tilt = 0.0;   // (facing: which way it faces, as an interior does; tilt: leaning back, degrees)
	double bright = 1.0;
	Vec3 centre;                        // (radar) the middle of what it watches
	bool hasCentre = false;
	double range = 2000.0;              // (radar) how far it sees
	int seed = 1;
};
inline const std::vector<std::string> &hologramKinds() {
	static const std::vector<std::string> k = {"globe", "ship", "orbits", "map"};
	return k;
}
inline const std::vector<std::string> &screenShows() {
	static const std::vector<std::string> k = {"bars", "static", "picture", "radar", "trace"};
	return k;
}

namespace detail {
// A glow that is no light to aim at (seen, and lighting what reflects it, but never sampled), on both sides.
inline std::string glowMaterial(Vec3 L) { return "AreaLightSource \"diffuse\" \"rgb L\" [ " + fmt3(L) + " ] \"bool twosided\" true \"bool sampled\" false\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]"; }

// The part of a triangle between the heights y0 and y1, as a fan of triangles appended to `out` (Sutherland and Hodgman's clipping, against the two planes).
inline void clipToSlab(const Vec3 (&tri)[3], double y0, double y1, Mesh &out) {
	std::vector<Vec3> poly(tri, tri + 3);
	const auto clip = [&poly](double y, bool keepAbove) {
		std::vector<Vec3> next;
		for (std::size_t i = 0; i < poly.size(); ++i) {
			const Vec3 a = poly[i], b = poly[(i + 1) % poly.size()];
			const bool ina = keepAbove ? a.y >= y : a.y <= y, inb = keepAbove ? b.y >= y : b.y <= y;
			if (ina) next.push_back(a);
			if (ina != inb) next.push_back(a + (b - a) * ((y - a.y) / (b.y - a.y)));
		}
		poly.swap(next);
	};
	clip(y0, true);
	if (poly.size() >= 3) clip(y1, false);
	if (poly.size() < 3) return;
	const Vec3 n = normalize(cross(tri[1] - tri[0], tri[2] - tri[0]) + Vec3{0.0, 1e-12, 0.0});
	const int base = static_cast<int>(out.verts.size());
	for (const Vec3 &p : poly) out.verts.push_back(p), out.normals.push_back(n);
	for (int k = 1; k + 1 < static_cast<int>(poly.size()); ++k) out.tris.push_back({base, base + k, base + k + 1});
}
// `m` through `place` (story space), cut into slices `pitch` high - lit for the first `duty` of each, climbing `scroll` slices from the height `y0` - appended to `out`.
inline void scanlines(const Mesh &m, const Mat4 &place, double y0, double pitch, double duty, double scroll, Mesh &out) {
	for (const auto &t : m.tris) {
		const Vec3 tri[3] = {transformPoint(place, m.verts[static_cast<std::size_t>(t[0])]), transformPoint(place, m.verts[static_cast<std::size_t>(t[1])]),
							 transformPoint(place, m.verts[static_cast<std::size_t>(t[2])])};
		const double lo = std::min({tri[0].y, tri[1].y, tri[2].y}), hi = std::max({tri[0].y, tri[1].y, tri[2].y});
		for (double k = std::floor((lo - y0) / pitch - scroll); k <= std::ceil((hi - y0) / pitch - scroll); k += 1.0) {
			const double a = y0 + (k + scroll) * pitch;
			if (a + duty * pitch >= lo && a <= hi) clipToSlab(tri, a, a + duty * pitch, out);
		}
	}
}
// A hologram's brightness at nature's time nt (its flicker) and how far it has slipped sideways (a glitch: now and then, for a twentieth of a second).
inline void flickerOf(const Hologram &h, double nt, double &bright, double &slip) {
	const double hum = clouds::detail::noise3(nt * 11.0, 0.5, 0.5, h.seed) - 0.5;
	const int window = static_cast<int>(std::floor(nt * 4.0));
	const bool glitch = hash01(h.seed * 977 + window, 3) < 0.07 * h.flicker && std::fmod(nt * 4.0, 1.0) < 0.2;
	bright = std::max(0.0, 1.0 + 0.25 * h.flicker * hum) * (glitch ? 0.55 : 1.0);
	slip = glitch ? (hash01(h.seed * 977 + window, 4) - 0.5) * 0.08 * h.size : 0.0;
}
}  // namespace detail

// The hologram at nature's time nt as scene text.
inline std::string hologramText(const Hologram &h, double nt) {
	using namespace detail;
	std::string out = "# a hologram (film_screens.h): " + h.name + "\n";
	const double S = h.size;
	const Vec3 foot{-h.at.x, h.at.y, h.at.z};   // (story space)
	// the projector: a dark drum, a glowing ring round its top and its lens - the one light of it
	out += attr(metalMaterial({0.08, 0.09, 0.1}, 0.3), emitMesh(lathe({{0.0, 0.0}, {0.3 * S, 0.0}, {0.32 * S, 0.06 * S}, {0.26 * S, 0.12 * S}, {0.0, 0.12 * S}}, 40), translate(foot.x, foot.y, foot.z)));
	double bright = 1.0, slip = 0.0;
	flickerOf(h, nt, bright, slip);
	out += attr(glowMaterial(h.colour * (3.0 * bright)), emitMesh(torus(0.27 * S, 0.008 * S, 48, 4), translate(foot.x, foot.y + 0.121 * S, foot.z)));
	out += emissiveSphere(foot + Vec3{0.0, 0.125 * S, 0.0}, 0.03 * S, h.colour * (6.0 * bright));
	// the picture, its middle above the projector
	const Vec3 mid = foot + Vec3{slip, 0.75 * S, 0.0};
	const Mat4 turn = mul(translate(mid.x, mid.y, mid.z), rotate({0.0, 1.0, 0.0}, h.spin * nt + 37.0 * h.seed));
	const double pitch = S / 30.0, scroll = std::fmod(nt * 0.6, 1.0), y0 = foot.y;
	Mesh bright0, faint;   // (two glows: the picture and its fainter parts)
	if (h.kind == "globe") {
		const Mesh sphere = uvSphere(0.5 * S, 72, 36);
		Mesh land, sea;
		for (const auto &t : sphere.tris) {
			const Vec3 c = normalize(sphere.verts[static_cast<std::size_t>(t[0])] + sphere.verts[static_cast<std::size_t>(t[1])] + sphere.verts[static_cast<std::size_t>(t[2])]);
			Mesh &to = clouds::detail::fbm3(c.x * 2.4 + 5.0, c.y * 2.4 + 9.0, c.z * 2.4 + 13.0, h.seed) > 0.5 ? land : sea;
			const int b = static_cast<int>(to.verts.size());
			for (int k = 0; k < 3; ++k) to.verts.push_back(sphere.verts[static_cast<std::size_t>(t[k])]), to.normals.push_back(sphere.normals[static_cast<std::size_t>(t[k])]);
			to.tris.push_back({b, b + 1, b + 2});
		}
		scanlines(land, turn, y0, pitch, 0.55, scroll, bright0);
		scanlines(sea, turn, y0, pitch, 0.3, scroll, faint);
	} else if (h.kind == "ship") {
		cosmos::Ship craft;
		craft.kind = h.ship, craft.seed = h.seed;
		for (const cosmos::ShipPart &p : cosmos::shipParts(craft, nt, 0.0)) scanlines(p.mesh, mul(turn, scale(S), p.place), y0, pitch, 0.5, scroll, bright0);
	} else if (h.kind == "orbits") {
		scanlines(uvSphere(0.12 * S, 40, 20), turn, y0, pitch * 0.6, 0.55, scroll, bright0);
		for (int k = 0; k < 3; ++k) {
			const double R = (0.25 + 0.13 * k) * S, tip = 8.0 * (k - 1);
			const Mat4 plane = mul(turn, rotate({1.0, 0.0, 0.0}, tip));
			out += attr(glowMaterial(h.colour * (0.9 * bright)), emitMesh(torus(R, 0.003 * S, 96, 4), plane));
			const double a = nt * 1.2 * std::pow(0.25 * S / R, 1.5) + 2.1 * k + h.seed;   // (Kepler's: the outer ones slower)
			out += attr(glowMaterial(h.colour * (3.5 * bright)), emitMesh(uvSphere((0.025 + 0.01 * k) * S, 16, 8), mul(plane, translate(R * std::cos(a), 0.0, R * std::sin(a)))));
		}
	} else {   // map: a hilly land drawn as its contour lines, on a faint grid
		const int n = 48;
		Mesh land;
		for (int j = 0; j <= n; ++j)
			for (int i = 0; i <= n; ++i) {
				const double u = (double(i) / n - 0.5) * S, v = (double(j) / n - 0.5) * S;
				const double fall = std::max(0.0, 1.0 - (u * u + v * v) / (0.25 * S * S));
				land.verts.push_back({u, 0.35 * S * fall * clouds::detail::fbm3(u / S * 3.0 + 3.0, v / S * 3.0 + 7.0, 0.5, h.seed) - 0.3 * S, v});
				land.normals.push_back({0.0, 1.0, 0.0});
			}
		for (int j = 0; j < n; ++j)
			for (int i = 0; i < n; ++i) {
				const int a = j * (n + 1) + i;
				land.tris.push_back({a, a + 1, a + n + 2}), land.tris.push_back({a, a + n + 2, a + n + 1});
			}
		scanlines(land, turn, y0 + 0.002 * S, S / 22.0, 0.22, 0.0, bright0);   // (contours: slices of the land, every 1/22 of its size - they stay put)
		out += attr(glowMaterial(h.colour * (0.35 * bright)), emitMesh(box(S, 0.002 * S, S), mul(turn, translate(0.0, -0.3 * S, 0.0))));
	}
	if (!bright0.tris.empty()) out += attr(glowMaterial(h.colour * (1.6 * bright)), emitMesh(bright0, identity()));
	if (!faint.tris.empty()) out += attr(glowMaterial(h.colour * (0.7 * bright)), emitMesh(faint, identity()));
	// the beam: faint lines from the lens up to the picture's foot
	for (int k = 0; k < 10; ++k) {
		const double a = 2.0 * kPi * k / 10.0 + nt * h.spin * kPi / 180.0;
		out += attr(glowMaterial(h.colour * (0.5 * bright)), bar(foot + Vec3{0.0, 0.125 * S, 0.0}, mid + Vec3{0.4 * S * std::cos(a), -0.42 * S, 0.4 * S * std::sin(a)}, 0.0025 * S, 4));
	}
	return out;
}

namespace detail {
// The pictures a screen shows, painted once into the film's cache (film_sky::picture).
inline std::string barsPicture(const std::string &film) {
	return film_sky::picture(film, "screen_bars", 448, 252, [](std::vector<float> &rgb) {
		static const float bars[7][3] = {{0.75f, 0.75f, 0.75f}, {0.75f, 0.75f, 0.0f}, {0.0f, 0.75f, 0.75f}, {0.0f, 0.75f, 0.0f}, {0.75f, 0.0f, 0.75f}, {0.75f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.75f}};
		for (int y = 0; y < 252; ++y)
			for (int x = 0; x < 448; ++x) {
				const float *c = bars[x * 7 / 448];
				const bool strip = y > 252 * 3 / 4;   // (the strip along the foot: dark, with a white block)
				for (int k = 0; k < 3; ++k) rgb[(static_cast<std::size_t>(y) * 448 + x) * 3 + k] = strip ? (x < 448 / 4 ? 0.0f : (x < 448 / 2 ? 0.9f : 0.03f)) : c[k];
			}
	});
}
inline std::string staticPicture(const std::string &film, int frame) {
	return film_sky::picture(film, "screen_static_" + std::to_string(frame), 192, 108, [frame](std::vector<float> &rgb) {
		for (int y = 0; y < 108; ++y) {
			const float roll = (y + frame * 13) % 108 < 6 ? 1.4f : 1.0f;   // (a brighter band rolling down)
			for (int x = 0; x < 192; ++x) {
				const float v = static_cast<float>(hash01(frame * 104729 + y * 192 + x, 9)) * 0.7f * roll;
				for (int k = 0; k < 3; ++k) rgb[(static_cast<std::size_t>(y) * 192 + x) * 3 + k] = v;
			}
		}
	});
}
// A dark screen with a faint grid (`rings`: a radar's rings and cross instead).
inline std::string gridPicture(const std::string &film, bool rings) {
	return film_sky::picture(film, rings ? "screen_radar" : "screen_trace", 320, 180, [rings](std::vector<float> &rgb) {
		for (int y = 0; y < 180; ++y)
			for (int x = 0; x < 320; ++x) {
				float g = 0.0f;
				if (rings) {
					const double dx = (x + 0.5 - 160.0) / 81.0, dy = (y + 0.5 - 90.0) / 81.0, r = std::sqrt(dx * dx + dy * dy);
					if (r <= 1.0) {
						g = 0.03f;
						if (std::fabs(r * 4.0 - std::round(r * 4.0)) < 0.025 * 4.0 || std::fabs(dx) < 0.006 || std::fabs(dy) < 0.006) g = 0.16f;
					}
				} else {
					g = (x % 20 == 0 || y % 20 == 0) ? 0.08f : 0.015f;
				}
				float *p = &rgb[(static_cast<std::size_t>(y) * 320 + x) * 3];
				p[0] = 0.15f * g, p[1] = g, p[2] = 0.35f * g;
			}
	});
}
// A screen's frame: local x across (as an interior's), y up, z out of its face.
inline Mat4 frameOf(const Screen &s) { return mul(translate(-s.at.x, s.at.y, s.at.z), rotate({0.0, 1.0, 0.0}, -s.facing), rotate({1.0, 0.0, 0.0}, -s.tilt)); }
// A point on the face (u across, as one looking at it sees it, v up; each -1 .. 1) a little in front of it, in story space.
inline Vec3 onFace(const Screen &s, double u, double v, double out = 0.004) { return transformPoint(frameOf(s), {u * 0.5 * s.width, v * 0.5 * s.height, 0.026 + out}); }
// How bright a heartbeat trace is at x (0 .. 1 along a beat): a flat line, a small bump, the spike and its dip.
inline double beat(double x) {
	const auto g = [x](double at, double w) { return std::exp(-(x - at) * (x - at) / (w * w)); };
	return 0.12 * g(0.3, 0.05) + 0.9 * g(0.5, 0.012) - 0.3 * g(0.53, 0.012) + 0.2 * g(0.72, 0.05);
}
}  // namespace detail

// Where a craft is at nature's time nt (pbrt's).
inline Vec3 shipWhere(const cosmos::Ship &s, double nt) { return s.orbit.on ? cosmos::orbitState(s.orbit, nt).at : s.at + s.move * nt; }

// The screen at nature's time nt as scene text (`film`: whose cache holds its pictures; `ships`: what a radar sees).
inline std::string screenText(const Screen &s, double nt, const std::string &film, const std::vector<cosmos::Ship> &ships) {
	using namespace detail;
	std::string out = "# a screen (film_screens.h): " + s.name + "\n";
	const Mat4 F = frameOf(s);
	out += attr(diffuseMaterial({0.04, 0.04, 0.045}), emitMesh(box(s.width + 0.08, s.height + 0.08, 0.05), F));
	std::string file = s.file;
	if (s.show == "bars") file = barsPicture(film);
	else if (s.show == "static") file = staticPicture(film, static_cast<int>(std::floor(nt * 24.0)) % 8);
	else if (s.show == "radar" || s.show == "trace") file = gridPicture(film, s.show == "radar");
	// the face: a quad with the picture on it, its u across as one looking at it sees it
	Mesh face;
	for (int k = 0; k < 4; ++k) {
		const double u = (k == 1 || k == 2) ? 1.0 : -1.0, v = k >= 2 ? 1.0 : -1.0;
		face.verts.push_back({u * 0.5 * s.width, v * 0.5 * s.height, 0.026}), face.normals.push_back({0.0, 0.0, 1.0}), face.uvs.push_back({0.5 + 0.5 * u, 0.5 + 0.5 * v});
	}
	face.tris = {{0, 1, 2}, {0, 2, 3}};
	char scale[48];
	std::snprintf(scale, sizeof scale, "%.4f", s.bright);
	out += attr("AreaLightSource \"diffuse\" \"string filename\" [ \"" + file + "\" ] \"float scale\" [ " + scale + " ] \"bool twosided\" true\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", emitMesh(face, F));
	const Vec3 green{0.25, 1.0, 0.45};
	const double aspect = s.height / s.width, line = 0.006 * s.height;
	if (s.show == "radar") {   // the beam turning once in three seconds, a fading wake behind it, and the craft it has passed
		const double a = nt * 2.0 * kPi / 3.0, rr = 0.9;   // (the disc: 0.9 of the face's height across, in the face's own units)
		for (int k = 0; k < 6; ++k) {
			const double b = a - 0.07 * k;
			out += attr(glowMaterial(green * (s.bright * 3.0 * std::exp(-0.6 * k))), bar(onFace(s, 0.0, 0.0), onFace(s, rr * aspect * std::cos(b), rr * std::sin(b)), line, 4));
		}
		const Vec3 centre = s.hasCentre ? s.centre : s.at;
		for (const cosmos::Ship &sh : ships) {
			if (sh.goneAt >= 0.0 && nt > sh.goneAt) continue;
			const Vec3 d = shipWhere(sh, nt) - centre;
			const double x = d.x / s.range, y = d.z / s.range;
			if (x * x + y * y > 1.0) continue;
			const double since = std::fmod(a - std::atan2(y, x) + 8.0 * kPi, 2.0 * kPi);   // (how long ago the beam passed it, as an angle)
			const Vec3 p = onFace(s, rr * aspect * x, rr * y);
			out += attr(glowMaterial(green * (s.bright * 6.0 * std::exp(-since / 1.8))), emitMesh(uvSphere(0.018 * s.height, 10, 5), translate(p.x, p.y, p.z)));
		}
	} else if (s.show == "trace") {   // a heartbeat running across, a beat every 1.1 seconds, brightest at its newest end
		const int n = 80;
		const double head = std::fmod(nt / 1.1, 1.0) * 2.0 - 1.0;   // (where the newest point is: the line is redrawn across, left to right)
		for (int k = 0; k < n; ++k) {
			const double u0 = -0.92 + 1.84 * k / n, u1 = -0.92 + 1.84 * (k + 1) / n;
			const double age = std::fmod(head - u1 + 4.0, 2.0);   // (how long since the trace drew here, in screen widths)
			const auto at = [&](double u) { return onFace(s, u, -0.25 + 1.1 * beat(std::fmod((u + 0.92) / 1.84 * 2.0, 1.0))); };
			out += attr(glowMaterial(green * (s.bright * 3.5 * std::exp(-1.5 * age))), bar(at(u0), at(u1), line, 4));
		}
	}
	return out;
}

inline std::string hologramUsage() { return "`hologram NAME globe|ship|orbits|map at X Y Z [size S] [colour R G B] [spin DEG] [flicker F] [ship KIND] [seed N]` (size 0.05 to 50 m)"; }
inline std::string screenUsage() {
	return "`screen NAME at X Y Z show bars|static|radar|trace|picture FILE [size W H] [facing DEG] [tilt DEG] [bright B] [centre X Y Z] [range R] [seed N]`";
}

}  // namespace screens
}  // namespace cartoon
