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

// Lines from the geometry, as Blender's Line Art draws them: where one material meets another and where one object meets another with no depth jump (the ID passes), and
// the lines inside a shape thinner than its outline. Off unless asked, so a film's toon frames keep their look.
TEST(ToonLook, LinesWhereMaterialsOrObjectsMeetAndThinnerInnerLines) {
	const int w = 64, h = 64;
	auto passes = flatPasses(w, h, 0.6f, 5.0f);   // one flat surface facing the camera: no depth jump, no crease
	passes.push_back(std::vector<float>(static_cast<std::size_t>(w) * h, 1.0f));   // id.object
	passes.push_back(std::vector<float>(static_cast<std::size_t>(w) * h, 7.0f));   // id.material
	for (int y = 0; y < h; ++y)
		for (int x = w / 2; x < w; ++x) passes[11][static_cast<std::size_t>(y) * w + x] = 8.0f;   // another material on the right half
	for (int y = h / 2; y < h; ++y)
		for (int x = 0; x < w; ++x) passes[10][static_cast<std::size_t>(y) * w + x] = 2.0f;   // another object in the bottom half
	toon_look::Style st;
	st.on = true;
	st.lines = 2.0 * 1080.0 / h;
	st.saturation = 1.0;
	const auto run = [&](const toon_look::Style &s) {
		std::vector<float> rgb(static_cast<std::size_t>(w) * h * 3, 0.3f);
		toon_look::apply(rgb, w, h, passes, s);
		return rgb;
	};
	const auto at = [&](const std::vector<float> &rgb, int x, int y) { return rgb[(static_cast<std::size_t>(y) * w + x) * 3]; };
	const std::vector<float> plain = run(st);
	EXPECT_GT(at(plain, w / 2, 10), 0.2) << "by default no line where the materials meet";
	EXPECT_GT(at(plain, 10, h / 2), 0.2);
	EXPECT_FALSE(st.needsIds());

	toon_look::Style mats = st;
	ASSERT_EQ(toon_look::parse("materials on", mats), "");
	EXPECT_TRUE(mats.needsIds());
	const std::vector<float> m = run(mats);
	EXPECT_LT(std::min(at(m, w / 2 - 1, 10), at(m, w / 2, 10)), 0.05) << "a line where the materials meet";
	EXPECT_GT(at(m, 10, h / 2), 0.2) << "not where only the object changes";
	EXPECT_GT(at(m, 10, 10), 0.2) << "nor on a surface of one material";

	toon_look::Style objs = st;
	ASSERT_EQ(toon_look::parse("objects on", objs), "");
	const std::vector<float> o = run(objs);
	EXPECT_LT(std::min(at(o, 10, h / 2 - 1), at(o, 10, h / 2)), 0.05) << "a line where the objects meet with no depth jump (an intersection)";
	EXPECT_GT(at(o, w / 2, 10), 0.2);

	// inner lines half as wide as the outline: the material line is narrower than it is with inner 1 (lines 8 pixels wide here, above the thinnest a stroke is drawn)
	toon_look::Style wideMats = mats;
	wideMats.lines = 8.0 * 1080.0 / h;
	toon_look::Style thin = wideMats;
	ASSERT_EQ(toon_look::parse("inner 0.5", thin), "");
	const std::vector<float> full = run(wideMats), t = run(thin);
	int wide = 0, narrow = 0;
	for (int x = 0; x < w; ++x) {
		wide += at(full, x, 10) < 0.15f;
		narrow += at(t, x, 10) < 0.15f;
	}
	EXPECT_LT(narrow, wide) << "inner 0.5 draws the inside lines thinner";
	EXPECT_GT(narrow, 0);

	// the scene line carries the new words only when they are set
	EXPECT_EQ(toon_look::tagOf(st).find("materials"), std::string::npos);
	toon_look::Style tagged = mats;   // (lines 2: a width a scene's line may have)
	tagged.inner = 0.5;
	tagged.lines = 2.0;
	EXPECT_NE(toon_look::tagOf(tagged).find("inner 0.5 materials on"), std::string::npos);
	toon_look::Style back;
	ASSERT_EQ(toon_look::parse(toon_look::tagOf(tagged).substr(10), back), "");
	EXPECT_TRUE(back.materials);
	EXPECT_EQ(back.inner, 0.5);
}

