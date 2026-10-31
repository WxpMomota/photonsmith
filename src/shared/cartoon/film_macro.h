#pragma once
// film_macro.h -- the small world close up (round 13): insects and dew, for macro shots (a close camera with `focus` and `aperture` for the shallow depth of field).
//
//   insects bees|dragonflies|beetles N in X0 Y0 Z0 X1 Y1 Z1 [size S] [film NM] [seed K]
//            bees and dragonflies hover at a point, then dart to the next (a new one every 1.5 to 3 s, somewhere in the box); their wings beat far faster than a
//            frame (a bee's 230 times a second, a dragonfly's 30), so each wing is drawn as what a camera sees of it: the fan it sweeps, see-through where it
//            passes fast and densest at the ends of its stroke where it turns (an alpha picture on the fan: the renderers' stochastic alpha test makes it
//            partly transparent). Beetles walk over the ground in the box on six legs (two tripods in turn). Their shells - and a dragonfly's body - are
//            iridescent: a conductor with a thin film on it ("float filmthickness", fresnel.h FrThinFilm), its colour changing with the angle it is seen
//            at; `film` sets the film's thickness in nanometres (a beetle's 380, green to gold; a dragonfly's 300, blue).
//   dew N in X0 Z0 X1 Z1 [size S] [seed K]
//            N blades of grass in the rectangle, bent over, with drops of water along them and hanging at their tips (glass of index 1.33 - each drop a lens
//            showing the world upside down); `size` scales the drops (1: 0.6 to 1.6 mm).
//
// On a terrain every height is above the ground there, as everywhere in a film. Sizes are real (a bee is 1.3 cm long): `size` scales an insect.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_sky.h"
#include "nature_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace macro {

struct Insects {
	std::string kind = "bees";
	int count = 3, seed = 1;
	Vec3 lo, hi;
	double size = 1.0, film = -1.0;   // (film < 0: the kind's own)
};
struct Dew {
	int count = 60, seed = 1;
	double x0 = -0.2, z0 = -0.2, x1 = 0.2, z1 = 0.2, size = 1.0;
};
struct World {
	std::vector<Insects> insects;
	std::vector<Dew> dews;
	bool any() const { return !insects.empty() || !dews.empty(); }
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k{"bees", "dragonflies", "beetles"};
	return k;
}

