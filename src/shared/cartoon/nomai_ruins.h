#pragma once
// nomai_ruins.h -- the places of "The Last Nomai Signal" (nomai_signal.h): the ruins in the canyon, the research station's corridor in the rock, the round room of the
// elevator, the observatory deep under the ground, the explorer's shuttle, the wreck of the old expedition's craft, a small grove and the campfire.
//
// Made after the game's Nomai (Mobius Digital's own account of them): weathered pale stone that still carries faded, colourful paint (teal, orange, ochre bands with
// mathematical patterns), curves, arches and natural symmetry, bronze fittings, stone lifted by purple gravity crystals, and technology that glows a ghostly pale blue. The
// observatory's great machine is woven like a basket, as the Orbital Probe Cannon is.
//
// The ruins are built in a frame of their own on the canyon floor (ruinsFrame: x along the canyon, z across it, +z towards the landing site, y up); the corridor, the
// elevator and the observatory are in the same frame, inside the rock beyond the far wall and deep below it.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "film_cache.h"
#include "film_sky.h"
#include "nomai_world.h"

namespace cartoon {
namespace nomai {

constexpr double kRuinsX = 20.0;
inline Mat4 ruinsFrame() {
	static const Mat4 f = groundFrame(kRuinsX, canyonZ(kRuinsX), 0.0, -0.2);
	return f;
}
inline Vec3 ruinsPoint(Vec3 local) { return transformPoint(ruinsFrame(), local); }
inline Vec3 ruinsDir(Vec3 local) {
	const Mat4 &f = ruinsFrame();
	return normalize({f[0] * local.x + f[1] * local.y + f[2] * local.z, f[4] * local.x + f[5] * local.y + f[6] * local.z, f[8] * local.x + f[9] * local.y + f[10] * local.z});
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Materials: stone with faded paint, plain stone, dark stone, bronze, the glow
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The painted stone, as a picture laid round a wall (u around it, v up it): mottled pale sandstone, grime towards the foot, cracks, and bands of faded paint - teal with a
// row of orange triangles, an ochre stripe, a frieze of small circles - chipped away in places.
inline Vec3 paintedStone(double u, double v, int salt) {
	const double n1 = nature::fbm2(u * 40.0, v * 14.0, salt), n2 = nature::fbm2(u * 160.0, v * 60.0, salt + 1);
	Vec3 c = lerp(Vec3{0.80, 0.74, 0.62}, Vec3{0.70, 0.63, 0.52}, n1) * (0.90 + 0.18 * n2);
	c = c * (0.78 + 0.22 * smoothstep(0.0, 0.25, v));   // (grime at the foot)
	// cracks: thin dark lines where a ridged noise peaks
	const double cr = 1.0 - std::abs(2.0 * nature::noise2(u * 30.0 + 3.0 * n1, v * 9.0, salt + 2) - 1.0);
	c = c * (1.0 - 0.28 * smoothstep(0.95, 0.995, cr));
	const double chip = nature::fbm2(u * 60.0, v * 22.0, salt + 3);   // where the paint has gone
	const double keep = smoothstep(0.38, 0.52, chip) * 0.8;
	const auto paint = [&](Vec3 colour, double amount) { c = lerp(c, colour * (0.85 + 0.2 * n2), amount * keep); };
	if (v > 0.62 && v < 0.72) {
		paint({0.24, 0.52, 0.52}, 1.0);
		const double cu = u * 16.0 - std::floor(u * 16.0), cv = (v - 0.62) / 0.10;
		if (std::abs(cu - 0.5) < 0.42 * (1.0 - cv)) paint({0.82, 0.46, 0.22}, 1.0);   // a row of triangles
	}
	if (v > 0.55 && v < 0.58) paint({0.66, 0.34, 0.18}, 1.0);
	if (v > 0.76 && v < 0.80) {
		const double cu = u * 40.0 - std::floor(u * 40.0);
		if (std::hypot(cu - 0.5, (v - 0.78) / 0.04 * 0.5) < 0.32) paint({0.86, 0.80, 0.62}, 1.0);
		else paint({0.20, 0.36, 0.44}, 0.8);
	}
	return c;
}
inline std::string paintedStoneTexture(int salt) {
	return film_sky::picture(kFilmKey, "paint_" + std::to_string(salt), 1024, 1024, [salt](std::vector<float> &rgb) {
		for (int j = 0; j < 1024; ++j)
			for (int i = 0; i < 1024; ++i) {
				const Vec3 c = paintedStone((i + 0.5) / 1024.0, 1.0 - (j + 0.5) / 1024.0, salt);
				const std::size_t k = (static_cast<std::size_t>(j) * 1024 + i) * 3;
				rgb[k] = static_cast<float>(c.x);
				rgb[k + 1] = static_cast<float>(c.y);
				rgb[k + 2] = static_cast<float>(c.z);
			}
	});
}
inline std::string paintedMaterial(int salt) {
	const std::string tex = "ns_paint_" + std::to_string(salt);
	return "Texture \"" + tex + "\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + paintedStoneTexture(salt) + "\" ]\n  Material \"diffuse\" \"texture reflectance\" \"" + tex + "\"";
}
inline std::string stoneMaterial() { return diffuseMaterial({0.74, 0.68, 0.57}); }
inline std::string darkStoneMaterial() { return diffuseMaterial({0.24, 0.23, 0.25}); }
inline std::string bronzeMaterial() { return metalMaterial({0.62, 0.47, 0.30}, 0.42); }
inline Vec3 nomaiBlue() { return {0.55, 0.80, 1.0}; }       // the ghostly blue of their technology
inline Vec3 crystalPurple() { return {0.75, 0.38, 1.0}; }   // a gravity crystal
inline std::string glowText(const std::string &shapes, Vec3 colour, double strength) {
	const Vec3 e = colour * strength;
	return attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", shapes, &e);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A round wall from rIn to rOut, its top at top(angle) (a broken wall: lower in places), with the painted picture laid round it (u = angle, v = height / `paintHeight`).
template <typename Top>
inline Mesh ringWall(double rIn, double rOut, Top top, double paintHeight, int segments = 96, double a0 = 0.0, double a1 = 2.0 * kPi) {
	Mesh m;
	const bool closed = a1 - a0 > 2.0 * kPi - 1e-6;
	const int n = segments;
	for (int k = 0; k <= n; ++k) {
		const double a = a0 + (a1 - a0) * k / n;
		const double h = top(a), ca = std::cos(a), sa = std::sin(a);
		const double u = (a - a0) / (2.0 * kPi);
		// outer bottom, outer top, inner top, inner bottom
		const Vec3 p[4] = {{rOut * ca, 0.0, rOut * sa}, {rOut * ca, h, rOut * sa}, {rIn * ca, h, rIn * sa}, {rIn * ca, 0.0, rIn * sa}};
		const Vec3 nrm[4] = {{ca, 0.0, sa}, {ca, 0.0, sa}, {-ca, 0.0, -sa}, {-ca, 0.0, -sa}};
		for (int q = 0; q < 4; ++q) {
			m.verts.push_back(p[q]);
			m.normals.push_back(nrm[q]);
			m.uvs.push_back({u * 3.0, p[q].y / paintHeight});
		}
	}
	for (int k = 0; k < n; ++k) {
		const int a = 4 * k, b = 4 * (k + 1);
		m.tris.push_back({a + 0, b + 0, b + 1});
		m.tris.push_back({a + 0, b + 1, a + 1});
		m.tris.push_back({a + 3, a + 2, b + 2});
		m.tris.push_back({a + 3, b + 2, b + 3});
		m.tris.push_back({a + 1, b + 1, b + 2});
		m.tris.push_back({a + 1, b + 2, a + 2});
	}
	if (!closed) {
		const int e = 4 * n;
		m.tris.push_back({0, 1, 2});
		m.tris.push_back({0, 2, 3});
		m.tris.push_back({e + 0, e + 2, e + 1});
		m.tris.push_back({e + 0, e + 3, e + 2});
	}
	sdf::computeNormals(m);
	return m;
}
// An arch: a semicircle of rectangular section (span between the inner faces of its feet, the crown at `rise` above the springing, `depth` through it) standing on two
// square piers `pier` tall.
inline Mesh archMesh(double span, double thick, double depth, double pier) {
	Mesh m;
	const double ri = span * 0.5, ro = ri + thick;
	const int n = 24;
	for (int k = 0; k <= n; ++k) {
		const double a = kPi * k / n;
		const double ca = std::cos(a), sa = std::sin(a);
		for (const double z : {-depth * 0.5, depth * 0.5}) {
			m.verts.push_back({ri * ca, pier + ri * sa, z});
			m.verts.push_back({ro * ca, pier + ro * sa, z});
		}
	}
	for (int k = 0; k < n; ++k) {
		const int a = 4 * k, b = 4 * (k + 1);
		// faces: front (z-), back (z+), inner, outer
		m.tris.push_back({a + 0, b + 0, b + 1});
		m.tris.push_back({a + 0, b + 1, a + 1});
		m.tris.push_back({a + 2, a + 3, b + 3});
		m.tris.push_back({a + 2, b + 3, b + 2});
		m.tris.push_back({a + 0, a + 2, b + 2});
		m.tris.push_back({a + 0, b + 2, b + 0});
		m.tris.push_back({a + 1, b + 1, b + 3});
		m.tris.push_back({a + 1, b + 3, a + 3});
	}
	sdf::computeNormals(m);
	Mesh piers[2] = {box(thick, pier, depth), box(thick, pier, depth)};
	for (int s = 0; s < 2; ++s) {
		const int base = static_cast<int>(m.verts.size());
		const double x = (s == 0 ? 1.0 : -1.0) * (ri + thick * 0.5);
		for (std::size_t i = 0; i < piers[s].verts.size(); ++i) {
			m.verts.push_back(piers[s].verts[i] + Vec3{x, pier * 0.5, 0.0});
			m.normals.push_back(piers[s].normals[i]);
		}
		for (const auto &t : piers[s].tris) m.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
	}
	return m;
}
inline Mesh columnMesh(double radius, double height, int salt) {
	std::vector<std::pair<double, double>> p;
	p.push_back({0.0, 0.0});
	p.push_back({radius * 1.35, 0.0});
	p.push_back({radius * 1.35, 0.25});
	p.push_back({radius * 1.05, 0.35});
	for (int i = 1; i <= 10; ++i) {
		const double y = 0.35 + (height - 0.8) * i / 10.0;
		p.push_back({radius * (1.0 - 0.06 * i / 10.0) * (0.97 + 0.06 * hash01(i, salt)), y});
	}
	p.push_back({radius * 1.25, height - 0.3});
	p.push_back({radius * 1.4, height});
	p.push_back({0.0, height});
	return lathe(p, 28);
}
// A broken dome over a round building: a spherical cap with a bite taken out of it.
inline Mesh brokenDome(double r, double rise, double biteAt, double biteWidth) {
	Mesh m;
	const int nu = 72, nv = 16;
	for (int j = 0; j <= nv; ++j) {
		const double t = static_cast<double>(j) / nv, el = t * kPi / 2.0;
		for (int i = 0; i <= nu; ++i) {
			const double a = 2.0 * kPi * i / nu;
			m.verts.push_back({r * std::cos(el) * std::cos(a), rise * std::sin(el), r * std::cos(el) * std::sin(a)});
			m.uvs.push_back({3.0 * i / nu, 0.4 + 0.6 * t});
		}
	}
	for (int j = 0; j < nv; ++j)
		for (int i = 0; i < nu; ++i) {
			const double a = 2.0 * kPi * (i + 0.5) / nu;
			const double t = (j + 0.5) / nv;
			const double off = std::abs(std::remainder(a - biteAt, 2.0 * kPi));
			if (off < biteWidth * (0.4 + 0.6 * t) + 0.15 * nature::noise2(i * 0.7, j * 0.9, 91)) continue;   // (fallen in)
			const int p0 = j * (nu + 1) + i, p1 = p0 + 1, p2 = p0 + nu + 1, p3 = p2 + 1;
			m.tris.push_back({p0, p2, p3});
			m.tris.push_back({p0, p3, p1});
		}
	sdf::computeNormals(m);
	for (Vec3 &n : m.normals) n = n * -1.0;   // (outward)
	for (auto &t : m.tris) std::swap(t[1], t[2]);
	return m;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The ruins in the canyon
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The research station's door: a round opening in the first step of the far wall, ringed with bronze, its stone disc rolled aside.
inline Vec3 stationDoor() { return {-10.0, 0.0, -17.0}; }
// The spiral of floating stone steps down the near wall, and the bridge of floating slabs from the rim to its top.
inline Vec3 spiralCentre() { return {-8.0, 0.0, 16.0}; }
constexpr double kSpiralRadius = 6.0, kSpiralDrop = 49.0, kSpiralTurns = 3.0;
inline Vec3 spiralStep(double u) {   // u from 0 (the top) to 1 (the floor): a point of the walking line
	const double a = 1.3 + u * kSpiralTurns * 2.0 * kPi;
	return spiralCentre() + Vec3{kSpiralRadius * std::cos(a), kSpiralDrop * (1.0 - u) + 0.2, kSpiralRadius * std::sin(a)};
}
// The glow of the old machines, which still wakes now and then: a slow pulse, with flickers.
inline double machinePulse(double t, int salt) {
	const double slow = 0.5 + 0.5 * std::sin(2.0 * kPi * t / 6.5 + salt * 1.7);
	const double flick = nature::noise2(t * 3.0, salt * 3.1, 93) > 0.8 ? 0.4 : 1.0;
	return (0.25 + 0.75 * slow * slow) * flick;
}

inline std::string ruinsStatic() {
	const Mat4 F = ruinsFrame();
	std::string out;
	// the round tower with its broken dome, against the far wall
	const Vec3 tower{6.0, 0.0, -8.0};
	{
		std::string shapes;
		Mesh wall = ringWall(5.2, 6.0, [](double a) { return 9.0 - 3.5 * smoothstep(0.6, 1.0, std::cos(a - 0.9)) - 0.6 * nature::noise2(a * 3.0, 0.5, 95); }, 9.0, 120);
		shapes += meshText(wall, mul(F, translate(tower + Vec3{0.0, -0.3, 0.0})));
		out += attr(paintedMaterial(1), shapes);
		out += attr(paintedMaterial(2), meshText(brokenDome(6.0, 4.5, 0.9, 0.7), mul(F, translate(tower + Vec3{0.0, 8.6, 0.0}))));
		// its door, facing the plaza: a ring of bronze and a dark opening
		out += attr(bronzeMaterial(), meshText(torus(1.25, 0.12, 40, 8), mul(F, translate(tower + Vec3{0.0, 1.35, 6.05}), rotate({1.0, 0.0, 0.0}, 90.0))));
		out += attr(diffuseMaterial({0.02, 0.02, 0.025}), meshText(lathe({{0.0, 0.0}, {1.2, 0.0}, {0.0, 0.02}}, 36), mul(F, translate(tower + Vec3{0.0, 1.35, 6.02}), rotate({1.0, 0.0, 0.0}, 90.0))));
	}
	// the plaza: a round floor of painted tiles (a sunk disc with a ring of steps), the arcade behind it, fallen columns
	const Vec3 plaza{-2.0, 0.0, 3.0};
	out += attr(paintedMaterial(3), meshText(ringWall(6.5, 7.2, [](double) { return 0.45; }, 1.4, 96), mul(F, translate(plaza + Vec3{0.0, -0.2, 0.0}))));
	out += attr(paintedMaterial(4), meshText(lathe({{0.0, 0.18}, {6.6, 0.18}, {6.6, -0.2}}, 96), mul(F, translate(plaza))));
	std::string arches;
	for (int k = 0; k < 6; ++k) {
		if (k == 4) continue;   // (one has fallen)
		arches += meshText(archMesh(3.0, 0.7, 0.9, 3.4 - (k == 5 ? 1.2 : 0.0)), mul(F, translate(-17.0 + 4.0 * k, -0.3, 10.5)));
	}
	out += attr(stoneMaterial(), arches);
	std::string cols;
	for (int k = 0; k < 7; ++k) {
		const double a = 2.0 * kPi * k / 7.0;
		const bool fallen = k == 2 || k == 5;
		const Vec3 at = plaza + Vec3{8.2 * std::cos(a), -0.2, 8.2 * std::sin(a)};
		if (fallen) cols += meshText(columnMesh(0.45, 5.0, 100 + k), mul(F, translate(at + Vec3{0.0, 0.45, 0.0}), rotate({0.0, 1.0, 0.0}, 40.0 * k), rotate({0.0, 0.0, 1.0}, 88.0)));
		else cols += meshText(columnMesh(0.45, 5.0 - 1.4 * hash01(k, 101), 100 + k), mul(F, translate(at)));
	}
	out += attr(paintedMaterial(5), cols);
	// the old machine at the plaza's edge: a dark stone pillar with bronze bands (its channels glow: ruinsGlow)
	const Vec3 machine = plaza + Vec3{-4.6, 0.0, -3.2};
	out += attr(darkStoneMaterial(), meshText(columnMesh(0.55, 3.2, 120), mul(F, translate(machine))));
	out += attr(bronzeMaterial(), meshText(torus(0.62, 0.06, 30, 6), mul(F, translate(machine + Vec3{0.0, 1.0, 0.0}))) + meshText(torus(0.60, 0.06, 30, 6), mul(F, translate(machine + Vec3{0.0, 2.3, 0.0}))));
	// the station's door in the far wall
	const Vec3 door = stationDoor();
	out += attr(bronzeMaterial(), meshText(torus(1.7, 0.22, 48, 10), mul(F, translate(door + Vec3{0.0, 1.9, 0.0}), rotate({1.0, 0.0, 0.0}, 90.0))));
	out += attr(paintedMaterial(6), meshText(ringWall(1.9, 2.7, [](double) { return 2.6; }, 2.6, 48), mul(F, translate(door + Vec3{0.0, 1.9, -1.3}), rotate({1.0, 0.0, 0.0}, 90.0))));
	out += attr(stoneMaterial(), meshText(lathe({{0.0, 0.0}, {1.65, 0.0}, {1.65, 0.35}, {0.0, 0.35}}, 40), mul(F, translate(door + Vec3{2.4, 1.75, 0.6}), rotate({1.0, 0.0, 0.0}, 90.0), rotate({0.0, 0.0, 1.0}, 8.0))));
	// (no dark disc in the opening: the corridor behind it is dark enough, and a camera in the corridor's mouth must see along it)
	// the floating steps down the near wall: slabs on a spiral, each with a small purple crystal under it, and the bridge of slabs to the rim
	std::string slabs, crystals;
	const int steps = 90;
	for (int k = 0; k < steps; ++k) {
		const double u = (k + 0.5) / steps;
		const Vec3 p = spiralStep(u);
		const double a = 1.3 + u * kSpiralTurns * 2.0 * kPi;
		slabs += meshText(roundedBox(1.7, 0.30, 1.0, 0.08, 2), mul(F, translate(p + Vec3{0.0, -0.15, 0.0}), rotate({0.0, 1.0, 0.0}, -a * 180.0 / kPi)));
		if (k % 3 == 0) crystals += meshText(uvSphere(0.10, 8, 5), mul(F, translate(p + Vec3{0.0, -0.45, 0.0}), scale(0.8, 1.6, 0.8)));
	}
	const Vec3 top = spiralStep(0.0);
	for (int k = 0; k < 9; ++k) {
		const Vec3 p = lerp(top, Vec3{top.x + 2.0, kSpiralDrop + 0.4, 38.0}, (k + 0.5) / 9.0);
		slabs += meshText(roundedBox(1.9, 0.32, 1.7, 0.08, 2), mul(F, translate(p + Vec3{0.0, -0.16, 0.0}), rotate({0.0, 1.0, 0.0}, 5.0 * std::sin(k * 1.7))));
		crystals += meshText(uvSphere(0.12, 8, 5), mul(F, translate(p + Vec3{0.0, -0.5, 0.0}), scale(0.8, 1.6, 0.8)));
	}
	out += attr(stoneMaterial(), slabs);
	out += glowText(crystals, crystalPurple(), 2.0);
	// the spiral's central spire
	out += attr(paintedMaterial(7), meshText(columnMesh(0.9, kSpiralDrop + 4.0, 130), mul(F, translate(spiralCentre() + Vec3{0.0, -0.3, 0.0}))));
	// the broken bridge high across the canyon: an arch from the far rim reaching most of the way over
	std::string bridge;
	for (int k = 0; k < 14; ++k) {
		const double u = k / 13.0;
		const double z = -36.0 + 54.0 * u, y = kCanyonDepth + 1.0 + 6.0 * std::sin(kPi * (z + 36.0) / 72.0);
		bridge += meshText(roundedBox(3.2, 1.0, 4.2, 0.15, 2), mul(F, translate(30.0, y, z), rotate({1.0, 0.0, 0.0}, -9.0 * std::cos(kPi * (z + 36.0) / 72.0))));
	}
	out += attr(stoneMaterial(), bridge);
	return out;
}
// What glows in the ruins at time t: the old machine's channels and the station door's ring, pulsing; `awake` (0..1) brings them up.
inline std::string ruinsGlow(double t, double awake) {
	const Mat4 F = ruinsFrame();
	const Vec3 machine = Vec3{-2.0, 0.0, 3.0} + Vec3{-4.6, 0.0, -3.2};
	std::string lines;
	for (int k = 0; k < 4; ++k) {
		const double a = kPi / 4.0 + k * kPi / 2.0;
		lines += meshText(capsule(0.02, 2.4, 6, 2), mul(F, translate(machine + Vec3{0.53 * std::cos(a), 0.4, 0.53 * std::sin(a)})));
	}
	std::string out = glowText(lines, nomaiBlue(), 3.0 * (0.15 + 0.85 * awake) * machinePulse(t, 1));
	out += glowText(meshText(torus(1.45, 0.035, 48, 6), mul(F, translate(stationDoor() + Vec3{0.0, 1.9, 0.12}), rotate({1.0, 0.0, 0.0}, 90.0))), nomaiBlue(), 2.5 * (0.1 + 0.9 * awake) * machinePulse(t, 2));
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The grove: a few alien plants on the canyon floor, tall stalks with pale pods and fronds
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string groveText(double t) {
	const Mat4 F = ruinsFrame();
	std::string stalks, pods, glow;
	for (int k = 0; k < 16; ++k) {
		const Vec3 base = Vec3{-24.0 + 6.0 * hash01(k, 141), -0.3, 2.0 + 7.0 * hash01(k, 142)};
		const double h = 1.4 + 2.2 * hash01(k, 143);
		const Vec3 sway{0.06 * std::sin(t * 0.7 + k), 0.0, 0.04 * std::cos(t * 0.6 + k * 1.3)};
		const Vec3 tip = base + Vec3{0.25 * (hash01(k, 144) - 0.5), h, 0.25 * (hash01(k, 145) - 0.5)} + sway;
		stalks += meshText(capsule(0.035, h, 6, 2), mul(F, alignY(base, tip - base)));
		pods += meshText(uvSphere(0.16 + 0.10 * hash01(k, 146), 12, 8), mul(F, translate(tip), scale(1.0, 1.3, 1.0)));
		if (k % 4 == 0) glow += meshText(uvSphere(0.05, 6, 4), mul(F, translate(tip + Vec3{0.0, 0.22, 0.0})));
		for (int f = 0; f < 3; ++f) {
			const double a = 2.0 * kPi * f / 3.0 + k;
			stalks += rod(transformPoint(F, base + Vec3{0.0, 0.15, 0.0}), transformPoint(F, base + Vec3{0.55 * std::cos(a), 0.55, 0.55 * std::sin(a)}), 0.03, 6);
		}
	}
	return attr(diffuseMaterial({0.26, 0.42, 0.36}), stalks) + attr(translucentMaterial({0.70, 0.78, 0.66}, {0.40, 0.55, 0.45}), pods) + glowText(glow, {0.60, 0.95, 0.85}, 1.5);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The wreck: the old expedition's craft, lying half buried on the canyon floor, its window broken
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline Mesh shuttleHull(double len, double radius) {
	std::vector<std::pair<double, double>> p;
	for (int i = 0; i <= 24; ++i) {
		const double u = static_cast<double>(i) / 24.0;
		const double r = radius * std::pow(std::sin(kPi * (0.08 + 0.84 * u)), 0.55) * (1.0 - 0.18 * u);
		p.push_back({i == 0 || i == 24 ? 0.0 : r, len * u});
	}
	return lathe(p, 40);
}
inline std::string wreckText() {
	const Mat4 F = ruinsFrame();
	const Mat4 at = mul(F, translate(34.0, -0.9, 1.0), rotate({0.0, 1.0, 0.0}, 63.0), rotate({0.0, 0.0, 1.0}, 78.0), rotate({1.0, 0.0, 0.0}, 12.0));
	std::string out = attr(paintedMaterial(8), meshText(shuttleHull(6.5, 1.6), at));
	std::string ribs;
	for (int k = 1; k < 8; ++k) ribs += meshText(torus(1.55 * std::pow(std::sin(kPi * (0.08 + 0.84 * k / 8.0)), 0.55), 0.07, 36, 6), mul(at, translate(0.0, 6.5 * k / 8.0, 0.0)));
	out += attr(bronzeMaterial(), ribs);
	out += attr(darkStoneMaterial(), meshText(torus(0.9, 0.12, 30, 6), mul(at, translate(0.0, 6.2, 0.0))));
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The explorer's shuttle: a rounded hull of pale stone in a woven bronze lattice, a great round window at its nose, three legs, a ring of blue light at its stern
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Its frame: `at` the point between its feet, the nose towards local +z.
inline std::string shuttleText(const Mat4 &at, double engine, double legs = 1.0, bool window = true) {
	std::string out;
	// the hull lies along local z, its belly 1.6 m above the ground
	const Mat4 hull = mul(at, translate(0.0, 2.2, -3.6), rotate({1.0, 0.0, 0.0}, 90.0));
	out += attr(paintedMaterial(9), meshText(shuttleHull(7.2, 1.9), hull));
	std::string lattice;
	for (int k = 0; k < 12; ++k) {   // the woven ribs: two families of helices round the hull
		for (int s = -1; s <= 1; s += 2) {
			std::vector<Vec3> pts;
			for (int i = 0; i <= 40; ++i) {
				const double u = 0.08 + 0.84 * i / 40.0;
				const double r = 1.93 * std::pow(std::sin(kPi * (0.08 + 0.84 * u)), 0.55) * (1.0 - 0.18 * u);
				const double a = 2.0 * kPi * k / 12.0 + s * u * 2.4;
				pts.push_back(transformPoint(hull, {r * std::cos(a), 7.2 * u, r * std::sin(a)}));
			}
			for (std::size_t i = 0; i + 1 < pts.size(); ++i) lattice += rod(pts[i], pts[i + 1], 0.035, 5);
		}
	}
	out += attr(bronzeMaterial(), lattice);
	// the window at the nose
	const Vec3 nose = transformPoint(hull, {0.0, 6.6, 0.0});
	if (window) out += attr(glassMaterial(1.45), meshText(uvSphere(0.85, 28, 14), mul(translate(nose), scale(1.0, 1.0, 0.45))));
	out += attr(bronzeMaterial(), meshText(torus(0.9, 0.10, 40, 8), mul(translate(nose), alignY({0.0, 0.0, 0.0}, transformPoint(at, {0.0, 0.0, 1.0}) - transformPoint(at, {0.0, 0.0, 0.0})))));
	// three legs
	std::string legText;
	for (int k = 0; k < 3; ++k) {
		const double a = kPi / 2.0 + 2.0 * kPi * k / 3.0;
		const Vec3 hip = transformPoint(at, {1.2 * std::cos(a), 1.9, 1.2 * std::sin(a) - 0.4});
		const Vec3 foot = transformPoint(at, {lerp(1.2, 2.4, legs) * std::cos(a), lerp(1.6, 0.05, legs), lerp(1.2, 2.4, legs) * std::sin(a) - 0.4});
		legText += rod(hip, foot, 0.09, 8) + meshText(lathe({{0.0, 0.0}, {0.34, 0.0}, {0.30, 0.10}, {0.0, 0.12}}, 20), translate(foot));
	}
	out += attr(bronzeMaterial(), legText);
	// the stern's ring of light
	const Mat4 stern = mul(hull, translate(0.0, 0.35, 0.0));
	out += glowText(meshText(torus(1.05, 0.09, 40, 6), stern), nomaiBlue(), 0.6 + 6.0 * engine);
	return out;
}
// Inside the cockpit: the seat back, the curved console with its glyph keys, and the screen where the signal's waveform is drawn. `at` the shuttle's frame.
inline Mat4 cockpitFrame(const Mat4 &at) { return mul(at, translate(0.0, 2.0, 1.7)); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The corridor of the research station, the elevator's round room, the shaft and the observatory
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The corridor runs from the door into the rock (-z), bending left on an arc, to the round room. Its walking line, s from 0 (the door) to 1 (the room's edge).
constexpr double kCorridorLength = 38.0, kCorridorBend = 26.0;
inline Vec3 corridorPoint(double s) {
	const Vec3 d = stationDoor();
	const double run = 6.0;   // straight at first
	const double len = s * kCorridorLength;
	if (len <= run) return d + Vec3{0.0, 0.0, -len};
	const double a = (len - run) / kCorridorBend;
	return d + Vec3{-kCorridorBend * (1.0 - std::cos(a)), 0.0, -run - kCorridorBend * std::sin(a)};
}
inline Vec3 corridorHeading(double s) { return normalize(corridorPoint(std::min(1.0, s + 0.01)) - corridorPoint(std::max(0.0, s - 0.01))); }
inline Vec3 rotundaCentre() { return corridorPoint(1.0) + corridorHeading(1.0) * 5.5; }
constexpr double kRotundaR = 5.5, kShaftR = 2.2, kShaftDepth = 300.0, kObservatoryR = 70.0;
constexpr double kObservatoryOffset = 30.0;   // (the shaft comes down 30 m from the hall's middle, so the platform passes beside the machine)
inline double observatoryRoofAtShaft() { return std::sqrt(kObservatoryR * kObservatoryR - kObservatoryOffset * kObservatoryOffset); }
inline Vec3 observatoryCentre() { return rotundaCentre() + Vec3{0.0, -kShaftDepth - observatoryRoofAtShaft(), -kObservatoryOffset}; }
inline double landingDrop() { return kShaftDepth + observatoryRoofAtShaft() + kObservatoryR * 0.55; }   // the platform's drop to the hall's floor
constexpr int kCorridorLamps = 12;
inline double corridorLampAt(int k) { return (k + 0.6) / kCorridorLamps; }

// The corridor: a vaulted tunnel of pale painted stone (a floor, walls and a pointed vault, closed at its ends by the door and the room), its wall panels carved with spiral
// writing (nomai_fx.h lays it), and lamps along it that wake one by one: `lampsOn` lamps lit.
inline std::string corridorText(double lampsOn, double t) {
	const Mat4 F = ruinsFrame();
	static const std::string ply = film_cache::ply(kFilmKey, "corridor", [] {
		Mesh m;
		const int n = 160, around = 24;
		for (int i = 0; i <= n; ++i) {
			const double s = static_cast<double>(i) / n;
			const Vec3 c = corridorPoint(s), h = corridorHeading(s);
			const Vec3 side = normalize(cross(Vec3{0.0, 1.0, 0.0}, h));
			for (int k = 0; k <= around; ++k) {
				// the section: a flat floor 3 m wide, walls and a pointed vault 3.6 m high, walked round from the right foot over the top to the left foot
				const double u = static_cast<double>(k) / around;
				double x, y;
				if (u < 0.15) {
					x = 1.5;
					y = 2.0 * u / 0.15;
				} else if (u < 0.85) {
					const double a = (u - 0.15) / 0.70 * kPi;
					x = 1.5 * std::cos(a);
					y = 2.0 + 1.6 * std::pow(std::sin(a), 0.8);
				} else {
					x = -1.5;
					y = 2.0 * (1.0 - (u - 0.85) / 0.15);
				}
				m.verts.push_back(c + side * x + Vec3{0.0, y, 0.0});
				m.uvs.push_back({s * kCorridorLength / 6.0, y / 3.6});
			}
		}
		for (int i = 0; i < n; ++i)
			for (int k = 0; k < around; ++k) {
				const int a = i * (around + 1) + k, b = a + around + 1;
				m.tris.push_back({a, a + 1, b + 1});
				m.tris.push_back({a, b + 1, b});
			}
		// the floor
		const int base = static_cast<int>(m.verts.size());
		for (int i = 0; i <= n; ++i) {
			const double s = static_cast<double>(i) / n;
			const Vec3 c = corridorPoint(s), side = normalize(cross(Vec3{0.0, 1.0, 0.0}, corridorHeading(s)));
			m.verts.push_back(c + side * 1.5);
			m.verts.push_back(c - side * 1.5);
			m.uvs.push_back({s * 8.0, 0.0});
			m.uvs.push_back({s * 8.0, 0.4});
		}
		for (int i = 0; i < n; ++i) {
			const int a = base + 2 * i;
			m.tris.push_back({a, a + 2, a + 3});
			m.tris.push_back({a, a + 3, a + 1});
		}
		sdf::computeNormals(m);
		// (both are seen from inside: the normals point in)
		return m;
	});
	std::string out;
	// (the PLY is in the ruins frame: placed by it)
	out += attr(paintedMaterial(10), film_cache::placement(F) + film_cache::plyShape(ply));
	// the lamps: bronze cups on the walls with a pale blue glow, lit one by one
	std::string cups;
	for (int k = 0; k < kCorridorLamps; ++k) {
		const double s = corridorLampAt(k);
		const Vec3 c = corridorPoint(s), side = normalize(cross(Vec3{0.0, 1.0, 0.0}, corridorHeading(s)));
		const double sideSign = k % 2 == 0 ? 1.0 : -1.0;
		const Vec3 lamp = c + side * (1.38 * sideSign) + Vec3{0.0, 2.2, 0.0};
		cups += meshText(lathe({{0.0, -0.12}, {0.12, -0.10}, {0.16, 0.04}, {0.13, 0.05}}, 16), mul(F, translate(lamp)));
		const double on = clamp01(lampsOn - k);
		const double flick = on > 0.0 && on < 1.0 ? (0.5 + 0.5 * std::sin(t * 41.0 + k)) : 1.0;
		if (on > 0.0) out += emissiveSphere(transformPoint(F, lamp + Vec3{0.0, 0.08, 0.0}), 0.07, nomaiBlue() * (14.0 * on * flick));
		else out += attr(diffuseMaterial({0.30, 0.38, 0.44}), meshText(uvSphere(0.07, 10, 6), mul(F, translate(lamp + Vec3{0.0, 0.08, 0.0}))));
	}
	out += attr(bronzeMaterial(), cups);
	return out;
}

// The round room of the elevator: a domed room of painted stone, the circular platform over the shaft, its bronze rim and the ring of glyphs on the floor that wakes when
// it is touched; the platform at `drop` metres down the shaft.
inline std::string rotundaText(double drop, double glow, double t) {
	const Mat4 F = ruinsFrame();
	const Vec3 c = rotundaCentre();
	std::string out;
	const Vec3 back = corridorHeading(1.0) * -1.0;
	const double gap = std::atan2(back.z, back.x), half = 0.32;   // (the opening the corridor comes in by)
	out += attr(paintedMaterial(11), meshText(ringWall(kRotundaR, kRotundaR + 0.6, [](double) { return 4.0; }, 4.0, 96, gap + half, gap + 2.0 * kPi - half), mul(F, translate(c))));
	out += attr(paintedMaterial(12), meshText(brokenDome(kRotundaR + 0.3, 3.0, 0.0, -1.0), mul(F, translate(c + Vec3{0.0, 3.95, 0.0}))));
	// lamps round the wall, lit
	for (int k = 0; k < 6; ++k) {
		const double a = gap + kPi / 6.0 + 2.0 * kPi * k / 6.0;
		const Vec3 lamp = c + Vec3{(kRotundaR - 0.12) * std::cos(a), 2.4, (kRotundaR - 0.12) * std::sin(a)};
		out += attr(bronzeMaterial(), meshText(lathe({{0.0, -0.12}, {0.12, -0.10}, {0.16, 0.04}, {0.13, 0.05}}, 16), mul(F, translate(lamp))));
		out += emissiveSphere(transformPoint(F, lamp + Vec3{0.0, 0.08, 0.0}), 0.07, nomaiBlue() * 14.0);
	}
	// the floor: a ring round the shaft
	out += attr(paintedMaterial(4), meshText(lathe({{kShaftR + 0.05, 0.0}, {kRotundaR + 0.1, 0.0}, {kRotundaR + 0.1, -0.4}, {kShaftR + 0.05, -0.4}}, 96), mul(F, translate(c))));
	// the shaft: a long round tube down to the observatory's roof
	out += attr(darkStoneMaterial(), meshText(ringWall(kShaftR + 0.05, kShaftR + 0.6, [](double) { return kShaftDepth + 2.0; }, 40.0, 64), mul(F, translate(c + Vec3{0.0, -kShaftDepth - 2.0, 0.0}))));
	// glowing bands down the shaft, every 12 m
	std::string bands;
	for (int k = 1; k < 25; ++k) bands += meshText(torus(kShaftR + 0.04, 0.03, 48, 4), mul(F, translate(c + Vec3{0.0, -12.0 * k, 0.0})));
	out += glowText(bands, nomaiBlue(), 1.2 * (0.3 + 0.7 * glow));
	// the platform
	const Vec3 pc = c + Vec3{0.0, -drop, 0.0};
	out += attr(stoneMaterial(), meshText(lathe({{0.0, 0.0}, {kShaftR - 0.08, 0.0}, {kShaftR - 0.08, -0.35}, {0.0, -0.5}}, 64), mul(F, translate(pc))));
	out += attr(bronzeMaterial(), meshText(torus(kShaftR - 0.12, 0.06, 64, 6), mul(F, translate(pc + Vec3{0.0, 0.03, 0.0}))));
	// the ring of glyphs on the platform (a circle of short strokes), glowing when it is woken
	std::string glyphs;
	for (int k = 0; k < 18; ++k) {
		const double a = 2.0 * kPi * k / 18.0;
		const double len = 0.12 + 0.12 * hash01(k, 151);
		glyphs += meshText(box(0.05, 0.012, len), mul(F, translate(pc + Vec3{1.45 * std::cos(a), 0.01, 1.45 * std::sin(a)}), rotate({0.0, 1.0, 0.0}, -a * 180.0 / kPi)));
	}
	out += glowText(glyphs, nomaiBlue(), 0.4 + 4.0 * glow * (0.85 + 0.15 * std::sin(t * 5.0)));
	return out;
}

// The observatory: a vast round hall in the dark, its floor a great disc with glowing channels that light outwards from the middle (`floorLight` 0..1), the machine at its
// centre (a sphere of woven bronze bands round a core), and dozens of stone rings floating round it on gravity crystals, which wake one by one and turn (`ringsAwake`).
inline Vec3 machineCentre() { return observatoryCentre() + Vec3{0.0, 14.0, 0.0}; }
inline Vec3 observatoryFloor() { return observatoryCentre() + Vec3{0.0, -kObservatoryR * 0.55, 0.0}; }
constexpr int kRings = 22;
inline std::string observatoryText(double t, double floorLight, double ringsAwake, double machineGlow) {
	const Mat4 F = ruinsFrame();
	std::string out;
	// the hall: a dark sphere seen from inside (its inner face), carved walls just visible where light reaches them
	const Vec3 o = observatoryCentre();
	out += attr(paintedMaterial(13), meshText(brokenDome(kObservatoryR, kObservatoryR, 0.0, -1.0), mul(F, translate(o))));
	out += attr(darkStoneMaterial(), meshText(brokenDome(kObservatoryR, kObservatoryR, 0.0, -1.0), mul(F, translate(o), scale(1.0, -1.0, 1.0))));
	// the floor disc and its channels
	const Vec3 fl = observatoryFloor();
	const double floorR = std::sqrt(kObservatoryR * kObservatoryR - std::pow(kObservatoryR * 0.55, 2.0));
	out += attr(paintedMaterial(14), meshText(lathe({{0.0, 0.0}, {floorR, 0.0}, {floorR, -1.0}, {0.0, -1.0}}, 128), mul(F, translate(fl))));
	std::string channels;
	for (int k = 0; k < 24; ++k) {   // spokes and rings of light in the floor, reaching out as it wakes
		const double a = 2.0 * kPi * k / 24.0;
		const double reach = 4.0 + (floorR - 6.0) * clamp01(floorLight * 1.2 - 0.2 * hash01(k, 161));
		if (floorLight <= 0.0) break;
		channels += meshText(box(0.12, 0.02, reach - 4.0), mul(F, translate(fl + Vec3{std::cos(a) * (4.0 + reach) * 0.5, 0.02, std::sin(a) * (4.0 + reach) * 0.5}), rotate({0.0, 1.0, 0.0}, 90.0 - a * 180.0 / kPi)));
	}
	for (int k = 1; k <= 4; ++k)
		if (floorLight * 5.0 > k) channels += meshText(torus(k * 9.0, 0.06, 160, 4), mul(F, translate(fl + Vec3{0.0, 0.03, 0.0})));
	if (!channels.empty()) out += glowText(channels, nomaiBlue(), 2.5 * (0.5 + 0.5 * floorLight));
	// the machine: a woven sphere of bronze bands round a pale core
	const Vec3 mc = machineCentre();
	std::string weave;
	for (int k = 0; k < 10; ++k) {
		const Mat4 r = mul(F, translate(mc), rotate({0.0, 1.0, 0.0}, 18.0 * k + 8.0 * t * machineGlow), rotate({1.0, 0.0, 0.0}, 63.0), rotate({0.0, 0.0, 1.0}, 36.0 * k));
		weave += meshText(torus(6.0, 0.22, 72, 8), r);
	}
	out += attr(bronzeMaterial(), weave);
	out += emissiveSphere(transformPoint(F, mc), 2.0, nomaiBlue() * (0.3 + 12.0 * machineGlow));
	out += attr(darkStoneMaterial(), meshText(columnMesh(2.2, 8.0, 170), mul(F, translate(fl))));
	// the rings: stone hoops round the machine, each larger than the last (an armillary of stone, so none crosses another), every one tipped its own way and held by a
	// small purple crystal; woken one after another, a ring turns about its own axis and its inlay glows
	std::string stones, inlays, crystals;
	for (int k = 0; k < kRings; ++k) {
		const double size = 7.8 + 1.15 * k;
		const double awake = clamp01(ringsAwake * kRings * 1.1 - k);
		const double spin = awake * (t * (4.0 + 9.0 * hash01(k, 174)) * (k % 2 == 0 ? 1.0 : -1.0)) + 360.0 * hash01(k, 178);
		const double tipYaw = 360.0 * hash01(k, 173), tip = 8.0 + 34.0 * hash01(k, 176);
		const Mat4 m = mul(F, translate(mc), rotate({0.0, 1.0, 0.0}, tipYaw), rotate({1.0, 0.0, 0.0}, tip), rotate({0.0, 1.0, 0.0}, spin));
		const double tube = 0.24 + 0.14 * hash01(k, 177);
		stones += meshText(torus(size, tube, 96, 8), m);
		if (awake > 0.0) inlays += meshText(torus(size, tube + 0.02, 72, 3), mul(m, scale(1.0, 0.25, 1.0)));
		crystals += meshText(uvSphere(0.22, 8, 5), mul(m, translate(size, 0.0, 0.0), scale(0.8, 1.6, 0.8)));
	}
	out += attr(stoneMaterial(), stones);
	if (!inlays.empty()) out += glowText(inlays, nomaiBlue(), 1.8);
	out += glowText(crystals, crystalPurple(), 2.2 + 1.5 * ringsAwake);
	// the shaft's mouth in the roof and the platform's landing in the floor's middle (a dais)
	out += attr(stoneMaterial(), meshText(lathe({{0.0, 0.0}, {kShaftR + 1.2, 0.0}, {kShaftR + 0.4, -0.6}, {0.0, -0.6}}, 64), mul(F, translate(fl + Vec3{0.0, 0.0, kObservatoryOffset}))));
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The campfire on the canyon's near rim, beside a ruined arch: a ring of stones, dry stalks from the grove, the flames
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline Vec3 campfireArc() { return {-30.0, -122.0, 0.0}; }   // (arc coordinates x, z)
// (its +z away from the canyon: the explorer sits on that side, looking over the fire to the canyon and the giant)
inline Mat4 campfireFrame() { return groundFrame(campfireArc().x, campfireArc().y, 5.0, -0.05); }
inline std::string campfireText(double t, double strength) {
	const Mat4 F = campfireFrame();
	std::string out, stones, sticks;
	for (int k = 0; k < 9; ++k) {
		const double a = 2.0 * kPi * k / 9.0;
		stones += meshText(nature::rock(0.28, 180 + k), mul(F, translate(0.42 * std::cos(a), 0.0, 0.42 * std::sin(a))));
	}
	out += attr(diffuseMaterial({0.40, 0.33, 0.28}), stones);
	for (int k = 0; k < 6; ++k) {
		const double a = 2.0 * kPi * k / 6.0 + 0.3;
		sticks += rod(transformPoint(F, {0.30 * std::cos(a), 0.02, 0.30 * std::sin(a)}), transformPoint(F, {0.04 * std::cos(a + 2.0), 0.36, 0.04 * std::sin(a + 2.0)}), 0.025, 6);
	}
	out += attr(diffuseMaterial({0.16, 0.12, 0.09}), sticks);
	if (strength <= 0.0) return out;
	// the flames: a few flickering tongues of light, and the fire's glow on everything round it
	std::string flames;
	for (int k = 0; k < 5; ++k) {
		const double ph = t * (7.0 + k) + k * 1.9;
		const double h = (0.30 + 0.16 * std::sin(ph) + 0.08 * std::sin(ph * 2.3)) * (k == 0 ? 1.4 : 1.0);
		const Vec3 base{0.07 * std::cos(k * 1.3), 0.06, 0.07 * std::sin(k * 1.3)};
		flames += meshText(lathe({{0.0, 0.0}, {0.07, 0.04}, {0.06, h * 0.45}, {0.025, h * 0.8}, {0.0, h}}, 10), mul(F, translate(base), rotate({0.0, 0.0, 1.0}, 8.0 * std::sin(ph * 0.7))));
	}
	const double flick = 0.85 + 0.15 * std::sin(t * 13.0) * std::sin(t * 7.3);
	out += glowText(flames, {1.0, 0.48, 0.14}, 9.0 * strength * flick);
	char buf[220];
	const Vec3 c = transformPoint(F, {0.0, 0.45, 0.0});
	const Vec3 I = Vec3{1.0, 0.52, 0.20} * (2.6 * strength * flick);
	std::snprintf(buf, sizeof buf, "LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ %.4f %.4f %.4f ]\n", -c.x, c.y, c.z, I.x, I.y, I.z);
	return out + buf;
}

}  // namespace nomai
}  // namespace cartoon