// Cel shading per material, as Blender's NPR materials ramp it: a material's (or object's) own bands, its shadows tinted, a flat highlight - the rest of the picture keeps
// the picture's ramp.
TEST(ToonLook, AMaterialHasItsOwnRampTintedShadowsAndAHighlight) {
	const int w = 200, h = 8;
	auto passes = flatPasses(w, h, 0.5f, 3.0f);
	const float skin = cryptomatte::idToFloat(cryptomatte::murmur3_32("skin")), cloth = cryptomatte::idToFloat(cryptomatte::murmur3_32("cloth"));
	passes.push_back(std::vector<float>(static_cast<std::size_t>(w) * h, 0.0f));   // id.object
	passes.push_back(std::vector<float>(static_cast<std::size_t>(w) * h, cloth));  // id.material: cloth, and skin in the bottom half
	for (int y = h / 2; y < h; ++y)
		for (int x = 0; x < w; ++x) passes[11][static_cast<std::size_t>(y) * w + x] = skin;
	std::vector<float> ramp(static_cast<std::size_t>(w) * h * 3);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			for (int c = 0; c < 3; ++c) ramp[(static_cast<std::size_t>(y) * w + x) * 3 + c] = 0.5f * (x + 0.5f) / w;   // the same ramp of light on both
	toon_look::Style st;
	ASSERT_EQ(toon_look::parse("bands 3 lines 0 saturation 1 material skin bands 2 shadow 0.5 shadow-tint #4060ff highlight 0.9 highlight-colour #ffffff", st), "");
	ASSERT_EQ(st.ramps.size(), 1u);
	EXPECT_TRUE(st.needsIds());
	std::vector<float> rgb = ramp;
	toon_look::apply(rgb, w, h, passes, st);
	const auto px = [&](int x, int y, int c) { return rgb[(static_cast<std::size_t>(y) * w + x) * 3 + c]; };
	const int sixty = static_cast<int>(0.6 * w);   // at 60% of the light: the skin (two bands) already in its bright band, the cloth (three) in its middle one
	EXPECT_GT(px(sixty, h - 2, 0), 1.3f * px(sixty, 1, 0)) << "the skin has its own two bands, the cloth the picture's three";
	EXPECT_GT(px(5, h - 2, 2), 1.5f * px(5, h - 2, 0)) << "the skin's shadow is tinted blue";
	EXPECT_NEAR(px(5, 1, 2), px(5, 1, 0), 0.02f) << "the cloth's shadow is not";
	EXPECT_NEAR(px(w - 2, h - 2, 0), px(w - 2, h - 2, 2), 0.02f) << "the skin's brightest end is the white highlight";
	EXPECT_GT(px(w - 2, h - 2, 1), px(w - 2, 1, 1)) << "brighter than the cloth's top band";

	// the scene line keeps the ramp
	toon_look::Style back;
	ASSERT_EQ(toon_look::parse(toon_look::tagOf(st).substr(10), back), "");
	ASSERT_EQ(back.ramps.size(), 1u);
	EXPECT_EQ(back.ramps[0].name, "skin");
	EXPECT_EQ(back.ramps[0].bands, 2);
	EXPECT_NEAR(back.ramps[0].shadowTint[2], st.ramps[0].shadowTint[2], 1e-3);
	EXPECT_EQ(back.ramps[0].highlight, 0.9);
	EXPECT_EQ(toon_look::tagOf(toon_look::Style()).find("highlight"), std::string::npos) << "a style without them writes none of the new words";
	EXPECT_NE(toon_look::parse("material skin lines 3", back).find("not a material's"), std::string::npos) << "a picture's word after a ramp is said";
}