namespace detail {
inline Vec3 story(Vec3 p) { return {-p.x, p.y, p.z}; }
inline void append(Mesh &into, const Mesh &m) {
	const int base = static_cast<int>(into.verts.size());
	into.verts.insert(into.verts.end(), m.verts.begin(), m.verts.end());
	into.normals.insert(into.normals.end(), m.normals.begin(), m.normals.end());
	for (const auto &t : m.tris) into.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
}
inline Mesh moved(Mesh m, const Mat4 &by) {   // (points and normals through a rigid move, no uvs)
	for (Vec3 &v : m.verts) v = transformPoint(by, v);
	for (Vec3 &n : m.normals) n = normalize(Vec3{by[0] * n.x + by[1] * n.y + by[2] * n.z, by[4] * n.x + by[5] * n.y + by[6] * n.z, by[8] * n.x + by[9] * n.y + by[10] * n.z});
	m.uvs.clear();
	return m;
}
// A body part along +z: a lathe of `profile` (radius, z) made `wide` times as wide and `deep` times as tall.
inline Mesh part(const std::vector<std::pair<double, double>> &profile, double wide = 1.0, double deep = 1.0, int segments = 16) {
	const Mesh l = lathe(profile, segments);
	Mesh m;
	for (std::size_t i = 0; i < l.verts.size(); ++i) {
		m.verts.push_back({l.verts[i].x * wide, l.verts[i].z * deep, l.verts[i].y});
		m.normals.push_back(normalize(Vec3{l.normals[i].x / wide, l.normals[i].z / deep, l.normals[i].y}));
	}
	for (const auto &t : l.tris) m.tris.push_back({t[0], t[2], t[1]});   // (y and z swapped: the winding turns over)
	return m;
}
inline Mesh ball(double r, Vec3 at, double wide = 1.0, double deep = 1.0) {
	return moved(part({{0.0, -r}, {0.7 * r, -0.7 * r}, {r, 0.0}, {0.7 * r, 0.7 * r}, {0.0, r}}, wide, deep, 12), translate(at));
}
inline Mat4 facingOf(Vec3 fwd) {
	fwd = normalize(fwd);
	Vec3 right = cross(Vec3{0, 1, 0}, fwd);
	right = length(right) > 1e-9 ? normalize(right) : Vec3{1, 0, 0};
	const Vec3 up = cross(fwd, right);
	return {right.x, up.x, fwd.x, 0, right.y, up.y, fwd.y, 0, right.z, up.z, fwd.z, 0, 0, 0, 0, 1};
}
// The picture of a beating wing's blur on its fan (u across the stroke, v out along the wing): how much of the time the wing covers each point - the
// stroke's ends, where it slows and turns, far more than its middle (a sine stroke: 1 / sqrt(1 - x^2)), and the wing's outline (narrow at the root,
// rounded at the tip) - scaled so the densest is `peak`.
inline std::string blurPicture(double peak) {
	char name[64];
	std::snprintf(name, sizeof name, "wing_blur_%.2f", peak);
	const int W = 128, H = 64;
	return film_sky::picture("_macro", name, W, H, [&](std::vector<float> &rgb) {
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x) {
				const double u = (x + 0.5) / W * 2.0 - 1.0, v = 1.0 - (y + 0.5) / H;   // (row 0 is the top: v 1, the wing's tip)
				const double dwell = std::min(4.0, 1.0 / std::sqrt(std::max(1e-3, 1.0 - u * u)));
				const double outline = std::sqrt(std::max(0.0, std::sin(kPi * std::min(1.0, v * 1.05)))) * (v < 0.08 ? v / 0.08 : 1.0);
				const float a = static_cast<float>(std::clamp(peak * 0.25 * dwell * outline, 0.0, 1.0));
				const std::size_t i = static_cast<std::size_t>(y * W + x) * 3;
				rgb[i] = rgb[i + 1] = rgb[i + 2] = a;
			}
	});
}
// A wing's fan: rooted at the origin, sweeping round `axis` from angle -amp to +amp about `mid` (a unit vector out along the wing at mid-stroke), `len` long.
inline Mesh fan(Vec3 mid, Vec3 axis, double amp, double len) {
	Mesh m;
	const int n = 16, rings = 4;
	const Vec3 side = normalize(cross(axis, mid));
	for (int r = 0; r <= rings; ++r)
		for (int k = 0; k <= n; ++k) {
			const double a = -amp + 2.0 * amp * k / n, v = static_cast<double>(r) / rings;
			const Vec3 dir = mid * std::cos(a) + side * std::sin(a);
			m.verts.push_back(dir * (len * v));
			m.normals.push_back(axis);
			m.uvs.push_back({static_cast<double>(k) / n, v});
		}
	for (int r = 0; r < rings; ++r)
		for (int k = 0; k < n; ++k) {
			const int a = r * (n + 1) + k, b = a + n + 1;
			m.tris.push_back({a, b, a + 1}), m.tris.push_back({a + 1, b, b + 1});
		}
	return m;
}
inline std::string filmMetal(Vec3 base, double nm, double roughness) {
	char buf[220];
	std::snprintf(buf, sizeof buf, "Material \"conductor\" \"rgb reflectance\" [ %.4f %.4f %.4f ] \"float roughness\" [ %.4f ] \"float filmthickness\" [ %.1f ] \"float filmeta\" [ 1.56 ]",
				  base.x, base.y, base.z, roughness, nm);
	return buf;
}
inline std::string wingMaterial() { return filmMetal({0.06, 0.06, 0.06}, 260.0, 0.05); }
// Where a flier is at t, and the way it faces: hovering near one point, then darting to the next.
inline void flier(const Insects &f, int i, double t, Vec3 &at, Vec3 &fwd, bool &darting) {
	const int s = f.seed * 131 + i * 17;
	const auto point = [&](int k) {
		return Vec3{f.lo.x + (f.hi.x - f.lo.x) * hash01(s + k, 1), f.lo.y + (f.hi.y - f.lo.y) * hash01(s + k, 2), f.lo.z + (f.hi.z - f.lo.z) * hash01(s + k, 3)};
	};
	double t0 = -10.0 * hash01(s, 4);   // (each its own rhythm: the hovers' starts)
	int k = 0;
	double stay = 1.5 + 1.5 * hash01(s, 5);
	while (t0 + stay <= t && k < 4000) t0 += stay, ++k, stay = 1.5 + 1.5 * hash01(s + k, 5);
	const double dart = 0.35, into = t - t0;   // (the last `dart` seconds of a stay are the dash to the next point)
	const Vec3 a = point(k), b = point(k + 1);
	const double jitter = 0.003 * f.size;
	const Vec3 wobble{jitter * std::sin(7.1 * t + i), jitter * std::sin(5.3 * t + 2 * i), jitter * std::sin(6.2 * t + 3 * i)};
	darting = into > stay - dart;
	const double u = darting ? smoothstep(0.0, 1.0, (into - (stay - dart)) / dart) : 0.0;
	at = lerp(a, b, u) + wobble;
	const Vec3 way = b - a;
	const double turn = smoothstep(0.3, 0.9, into / stay);   // (turning to face where it goes next)
	const Vec3 before = normalize(point(k) - point(k - 1) + Vec3{1e-9, 0, 0});
	fwd = normalize(lerp(Vec3{before.x, 0.0, before.z}, Vec3{way.x, darting ? way.y * 0.5 : 0.0, way.z}, turn) + Vec3{1e-9, 0, 0});
}
}  // namespace detail

