#pragma once
// scene_materials.h -- ready-made materials for the Scene Builder: a name, and the kind/colour/roughness/index of refraction that makes it look like gold, frosted
// glass, red plastic, wax... Choosing one in the inspector sets those fields on the object's material (a preset is not remembered afterwards: the object keeps
// ordinary material values that can be tuned further). Standard-library only, like the rest of the document.
//
// Metal colours are the reflectance at normal incidence (F0) of the real metals, linear RGB; glass and water use their real indices of refraction.

#include "scene_model.h"

#include <string>
#include <vector>

namespace scene_doc {

struct MaterialPreset {
	const char* id;     // stable, never shown; the name below is what a user sees (translated by the GUI)
	const char* name;
	const char* group;  // "Matte", "Plastic", "Metal", "Glass", "Translucent", "Subsurface"
	Material material;
};

namespace presets_detail {
inline Material make(MaterialKind kind, Rgb color, double roughness = 0.0, double ior = 1.5, Rgb transmittance = {0.5, 0.5, 0.5}) {
	Material m;
	m.kind = kind;
	m.color = color;
	m.roughness = roughness;
	m.ior = ior;
	m.transmittance = transmittance;
	return m;
}
// A subsurface material: `color` is the overall colour, `distance` how far each colour travels under the surface (scene units: tuned for an object about two units across).
inline Material subsurface(Rgb color, Rgb distance, double ior, double roughness) {
	Material m;
	m.kind = MaterialKind::Subsurface;
	m.color = color;
	m.scatterDistance = distance;
	m.ior = ior;
	m.roughness = roughness;
	return m;
}
}  // namespace presets_detail

inline const std::vector<MaterialPreset>& materialPresets() {
	using namespace presets_detail;
	static const std::vector<MaterialPreset> all = {
		{"chalk", "Chalk", "Matte", make(MaterialKind::Diffuse, {0.88, 0.88, 0.85})},
		{"rubber", "Black rubber", "Matte", make(MaterialKind::Diffuse, {0.04, 0.04, 0.045})},
		{"clay", "Terracotta", "Matte", make(MaterialKind::Diffuse, {0.62, 0.30, 0.18})},
		{"concrete", "Concrete", "Matte", make(MaterialKind::Diffuse, {0.48, 0.48, 0.47})},
		{"red-plastic", "Red plastic", "Plastic", make(MaterialKind::CoatedDiffuse, {0.70, 0.04, 0.04}, 0.08, 1.5)},
		{"blue-plastic", "Blue plastic", "Plastic", make(MaterialKind::CoatedDiffuse, {0.05, 0.12, 0.65}, 0.08, 1.5)},
		{"ceramic", "White ceramic", "Plastic", make(MaterialKind::CoatedDiffuse, {0.90, 0.90, 0.88}, 0.02, 1.55)},
		{"car-paint", "Car paint", "Plastic", make(MaterialKind::CoatedDiffuse, {0.45, 0.02, 0.06}, 0.03, 1.6)},
		{"gold", "Gold", "Metal", make(MaterialKind::Conductor, {1.000, 0.766, 0.336}, 0.08)},
		{"copper", "Copper", "Metal", make(MaterialKind::Conductor, {0.955, 0.638, 0.538}, 0.10)},
		{"silver", "Silver", "Metal", make(MaterialKind::Conductor, {0.972, 0.960, 0.915}, 0.05)},
		{"aluminium", "Aluminium", "Metal", make(MaterialKind::Conductor, {0.913, 0.922, 0.924}, 0.12)},
		{"chrome", "Chrome", "Metal", make(MaterialKind::Conductor, {0.550, 0.556, 0.554}, 0.02)},
		{"brushed-steel", "Brushed steel", "Metal", make(MaterialKind::Conductor, {0.62, 0.62, 0.64}, 0.35)},
		{"glass", "Clear glass", "Glass", make(MaterialKind::Dielectric, {0.8, 0.8, 0.8}, 0.0, 1.5)},
		{"frosted-glass", "Frosted glass", "Glass", make(MaterialKind::Dielectric, {0.8, 0.8, 0.8}, 0.25, 1.5)},
		{"water", "Water", "Glass", make(MaterialKind::Dielectric, {0.8, 0.8, 0.8}, 0.0, 1.33)},
		{"diamond", "Diamond", "Glass", make(MaterialKind::Dielectric, {0.8, 0.8, 0.8}, 0.0, 2.42)},
		{"wax", "Wax", "Translucent", make(MaterialKind::DiffuseTransmission, {0.70, 0.62, 0.40}, 0.0, 1.5, {0.40, 0.34, 0.16})},
		{"leaf", "Leaf", "Translucent", make(MaterialKind::DiffuseTransmission, {0.10, 0.38, 0.07}, 0.0, 1.5, {0.12, 0.34, 0.05})},
		{"paper", "Paper", "Translucent", make(MaterialKind::DiffuseTransmission, {0.78, 0.78, 0.74}, 0.0, 1.5, {0.45, 0.45, 0.42})},
		// Light goes into these and comes out elsewhere, softening the shading (the red of skin travels furthest).
		{"skin", "Skin", "Subsurface", subsurface({0.80, 0.58, 0.48}, {0.140, 0.055, 0.036}, 1.4, 0.35)},
		{"milk", "Milk", "Subsurface", subsurface({0.92, 0.90, 0.82}, {0.105, 0.085, 0.060}, 1.33, 0.15)},
		{"marble-stone", "Marble stone", "Subsurface", subsurface({0.88, 0.86, 0.82}, {0.060, 0.050, 0.040}, 1.5, 0.05)},
		{"jade", "Jade", "Subsurface", subsurface({0.30, 0.62, 0.42}, {0.090, 0.036, 0.066}, 1.6, 0.10)},
		{"candle-wax", "Candle wax", "Subsurface", subsurface({0.90, 0.78, 0.52}, {0.105, 0.066, 0.036}, 1.45, 0.20)},
	};
	return all;
}

inline const MaterialPreset* findMaterialPreset(const std::string& id) {
	for (const MaterialPreset& p : materialPresets())
		if (id == p.id) return &p;
	return nullptr;
}

// Gives `m` the preset's kind and values. The picture and the checker pattern are dropped (they would replace the preset's colour); everything else about the
// material is the preset's. Returns false for an unknown id.
inline bool applyMaterialPreset(Material& m, const std::string& id) {
	const MaterialPreset* p = findMaterialPreset(id);
	if (!p) return false;
	const std::string bumpFile = m.bumpFile;   // (a bump slot is the surface's relief, not its colour: it stays)
	const BumpKind bumpKind = m.bumpKind;
	const double bumpStrength = m.bumpStrength;
	const double uvScaleU = m.uvScaleU, uvScaleV = m.uvScaleV, uvOffsetU = m.uvOffsetU, uvOffsetV = m.uvOffsetV;   // (where the bump picture sits)
	m = p->material;
	m.bumpFile = bumpFile;
	m.bumpKind = bumpKind;
	m.bumpStrength = bumpStrength;
	m.uvScaleU = uvScaleU;
	m.uvScaleV = uvScaleV;
	m.uvOffsetU = uvOffsetU;
	m.uvOffsetV = uvOffsetV;
	m.imageFile.clear();
	m.checker = false;
	m.pattern = Pattern::None;
	return true;
}

}  // namespace scene_doc
