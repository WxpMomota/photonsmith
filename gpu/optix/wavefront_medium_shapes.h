#pragma once
// wavefront_medium_shapes.h -- which spheres of a GPU scene are the volumes the wavefront renderer samples along each ray's flight (GpuMediumShape,
// wavefront_types.h): OptiXRenderer::uploadMediumShapes() uploads them, and a scene with the camera or a surface inside one is drawn by the wavefront renderer
// (SceneData::needsWavefront, scene_builder.cpp), since the default renderer still samples a volume only where a ray meets its boundary.
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

// The listed volumes among a scene's top-level spheres, and (isListed) a 1 for each sphere that is one.
inline std::vector<GpuMediumShape> shapes(const std::vector<SphereData>& spheres, const std::vector<MaterialData>& materials,
										  std::vector<unsigned char>* isListed = nullptr) {
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
		out.push_back(m);
		if (isListed) (*isListed)[i] = 1;
	}
	return out;
}

}  // namespace wavefront_media