// A bee at t: a banded body, a fuzzy thorax, dark eyes, two blurred wings.
inline std::string beeText(const Insects &f, int i, double t, const std::string &blur) {
	Vec3 at, fwd;
	bool darting = false;
	detail::flier(f, i, t, at, fwd, darting);
	const double L = 0.013 * f.size;
	const Mat4 place = mul(translate(detail::story(at)), detail::facingOf(detail::story(fwd)), rotate({1, 0, 0}, darting ? 12.0 : 25.0), scale(L));
	Mesh yellow, black, fuzz, eyes;
	detail::append(fuzz, detail::ball(0.2, {0.0, 0.03, 0.12}, 1.0, 0.95));
	detail::append(black, detail::ball(0.14, {0.0, 0.02, 0.36}, 1.1, 1.0));
	for (const double side : {-1.0, 1.0}) detail::append(eyes, detail::ball(0.07, {side * 0.1, 0.05, 0.4}, 0.7, 1.2));
	for (int b = 0; b < 5; ++b) {   // the abdomen, in bands
		const double z0 = -0.05 - 0.12 * b, z1 = z0 - 0.12, r0 = 0.2 * std::sin(kPi * (0.15 + 0.17 * b)), r1 = 0.2 * std::sin(kPi * (0.15 + 0.17 * (b + 1)));
		Mesh seg = detail::part({{b == 0 ? 0.0 : r0 * 0.9, z0 + 0.001}, {r0, z0}, {r1, z1}, {b == 4 ? 0.0 : r1 * 0.9, z1 - 0.001}}, 1.0, 0.95, 14);
		detail::append(b % 2 == 0 ? yellow : black, seg);
	}
	std::string out = attr(diffuseMaterial({0.85, 0.58, 0.07}), emitMesh(yellow, place)) + attr(coated({0.03, 0.025, 0.02}, 0.4), emitMesh(black, place)) +
					  attr(diffuseMaterial({0.45, 0.3, 0.1}), emitMesh(fuzz, place)) + attr(coated({0.01, 0.01, 0.01}, 0.1), emitMesh(eyes, place));
	for (const double side : {-1.0, 1.0}) {   // the wings' fans, sweeping about the vertical
		Mesh w = detail::fan(normalize(Vec3{side, 0.15, -0.35}), Vec3{0.0, 1.0, 0.0}, 1.0, 0.62);
		for (Vec3 &v : w.verts) v = v + Vec3{side * 0.1, 0.2, 0.15};
		out += attr(detail::wingMaterial(), emitMesh(w, place) + "  \"texture alpha\" \"" + blur + "\"\n");
	}
	return out;
}
// A dragonfly at t: big eyes, an iridescent body (a thin film), two pairs of long wings, blurred.
inline std::string dragonflyText(const Insects &f, int i, double t, const std::string &blur) {
	Vec3 at, fwd;
	bool darting = false;
	detail::flier(f, i, t, at, fwd, darting);
	const double L = 0.07 * f.size, nm = f.film > 0.0 ? f.film : 300.0;
	const Mat4 place = mul(translate(detail::story(at)), detail::facingOf(detail::story(fwd)), rotate({1, 0, 0}, darting ? 6.0 : 2.0), scale(L));
	Mesh body, eyes;
	detail::append(body, detail::part({{0.0, 0.22}, {0.05, 0.2}, {0.06, 0.12}, {0.045, 0.06}, {0.022, 0.03}, {0.02, -0.2}, {0.017, -0.5}, {0.022, -0.66}, {0.0, -0.68}}, 1.0, 1.1, 14));
	for (const double side : {-1.0, 1.0}) detail::append(eyes, detail::ball(0.05, {side * 0.035, 0.02, 0.25}, 1.0, 1.0));
	std::string out = attr(detail::filmMetal({0.08, 0.12, 0.1}, nm, 0.12), emitMesh(body, place)) + attr(coated({0.05, 0.12, 0.09}, 0.05), emitMesh(eyes, place));
	for (const double z : {0.13, 0.06})
		for (const double side : {-1.0, 1.0}) {
			Mesh w = detail::fan(normalize(Vec3{side, 0.0, z > 0.1 ? 0.1 : -0.15}), normalize(Vec3{0.0, 1.0, 0.15}), 0.45, 0.48);
			for (Vec3 &v : w.verts) v = v + Vec3{side * 0.02, 0.05, z};
			out += attr(detail::wingMaterial(), emitMesh(w, place) + "  \"texture alpha\" \"" + blur + "\"\n");
		}
	return out;
}
// A beetle at t, walking over the ground: an iridescent domed shell, a dark head and thorax, six legs in two tripods, antennae.
inline std::string beetleText(const Insects &f, int i, double t, const std::function<double(double, double)> &groundAt) {
	const int s = f.seed * 151 + i * 13;
	const double L = 0.015 * f.size, nm = f.film > 0.0 ? f.film : 380.0, speed = 0.012 * f.size;
	// a wandering way: a heading that turns slowly, from a start in the box
	const auto heading = [&](double tt) { return 6.2832 * hash01(s, 1) + 0.6 * std::sin(0.31 * tt + i) + 0.4 * std::sin(0.17 * tt + 2 * i); };
	Vec3 p{f.lo.x + (f.hi.x - f.lo.x) * hash01(s, 2), 0.0, f.lo.z + (f.hi.z - f.lo.z) * hash01(s, 3)};
	for (double tt = 0.0; tt < t; tt += 0.25) {
		const double a = heading(tt);
		p = p + Vec3{std::cos(a), 0.0, std::sin(a)} * (speed * std::min(0.25, t - tt));
		if (p.x < f.lo.x || p.x > f.hi.x) p.x = std::clamp(p.x, f.lo.x, f.hi.x);   // (kept in its box)
		if (p.z < f.lo.z || p.z > f.hi.z) p.z = std::clamp(p.z, f.lo.z, f.hi.z);
	}
	const double a = heading(t);
	const Vec3 fwd{std::cos(a), 0.0, std::sin(a)};
	p.y = groundAt(p.x, p.z) + f.lo.y;
	const Mat4 place = mul(translate(detail::story(p)), detail::facingOf(detail::story(fwd)), scale(L));
	Mesh shell = detail::part({{0.0, -0.5}, {0.22, -0.42}, {0.32, -0.15}, {0.33, 0.05}, {0.28, 0.2}, {0.0, 0.22}}, 1.0, 0.75, 20);
	for (Vec3 &v : shell.verts) v.y = std::max(v.y, -0.02) + 0.18;   // (its underside flat)
	Mesh dark, legs;
	detail::append(dark, detail::ball(0.16, {0.0, 0.17, 0.3}, 1.35, 0.6));
	detail::append(dark, detail::ball(0.09, {0.0, 0.14, 0.46}, 1.2, 0.8));
	const double phase = std::fmod(t * speed / (0.25 * L) + hash01(s, 4), 1.0);   // (a step cycle every quarter body length)
	for (int leg = 0; leg < 6; ++leg) {
		const double side = leg < 3 ? -1.0 : 1.0, along = 0.2 - 0.2 * (leg % 3);
		const bool tripodA = (leg == 0 || leg == 2 || leg == 4);
		const double ph = std::fmod(phase + (tripodA ? 0.0 : 0.5), 1.0), swing = ph < 0.5 ? std::sin(kPi * ph / 0.5) : 0.0;
		const double reach = ph < 0.5 ? -0.12 + 0.24 * ph / 0.5 : 0.12 - 0.24 * (ph - 0.5) / 0.5;
		const Vec3 hip{side * 0.15, 0.14, along}, knee{side * 0.38, 0.22 + 0.06 * swing, along + reach * 0.6}, foot{side * 0.5, 0.0 + 0.05 * swing, along + reach};
		detail::append(legs, sdf::tubeMesh({hip, knee, foot}, 0.025, 0.6, 5));
	}
	for (const double side : {-1.0, 1.0}) detail::append(legs, sdf::tubeMesh({{side * 0.04, 0.17, 0.53}, {side * 0.14, 0.27, 0.68}, {side * 0.22, 0.3, 0.8}}, 0.014, 0.7, 4));
	return attr(detail::filmMetal({0.05, 0.07, 0.04}, nm, 0.15), emitMesh(shell, place)) + attr(coated({0.02, 0.02, 0.015}, 0.25), emitMesh(dark, place)) +
		   attr(diffuseMaterial({0.03, 0.025, 0.02}), emitMesh(legs, place));
}
// Dewy grass (see the top).
inline std::string dewText(const Dew &d, const std::function<double(double, double)> &groundAt) {
	Mesh blades;
	std::string drops;
	char buf[200];
	for (int i = 0; i < d.count; ++i) {
		const int s = d.seed * 173 + i;
		const double x = d.x0 + (d.x1 - d.x0) * hash01(s, 1), z = d.z0 + (d.z1 - d.z0) * hash01(s, 2), g = groundAt(x, z);
		const double h = 0.05 + 0.08 * hash01(s, 3), lean = 0.3 + 0.5 * hash01(s, 4), dir = 6.2832 * hash01(s, 5), w = 0.0018 + 0.0012 * hash01(s, 6);
		const Vec3 out{std::cos(dir), 0.0, std::sin(dir)}, across{-out.z, 0.0, out.x};
		std::vector<Vec3> line;   // (pbrt's coordinates: up, then over, as a blade bends under its own weight)
		for (int q = 0; q <= 8; ++q) {
			const double u = q / 8.0;
			line.push_back(Vec3{x, g, z} + Vec3{0.0, h * std::sin(u * kPi * 0.5 * (1.0 + 0.4 * (1.0 - lean))), 0.0} + out * (h * lean * u * u));
		}
		const int base = static_cast<int>(blades.verts.size());
		for (int q = 0; q <= 8; ++q) {
			const double half = w * (1.0 - q / 9.0);
			const Vec3 tangent = normalize(line[std::min(q + 1, 8)] - line[std::max(q - 1, 0)]), n = normalize(cross(across, tangent));
			for (const double sgn : {-1.0, 1.0}) blades.verts.push_back(detail::story(line[q] + across * (sgn * half))), blades.normals.push_back(detail::story(n));
		}
		for (int q = 0; q < 8; ++q) {
			const int a = base + 2 * q;
			blades.tris.push_back({a, a + 2, a + 1}), blades.tris.push_back({a + 1, a + 2, a + 3});
		}
		const int nd = 1 + static_cast<int>(3.0 * hash01(s, 7));
		for (int k = 0; k < nd; ++k) {   // drops sitting on the blade's top face, the last hanging at the tip
			const bool tip = k == nd - 1 && hash01(s, 8) < 0.7;
			const double u = tip ? 1.0 : 0.3 + 0.6 * hash01(s * 7 + k, 9), r = (0.0006 + 0.001 * hash01(s * 7 + k, 10)) * d.size * (tip ? 1.3 : 1.0);
			const int q = std::clamp(static_cast<int>(u * 8.0 + 0.5), 0, 8);
			const Vec3 tangent = normalize(line[std::min(q + 1, 8)] - line[std::max(q - 1, 0)]), n = normalize(cross(across, tangent));
			const Vec3 c = tip ? line[8] + Vec3{0.0, -0.8 * r, 0.0} : line[q] + (n.y < 0.0 ? n * -1.0 : n) * (0.75 * r);
			std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.5f %.5f %.5f\n  Shape \"sphere\" \"float radius\" [ %.5f ]\nAttributeEnd\n", c.x, c.y, c.z, r);   // (round: the GPU's spheres take no squash)
			drops += buf;
		}
	}
	return "# dew (film_macro.h)\n" + attr(translucentMaterial({0.12, 0.3, 0.06}, {0.1, 0.25, 0.04}), emitMesh(blades, identity())) +
		   attr("Material \"dielectric\" \"float eta\" [ 1.33 ]", drops);
}

