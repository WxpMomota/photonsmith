#pragma once
// scene_projector.h -- where a projector light (Light of kind Projector) looks: the frame its slide is projected through, and how big the slide's frustum is at a distance. The
// pbrt file gets the frame as a `Transform` before the light (a projection light looks along +Z of its own space, the slide's top towards +Y, its right towards +X); the 3D view
// draws the frustum from the same numbers. Qt-free and std-only.

#include "scene_model.h"

#include <algorithm>
#include <cmath>

namespace scene_doc {
namespace projector {

struct Frame {
	Float3 right, up, dir;   // the projector's own X, Y and Z axes in the scene (unit length, at right angles)
};

inline Float3 cross(const Float3& a, const Float3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Float3 unit(const Float3& a, const Float3& fallback) {
	const double n = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
	return n > 1e-12 ? Float3{a.x / n, a.y / n, a.z / n} : fallback;
}

// The slide's top points up in the scene as far as the aim allows; aimed straight up or down it points towards -Z (so the slide reads upright from the starter camera's side).
inline Frame frameOf(const Light& l) {
	Frame f;
	f.dir = unit({l.target.x - l.position.x, l.target.y - l.position.y, l.target.z - l.position.z}, {0.0, -1.0, 0.0});
	Float3 up{0.0, 1.0, 0.0};
	if (std::fabs(f.dir.y) > 0.9995) up = {0.0, 0.0, -1.0};
	const double along = up.x * f.dir.x + up.y * f.dir.y + up.z * f.dir.z;
	f.up = unit({up.x - along * f.dir.x, up.y - along * f.dir.y, up.z - along * f.dir.z}, {0.0, 1.0, 0.0});
	f.right = unit(cross(f.up, f.dir), {1.0, 0.0, 0.0});
	return f;
}

// Half the width and height of the slide's rectangle `distance` along the axis: the field of view is the angle across the shorter side of the picture (aspect = width / height).
inline void halfSize(double fovDeg, double aspect, double distance, double& halfWidth, double& halfHeight) {
	const double half = distance * std::tan(std::clamp(fovDeg, 1.0, 170.0) * 3.14159265358979323846 / 360.0);
	if (aspect >= 1.0) { halfHeight = half; halfWidth = half * aspect; }
	else { halfWidth = half; halfHeight = half / std::max(aspect, 1e-3); }
}

}  // namespace projector
}  // namespace scene_doc
