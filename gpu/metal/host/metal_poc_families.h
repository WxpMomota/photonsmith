// metal_poc_families.h -- which of the rarely used material families a pipeline is built with.
//
// Compiling the Metal kernel takes 127 s with every family and 17.6 s without twelve of them (docs/METAL_BACKEND.md, "first run"). primaryRayKernel has one function constant per group
// (kFcLayered ... kFcExtras, metal_poc_kernel_bounce.metal); createKernelPipeline() sets them from a FamilySet, and a FamilySet comes from the materials a scene has. Plain C++ (no Metal), so
// the host tests cover the mapping. A family left out for a scene that uses it would be drawn as a plain Lambertian, so the rule is: when in doubt, include it (a Mix material hides the
// families of the two it mixes, and a Live Preview session can change any material later: both get everything).
#pragma once

#include <cstdint>
#include <vector>

#include "../shaders/metal_poc_material_ids.metal"

struct FamilySet {
	bool layered = false;      // coated diffuse, coated conductor, image clearcoat, normalized Fresnel
	bool measured = false;
	bool subsurface = false;
	bool hair = false;
	bool extras = false;       // principled, Oren-Nayar, velvet, diffuse transmission, clearcoat

	static FamilySet all() { FamilySet s; s.layered = s.measured = s.subsurface = s.hair = s.extras = true; return s; }
	static FamilySet none() { return FamilySet(); }
	bool isAll() const { return layered && measured && subsurface && hair && extras; }
	bool isNone() const { return !layered && !measured && !subsurface && !hair && !extras; }
	bool operator==(const FamilySet& o) const { return layered == o.layered && measured == o.measured && subsurface == o.subsurface && hair == o.hair && extras == o.extras; }

	// Adds the family a materialType belongs to (nothing for the common ones). A Mix material adds everything: its two sub-materials are stored apart from the scene's material arrays.
	void add(uint32_t materialType) {
		switch (materialType) {
			case METAL_MAT_COATED_DIFFUSE: case METAL_MAT_COATED_CONDUCTOR: case METAL_MAT_IMAGE_CLEARCOAT: case METAL_MAT_NORMALIZED_FRESNEL: layered = true; break;
			case METAL_MAT_MEASURED: measured = true; break;
			case METAL_MAT_SUBSURFACE: subsurface = true; break;
			case METAL_MAT_HAIR: hair = true; break;
			case METAL_MAT_PRINCIPLED: case METAL_MAT_OREN_NAYAR: case METAL_MAT_VELVET: case METAL_MAT_DIFFUSE_TRANSMISSION: case METAL_MAT_CLEARCOAT: extras = true; break;
			case METAL_MAT_MIX: *this = all(); break;
			default: break;
		}
	}

	// `materials` is any vector of structs with a materialType member (TriangleMaterial).
	template <class Material>
	void addAll(const std::vector<Material>& materials) { for (const Material& m : materials) add(m.materialType); }
};