// The macro world at t (heights lifted onto the ground: see the top).
inline std::string text(const World &w, double t, const std::function<double(double, double)> &groundAt) {
	if (!w.any()) return std::string();
	std::string out = "# the small world (film_macro.h)\n";
	bool fliers = false;
	for (const Insects &f : w.insects) fliers = fliers || f.kind != "beetles";
	const std::string blur = "rt_wing_blur";
	if (fliers) out += "Texture \"" + blur + "\" \"float\" \"imagemap\" \"string filename\" [ \"" + detail::blurPicture(0.9) + "\" ]\n";
	for (const Insects &f0 : w.insects) {
		Insects f = f0;
		const double g = groundAt(0.5 * (f.lo.x + f.hi.x), 0.5 * (f.lo.z + f.hi.z));
		if (f.kind != "beetles") f.lo.y += g, f.hi.y += g;
		for (int i = 0; i < f.count; ++i) {
			if (f.kind == "bees") out += beeText(f, i, t, blur);
			else if (f.kind == "dragonflies") out += dragonflyText(f, i, t, blur);
			else out += beetleText(f, i, t, groundAt);
		}
	}
	for (const Dew &d : w.dews) out += dewText(d, groundAt);
	return out;
}

}  // namespace macro
}  // namespace cartoon
