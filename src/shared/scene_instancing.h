#pragma once
// scene_instancing.h -- which objects of a document are written as pbrt instances (ObjectBegin ... ObjectInstance) instead of each carrying its own copy of the triangles.
//
// A forest made with Array or Scatter is dozens of copies of the same cone, torus, terrain or mesh file, moved, turned and scaled. Written one by one each copy repeats thousands of
// numbers (a ready-made shape is a triangle mesh) and the renderers build and keep every copy; written as an instance the shape is stated once and each copy is a transform.
//
// The document does not say which objects are copies of one another: they are ordinary objects (recolour or reshape one and it simply stops being a copy). So this is worked out from
// the objects themselves each time the scene is written: two objects are copies when they are the same shape with the same material and every length of one is the other's times one
// factor (what scaleObject() in scene_array.h does), whatever their position and rotation. Only the shapes that are heavy triangle meshes are worth it (a sphere, disk or cylinder is
// one line of text and an analytic shape in the renderers), an emitter cannot be instanced (pbrt refuses an area light in an instance) and a volume is a medium boundary, not geometry.
// A procedural pattern (marble, noise) is a texture of the position in space, which an instance would have to place the way the renderers do not agree on, so it stays out.
//
// std-only, like scene_document.h (this is included by scene_pbrt_writer.h and needs the model and the JSON of a material).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "scene_model.h"
#include "scene_json.h"

namespace scene_doc {

namespace instancing_detail {

// The shapes written as big triangle meshes (or a mesh file), the ones an instance saves something for.
inline bool heavyShape(ShapeKind k) {
	switch (k) {
		case ShapeKind::Cone:
		case ShapeKind::Torus:
		case ShapeKind::Capsule:
		case ShapeKind::Dome:
		case ShapeKind::Tube:
		case ShapeKind::RoundedBox:
		case ShapeKind::Cyclorama:
		case ShapeKind::Terrain:
		case ShapeKind::Mesh:
			return true;
		default:
			return false;
	}
}

inline bool eligible(const Object& o) {
	return heavyShape(o.shape) && o.volume.kind == VolumeKind::None && !o.emissive && o.material.pattern == Pattern::None && !(o.shape == ShapeKind::Mesh && o.meshFile.empty());
}

// The length that tells how big the object is, the one the others scale with (what a copy's scale factor is measured by).
inline double primaryLength(const Object& o) {
	switch (o.shape) {
		case ShapeKind::Mesh: return o.meshScale;
		case ShapeKind::RoundedBox:
		case ShapeKind::Cyclorama:
		case ShapeKind::Terrain: return o.size.x;
		default: return o.radius;   // cone, torus, capsule, dome, tube
	}
}

inline bool closeTo(double a, double b) { return std::fabs(a - b) <= 1e-5 * std::max(std::fabs(a), std::fabs(b)) + 1e-12; }

inline std::string materialKey(const Material& m) {
	std::string out;
	detail::dump(detail::materialToJson(m), out);
	return out;
}

// Whether `o` is `ref` with every length multiplied by `s` (position and rotation aside), and has the same material.
inline bool sameUpToScale(const Object& ref, const std::string& refMaterial, const Object& o, const std::string& oMaterial, double s) {
	if (o.shape != ref.shape || o.steps != ref.steps || o.seed != ref.seed || o.meshFile != ref.meshFile) return false;
	if (!closeTo(o.size.x, ref.size.x * s) || !closeTo(o.size.y, ref.size.y * s) || !closeTo(o.size.z, ref.size.z * s)) return false;
	if (!closeTo(o.height, ref.height * s) || !closeTo(o.meshScale, ref.meshScale * s)) return false;
	if (o.shape == ShapeKind::Terrain) {   // (its radius and radius2 are the roughness and the number of hills, not lengths)
		if (!closeTo(o.radius, ref.radius) || !closeTo(o.radius2, ref.radius2)) return false;
	} else if (!closeTo(o.radius, ref.radius * s) || !closeTo(o.radius2, ref.radius2 * s)) {
		return false;
	}
	if (!o.material.bumpFile.empty() && !closeTo(s, 1.0)) return false;   // (a height map's relief is a length of its own: it would not scale with the instance)
	return oMaterial == refMaterial;
}

}  // namespace instancing_detail

struct InstancePlan {
	std::vector<int> templateOf;   // per object: the template it is a copy of, or -1 (written on its own)
	std::vector<double> scale;     // per object: the factor its lengths have over its template's
	std::vector<int> source;       // per template: the document's first object of the group, which the template is written from
	int templates() const { return static_cast<int>(source.size()); }
};

// The plan for a document: templates with at least two copies. An object written on its own has templateOf -1.
inline InstancePlan planInstances(const Document& d) {
	using namespace instancing_detail;
	InstancePlan plan;
	const std::size_t n = d.objects.size();
	plan.templateOf.assign(n, -1);
	plan.scale.assign(n, 1.0);
	struct Candidate { std::size_t first; std::string material; std::vector<std::size_t> members; };
	std::vector<Candidate> candidates;
	for (std::size_t i = 0; i < n; ++i) {
		const Object& o = d.objects[i];
		if (!eligible(o)) continue;
		const double len = primaryLength(o);
		if (!(len > 1e-9) || !std::isfinite(len)) continue;
		const std::string material = materialKey(o.material);
		bool placed = false;
		for (Candidate& c : candidates) {
			const Object& ref = d.objects[c.first];
			if (ref.shape != o.shape) continue;
			if (sameUpToScale(ref, c.material, o, material, len / primaryLength(ref))) { c.members.push_back(i); placed = true; break; }
		}
		if (!placed) candidates.push_back({i, material, {i}});
	}
	for (const Candidate& c : candidates) {
		if (c.members.size() < 2) continue;
		const int t = static_cast<int>(plan.source.size());
		plan.source.push_back(static_cast<int>(c.first));
		const double base = primaryLength(d.objects[c.first]);
		for (std::size_t i : c.members) {
			plan.templateOf[i] = t;
			plan.scale[i] = primaryLength(d.objects[i]) / base;
		}
	}
	return plan;
}

}  // namespace scene_doc
