#pragma once
// film_waterside.h -- places on the water (water round 9):
//
//   pier X Z toward A [length L]              planks on posts going out over the water, a rail, lamps
//   harbour X Z [facing A] [length L]         a stone quay, bollards, crates and coiled rope, lamp posts, a crane
//   lighthouse X Z [height H]                 a striped tower, its gallery and lantern; after dark its light turns (its beam shows in fog or mist)
//   bridge X Z to X Z [arches N] [height H]   a stone bridge on arches, parapets, its reflection under it
//   watermill X Z [facing A] [river N]        a mill with a thatched roof and a wheel turning as fast as the water goes, wet and dripping
//   steppingstones X Z to X Z [count N]       flat stones across
//   stilthouses N at X Z [radius R]           huts on poles over the water, ladders, nets drying
//   dam X Z to X Z [height H] [burst T] [river N]   a wall across a river; at `burst T` its middle goes, and a surge runs down the river (`bore`)
//
// Coordinates: pbrt's in a film file; heights are from the water's level unless a terrain says otherwise. They are written as meshes each frame only where they move (a mill wheel, a lamp, a dam going).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_craft.h"
#include "film_flow.h"
#include "film_stream.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace waterside {

struct Place {
	std::string kind;
	double x = 0.0, z = 0.0, x1 = 0.0, z1 = 0.0, facing = 0.0, length = 0.0, height = 0.0, radius = 12.0, burst = -1.0;
	int count = 0, seed = 1, river = -1;
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"pier", "harbour", "lighthouse", "bridge", "watermill", "steppingstones", "stilthouses", "dam"};
	return k;
}

