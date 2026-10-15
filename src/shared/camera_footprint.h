// camera_footprint.h -- pbrt-v4's CameraBase::Approximate_dp_dxy(): the screen-space footprint a hit gets when its own ray carries no differentials (every diffuse or glossy bounce).
//
// pbrt does NOT trace a camera ray through the hit point. It rotates the point to the camera's +z axis and intersects the surface's tangent plane with two rays of the camera's
// SMALLEST pixel differentials (CameraBase::FindMinimumDifferentials: the smallest angular step of a pixel along the film diagonal, found once when the camera is made), scaled by
// max(1/8, 1/sqrt(samples per pixel)). So the footprint of a bounced hit is "one of the smallest pixels, seen from the camera, in the direction of the hit" - it does not shrink as
// the hit lies further off the image's axis (a rectilinear pixel's angular size falls with cos^2 of that angle), which an exact camera ray through the point would give: a floor
// that runs out of the picture was read through ever smaller mip levels, and a bump map on it 7% too bright against pbrt (pbrt_scenes/bump-fine-floor.pbrt).
//
// std-only and double throughout, usable on the host (the CPU camera) and in CUDA device code (both OptiX backends). The minimum differentials are made on the host once.
#pragma once

#include "cpu_gpu.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace camera_footprint {

// (no default member initializers: these live in __constant__ launch parameters, which CUDA does not let have a dynamic initialization - value-initialize, V3(), for zeros)
struct V3 {
	double x, y, z;
};
CPU_GPU inline V3 make(double x, double y, double z) { V3 v; v.x = x; v.y = y; v.z = z; return v; }
CPU_GPU inline V3 operator+(const V3& a, const V3& b) { return make(a.x + b.x, a.y + b.y, a.z + b.z); }
CPU_GPU inline V3 operator-(const V3& a, const V3& b) { return make(a.x - b.x, a.y - b.y, a.z - b.z); }
CPU_GPU inline V3 operator*(const V3& a, double s) { return make(a.x * s, a.y * s, a.z * s); }
CPU_GPU inline double dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
CPU_GPU inline double length(const V3& a) { return sqrt(dot(a, a)); }
CPU_GPU inline V3 unit(const V3& a) {
	const double l = length(a);
	return l > 0.0 ? a * (1.0 / l) : a;
}

// The camera: its position, the world-space axes of its own space (image right, image up, viewing direction) and pbrt's minimum direction differentials.
struct Camera {
	int valid;                    // 0: no differentials (not a perspective camera, or an animated one); Camera() is that
	V3 origin, right, up, forward;
	V3 minDirX, minDirY;          // pbrt's minDirDifferentialX/Y (the position differentials of a pinhole are zero)
	double sppScale;              // max(1/8, 1/sqrt(samples per pixel))
};

// pbrt's CoordinateSystem(): two vectors that make an orthonormal frame with the unit vector v1.
CPU_GPU inline void coordinateSystem(const V3& v1, V3& v2, V3& v3) {
	const double sign = copysign(1.0, v1.z);
	const double a = -1.0 / (sign + v1.z);
	const double b = v1.x * v1.y * a;
	v2 = make(1.0 + sign * v1.x * v1.x * a, sign * b, -sign * v1.x);
	v3 = make(b, sign + v1.y * v1.y * a, -v1.y);
}

// pbrt's RotateFromTo(from, to) as a 3x3 matrix (row major): the rotation that takes the unit vector `from` to `to`, built from two reflections.
CPU_GPU inline void rotateFromTo(const V3& from, const V3& to, double r[3][3]) {
	V3 refl;
	if (fabs(from.x) < 0.72 && fabs(to.x) < 0.72) refl = make(1.0, 0.0, 0.0);
	else if (fabs(from.y) < 0.72 && fabs(to.y) < 0.72) refl = make(0.0, 1.0, 0.0);
	else refl = make(0.0, 0.0, 1.0);
	const V3 u = refl - from, v = refl - to;
	const double uu = dot(u, u), vv = dot(v, v), uv = dot(u, v);
	const double ua[3] = {u.x, u.y, u.z}, va[3] = {v.x, v.y, v.z};
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			r[i][j] = (i == j ? 1.0 : 0.0) - 2.0 / uu * ua[i] * ua[j] - 2.0 / vv * va[i] * va[j] + 4.0 * uv / (uu * vv) * va[i] * ua[j];
}

CPU_GPU inline V3 fromCamera(const Camera& c, const V3& v) { return make(dot(v, c.right), dot(v, c.up), dot(v, c.forward)); }
CPU_GPU inline V3 toCamera(const Camera& c, const V3& v) { return c.right * v.x + c.up * v.y + c.forward * v.z; }

