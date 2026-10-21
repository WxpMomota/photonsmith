#pragma once
// texture_position_mapping.h -- pbrt-v4's position-based 2D texture mappings (textures.h: SphericalMapping, CylindricalMapping, PlanarMapping), shared by the CPU texture wrapper
// (mapped_texture, texture.h) and both OptiX backends' image lookup. Kind 0 (the surface's own uv with uscale/vscale/udelta/vdelta) is left to the callers, which already do it.
//
// The hit point p (world space) is carried into the texture's own space by `xf` (the inverse of the CTM at the Texture directive: a 3x4 row-major affine matrix), then:
//   spherical   : s = theta / pi, t = phi / 2 pi          (theta from the texture's +z axis, phi in [0, 2 pi) about it)
//   cylindrical : s = (pi + atan2(y, x)) / 2 pi, t = z
//   planar      : s = du + p . v1, t = dv + p . v2
// pbrt carries texture-space DIFFERENTIALS through these too (for filtering); the renderers here sample such a lookup at full resolution (bilinear), which is unbiased and only
// shimmers where a picture is minified.

#ifdef __CUDACC__
#define TEXMAP_HD __host__ __device__
#else
#define TEXMAP_HD
#endif

#include <cmath>

namespace texture_position_mapping {

enum Kind { kUv = 0, kSpherical = 1, kCylindrical = 2, kPlanar = 3 };

template <typename T>
TEXMAP_HD inline void positionToSt(int kind, const T *xf, const T *v1, const T *v2, T du, T dv, T px, T py, T pz, T &s, T &t) {
	const T x = xf[0] * px + xf[1] * py + xf[2] * pz + xf[3];
	const T y = xf[4] * px + xf[5] * py + xf[6] * pz + xf[7];
	const T z = xf[8] * px + xf[9] * py + xf[10] * pz + xf[11];
	const T pi = static_cast<T>(3.14159265358979323846);
	if (kind == kSpherical) {
		const T len = std::sqrt(x * x + y * y + z * z);
		const T cosTheta = len > T(0) ? z / len : T(1);
		const T theta = std::acos(cosTheta < T(-1) ? T(-1) : (cosTheta > T(1) ? T(1) : cosTheta));
		T phi = std::atan2(y, x);
		if (phi < T(0)) phi += T(2) * pi;
		s = theta / pi;
		t = phi / (T(2) * pi);
	} else if (kind == kCylindrical) {
		s = (pi + std::atan2(y, x)) / (T(2) * pi);
		t = z;
	} else {   // planar
		s = du + (x * v1[0] + y * v1[1] + z * v1[2]);
		t = dv + (x * v2[0] + y * v2[1] + z * v2[2]);
	}
}

}  // namespace texture_position_mapping
