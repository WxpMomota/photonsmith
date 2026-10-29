#pragma once
// aperture_device.h -- the camera's aperture shape (src/shared/aperture_shape.h, Cycles' blades and anamorphic ratio) on both OptiX renderers' thin-lens cameras: a
// depth-of-field ray's lens point taken in the blades' polygon, squeezed by the ratio. A round aperture (GpuCameraParams::apertureBlades below 3, ratio 1 or 0) draws no
// extra random numbers, so its renders stay as they were.

#include "../../src/shared/aperture_shape.h"

// Whether the camera's aperture is shaped (blades, or an anamorphic ratio).
__device__ __forceinline__ bool gpu_aperture_shaped(const GpuCameraParams &cam) {
	return cam.apertureBlades >= 3 || (cam.apertureRatio > 0.0f && cam.apertureRatio != 1.0f);
}

// The lens point (x, y: in units of the lens radius; on entry the renderer's own disk sample) of a shaped aperture, from two more uniform numbers `a` and `b`.
__device__ __forceinline__ void gpu_aperture_point(const GpuCameraParams &cam, float a, float b, float &x, float &y) {
	float px = 0.0f, py = 0.0f;
	aperture_shape::sample(cam.apertureBlades, cam.apertureRotation, cam.apertureRatio > 0.0f ? cam.apertureRatio : 1.0f, a, b, x, y, px, py);
	x = px, y = py;
}
