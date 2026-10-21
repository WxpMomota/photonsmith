#pragma once
// gpu_scene_inventory.h -- how many of each thing a GPU backend should end up with for a scene: the one list both backends' builders are checked against.
//
// Both GPU backends read the same FlatScene and each builds its own arrays from it (gpu/optix/pbrt_gpu_builder.h, gpu/metal/host/metal_poc_pbrt_loader.mm). What each one keeps,
// drops or turns into something else is decided separately in both, and the parity sweeps only notice a disagreement that changes a picture. This counts, from the FlatScene and the
// backend's TessellationCaps alone, what there should be: the triangles after the shapes the backend cannot draw are tessellated (gpu_tessellate.h), the analytic shapes, the object
// instances, the lights by kind and the media. A test per backend builds its arrays for the same scene and compares them with this (Metal: gpu/metal/tests/metal_poc_loader_tests.mm;
// OptiX: tests/unit/gpu_scene_inventory_optix_tests.cpp, the Windows session's half), so a shape or a light kind that one side drops, or counts twice, fails there the day it is written.
//
// What is counted is deliberately what a builder can look at in its own arrays, no more:
//   * instancedTriangles / instancedSpheres: what the placements add if they are written out one by one (Metal does that for spheres and shares the group's triangles; a backend states which);
//   * triangles: the scene's triangles after tessellation, without those whose material is an "interface" (they only bound a medium and no backend draws them);
//   * groupTriangles / groupSpheres: what the object-instance groups hold once; instances is how many placements there are;
//   * spheres / disks / cylinders: the analytic shapes the backend keeps native, whatever their material (a medium-bounding one included);
//   * bilinearPatches / curves / cones / paraboloids: the shapes the caps left alone (zero where the backend tessellates them);
//   * punctual lights by kind, the image infinite light count (the main light and every extra portal) and the media.
// Standard library only: the CPU unit tests reach it.

#include "gpu_tessellate.h"
#include "pbrt_flatten.h"

namespace gpu_scene_inventory {

struct Inventory {
	int triangles = 0;
	int groupTriangles = 0;
	int groupSpheres = 0;
	int instances = 0;
	int instancedTriangles = 0;   // the triangles of every placement if each were written out (a backend that expands instances ends with triangles + this)
	int instancedSpheres = 0;     // the same for spheres
	int spheres = 0, disks = 0, cylinders = 0;
	int bilinearPatches = 0, curves = 0, cones = 0, paraboloids = 0;
	int pointLights = 0, spotLights = 0, distantLights = 0, goniometricLights = 0, projectionLights = 0;
	int infiniteLights = 0;   // the scene's infinite light (when there is one) and every extra portal light
	int media = 0;
	bool cameraMedium = false;

	bool operator==(const Inventory& o) const {
		return triangles == o.triangles && groupTriangles == o.groupTriangles && groupSpheres == o.groupSpheres && instances == o.instances && instancedTriangles == o.instancedTriangles &&
		       instancedSpheres == o.instancedSpheres && spheres == o.spheres &&
		       disks == o.disks && cylinders == o.cylinders && bilinearPatches == o.bilinearPatches && curves == o.curves && cones == o.cones && paraboloids == o.paraboloids &&
		       pointLights == o.pointLights && spotLights == o.spotLights && distantLights == o.distantLights && goniometricLights == o.goniometricLights &&
		       projectionLights == o.projectionLights && infiniteLights == o.infiniteLights && media == o.media && cameraMedium == o.cameraMedium;
	}
};

inline bool isInterface(const pbrt_flatten::FlatScene& scene, int material) {
	return material >= 0 && material < static_cast<int>(scene.materials.size()) && scene.materials[static_cast<std::size_t>(material)].kind == pbrt_flatten::MaterialKind::Interface;
}

// The inventory of `scene` for a backend with these caps. Works on a copy (the tessellation rewrites the scene it is given).
inline Inventory inventory(const pbrt_flatten::FlatScene& source, const gpu_tessellate::TessellationCaps& caps) {
	pbrt_flatten::FlatScene scene = source;
	gpu_tessellate::tessellateForBackend(scene, caps, nullptr);
	Inventory inv;
	for (const pbrt_flatten::Triangle& t : scene.triangles)
		if (!isInterface(scene, t.material)) ++inv.triangles;
	for (const pbrt_flatten::InstanceGroup& g : scene.groups) {
		inv.groupTriangles += static_cast<int>(g.triangles.size());
		inv.groupSpheres += static_cast<int>(g.spheres.size());
	}
	inv.instances = static_cast<int>(scene.instances.size());
	for (const pbrt_flatten::Instance& i : scene.instances) {
		if (i.group < 0 || i.group >= static_cast<int>(scene.groups.size())) continue;
		inv.instancedTriangles += static_cast<int>(scene.groups[static_cast<std::size_t>(i.group)].triangles.size());
		inv.instancedSpheres += static_cast<int>(scene.groups[static_cast<std::size_t>(i.group)].spheres.size());
	}
	inv.spheres = static_cast<int>(scene.spheres.size());
	inv.disks = static_cast<int>(scene.disks.size());
	inv.cylinders = static_cast<int>(scene.cylinders.size());
	inv.bilinearPatches = static_cast<int>(scene.bilinearPatches.size());
	inv.curves = static_cast<int>(scene.curves.size());
	inv.cones = static_cast<int>(scene.cones.size());
	inv.paraboloids = static_cast<int>(scene.paraboloids.size());
	for (const pbrt_flatten::PunctualLight& p : scene.punctualLights) {
		switch (p.kind) {
			case pbrt_flatten::PunctualLightKind::Point: ++inv.pointLights; break;
			case pbrt_flatten::PunctualLightKind::Spot: ++inv.spotLights; break;
			case pbrt_flatten::PunctualLightKind::Distant: ++inv.distantLights; break;
			case pbrt_flatten::PunctualLightKind::Goniometric: ++inv.goniometricLights; break;
			case pbrt_flatten::PunctualLightKind::Projection: ++inv.projectionLights; break;
		}
	}
	inv.infiniteLights = (scene.infiniteLight.present ? 1 : 0) + static_cast<int>(scene.extraPortalLights.size());
	inv.media = static_cast<int>(scene.media.size());
	inv.cameraMedium = scene.cameraMediumIndex >= 0;
	return inv;
}

}  // namespace gpu_scene_inventory
