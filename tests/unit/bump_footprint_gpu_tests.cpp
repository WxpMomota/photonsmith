/**
 * @file bump_footprint_gpu_tests.cpp
 * @brief A bump map's height is read from the mip level that fits the pixel footprint, and every hit has a footprint, as in pbrt-v4.
 *
 * pbrt's BumpMap passes the shifted texture context (with its dudx, dudy..) to the displacement texture, whose default "bilinear" filter reads the mip level that fits, and a hit
 * whose ray has no differentials (any diffuse or glossy bounce) gets the camera's estimate of the footprint (CameraBase::Approximate_dp_dxy). This renderer read the height
 * at full resolution at every bounce, so a finely textured floor seen from afar (the Sibenik cathedral's marble) had its normals tilted by texel-sized noise and read 2x too
 * bright at a grazing light, 30% on the whole picture, against pbrt.
 */
#include <gtest/gtest.h>

#include <cmath>

#include "pbrt_gpu_builder.h"
#include "gpu_bump_map.h"

namespace {

// A square image with alternating black and white texels (0, 255).
SceneData checkerHeights(int size, int& idx) {
	SceneData out;
	TextureData t{};
	t.kind = TextureKind::Image;
	t.width = size;
	t.height = size;
	t.wrapMode = GpuWrapMode::Repeat;
	t.srgb = false;
	t.pixelOffset = 0;
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x) {
			const unsigned char v = ((x + y) % 2) ? 255 : 0;
			out.texturePixels.push_back(v);
			out.texturePixels.push_back(v);
			out.texturePixels.push_back(v);
		}
	out.textures.push_back(t);
	idx = 0;
	return out;
}

}  // namespace

TEST(BumpMipChain, HalvesDownToOneTexelAndAveragesTheBoxes) {
	int idx = 0;
	SceneData scene = checkerHeights(8, idx);
	pbrt_gpu::detail::buildBumpMipChain(scene, idx);
	const TextureData& base = scene.textures[0];
	EXPECT_EQ(base.bumpLevels, 3) << "8 -> 4 -> 2 -> 1";
	ASSERT_GE(base.bumpFirstLevel, 1);
	const TextureData& first = scene.textures[static_cast<std::size_t>(base.bumpFirstLevel)];
	EXPECT_EQ(first.width, 4);
	EXPECT_EQ(first.height, 4);
	EXPECT_GT(first.heightRange, 0.0f) << "a chain level is stored remapped over its own range";
	// Every 2x2 box of a checker holds two black and two white texels: 0.5 everywhere.
	for (int level = 0; level < 3; ++level) {
		const int at = base.bumpFirstLevel + level;
		EXPECT_NEAR(gpu_sample_displacement(scene.textures.data(), scene.texturePixels.data(), at, 0.3f, 0.7f), 0.5f, 0.01f) << "level " << (level + 1);
	}
	// A second call does nothing more.
	const std::size_t entries = scene.textures.size();
	pbrt_gpu::detail::buildBumpMipChain(scene, idx);
	EXPECT_EQ(scene.textures.size(), entries);
}

TEST(BumpMipChain, TheLevelFollowsTheFootprintAsPbrtsBilinearFilterDoes) {
	int idx = 0;
	SceneData scene = checkerHeights(8, idx);
	pbrt_gpu::detail::buildBumpMipChain(scene, idx);
	const auto read = [&](float u, float v, float width) {
		return gpu_sample_displacement_filtered(scene.textures.data(), scene.texturePixels.data(), 0, u, v, width);
	};
	// u, v at the centre of texel (1, 0), image row 0 on top: v = 1 - (0 + 0.5) / 8.
	const float u = 1.5f / 8.0f, v = 1.0f - 0.5f / 8.0f;
	const float full = gpu_sample_displacement(scene.textures.data(), scene.texturePixels.data(), 0, u, v);
	EXPECT_NEAR(full, 1.0f, 1e-6f);
	EXPECT_NEAR(read(u, v, 0.0f), full, 1e-6f) << "no footprint: the full-resolution texel";
	EXPECT_NEAR(read(u, v, 1e-4f), full, 1e-6f) << "a footprint under a texel: level 0";
	EXPECT_NEAR(read(u, v, 0.25f), 0.5f, 0.01f) << "two texels wide (2/8): pbrt's level 8 - 1... = 1, a 4x4 level, every box 0.5";
	EXPECT_NEAR(read(u, v, 5.0f), 0.5f, 0.01f) << "wider than the picture: its coarsest texel, the mean";
}

TEST(BumpFootprint, EveryHitGetsTheCamerasEstimate) {
	GpuCameraParams cam{};
	cam.kind = CameraKind::Perspective;
	cam.origin = make_float3(0.0f, 0.0f, 0.0f);
	cam.lower_left_corner = make_float3(-1.0f, -1.0f, -1.0f);
	cam.horizontal = make_float3(2.0f, 0.0f, 0.0f);
	cam.vertical = make_float3(0.0f, 2.0f, 0.0f);
	const GpuBumpFootprint f = gpu_make_bump_footprint(cam, 100, 100, 1);
	// A hit straight ahead on a plane facing the camera, 5 away; (u, v) = (x, y) in world units.
	const float3 p = make_float3(0.0f, 0.0f, -5.0f), n = make_float3(0.0f, 0.0f, 1.0f);
	const GpuBumpDiff d = gpu_bump_footprint_at(f, p, n, make_float3(1.0f, 0.0f, 0.0f), make_float3(0.0f, 1.0f, 0.0f));
	// One pixel is 2 / 100 of the viewport (1 away) = 0.02, which is 0.1 at 5 away; scaled by max(1/8, 1/sqrt(1)) = 1.
	EXPECT_NEAR(d.dudx, 0.1f, 1e-4f);
	EXPECT_NEAR(d.dvdy, 0.1f, 1e-4f);
	EXPECT_NEAR(d.step, 0.05f, 1e-4f) << "0.5 * (|dudx| + |dudy|), and here a pixel row does not move u";
	// The same hit seen from anywhere else (a bounce) has the same footprint: it is the camera's estimate of the point, not a property of the ray that arrived.
	const GpuBumpDiff again = gpu_bump_footprint_at(f, p, n, make_float3(1.0f, 0.0f, 0.0f), make_float3(0.0f, 1.0f, 0.0f));
	EXPECT_FLOAT_EQ(again.step, d.step);
	// A camera that gives no differentials gives none here either.
	GpuCameraParams ortho = cam;
	ortho.kind = CameraKind::Orthographic;
	const GpuBumpDiff none = gpu_bump_footprint_at(gpu_make_bump_footprint(ortho, 100, 100, 1), p, n, make_float3(1.0f, 0.0f, 0.0f), make_float3(0.0f, 1.0f, 0.0f));
	EXPECT_EQ(none.step, 0.0f);
}