// pbrt's Approximate_dp_dxy(p, n, ...): dpdx and dpdy of a hit at `p` with geometric normal `n`, in world space. False (both zero) when the camera gives none or the plane is parallel.
CPU_GPU inline bool approximate(const Camera& c, const V3& p, const V3& n, V3& dpdx, V3& dpdy) {
	dpdx = V3();
	dpdy = V3();
	if (!c.valid) return false;
	const V3 pCamera = fromCamera(c, p - c.origin);
	const double pLen = length(pCamera);
	if (!(pLen > 1e-12)) return false;
	double R[3][3];
	rotateFromTo(pCamera * (1.0 / pLen), make(0.0, 0.0, 1.0), R);
	const auto apply = [&R](const V3& v) { return make(R[0][0] * v.x + R[0][1] * v.y + R[0][2] * v.z, R[1][0] * v.x + R[1][1] * v.y + R[1][2] * v.z, R[2][0] * v.x + R[2][1] * v.y + R[2][2] * v.z); };
	const auto applyInverse = [&R](const V3& v) { return make(R[0][0] * v.x + R[1][0] * v.y + R[2][0] * v.z, R[0][1] * v.x + R[1][1] * v.y + R[2][1] * v.z, R[0][2] * v.x + R[1][2] * v.y + R[2][2] * v.z); };
	const V3 pDownZ = apply(pCamera);
	const V3 nDownZ = apply(fromCamera(c, n));
	const double d = nDownZ.z * pDownZ.z;
	// (rays from the camera's origin: the minimum position differentials of a pinhole camera are zero)
	const V3 dirX = make(0.0, 0.0, 1.0) + c.minDirX, dirY = make(0.0, 0.0, 1.0) + c.minDirY;
	const double denomX = dot(nDownZ, dirX), denomY = dot(nDownZ, dirY);
	if (denomX == 0.0 || denomY == 0.0) return false;
	const V3 px = dirX * (d / denomX), py = dirY * (d / denomY);
	dpdx = toCamera(c, applyInverse(px - pDownZ)) * c.sppScale;
	dpdy = toCamera(c, applyInverse(py - pDownZ)) * c.sppScale;
	return true;
}

// Host: the camera of a perspective pinhole with the viewport given as pixel (0, 0)'s centre and the offsets to the pixel on the right (du) and the pixel below (dv). The minimum
// differentials are found the way pbrt's FindMinimumDifferentials does: 512 film points along the diagonal, the smallest angular step to the neighbouring pixel in x and in y,
// measured in a frame made round the ray (render space is the world here, as pbrt's default "cameraworld" space is).
inline Camera makeCamera(const V3& origin, const V3& pixel00, const V3& du, const V3& dv, int width, int height, int samplesPerPixel) {
	Camera c = Camera();
	c.origin = origin;
	c.right = unit(du);
	c.up = unit(dv) * -1.0;
	const V3 middle = pixel00 + du * (0.5 * width - 0.5) + dv * (0.5 * height - 0.5);
	c.forward = unit(middle - origin);
	c.sppScale = std::max(0.125, 1.0 / std::sqrt(static_cast<double>(std::max(1, samplesPerPixel))));
	double bestX = std::numeric_limits<double>::infinity(), bestY = bestX;
	const int n = 512;
	for (int i = 0; i < n; ++i) {
		const double fx = static_cast<double>(i) / (n - 1) * width, fy = static_cast<double>(i) / (n - 1) * height;
		const V3 p = pixel00 + du * (fx - 0.5) + dv * (fy - 0.5);   // (film coordinates put a pixel's centre at +0.5)
		const V3 d = unit(p - origin), rx = unit(p + du - origin), ry = unit(p + dv - origin);
		V3 fxAxis, fyAxis;
		coordinateSystem(d, fxAxis, fyAxis);   // pbrt's Frame::FromZ(d): its x and y axes are these, its z is d
		const auto local = [&](const V3& v) { return make(dot(v, fxAxis), dot(v, fyAxis), dot(v, d)); };
		const V3 df = local(d);
		const V3 dxf = unit(local(rx)) - df, dyf = unit(local(ry)) - df;
		if (length(dxf) < bestX) { bestX = length(dxf); c.minDirX = dxf; }
		if (length(dyf) < bestY) { bestY = length(dyf); c.minDirY = dyf; }
	}
	c.valid = (width > 0 && height > 0 && std::isfinite(bestX) && std::isfinite(bestY)) ? 1 : 0;
	return c;
}

}  // namespace camera_footprint
