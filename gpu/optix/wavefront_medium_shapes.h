#pragma once
// wavefront_medium_shapes.h -- which spheres of a GPU scene are the volumes the wavefront renderer samples along each ray's flight (GpuMediumShape,
// wavefront_types.h): OptiXRenderer::uploadMediumShapes() uploads them, and a scene with the camera or a surface inside one is drawn by the wavefront renderer
// (SceneData::needsWavefront, scene_builder.cpp), since the default renderer still samples a volume only where a ray meets its boundary.
#include <algorithm>
#include <cmath>
#include <vector>
#include "optix_types.h"
#include "wavefront_types.h"

namespace wavefront_media {

// Whether a top-level sphere is such a volume: a sphere or box whose material is a fog, smoke or cloud (Medium, CloudMedium, RgbGridMedium,
// GridMedium - not DielectricMedium, a glass ball holding one, whose surface comes first), neither clipped nor moving (those keep the boundary-hit
// sampling). A static sphere's center1 is left zero or equal to its centre.
inline bool listed(const SphereData& s, const std::vector<MaterialData>& materials) {
	if (s.materialIdx < 0 || static_cast<size_t>(s.materialIdx) >= materials.size()) return false;
	const MaterialType t = materials[static_cast<size_t>(s.materialIdx)].type;
	if (t != MaterialType::Medium && t != MaterialType::CloudMedium && t != MaterialType::RgbGridMedium && t != MaterialType::GridMedium) return false;
	if (s.shapeKind == GpuMediumShapeKind::ClippedSphere) return false;
	const bool c1Zero = s.center1.x == 0.0f && s.center1.y == 0.0f && s.center1.z == 0.0f;
	const bool c1Same = s.center1.x == s.center.x && s.center1.y == s.center.y && s.center1.z == s.center.z;
	return c1Zero || c1Same;
}

// The grid media's tables (SceneData's), for each grid volume's own box (GpuMediumShape::clip). Any may be null.
struct GridTables {
	const std::vector<CloudMedium<float>>* clouds = nullptr;
	const std::vector<GpuRgbGridMedium>* rgbGrids = nullptr;
	const std::vector<GpuGridMedium>* grids = nullptr;
};

// A procedural cloud's box is in its own space (CloudMedium: world_to_medium(p) = mat * p + translate): its eight corners carried to the world, and the box round them.
inline bool cloudWorldBox(const CloudMedium<float>& c, float3& lo, float3& hi) {
	const float* m = c.mat;
	const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
	if (!(std::abs(det) > 1e-20)) return false;
	const double inv[9] = {(m[4] * m[8] - m[5] * m[7]) / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
						   (m[5] * m[6] - m[3] * m[8]) / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
						   (m[3] * m[7] - m[4] * m[6]) / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
	double l[3] = {1e30, 1e30, 1e30}, h[3] = {-1e30, -1e30, -1e30};
	for (int k = 0; k < 8; ++k) {
		const double p[3] = {(k & 1 ? c.bounds_max[0] : c.bounds_min[0]) - c.translate[0], (k & 2 ? c.bounds_max[1] : c.bounds_min[1]) - c.translate[1],
							 (k & 4 ? c.bounds_max[2] : c.bounds_min[2]) - c.translate[2]};
		for (int r = 0; r < 3; ++r) {
			const double w = inv[3 * r] * p[0] + inv[3 * r + 1] * p[1] + inv[3 * r + 2] * p[2];
			l[r] = std::min(l[r], w);
			h[r] = std::max(h[r], w);
		}
	}
	lo = make_float3(static_cast<float>(l[0]), static_cast<float>(l[1]), static_cast<float>(l[2]));
	hi = make_float3(static_cast<float>(h[0]), static_cast<float>(h[1]), static_cast<float>(h[2]));
	return true;
}

// The world-space box of the grid a medium material holds, if it is a grid medium and its table is at hand.
inline bool gridBox(const MaterialData& m, const GridTables& g, float3& lo, float3& hi) {
	const float* bmin = nullptr;
	const float* bmax = nullptr;
	if (m.type == MaterialType::CloudMedium && g.clouds) {
		const int i = static_cast<int>(m.cloud_medium_extra.cloudMediumIdx);
		return i >= 0 && static_cast<size_t>(i) < g.clouds->size() && cloudWorldBox((*g.clouds)[static_cast<size_t>(i)], lo, hi);
	} else if (m.type == MaterialType::RgbGridMedium && g.rgbGrids) {
		const int i = static_cast<int>(m.rgb_grid_medium_extra.rgbGridMediumIdx);
		if (i >= 0 && static_cast<size_t>(i) < g.rgbGrids->size()) bmin = (*g.rgbGrids)[static_cast<size_t>(i)].bounds_min, bmax = (*g.rgbGrids)[static_cast<size_t>(i)].bounds_max;
	} else if (m.type == MaterialType::GridMedium && g.grids) {
		const int i = static_cast<int>(m.grid_medium_extra.gridMediumIdx);
		if (i >= 0 && static_cast<size_t>(i) < g.grids->size()) bmin = (*g.grids)[static_cast<size_t>(i)].bounds_min, bmax = (*g.grids)[static_cast<size_t>(i)].bounds_max;
	}
	if (!bmin || !(bmin[0] <= bmax[0] && bmin[1] <= bmax[1] && bmin[2] <= bmax[2])) return false;
	lo = make_float3(bmin[0], bmin[1], bmin[2]);
	hi = make_float3(bmax[0], bmax[1], bmax[2]);
	return true;
}

// The listed volumes among a scene's top-level spheres, and (isListed) a 1 for each sphere that is one. With the grid tables, a grid volume's flight is also cut to its box.
inline std::vector<GpuMediumShape> shapes(const std::vector<SphereData>& spheres, const std::vector<MaterialData>& materials,
										  std::vector<unsigned char>* isListed = nullptr, const GridTables& grids = GridTables()) {
	std::vector<GpuMediumShape> out;
	if (isListed) isListed->assign(spheres.size(), 0);
	for (size_t i = 0; i < spheres.size(); ++i) {
		const SphereData& s = spheres[i];
		if (!listed(s, materials)) continue;
		GpuMediumShape m{};
		m.isBox = s.shapeKind == GpuMediumShapeKind::Box ? 1 : 0;
		m.center = s.center;
		m.radius = s.radius;
		m.boxMin = s.boxMin;
		m.boxMax = s.boxMax;
		m.materialIdx = s.materialIdx;
		m.clip = gridBox(materials[static_cast<size_t>(s.materialIdx)], grids, m.clipMin, m.clipMax) ? 1 : 0;
		out.push_back(m);
		if (isListed) (*isListed)[i] = 1;
	}
	return out;
}

}  // namespace wavefront_media
