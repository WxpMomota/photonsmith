// The toon look (src/shared/toon_look.h): the light in bands, ink lines where the depth jumps or the surface turns (not across a plane seen edge-on), the background left
// alone; its style from a `# @rt-toon` line, which a film's `toon` line writes into every frame.

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "../../src/shared/toon_look.h"
#include "../../src/shared/cartoon/cartoon_films.h"   // (the films themselves, which these tests reach into)
#include "../../launcher/toon_frame.h"

namespace {

// Passes for a w x h picture: everything covered at depth `depth`, facing the camera, of albedo `albedo`.
std::vector<std::vector<float>> flatPasses(int w, int h, float albedo, float depth) {
	const std::size_t n = static_cast<std::size_t>(w) * h;
	std::vector<std::vector<float>> p(10, std::vector<float>(n, 0.0f));
	for (std::size_t i = 0; i < n; ++i) {
		p[0][i] = p[1][i] = p[2][i] = albedo;
		p[5][i] = 1.0f;   // normal +z
		p[6][i] = depth;
		p[9][i] = 1.0f;
	}
	return p;
}

}  // namespace

TEST(ToonLook, TheLightFallsIntoBands) {
	const int w = 200, h = 4;
	auto passes = flatPasses(w, h, 0.5f, 3.0f);
	std::vector<float> rgb(static_cast<std::size_t>(w) * h * 3);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			for (int c = 0; c < 3; ++c) rgb[(static_cast<std::size_t>(y) * w + x) * 3 + c] = 0.5f * (x + 0.5f) / w;   // a smooth ramp of light
	toon_look::Style st;
	st.on = true;
	st.bands = 3;
	st.lines = 0.0;
	st.saturation = 1.0;
	toon_look::apply(rgb, w, h, passes, st);
	// three levels, the darkest `shadow` of the brightest, with only a few pixels between them
	std::set<int> levels;
	int between = 0;
	const double top = rgb[(static_cast<std::size_t>(w) - 1) * 3];
	for (int x = 0; x < w; ++x) {
		const double v = rgb[static_cast<std::size_t>(x) * 3] / top;
		bool onLevel = false;
		for (const double level : {0.35, 0.675, 1.0})   // shadow 0.35, then evenly to 1
			if (std::abs(v - level) < 0.01) levels.insert(static_cast<int>(level * 1000)), onLevel = true;
		between += !onLevel;
	}
	EXPECT_EQ(levels.size(), 3u);
	EXPECT_LE(between, 40) << "the steps between bands are soft, but short";
	// it only gets brighter along the ramp
	for (int x = 1; x < w; ++x) EXPECT_GE(rgb[static_cast<std::size_t>(x) * 3] + 1e-6, rgb[static_cast<std::size_t>(x - 1) * 3]);
}

TEST(ToonLook, LinesGoRoundWhatStandsInFrontButNotAcrossAPlane) {
	const int w = 64, h = 64;
	auto passes = flatPasses(w, h, 0.6f, 10.0f);
	// a floor: depth along the ray of a plane seen edge-on (its inverse is flat across the picture)
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) passes[6][static_cast<std::size_t>(y) * w + x] = static_cast<float>(1.0 / (0.02 + 0.004 * y));
	// a box in front of it
	for (int y = 20; y < 44; ++y)
		for (int x = 20; x < 44; ++x) passes[6][static_cast<std::size_t>(y) * w + x] = 2.0f;
	std::vector<float> rgb(static_cast<std::size_t>(w) * h * 3, 0.3f);
	toon_look::Style st;
	st.on = true;
	st.lines = 2.0 * 1080.0 / h;   // (two pixels wide in this picture)
	st.saturation = 1.0;
	toon_look::apply(rgb, w, h, passes, st);
	const auto at = [&](int x, int y) { return rgb[(static_cast<std::size_t>(y) * w + x) * 3]; };
	EXPECT_LT(at(20, 30), 0.05) << "the box's edge is inked";
	EXPECT_LT(at(43, 30), 0.05);
	EXPECT_LT(at(30, 20), 0.05);
	EXPECT_GT(at(30, 30), 0.2) << "the box's face is not";
	EXPECT_GT(at(5, 10), 0.2) << "nor is the floor seen edge-on";
	EXPECT_GT(at(5, 55), 0.2);
	// a crease: the normal turns by 90 degrees halfway across
	auto crease = flatPasses(w, h, 0.6f, 5.0f);
	for (int y = 0; y < h; ++y)
		for (int x = w / 2; x < w; ++x) {
			const std::size_t i = static_cast<std::size_t>(y) * w + x;
			crease[3][i] = 1.0f, crease[5][i] = 0.0f;
		}
	std::vector<float> rgb2(static_cast<std::size_t>(w) * h * 3, 0.3f);
	toon_look::apply(rgb2, w, h, crease, st);
	EXPECT_LT(rgb2[(static_cast<std::size_t>(30) * w + w / 2) * 3], 0.05);
	EXPECT_GT(rgb2[(static_cast<std::size_t>(30) * w + 10) * 3], 0.2);
}

