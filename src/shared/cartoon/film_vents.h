#pragma once
// film_vents.h -- round 16: hydrothermal vents in the deep.
//
//   vents N in X0 Z0 X1 Z1 [seed K]
//            black smokers: chimneys of rough mineral rock two to six metres tall (dark, crusted yellow and rust towards the top), each pouring a black plume that billows
//            up and spreads (an absorbing grid in its own sphere, the sea outside it: the GPU's wavefront renderer draws it); the water just over the mouth shimmers in the
//            heat (a sheet that bends the light a little and casts no shadow); round the foot, thickets of tube worms - white tubes, red plumes swaying out of them - with
//            pale crabs walking among them and a swarm of shrimp crawling over the rock. Film it deep and dark, with a lamp.
//
// Coordinates: pbrt's (x, z) in the film file, on the seabed there; meshes in story space (x mirrored), the chimneys made once as PLY files (film_cache.h).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_deepsea.h"
#include "film_fire.h"
#include "film_seabed.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace vents {

struct Vents {
	int count = 3, seed = 1;
	double x0 = -5.0, z0 = -5.0, x1 = 5.0, z1 = 5.0;
};

// Vent i: where it stands (pbrt's, on the seabed), how tall and how wide at its foot.
inline Vec3 ventAt(const Vents &v, int i, const std::function<double(double, double)> &groundAt, double &height, double &base) {
	const int s = v.seed * 577 + i;
	const double x = v.x0 + (v.x1 - v.x0) * hash01(s, 1), z = v.z0 + (v.z1 - v.z0) * hash01(s, 2);
	height = 2.0 + 4.0 * hash01(s, 3), base = 0.6 + 0.6 * hash01(s, 4);
	return {x, groundAt(x, z), z};
}
// A chimney's rock (local: its foot at the origin, up +y): a lumpy tapering stack with a mouth at the top, crusted.
inline Mesh chimneyMesh(double H, double R, int salt) {
	const auto f = [H, R, salt](Vec3 q) {
		const double u = std::clamp(q.y / H, 0.0, 1.0);
		const double r = R * (1.0 - 0.65 * u) * (1.0 + 0.18 * (nature::noise2(q.y * 2.3 / R, std::atan2(q.z, q.x) * 1.7, salt) - 0.5));
		const double side = std::hypot(q.x, q.z) - r;
		const double mouth = std::hypot(q.x, q.z) - 0.18 * R;   // (the hole down its top)
		return sdf::smax(sdf::smax(side, std::max(-q.y - 0.1, q.y - H), 0.15 * R), -std::max(mouth, H - 0.25 * R - q.y), 0.05 * R);
	};
	Mesh m = sdf::surfaceNets(f, {-1.4 * R, -0.2, -1.4 * R}, {1.4 * R, H + 0.3, 1.4 * R}, std::max(0.04, R / 14.0));
	sdf::computeNormals(m);
	return m;
}
// A black plume pouring from a mouth at pbrt's `top` at t: billowing up `rise` metres and spreading (an rgbgrid medium: it absorbs; the sea round it).
inline std::string plumeText(const std::string &name, Vec3 top, double rise, double t, int seed) {
	const int nx = 14, ny = 28, nz = 14;
	const double width = 0.5 * rise;
	std::string sa, ss;
	char buf[64];
	for (int k = 0; k < nz; ++k)
		for (int j = 0; j < ny; ++j)
			for (int i = 0; i < nx; ++i) {
				const double x = (i + 0.5) / nx - 0.5, y = (j + 0.5) / ny, z = (k + 0.5) / nz - 0.5;
				const double r = std::hypot(x, z), R = 0.04 + 0.42 * std::pow(y, 0.7);
				const double billow = fire::detail::fbm3(x * 5.0, y * 6.0 - t * 0.35, z * 5.0, seed);
				const double d = std::max(0.0, smoothstep(R, R * 0.3, r * (0.6 + 0.8 * billow)) * (1.0 - smoothstep(0.7, 1.0, y)) * (1.2 - 0.6 * y));
				std::snprintf(buf, sizeof buf, "%.3f %.3f %.3f ", 9.0 * d, 9.0 * d, 9.5 * d);
				sa += buf;
				std::snprintf(buf, sizeof buf, "%.3f %.3f %.3f ", 0.4 * d, 0.38 * d, 0.36 * d);
				ss += buf;
			}
	char head[600];
	std::snprintf(head, sizeof head,
				  "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Scale %.4f %.4f %.4f\n  MakeNamedMedium \"%s\" \"string type\" [ \"rgbgrid\" ] \"integer nx\" [ %d ] \"integer ny\" [ %d ] \"integer nz\" [ %d ] \"float g\" [ 0.3 ]\n",
				  top.x - 0.5 * width, top.y, top.z - 0.5 * width, width, rise, width, name.c_str(), nx, ny, nz);
	std::string out = head + ("    \"rgb sigma_a\" [ " + sa + "]\n    \"rgb sigma_s\" [ " + ss + "]\nAttributeEnd\n");
	std::snprintf(head, sizeof head, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Material \"interface\"\n  MediumInterface \"%s\" \"sea\"\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", top.x,
				  top.y + 0.5 * rise, top.z, name.c_str(), 0.5 * std::sqrt(2.0 * width * width + rise * rise) * 1.02);
	return out + head;
}
// The heat's shimmer over a mouth (pbrt's `top`): a slowly writhing column of water that bends the light a little, casting no shadow.
inline std::string shimmerText(Vec3 top, double R, double t) {
	Mesh m;
	const int around = 24, up = 12;
	const double H = 1.4;
	for (int j = 0; j <= up; ++j)
		for (int i = 0; i <= around; ++i) {
			const double a = 2.0 * kPi * i / around, y = H * j / up;
			const double r = R * (0.5 + 0.3 * y / H) * (1.0 + 0.15 * std::sin(5.0 * a + 4.0 * y - 3.0 * t) + 0.1 * std::sin(3.0 * a - 6.0 * y + 2.2 * t));
			m.verts.push_back({-(top.x + r * std::cos(a)), top.y + 0.05 + y, top.z + r * std::sin(a)});
			m.normals.push_back({-std::cos(a), 0.0, std::sin(a)});
		}
	for (int j = 0; j < up; ++j)
		for (int i = 0; i < around; ++i) {
			const int a = j * (around + 1) + i;
			m.tris.push_back({a, a + around + 1, a + 1}), m.tris.push_back({a + 1, a + around + 1, a + around + 2});
		}
	sdf::computeNormals(m);
	return attr("Material \"dielectric\" \"float eta\" [ 1.015 ]", undersea::detail::noShadow(emitMesh(m, identity())));
}
// The vents at t, as scene text.
inline std::string text(const std::string &film, const Vents &v, int index, double t, const std::function<double(double, double)> &groundAt, double level = 1e30) {
	std::string out = "# hydrothermal vents (film_vents.h)\n";
	Mesh worms, plumes;
	std::vector<Vec3> shrimp;
	for (int i = 0; i < v.count; ++i) {
		double H = 0.0, R = 0.0;
		const Vec3 foot = ventAt(v, i, groundAt, H, R);
		char name[64];
		std::snprintf(name, sizeof name, "vent_%d_%d_%d", index, v.seed, i);
		const Mat4 place = translate(sealife::detail::story(foot));
		out += film_cache::part(film, name, coated({0.11, 0.1, 0.09}, 0.8), place, [&] { return chimneyMesh(H, R, v.seed * 31 + i); });
		Mesh crust = lathe({{0.38 * R, H - 0.35 * R}, {0.42 * R, H - 0.1 * R}, {0.3 * R, H + 0.02}, {0.2 * R, H + 0.02}}, 18);
		crust.uvs.clear();
		out += attr(coated({0.55, 0.42, 0.15}, 0.7), emitMesh(crust, place));   // (the crust of minerals round its mouth)
		const Vec3 top = foot + Vec3{0.0, H, 0.0};
		const double rise = std::min(6.0 + 2.0 * H, 0.8 * (level - top.y));   // (spreading out before the surface, in a shallow sea)
		if (rise > 0.5) out += plumeText("vent_plume_" + std::to_string(index) + "_" + std::to_string(i), top, rise, t, v.seed * 7 + i);
		out += shimmerText(top, 0.25 * R, t);
		for (int k = 0; k < 40; ++k) {   // tube worms round its foot, their red plumes swaying out of the tubes
			const int s = i * 113 + k + v.seed * 7;
			const double a = 2.0 * kPi * hash01(s, 1), r = R * (1.1 + 0.9 * hash01(s, 2)), h = 0.4 + 1.0 * hash01(s, 3);
			const Vec3 base{foot.x + r * std::cos(a), groundAt(foot.x + r * std::cos(a), foot.z + r * std::sin(a)), foot.z + r * std::sin(a)};
			const Vec3 lean{0.12 * std::cos(a + 0.5), 1.0, 0.12 * std::sin(a + 0.5)};
			const Vec3 tip = base + normalize(lean) * h;
			deepsea::detail::add(worms, sdf::tubeMesh({sealife::detail::story(base), sealife::detail::story(base + normalize(lean) * (0.5 * h)), sealife::detail::story(tip)}, 0.022, 0.9, 6));
			for (int q = 0; q < 6; ++q) {   // (the plume: a tuft of red filaments)
				const double b = 2.0 * kPi * q / 6.0, sway = 0.03 * std::sin(1.4 * t + s + q);
				deepsea::detail::add(plumes, sdf::tubeMesh({sealife::detail::story(tip), sealife::detail::story(tip + Vec3{0.05 * std::cos(b) + sway, 0.07, 0.05 * std::sin(b)})}, 0.009, 0.3, 4));
			}
		}
		for (int k = 0; k < 160; ++k) {   // shrimp crawling over the rock
			const int s = i * 977 + k;
			const double y = H * hash01(s, 1), a = 2.0 * kPi * hash01(s, 2) + 0.05 * t * (hash01(s, 3) - 0.5);
			const double r = R * (1.0 - 0.65 * y / H) + 0.02;
			shrimp.push_back(sealife::detail::story(foot + Vec3{r * std::cos(a), y + 0.02 * std::sin(t + k), r * std::sin(a)}));
		}
		seabed::Patch crabs;
		crabs.kind = "crabs", crabs.count = 5, crabs.seed = v.seed + i;
		crabs.x0 = foot.x - 2.0 * R, crabs.x1 = foot.x + 2.0 * R, crabs.z0 = foot.z - 2.0 * R, crabs.z1 = foot.z + 2.0 * R;
		out += seabed::crabText(crabs, t, groundAt, true);
	}
	out += attr(coated({0.92, 0.9, 0.85}, 0.4), emitMesh(worms, identity())) + attr(coated({0.75, 0.06, 0.05}, 0.3), emitMesh(plumes, identity()));
	if (!shrimp.empty()) out += attr(coated({0.85, 0.8, 0.72}, 0.4), emitMesh(deepsea::detail::beads(shrimp, 0.01), identity()));
	return out;
}

}  // namespace vents
}  // namespace cartoon
