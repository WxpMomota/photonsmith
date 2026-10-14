/**
 * @file bump_footprint_cpu_tests.cpp
 * @brief A bump map's height is read from the mip level that fits the pixel footprint, and every hit has a footprint, as in pbrt-v4.
 *
 * pbrt's BumpMap passes the shifted texture context (with its dudx, dudy..) to the displacement texture, whose default "bilinear" filter reads the mip level that fits, and a hit
 * whose ray has no differentials (any diffuse or glossy bounce) gets the camera's estimate of the footprint (CameraBase::Approximate_dp_dxy). This renderer read the height
 * at full resolution at every bounce, so a finely textured floor seen from afar (the Sibenik cathedral's marble) had its normals tilted by texel-sized noise and read 2x too
 * bright at a grazing light, 30% on the whole picture, against pbrt.
 */
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "../../src/TheRestOfYourLife/hittable.h"
#include "../../src/TheRestOfYourLife/material_simple.h"
#include "../../src/TheRestOfYourLife/normal_map_materials.h"
#include "../../src/TheRestOfYourLife/texture.h"

namespace {

// An 8x8 24bpp BMP of alternating black and white texels, written to a temp file for the CPU's mipmap texture.
std::string writeCheckerBmp(const std::string& path, int size) {
	std::string bytes;
	const auto u16 = [&](unsigned short v) { bytes.push_back(static_cast<char>(v & 0xFF)); bytes.push_back(static_cast<char>((v >> 8) & 0xFF)); };
	const auto u32 = [&](unsigned int v) { for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); };
	const int rowBytes = ((size * 3 + 3) / 4) * 4;
	bytes.push_back('B'); bytes.push_back('M');
	u32(14 + 40 + rowBytes * size); u32(0); u32(14 + 40);
	u32(40); u32(size); u32(size); u16(1); u16(24); u32(0); u32(rowBytes * size); u32(0); u32(0); u32(0); u32(0);
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			const char v = ((x + y) % 2) ? static_cast<char>(255) : 0;
			bytes.push_back(v); bytes.push_back(v); bytes.push_back(v);
		}
		for (int pad = size * 3; pad < rowBytes; ++pad) bytes.push_back(0);
	}
	std::ofstream(path, std::ios::binary) << bytes;
	return path;
}

}  // namespace

// The CPU: the same lookup through a mipmapped texture. A finely checkered height map seen with a footprint of many texels tilts the normal far less than seen texel by texel.
TEST(BumpFootprint, ACpuHeightMapIsReadAtTheLevelThatFitsTheFootprint) {
	const std::string file = writeCheckerBmp("bump_footprint_checker.bmp", 8);
	auto heights = std::make_shared<mipmap_texture>(file.c_str(), MipMapOptions{MipFilter::Bilinear, 8.0f, MipWrapMode::Repeat});
	auto material = std::make_shared<bump_map_material>(heights, std::make_shared<lambertian>(color(0.5, 0.5, 0.5)), 0.5);
	const auto tiltFor = [&](double footprint) {
		hit_record rec;
		rec.p = point3(0, 0, 0);
		rec.normal = vec3(0, 0, 1);
		rec.front_face = true;
		rec.u = 0.3;
		rec.v = 0.4;
		rec.dpdu = vec3(1, 0, 0);
		rec.dpdv = vec3(0, 1, 0);
		rec.dudx = footprint; rec.dvdx = 0.0; rec.dudy = 0.0; rec.dvdy = footprint;
		const hit_record bumped = material->apply(ray(point3(0, 0, 5), vec3(0, 0, -1)), rec);
		return std::acos(std::min(1.0, dot(bumped.normal, vec3(0, 0, 1))));
	};
	const double sharp = tiltFor(1.0 / 64.0);   // an eighth of a texel: the texel's own contrast
	const double blurred = tiltFor(0.5);        // four texels: the checker averages out
	EXPECT_GT(sharp, 0.2) << "a footprint under a texel sees the checker";
	EXPECT_LT(blurred, 0.2 * sharp) << "a footprint of several texels sees a flat grey: the normal barely tilts";
	std::remove(file.c_str());
}