namespace detail {
using craft::detail::add;
using craft::detail::bar;
using craft::detail::boxAt;
struct Bits {
	Mesh wood, dark, stone, pale, thatch, metal, glass, red, white, lamps;
	// `lit`: 0 by day to 1 at night - the lamps' globes glow, warm (seen, not lighting the scene: there are many)
	void put(std::string &out, double lit = 0.0) const {
		if (!lamps.tris.empty()) out += lit > 0.02 ? flow::glowAttr(emitMesh(lamps, identity()), Vec3{3.2, 2.3, 1.2} * lit) : attr(diffuseMaterial({0.86, 0.82, 0.72}), emitMesh(lamps, identity()));
		if (!wood.tris.empty()) out += attr(coated({0.42, 0.29, 0.17}, 0.5), emitMesh(wood, identity()));
		if (!dark.tris.empty()) out += attr(diffuseMaterial({0.14, 0.1, 0.08}), emitMesh(dark, identity()));
		if (!stone.tris.empty()) out += attr(diffuseMaterial({0.5, 0.47, 0.42}), emitMesh(stone, identity()));
		if (!pale.tris.empty()) out += attr(diffuseMaterial({0.86, 0.82, 0.72}), emitMesh(pale, identity()));
		if (!thatch.tris.empty()) out += attr(diffuseMaterial({0.54, 0.42, 0.22}), emitMesh(thatch, identity()));
		if (!metal.tris.empty()) out += attr(metalMaterial({0.45, 0.45, 0.46}, 0.35), emitMesh(metal, identity()));
		if (!glass.tris.empty()) out += attr(coated({0.04, 0.06, 0.09}, 0.05), emitMesh(glass, identity()));
		if (!red.tris.empty()) out += attr(coated({0.72, 0.14, 0.1}, 0.3), emitMesh(red, identity()));
		if (!white.tris.empty()) out += attr(coated({0.92, 0.91, 0.87}, 0.3), emitMesh(white, identity()));
	}
};
inline Mat4 frameAt(double x, double y, double z, double facing) { return mul(translate(-x, y, z), rotate({0.0, 1.0, 0.0}, -facing)); }
inline Vec3 on(const Mat4 &m, Vec3 p) { return transformPoint(m, p); }
inline void post(Mesh &m, const Mat4 &f, Vec3 foot, double h, double r) { bar(m, on(f, foot), on(f, foot + Vec3{0.0, h, 0.0}), r); }

inline void pier(Bits &b, const Place &p, double y) {
	const double L = p.length > 0.0 ? p.length : 24.0, W = 2.4, deck = 1.1;
	const Mat4 f = frameAt(p.x, y, p.z, p.facing);
	for (double z = 0.15; z < L; z += 0.34) add(b.wood, box(W, 0.05, 0.3), mul(f, translate(0.0, deck, z)));
	for (const double side : {-1.0, 1.0}) {
		add(b.wood, box(0.1, 0.16, L), mul(f, translate(side * (0.5 * W - 0.1), deck - 0.1, 0.5 * L)));
		for (double z = 1.0; z < L; z += 3.0) {
			post(b.wood, f, {side * 0.5 * W, -2.0, z}, 2.0 + deck + 0.9, 0.09);
			bar(b.dark, on(f, {side * 0.5 * W, deck * 0.15, z}), on(f, {-side * 0.5 * W, deck * 0.8, z + 1.5}), 0.018);
		}
		bar(b.wood, on(f, {side * 0.5 * W, deck + 0.9, 0.0}), on(f, {side * 0.5 * W, deck + 0.9, L}), 0.05);
	}
	for (double z = 6.0; z < L; z += 9.0) {
		post(b.dark, f, {0.5 * W, deck, z}, 2.6, 0.04);
		add(b.lamps, uvSphere(0.14, 12, 8), mul(f, translate(0.5 * W, deck + 2.7, z)));
	}
}
inline void harbour(Bits &b, const Place &p, double y) {
	const double L = p.length > 0.0 ? p.length : 46.0, top = 1.5;
	const Mat4 f = frameAt(p.x, y, p.z, p.facing);
	add(b.stone, box(L, top + 4.0, 9.0), mul(f, translate(0.0, 0.5 * (top - 4.0), -4.0)));
	for (double x = -0.5 * L; x <= 0.5 * L; x += 1.4) add(b.dark, box(0.03, top + 3.0, 0.02), mul(f, translate(x, 0.5 * (top - 3.0), 0.505)));
	for (double h = -1.0; h < top; h += 0.42) add(b.dark, box(L, 0.02, 0.02), mul(f, translate(0.0, h, 0.505)));
	add(b.dark, box(L, 1.6, 0.03), mul(f, translate(0.0, -0.75, 0.52)));
	for (double x = -0.45 * L; x < 0.5 * L; x += 8.0) {
		add(b.dark, lathe({{0.16, 0.0}, {0.12, 0.3}, {0.2, 0.42}, {0.2, 0.5}, {0.0, 0.5}}, 12), mul(f, translate(x, top, -0.5)));
		add(b.dark, torus(0.35, 0.09, 16, 6), mul(f, translate(x, 0.5, 0.56), rotate({1.0, 0.0, 0.0}, 90.0)));
	}
	for (double x = -0.36 * L; x < 0.4 * L; x += 13.0) {
		add(b.wood, box(1.1, 0.9, 1.1), mul(f, translate(x, top + 0.45, -3.5)));
		add(b.wood, box(0.8, 0.7, 0.8), mul(f, translate(x + 1.3, top + 0.35, -3.2), rotate({0.0, 1.0, 0.0}, 25.0)));
		add(b.pale, lathe({{0.45, 0.0}, {0.48, 0.12}, {0.32, 0.14}, {0.3, 0.26}, {0.0, 0.26}}, 16), mul(f, translate(x - 1.4, top, -2.4)));
		post(b.dark, f, {x + 3.0, top, -7.0}, 4.4, 0.05);
		add(b.lamps, uvSphere(0.2, 12, 8), mul(f, translate(x + 3.0, top + 4.5, -7.0)));
	}
	// a crane
	bar(b.red, on(f, {0.2 * L, top, -3.0}), on(f, {0.2 * L, top + 7.0, -3.0}), 0.22);
	bar(b.red, on(f, {0.2 * L, top + 6.8, -3.4}), on(f, {0.2 * L, top + 9.0, 3.6}), 0.13);
	bar(b.dark, on(f, {0.2 * L, top + 8.9, 3.5}), on(f, {0.2 * L, top + 2.6, 3.5}), 0.02);
	add(b.dark, torus(0.16, 0.035, 12, 5), mul(f, translate(0.2 * L, top + 2.5, 3.5)));
}
inline void lighthouse(Bits &b, const Place &p, double y) {
	const double H = p.height > 0.0 ? p.height : 24.0, foot = 0.13 * H, top = 0.07 * H;
	const Mat4 f = translate(-p.x, y, p.z);
	const int bands = 6;
	for (int k = 0; k < bands; ++k) {
		const double a = k / double(bands), c = (k + 1.0) / bands;
		add(k % 2 == 0 ? b.white : b.red, lathe({{lerp(foot, top, a), a * 0.82 * H}, {lerp(foot, top, c), c * 0.82 * H}}, 28), f);
	}
	add(b.stone, lathe({{1.25 * foot, -3.0}, {1.3 * foot, 0.02}, {foot, 0.05}, {0.0, 0.05}}, 28), f);
	add(b.dark, lathe({{1.75 * top, 0.82 * H}, {1.75 * top, 0.825 * H}, {0.0, 0.825 * H}}, 24), f);
	add(b.dark, lathe({{1.1 * top, 0.92 * H}, {1.05 * top, 0.925 * H}, {0.5 * top, 0.99 * H}, {0.0, H}}, 22), f);
	for (int k = 0; k < 8; ++k) {
		const double a = k * 0.785;
		bar(b.dark, on(f, {top * std::cos(a), 0.825 * H, top * std::sin(a)}), on(f, {top * std::cos(a), 0.92 * H, top * std::sin(a)}), 0.03 * top * 2.0);
		bar(b.dark, on(f, {1.7 * top * std::cos(a), 0.825 * H, 1.7 * top * std::sin(a)}), on(f, {1.7 * top * std::cos(a), 0.86 * H, 1.7 * top * std::sin(a)}), 0.02);
	}
	add(b.dark, torus(1.7 * top, 0.02, 32, 5), mul(f, translate(0.0, 0.86 * H, 0.0)));
	for (int k = 0; k < 5; ++k) add(b.glass, box(0.14 * foot, 0.1 * H * 0.5, 0.08), mul(f, translate(0.0, (0.14 + 0.15 * k) * H, lerp(foot, top, (0.14 + 0.15 * k) / 0.82) + 0.01)));   // (on the wall: it narrows to `top` at 0.82 H)
}
// A stone bridge along +z (local), arches of `n`; side faces cut by each arch's round.
inline void bridge(Bits &b, const Place &p, double y) {
	const double dx = p.x1 - p.x, dz = p.z1 - p.z, L = std::hypot(dx, dz), W = 3.2, H = p.height > 0.0 ? p.height : std::max(2.4, 0.2 * L);
	const int n = p.count > 0 ? p.count : std::max(1, static_cast<int>(L / 9.0));
	const Mat4 f = frameAt(p.x, y, p.z, std::atan2(dx, dz) * 180.0 / kPi);
	const double span = L / n;
	const auto deckAt = [&](double z) { const double u = z / L; return H + 0.5 * H * 4.0 * u * (1.0 - u) * 0.35; };
	const auto archAt = [&](double z) {   // how high the arch is over z: its underside
		const double u = std::fmod(z, span) / span - 0.5, r = 0.46 * span, q = u * span;
		return std::abs(q) < r ? 0.72 * (deckAt(z) - 0.8) * std::sqrt(1.0 - (q / r) * (q / r)) * 1.0 : -3.0;
	};
	const int steps = 16 * n;
	Mesh side;
	for (const double s : {-1.0, 1.0}) {
		const int base = static_cast<int>(side.verts.size());
		for (int k = 0; k <= steps; ++k) {
			const double z = L * k / steps;
			side.verts.push_back(on(f, {s * 0.5 * W, deckAt(z), z})), side.verts.push_back(on(f, {s * 0.5 * W, archAt(z), z}));
			side.normals.push_back({0.0, 0.0, 1.0}), side.normals.push_back({0.0, 0.0, 1.0});
		}
		for (int k = 0; k < steps; ++k) {
			const int a = base + 2 * k;
			side.tris.push_back({a, a + 1, a + 2}), side.tris.push_back({a + 1, a + 3, a + 2});
		}
	}
	Mesh under, top;
	const int cols = 3;
	for (int k = 0; k <= steps; ++k) {
		const double z = L * k / steps;
		for (int c = 0; c <= cols; ++c) {
			const double x = (c / double(cols) - 0.5) * W;
			under.verts.push_back(on(f, {x, archAt(z), z})), under.normals.push_back({0.0, -1.0, 0.0});
			top.verts.push_back(on(f, {x, deckAt(z), z})), top.normals.push_back({0.0, 1.0, 0.0});
		}
	}
	for (int k = 0; k < steps; ++k)
		for (int c = 0; c < cols; ++c) {
			const int a = k * (cols + 1) + c, w = cols + 1;
			under.tris.push_back({a, a + 1, a + w}), under.tris.push_back({a + 1, a + w + 1, a + w});
			top.tris.push_back({a, a + w, a + 1}), top.tris.push_back({a + 1, a + w, a + w + 1});
		}
	sdf::computeNormals(side);
	add(b.stone, side), add(b.stone, under), add(b.pale, top);
	for (const double s : {-1.0, 1.0}) {   // parapets, and each pier's cutwater
		std::vector<Vec3> line;
		for (int k = 0; k <= 32; ++k) {
			const double z = L * k / 32.0;
			line.push_back(on(f, {s * 0.5 * W, deckAt(z) + 0.5, z}));
		}
		for (int k = 0; k < 32; ++k) {
			const double z = L * (k + 0.5) / 32.0;
			add(b.stone, box(0.26, 0.6, L / 32.0 * 1.02), mul(f, translate(s * 0.5 * W, deckAt(z) + 0.22, z)));
		}
		craft::detail::tube(b.pale, line, 0.16, 6);
	}
	for (int k = 1; k < n; ++k) add(b.stone, lathe({{0.8, -3.0}, {0.75, 1.0}, {0.0, 1.5}}, 14), mul(f, translate(0.0, 0.0, k * span), scale(1.2, 1.0, 0.8)));
}
inline void mill(Bits &b, const Place &p, double y) {
	const Mat4 f = frameAt(p.x, y, p.z, p.facing);
	add(b.stone, box(6.0, 2.6, 5.0), mul(f, translate(-3.4, 1.3, 0.0)));
	add(b.pale, box(6.0, 2.2, 5.0), mul(f, translate(-3.4, 3.7, 0.0)));
	for (int k = 0; k < 6; ++k) add(b.dark, box(0.14, 2.2, 0.03), mul(f, translate(-6.3 + 1.15 * k, 3.7, 2.52)));
	add(b.dark, box(6.0, 0.14, 0.03), mul(f, translate(-3.4, 4.75, 2.52)));
	add(b.dark, box(1.0, 1.9, 0.05), mul(f, translate(-4.6, 0.95, 2.52)));
	for (int k = 0; k < 2; ++k) add(b.glass, box(0.8, 0.8, 0.05), mul(f, translate(-5.2 + 3.0 * k, 3.7, 2.52)));
	Mesh roof;
	const double ridge = 7.0, eave = 4.7, half = 3.0, xa = -6.8, xb = 0.0;
	for (const double s : {-1.0, 1.0}) {
		const int base = static_cast<int>(roof.verts.size());
		for (const Vec3 &q : {Vec3{xa, eave, s * half}, Vec3{xb, eave, s * half}, Vec3{xb, ridge, 0.0}, Vec3{xa, ridge, 0.0}}) roof.verts.push_back(on(f, q)), roof.normals.push_back({0.0, 1.0, 0.0});
		roof.tris.push_back({base, base + 1, base + 2}), roof.tris.push_back({base, base + 2, base + 3});
	}
	sdf::computeNormals(roof);
	add(b.thatch, roof);
	for (const double z : {-half, half})   // (its gable ends)
		(void)z;
	add(b.pale, box(0.1, 0.1, 0.1), mul(f, translate(-3.4, 6.0, 0.0)));
}
inline std::string millWheel(const Place &p, double y, double t, double flow) {
	const Mat4 f = frameAt(p.x, y, p.z, p.facing);
	const double R = 2.6, W = 1.3, turn = t * flow / R * 57.3;
	const Mat4 wheel = mul(f, translate(1.1, 1.4, 0.0), rotate({1.0, 0.0, 0.0}, 0.0), rotate({0.0, 0.0, 1.0}, -turn));
	Mesh wood;
	add(wood, torus(R, 0.07, 44, 6), mul(wheel, translate(0.0, 0.0, 0.5 * W), rotate({1.0, 0.0, 0.0}, 90.0)));
	add(wood, torus(R, 0.07, 44, 6), mul(wheel, translate(0.0, 0.0, -0.5 * W), rotate({1.0, 0.0, 0.0}, 90.0)));
	for (int k = 0; k < 18; ++k) {
		const double a = k * 6.283 / 18.0;
		add(wood, box(0.5, 0.06, W), mul(wheel, translate((R + 0.1) * std::cos(a), (R + 0.1) * std::sin(a), 0.0), rotate({0.0, 0.0, 1.0}, a * 57.3)));
		if (k % 3 == 0)
			for (const double s : {-0.5, 0.5}) bar(wood, on(wheel, {0.0, 0.0, s * W}), on(wheel, {R * std::cos(a), R * std::sin(a), s * W}), 0.05);
	}
	add(wood, capsule(0.14, W + 1.4, 12, 3), mul(wheel, rotate({1.0, 0.0, 0.0}, 90.0), translate(0.0, -0.5 * (W + 1.4), 0.0)));
	std::string out = "# a mill wheel (film_waterside.h)\n" + attr(coated({0.2, 0.14, 0.09}, 0.22), emitMesh(wood, identity()));
	std::string drops;
	char buf[150];
	for (int k = 0; k < 60; ++k) {   // water falling off the wheel
		const double u = std::fmod(t * (1.0 + 0.3 * hash01(k, 1)) + hash01(k, 2), 1.0), a = 0.4 + 1.3 * hash01(k, 3), h = 1.4 + R * std::sin(a) - 4.5 * u * u * 0.8;
		if (h < 0.0) continue;
		const Vec3 q = on(f, {1.1 + R * std::cos(a), h, W * (hash01(k, 4) - 0.5)});
		std::snprintf(buf, sizeof buf, "  AttributeBegin\n    Translate %.4f %.4f %.4f\n    Shape \"sphere\" \"float radius\" [ %.4f ]\n  AttributeEnd\n", -q.x, q.y, q.z, 0.018 + 0.02 * hash01(k, 5));
		drops += buf;
	}
	return out + "AttributeBegin\n  Material \"dielectric\" \"float eta\" [ 1.33 ]\n" + drops + "AttributeEnd\n";
}
inline void stones(Bits &b, const Place &p, double y) {
	const double L = std::hypot(p.x1 - p.x, p.z1 - p.z);
	const int n = p.count > 0 ? p.count : std::max(2, static_cast<int>(L / 0.9));
	for (int k = 0; k < n; ++k) {
		const double u = (k + 0.5) / n, r = 0.32 + 0.12 * hash01(p.seed + k, 1);
		Mesh m = stream::detail::boulder(r, p.seed + k, 14, 7);
		for (Vec3 &q : m.verts) q.y = std::min(q.y, 0.14);
		sdf::computeNormals(m);
		add(b.stone, m, translate(-lerp(p.x, p.x1, u) + 0.2 * (hash01(p.seed + k, 2) - 0.5), y, lerp(p.z, p.z1, u) + 0.2 * (hash01(p.seed + k, 3) - 0.5)));
	}
}
inline void hut(Bits &b, Vec3 at, double facing, int seed) {
	const Mat4 f = frameAt(at.x, at.y, at.z, facing);
	const double floorY = 2.0 + 0.5 * hash01(seed, 1), W = 3.6 + hash01(seed, 2), D = 3.0 + 0.6 * hash01(seed, 3);
	for (const double x : {-0.45, 0.45})
		for (const double z : {-0.45, 0.45}) post(b.wood, f, {x * W, -2.5, z * D}, 2.5 + floorY, 0.08);
	add(b.wood, box(W + 0.8, 0.1, D + 1.0), mul(f, translate(0.0, floorY, 0.0)));
	add(b.pale, box(W * 0.86, 1.9, D * 0.86), mul(f, translate(0.0, floorY + 0.98, 0.0)));
	add(b.dark, box(0.8, 1.5, 0.05), mul(f, translate(0.0, floorY + 0.78, 0.44 * D)));
	Mesh roof;
	const double H = floorY + 1.9, half = 0.62 * D, top = H + 1.5;
	for (const double s : {-1.0, 1.0}) {
		const int base = static_cast<int>(roof.verts.size());
		for (const Vec3 &q : {Vec3{-0.62 * W, H, s * half}, Vec3{0.62 * W, H, s * half}, Vec3{0.62 * W, top, 0.0}, Vec3{-0.62 * W, top, 0.0}}) roof.verts.push_back(on(f, q)), roof.normals.push_back({0.0, 1.0, 0.0});
		roof.tris.push_back({base, base + 1, base + 2}), roof.tris.push_back({base, base + 2, base + 3});
	}
	sdf::computeNormals(roof);
	add(b.thatch, roof);
	for (const double s : {-0.3, 0.3}) bar(b.wood, on(f, {s, -0.2, 0.5 * D + 0.9}), on(f, {s, floorY, 0.5 * D + 0.45}), 0.03);
	for (double h = 0.2; h < floorY; h += 0.34) bar(b.wood, on(f, {-0.3, h, 0.5 * D + 0.9 - 0.45 * h / floorY}), on(f, {0.3, h, 0.5 * D + 0.9 - 0.45 * h / floorY}), 0.018);
}
}  // namespace detail

