#pragma once
// pbrt_light_size.h -- lights with a size and lights the camera does not see, as Blender has them (our extensions to pbrt's format), made at load time out of the punctual lights
// every renderer already has, so they work on all of them and no renderer knows they exist.
//
//   LightSource "point" ... "float radius" R [ "integer samples" N ]         a light of that size: N lights (16) spread through its ball, each with its share of the intensity, so
//   LightSource "spot" ... "float radius" R [ "integer samples" N ]          its shadows are soft. A spot's are spread over a disk across its axis.
//   AreaLightSource "diffuse" ... "bool camera" false [ "integer samples" N ]   a panel that lights the scene but is not seen: its triangles are taken out and N wide soft spots (16)
//                                                                                spread over them instead, each with its share of L x area, facing the way the panel does
//
// What this is not: a light of a real size. N points are N shadows, close enough to read as one soft one under a denoiser, and a shiny surface shows N highlights where a real
// panel would show one shape. A hidden panel neither reflects in mirrors nor shows in glass. A `radius` much larger than the light's distance to what it lights needs more samples.

#include <algorithm>
#include <cmath>
#include <vector>

#include "pbrt_flat_scene_types.h"

namespace pbrt_flatten {

inline void spreadSizedLights(FlatScene &out) {
	const double kGolden = 3.14159265358979323846 * (3.0 - std::sqrt(5.0));
	std::vector<PunctualLight> lights;
	lights.reserve(out.punctualLights.size());
	for (const PunctualLight &pl : out.punctualLights) {
		const bool sized = pl.radius > 0.0 && (pl.kind == PunctualLightKind::Point || pl.kind == PunctualLightKind::Spot);
		if (!sized) {
			lights.push_back(pl);
			continue;
		}
		const int n = std::clamp(pl.spreadSamples, 1, 256);
		// (a spot's disk across its axis)
		double u[3] = {0, 0, 0}, v[3] = {0, 0, 0};
		const double *d = pl.dir;
		const double helper[3] = {std::abs(d[1]) > 0.9 ? 1.0 : 0.0, std::abs(d[1]) > 0.9 ? 0.0 : 1.0, 0.0};
		u[0] = d[1] * helper[2] - d[2] * helper[1], u[1] = d[2] * helper[0] - d[0] * helper[2], u[2] = d[0] * helper[1] - d[1] * helper[0];
		const double ul = std::max(1e-12, std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]));
		for (double &x : u) x /= ul;
		v[0] = d[1] * u[2] - d[2] * u[1], v[1] = d[2] * u[0] - d[0] * u[2], v[2] = d[0] * u[1] - d[1] * u[0];
		for (int k = 0; k < n; ++k) {
			PunctualLight one = pl;
			one.radius = 0.0;
			one.scale = pl.scale / n;
			const double a = k * kGolden;
			if (pl.kind == PunctualLightKind::Spot) {
				const double r = pl.radius * std::sqrt((k + 0.5) / n);   // (evenly over the disk)
				for (int c = 0; c < 3; ++c) one.pos[c] = pl.pos[c] + (u[c] * std::cos(a) + v[c] * std::sin(a)) * r;
			} else {
				const double y = 1.0 - 2.0 * (k + 0.5) / n, ring = std::sqrt(std::max(0.0, 1.0 - y * y)), r = pl.radius * 0.8;   // (a shell inside the ball: as bright all round as the point it replaces)
				one.pos[0] = pl.pos[0] + r * ring * std::cos(a);
				one.pos[1] = pl.pos[1] + r * y;
				one.pos[2] = pl.pos[2] + r * ring * std::sin(a);
			}
			lights.push_back(one);
		}
	}
	out.punctualLights = std::move(lights);
}

inline void hiddenPanelsToSpots(FlatScene &out) {
	std::vector<std::vector<const Triangle *>> panels(out.areaLights.size());
	bool any = false;
	for (const Triangle &t : out.triangles)
		if (t.areaLight >= 0 && static_cast<std::size_t>(t.areaLight) < out.areaLights.size() && !out.areaLights[static_cast<std::size_t>(t.areaLight)].camera) {
			panels[static_cast<std::size_t>(t.areaLight)].push_back(&t);
			any = true;
		}
	for (std::size_t e = 0; e < out.areaLights.size(); ++e) {
		const Emission &em = out.areaLights[e];
		if (em.camera || panels[e].empty()) continue;
		if (!em.filename.empty()) out.warnings.push_back({0, std::string(), "a hidden area light (\"bool camera\" false) with a picture lights as a flat colour: its L is used, the picture is not"});
		std::vector<double> area;
		double total = 0.0;
		const auto cross = [](const Triangle &t, double *n) {
			const double a[3] = {t.v[3] - t.v[0], t.v[4] - t.v[1], t.v[5] - t.v[2]}, b[3] = {t.v[6] - t.v[0], t.v[7] - t.v[1], t.v[8] - t.v[2]};
			n[0] = a[1] * b[2] - a[2] * b[1], n[1] = a[2] * b[0] - a[0] * b[2], n[2] = a[0] * b[1] - a[1] * b[0];
			return 0.5 * std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		};
		for (const Triangle *t : panels[e]) {
			double n[3];
			total += cross(*t, n);
			area.push_back(total);
		}
		const int count = std::clamp(em.spreadSamples, 1, 256);
		for (int k = 0; k < count; ++k) {
			const double pick = (k + 0.5) / count * total;
			const std::size_t at = std::min<std::size_t>(std::lower_bound(area.begin(), area.end(), pick) - area.begin(), panels[e].size() - 1);
			const Triangle &t = *panels[e][at];
			double r1 = std::fmod(0.7548776662 * (k + 1), 1.0), r2 = std::fmod(0.5698402910 * (k + 1), 1.0);
			if (r1 + r2 > 1.0) r1 = 1.0 - r1, r2 = 1.0 - r2;
			PunctualLight pl;
			pl.kind = PunctualLightKind::Spot;
			double n[3];
			cross(t, n);
			const double nl = std::max(1e-12, std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]));
			for (int c = 0; c < 3; ++c) {
				pl.pos[c] = t.v[c] + (t.v[3 + c] - t.v[c]) * r1 + (t.v[6 + c] - t.v[c]) * r2;
				pl.dir[c] = n[c] / nl;
				pl.intensity[c] = em.L[c] * total / count;
			}
			pl.scale = em.scale;
			pl.coneAngleDeg = 89.0, pl.falloffStartAngleDeg = 0.0;
			pl.illuminates = em.illuminates, pl.lightGroup = em.lightGroup;
			out.punctualLights.push_back(pl);
			if (em.twoSided) {
				for (double &x : pl.dir) x = -x;
				out.punctualLights.push_back(pl);
			}
		}
	}
	if (any)
		out.triangles.erase(std::remove_if(out.triangles.begin(), out.triangles.end(), [&](const Triangle &t) {
			return t.areaLight >= 0 && static_cast<std::size_t>(t.areaLight) < out.areaLights.size() && !out.areaLights[static_cast<std::size_t>(t.areaLight)].camera;
		}), out.triangles.end());
}

}  // namespace pbrt_flatten