TEST(ToonLook, TheBackgroundAndLightsKeepTheirOwnLight) {
	const int w = 16, h = 16;
	auto passes = flatPasses(w, h, 0.0f, 0.0f);
	for (float &a : passes[9]) a = 0.0f;   // nothing hit: the sky
	std::vector<float> rgb(static_cast<std::size_t>(w) * h * 3);
	for (std::size_t i = 0; i < rgb.size(); ++i) rgb[i] = 0.1f + 0.001f * static_cast<float>(i % 97);
	const std::vector<float> before = rgb;
	toon_look::Style st;
	st.on = true;
	st.saturation = 1.0;
	toon_look::apply(rgb, w, h, passes, st);
	EXPECT_EQ(rgb, before);
}

TEST(ToonLook, ItsStyleIsReadFromAScenesLineAndAFilmWritesOneIntoEveryFrame) {
	toon_look::Style s;
	EXPECT_EQ(toon_look::parse("bands 4 lines 3 ink #102030 saturation 1.4 shadow 0.2", s), "");
	EXPECT_TRUE(s.on);
	EXPECT_EQ(s.bands, 4);
	EXPECT_DOUBLE_EQ(s.lines, 3.0);
	EXPECT_NEAR(s.ink[2], toon_look::srgbToLinear(0x30 / 255.0), 1e-12);
	toon_look::Style again;
	const std::string tag = toon_look::tagOf(s);
	ASSERT_EQ(tag.rfind("# @rt-toon", 0), 0u);
	EXPECT_EQ(toon_look::parse(tag.substr(10), again), "");
	EXPECT_EQ(again.bands, 4);
	EXPECT_NEAR(again.ink[2], s.ink[2], 1e-4);
	EXPECT_NE(toon_look::parse("bands 9", s).find("2 to 6"), std::string::npos);
	EXPECT_NE(toon_look::parse("ink purple", s).find("#rrggbb"), std::string::npos);
	EXPECT_NE(toon_look::parse("glow 2", s).find("takes bands"), std::string::npos);
	// a film's `toon` line: every frame's header carries it, and a frame's run reads it from the frame's scene
	(void)cartoon::films();
	const std::filesystem::path d = std::filesystem::temp_directory_path() / "photonsmith_toon_test";
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt") << "LookAt 0 1 5  0 1 0  0 1 0\nCamera \"perspective\" \"float fov\" [ 40 ]\nWorldBegin\nShape \"sphere\"\n";
	std::ofstream(d / "t.film") << "scene s.pbrt\nlength 4\ntoon bands 2 lines 1.5\nshot 0 A\n key 0 1 5  0 1 0\n";
	cartoon::film_file::DataFilm film;
	ASSERT_EQ(film.load((d / "t.film").string()), "");
	const std::string frame = film.sceneAt(1.0);
	EXPECT_EQ(frame.rfind("# @rt-toon bands 2 lines 1.5", 0), 0u) << frame.substr(0, 120);
	std::ofstream(d / "frame.pbrt") << frame;
	const toon_look::Style read = toon_frame::styleFor(false, (d / "frame.pbrt").string());
	EXPECT_TRUE(read.on);
	EXPECT_EQ(read.bands, 2);
	EXPECT_FALSE(toon_frame::styleFor(false, (d / "s.pbrt").string()).on) << "a scene without the line has no look";
	EXPECT_TRUE(toon_frame::styleFor(true, (d / "s.pbrt").string()).on) << "unless --toon asks for it";
	cartoon::film_file::DataFilm bad;
	std::ofstream(d / "bad.film") << "scene s.pbrt\nlength 4\ntoon bands 1\n";
	EXPECT_NE(bad.load((d / "bad.film").string()).find("bad.film:3:"), std::string::npos);
}