// What does not move, as a mesh in the film's cache would be: here as text for a frame's first use, via the caller's cache of it by place. `dark`: 0 by day to 1 at night (the lamps).
inline std::string staticText(const Place &p, double y, double t, double dark = 0.0) {
	detail::Bits b;
	std::string out = "# " + p.kind + " (film_waterside.h)\n";
	if (p.kind == "pier") detail::pier(b, p, y);
	else if (p.kind == "harbour") detail::harbour(b, p, y);
	else if (p.kind == "lighthouse") detail::lighthouse(b, p, y);
	else if (p.kind == "bridge") detail::bridge(b, p, y);
	else if (p.kind == "watermill") detail::mill(b, p, y);
	else if (p.kind == "steppingstones") detail::stones(b, p, y);
	else if (p.kind == "stilthouses") {
		for (int n = 0; n < std::max(1, p.count); ++n) {
			const int k = p.seed * 83 + n;
			const double a = 6.283 * n / std::max(1, p.count) + hash01(k, 1), d = p.radius * (0.4 + 0.6 * hash01(k, 2));
			detail::hut(b, {p.x + d * std::cos(a), y, p.z + d * std::sin(a)}, 360.0 * hash01(k, 3), k);
		}
	} else if (p.kind == "dam") {
		const double dx = p.x1 - p.x, dz = p.z1 - p.z, L = std::hypot(dx, dz), H = p.height > 0.0 ? p.height : 3.0;
		const Mat4 f = detail::frameAt(p.x, y, p.z, std::atan2(dx, dz) * 180.0 / kPi);
		const bool gone = p.burst >= 0.0 && t >= p.burst;
		for (int k = 0; k < 14; ++k) {
			const double z = L * (k + 0.5) / 14.0, u = (k + 0.5) / 14.0;
			if (gone && u > 0.34 && u < 0.66) continue;
			detail::add(b.stone, box(1.3, H + 3.0, L / 14.0 * 1.01), mul(f, translate(0.0, 0.5 * (H - 3.0), z)));
			detail::add(b.dark, box(1.32, 0.04, L / 14.0), mul(f, translate(0.0, 0.6 * H, z)));
		}
		if (gone) {
			for (int k = 0; k < 24; ++k) {   // what is left of its middle, thrown out with the water and tumbling
				const double since = t - p.burst, a = hash01(k, 1), out_ = (3.0 + 7.0 * hash01(k, 2)) * since, fall = H - 4.9 * since * since + 5.0 * hash01(k, 3) * since;
				if (fall < -1.0) continue;
				detail::add(b.stone, box(0.7, 0.5, 0.6), mul(f, translate(out_, fall, L * (0.36 + 0.28 * a)), rotate({1.0, 0.2, 0.4}, 300.0 * since * (0.5 + hash01(k, 4)))));
			}
		}
	}
	b.put(out, dark);
	return out;
}
// A lighthouse's lantern at t: its lamp, and a turning spot after dark. `dark`: 0 by day to 1 at night.
inline std::string lanternText(const Place &p, double y, double t, double dark) {
	const double H = p.height > 0.0 ? p.height : 24.0, ly = y + 0.875 * H;
	std::string out = emissiveSphere({-p.x, ly, p.z}, 0.04 * H, Vec3{70.0, 58.0, 34.0} * (0.05 + dark));
	if (dark <= 0.02) return out;
	char buf[300];
	for (int k = 0; k < 2; ++k) {
		const double a = 0.55 * t + k * kPi;
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"spot\" \"point3 from\" [ %.3f %.3f %.3f ] \"point3 to\" [ %.3f %.3f %.3f ] \"float coneangle\" [ 7 ] \"float conedeltaangle\" [ 2 ] \"rgb I\" [ %.1f %.1f %.1f ]\nAttributeEnd\n", p.x, ly, p.z, p.x + 100.0 * std::cos(a), ly - 7.0, p.z + 100.0 * std::sin(a), 90000.0 * dark, 80000.0 * dark, 50000.0 * dark);
		out += buf;
	}
	return out;
}

}  // namespace waterside
}  // namespace cartoon
