// pbrt_cpu_builder_test_helpers.h -- what the pbrt_cpu_builder_*_tests.cpp files share: build-from-text, cast-a-ray, the unit quad, two tiny BMP writers and the temp-tree fixture.
// (One file of 3000 lines was split by feature; nothing changed in the tests themselves.)
#pragma once


#include <gtest/gtest.h>

#include "pbrt_cpu_builder.h"
#include "pbrt_flatten.h"
#include "pbrt_load.h"
#include "pbrt_scene.h"
#include "cone_paraboloid_hittable.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>



inline pbrt_cpu::BuildResult buildFrom(const std::string &text) {
	const pbrt_scene::ParseResult parsed = pbrt_scene::parse(text);
	EXPECT_TRUE(parsed.ok) << parsed.error;
	return pbrt_cpu::build(pbrt_flatten::flatten(parsed.scene));
}

// Casts a ray and reports whether it hit, and how far along.
inline bool castRay(const pbrt_cpu::BuildResult &b, const point3 &origin,
			 const vec3 &direction, double &tOut) {
	hit_record rec;
	const ray r(origin, direction);
	if (!b.world->hit(r, interval(0.001, infinity), rec)) return false;
	tOut = rec.t;
	return true;
}

// A unit quad in the z=0 plane spanning (0,0)..(1,1).
inline const char *const kQuad =
	"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ]\n"
	"  \"point3 P\" [ 0 0 0  1 0 0  1 1 0  0 1 0 ]\n";

// Hand-authored minimal 1x1 24bpp BMP - same technique as pbrt_alpha_cutout_
// tests.cpp's own solidBmp1x1() (duplicated, not shared - small self-
// contained test-file helpers). A uniform-gray pixel decodes as a genuine
// grayscale bump/displacement map (is_grayscale_image()'s own convention).
inline std::string solidGrayBmp1x1() {
	std::string bytes;
	const auto u16 = [&](unsigned short v) {
		bytes.push_back(static_cast<char>(v & 0xFF));
		bytes.push_back(static_cast<char>((v >> 8) & 0xFF));
	};
	const auto u32 = [&](unsigned int v) {
		bytes.push_back(static_cast<char>(v & 0xFF));
		bytes.push_back(static_cast<char>((v >> 8) & 0xFF));
		bytes.push_back(static_cast<char>((v >> 16) & 0xFF));
		bytes.push_back(static_cast<char>((v >> 24) & 0xFF));
	};
	bytes.push_back('B'); bytes.push_back('M');
	u32(14 + 40 + 4);
	u32(0);
	u32(14 + 40);
	u32(40);
	u32(1);
	u32(1);
	u16(1);
	u16(24);
	u32(0);
	u32(4);
	u32(0); u32(0);
	u32(0); u32(0);
	bytes.push_back(static_cast<char>(128));
	bytes.push_back(static_cast<char>(128));
	bytes.push_back(static_cast<char>(128));
	bytes.push_back('\0');
	return bytes;
}

// Hand-authored minimal 2x2 24bpp BMP, two-tone (white/black by row) rather
// than solidGrayBmp1x1()'s single uniform pixel - needed to prove a
// goniometric/projection light's real decoded image (not the flat
// kUniformImage/kUniformSlide fallback) is what reached eval_I/eval_I_rgb:
// a uniform image gives identical intensity at every direction by
// construction, so seeing ANY variance across sampled directions is only
// possible with real, non-uniform pixel data actually decoded and used.
inline std::string twoToneBmp2x2() {
	std::string bytes;
	const auto u16 = [&](unsigned short v) {
		bytes.push_back(static_cast<char>(v & 0xFF));
		bytes.push_back(static_cast<char>((v >> 8) & 0xFF));
	};
	const auto u32 = [&](unsigned int v) {
		bytes.push_back(static_cast<char>(v & 0xFF));
		bytes.push_back(static_cast<char>((v >> 8) & 0xFF));
		bytes.push_back(static_cast<char>((v >> 16) & 0xFF));
		bytes.push_back(static_cast<char>((v >> 24) & 0xFF));
	};
	const std::size_t rowBytes = 8;  // 2 px * 3 bytes, padded to a 4-byte boundary
	const std::size_t dataSize = rowBytes * 2;
	bytes.push_back('B'); bytes.push_back('M');
	u32(static_cast<unsigned int>(14 + 40 + dataSize));
	u32(0);
	u32(14 + 40);
	u32(40);
	u32(2);
	u32(2);
	u16(1);
	u16(24);
	u32(0);
	u32(static_cast<unsigned int>(dataSize));
	u32(0); u32(0);
	u32(0); u32(0);
	// Bottom row first (BMP storage order) - black; top row - white.
	// (B,G,R) byte order per pixel, but black/white are channel-symmetric so
	// this doesn't matter here.
	for (int row = 0; row < 2; ++row) {
		const unsigned char v = (row == 0) ? 0 : 255;
		for (int px = 0; px < 2; ++px) {
			bytes.push_back(static_cast<char>(v));
			bytes.push_back(static_cast<char>(v));
			bytes.push_back(static_cast<char>(v));
		}
		bytes.push_back('\0'); bytes.push_back('\0');  // row padding to 8 bytes
	}
	return bytes;
}

class CpuBuilderTempTree : public ::testing::Test {
protected:
	void SetUp() override {
		const char *tmp = std::getenv("TEMP");
		if (!tmp) tmp = std::getenv("TMPDIR");
		root_ = std::string(tmp ? tmp : ".") + "/pbrt_cpu_builder_tests/";
		std::filesystem::create_directories(root_);
	}
	void TearDown() override {
		for (const std::string &f : written_) std::remove(f.c_str());
	}
	void write(const std::string &relative, const std::string &contents) {
		const std::string full = root_ + relative;
		std::ofstream out(full, std::ios::binary);
		out << contents;
		out.close();
		written_.push_back(full);
	}
	std::string path(const std::string &relative) const { return root_ + relative; }
private:
	std::string root_;
	std::vector<std::string> written_;
};

