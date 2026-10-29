#pragma once
// aperture_shape.h -- a lens's aperture shaped by its blades: the point on the lens a depth-of-field ray leaves from, sampled in a regular polygon of `blades` corners
// turned by `rotation` instead of a disk, and squeezed across by `ratio` (an anamorphic lens's oval bokeh) - so an out-of-focus light is drawn as the polygon, a
// hexagon for six blades, as a real lens draws it. Ported from Cycles (Blender's path tracer, Apache-2.0: intern/cycles/kernel/sample/mapping.h
// regular_polygon_sample, kernel/camera/camera.h camera_sample_aperture).
//
// A pbrt Camera's `"integer blades" [ 6 ]`, `"float bladesrotation" [ 15 ]` (degrees) and `"float apertureratio" [ 1.33 ]` (our extension; Blender's Blades,
// Rotation and Ratio); fewer than 3 blades (the default 0) keeps the round aperture. The polygon is inside the unit circle, its corners on it, so the lens radius is
// the corners' (Cycles').

#if defined(__CUDACC__)
#define RT_APERTURE_FN __host__ __device__ __forceinline__
#else
#define RT_APERTURE_FN inline
#include <cmath>
#endif

namespace aperture_shape {

// A point of the regular polygon with `corners` corners turned by `rotation` radians, from two uniform numbers in [0, 1): uniform over its area.
template <class T>
RT_APERTURE_FN void polygon(T corners, T rotation, T u, T v, T &x, T &y) {
#if !defined(__CUDACC__)
	using std::cos;
	using std::floor;
	using std::sin;
	using std::sqrt;
#endif
	const T corner = floor(u * corners);   // (which of its triangles, and u used again within it)
	u = u * corners - corner;
	u = sqrt(u);   // (uniform in the triangle from the centre to two corners)
	v = v * u;
	u = T(1) - u;
	const T angle = T(3.14159265358979323846) / corners;
	const T px = (u + v) * cos(angle), py = (u - v) * sin(angle);
	rotation += corner * T(2) * angle;
	const T cr = cos(rotation), sr = sin(rotation);
	x = cr * px - sr * py;
	y = sr * px + cr * py;
}

// The lens point (x, y in units of the lens radius) for two uniform numbers: the polygon when there are 3 or more blades, else `diskX/diskY` (the renderer's own disk
// sample, kept so that a round aperture renders exactly as before); squeezed across by the ratio (above 1: taller than wide).
template <class T>
RT_APERTURE_FN void sample(int blades, T rotationDegrees, T ratio, T u, T v, T diskX, T diskY, T &x, T &y) {
	if (blades >= 3) polygon(T(blades), rotationDegrees * T(3.14159265358979323846) / T(180), u, v, x, y);
	else x = diskX, y = diskY;
	if (ratio > T(0)) x /= ratio;
}

}  // namespace aperture_shape
