// film_animals13_tests.cpp - round 13, real animals: more species, coat patterns, hero fur, gaits, hunts and the young, animals in water, flights that take off and
// land, life under the sea (film_sealife.h), the small world close up with the renderer's thin-film iridescence (film_macro.h, fresnel.h FrThinFilm), and night shots,
// heat shimmer and animal calls (film_night.h, lens_look.h, film_soundscape.h).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/cartoon_films.h"
#include "../../src/shared/cartoon/film_coats.h"
#include "../../src/shared/cartoon/film_macro.h"
#include "../../src/shared/cartoon/film_night.h"
#include "../../src/shared/cartoon/film_sealife.h"
#include "../../src/shared/cartoon/film_soundscape.h"
#include "../../src/shared/cartoon/film_wildlife.h"
#include "../../src/shared/bxdfs_conductor.h"
#include "../../src/shared/fresnel.h"
#include "../../src/shared/lens_look.h"

namespace ff = cartoon::film_file;
namespace wildlife = cartoon::wildlife;
namespace animals = cartoon::animals;
using V3 = cartoon::Vec3;

namespace {

const char *kOpenSet = R"(LookAt 0 2 12  0 1 0  0 1 0
Camera "perspective" "float fov" [ 40 ]
Film "rgb" "integer xresolution" [ 320 ] "integer yresolution" [ 180 ]
Sampler "halton" "integer pixelsamples" [ 16 ]
WorldBegin
LightSource "infinite" "rgb L" [ 0.2 0.25 0.3 ]
)";

std::string loadFilm(const std::string &name, const std::string &filmText, ff::DataFilm &film) {
	cartoon::film_kinds::install();
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_animals13_test_" + name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt", std::ios::binary) << kOpenSet;
	std::ofstream(d / (name + ".film"), std::ios::binary) << filmText;
	return film.load((d / (name + ".film")).string());
}
std::string problemWith(const std::string &lines) {
	ff::DataFilm bad;
	return loadFilm("bad", "scene s.pbrt\nlength 4\n" + lines + "\n", bad);
}
std::size_t count(const std::string &text, const std::string &what) {
	std::size_t n = 0;
	for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
	return n;
}

}  // namespace

// ---- 1 to 3: species, coats, hero fur ----------------------------------------------------------------------------------------------------------------------------------------

// Every kind has its model (with the clips its gaits use) and its numbers; a posed object of each is written once a frame.
TEST(FilmAnimals13, EveryKindHasAModelAndPoses) {
	for (const char *kind : {"deer", "horses", "wolves", "lions", "zebras", "giraffes", "elephants", "bears", "foxes", "rabbits", "penguins", "snakes"}) {
		SCOPED_TRACE(kind);
		const animals::Species *sp = animals::species(kind);
		ASSERT_NE(sp, nullptr);
		EXPECT_GT(sp->walkSpeed, 0.0);
		EXPECT_LE(sp->walkSpeed, sp->fleeSpeed);
		std::string why;
		const auto m = animals::modelOf(kind, &why);
		ASSERT_TRUE(m) << why;
		EXPECT_GE(m->clip("walk"), 0);
		animals::Poses poses;
		std::string text;
		const std::string a = poses.poseName(kind, "walk", 3, text), b = poses.poseName(kind, "walk", 3, text);
		EXPECT_EQ(a, b);
		EXPECT_EQ(count(text, "ObjectBegin"), 1u) << "written once";
		EXPECT_NE(text.find("trianglemesh"), std::string::npos);
	}
	const auto eagle = animals::modelOf("eagles");
	ASSERT_TRUE(eagle);
	EXPECT_GE(eagle->clip("perch"), 0);
	EXPECT_GE(eagle->clip("flare"), 0);
}

// Coat patterns: a zebra's stripes are dark and light, the same every time; each variant is its own; a kind's own pattern when none is named.
TEST(FilmAnimals13, CoatPatternsAreStableAndVaried) {
	namespace coats = cartoon::coats;
	EXPECT_EQ(coats::defaultFor("zebras"), "zebra");
	EXPECT_EQ(coats::defaultFor("giraffes"), "giraffe");
	EXPECT_EQ(coats::defaultFor("deer"), "");
	for (const std::string &p : {std::string("zebra"), std::string("leopard"), std::string("giraffe")}) {
		SCOPED_TRACE(p);
		double lo = 1e9, hi = -1e9, differ = 0.0;
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x) {
				const V3 c = coats::colourAt(p, 0, {0.9, 0.85, 0.8}, {0.9, 0.85, 0.8}, x / 16.0, 0.3 + y / 64.0);
				const V3 d = coats::colourAt(p, 1, {0.9, 0.85, 0.8}, {0.9, 0.85, 0.8}, x / 16.0, 0.3 + y / 64.0);
				const double l = c.x + c.y + c.z;
				EXPECT_TRUE(std::isfinite(l));
				lo = std::min(lo, l), hi = std::max(hi, l), differ += std::fabs(l - (d.x + d.y + d.z));
			}
		EXPECT_GT(hi - lo, 0.8) << "dark marks on a light coat";
		EXPECT_GT(differ, 0.5) << "a second animal's pattern is its own";
	}
}

// A hero's fur is real strands (pbrt curves in the hair material); the rest of a herd keeps its plain coat.
TEST(FilmAnimals13, AHeroGetsStrandsOfFur) {
	wildlife::Herd h;
	h.kind = "zebras", h.count = 3, h.fx = -5, h.tx = 5, h.heroes = 1;
	animals::Poses poses;
	const std::string text = wildlife::herdText(h, 1.0, nullptr, poses);
	EXPECT_NE(text.find("\"hair\""), std::string::npos);
	EXPECT_NE(text.find("\"curve\""), std::string::npos);
	EXPECT_EQ(count(text, "ObjectInstance"), 3u);
}

// ---- 4 to 7: gaits, behaviour, water, flight ----------------------------------------------------------------------------------------------------------------------------------

// A hunt: lions stalk, then run the zebras down - with `catch` one of the zebras is on its side at the end; without, none is.
TEST(FilmAnimals13, AHuntEndsWithACatchOnlyWhenAsked) {
	wildlife::Herd lions, zebras;
	lions.kind = "lions", lions.count = 3, lions.fx = -32, lions.tx = -28, lions.name = "pride";
	zebras.kind = "zebras", zebras.count = 8, zebras.fx = 0, zebras.tx = 5, zebras.name = "herd";
	wildlife::Hunt hu;
	hu.predators = "pride", hu.prey = "herd", hu.at = 1.0, hu.kill = true;
	std::vector<wildlife::Animal> P, Q;
	wildlife::huntAt(hu, lions, zebras, 30.0, nullptr, P, Q);
	ASSERT_EQ(Q.size(), 8u);
	EXPECT_GE(std::count_if(Q.begin(), Q.end(), [](const wildlife::Animal &a) { return a.fallen; }), 1);
	hu.kill = false;
	wildlife::huntAt(hu, lions, zebras, 30.0, nullptr, P, Q);
	EXPECT_EQ(std::count_if(Q.begin(), Q.end(), [](const wildlife::Animal &a) { return a.fallen; }), 0);
	EXPECT_NE(problemWith("herd lions 2 from 0 0 to 5 0 name a\nhunt a b at 2"), "") << "a hunt names two herds";
}

// The young: a herd with young has more animals in it, the young ones half their mothers' size.
TEST(FilmAnimals13, YoungFollowTheirMothers) {
	wildlife::Herd h;
	h.kind = "deer", h.count = 4, h.fx = -5, h.tx = 5;
	animals::Poses p0, p1;
	const std::string without = wildlife::herdText(h, 2.0, nullptr, p0);
	h.young = 2;
	const std::string with = wildlife::herdText(h, 2.0, nullptr, p1);
	EXPECT_EQ(count(with, "ObjectInstance"), count(without, "ObjectInstance") + 2);
}

// Animals in water throw spray and leave a wake; out of it they are wet (the wet coat's own objects).
TEST(FilmAnimals13, AnimalsSplashAndComeOutWet) {
	wildlife::Herd h;
	h.kind = "horses", h.count = 2, h.fx = -20, h.tx = 40, h.flee = 0.0;
	animals::Poses p0, p1;
	const std::string dry = wildlife::herdText(h, 3.0, nullptr, p0);
	const std::string inWater = wildlife::herdText(h, 3.0, nullptr, p1, {}, {}, 0.4);   // (the ground at 0, the water's top at 0.4)
	EXPECT_EQ(dry.find("\"sphere\""), std::string::npos);
	EXPECT_NE(inWater.find("\"sphere\""), std::string::npos) << "spray";
	EXPECT_NE(inWater.find("1.33"), std::string::npos) << "water's drops";
}

// A flight: perched before it goes, flying (flapping or gliding) on the way, perched where it lands.
TEST(FilmAnimals13, AFlightTakesOffFliesAndLands) {
	wildlife::Flight f;
	f.kind = "eagles", f.from = {0, 5, 0}, f.to = {60, 8, 0}, f.at = 2.0;
	animals::Poses p;
	const std::string before = wildlife::flightText(f, 1.0, p), during = wildlife::flightText(f, 6.0, p), after = wildlife::flightText(f, 200.0, p);
	EXPECT_NE(before.find("_perch_"), std::string::npos);
	EXPECT_TRUE(during.find("_flap_") != std::string::npos || during.find("_glide_") != std::string::npos);
	EXPECT_NE(after.find("_perch_"), std::string::npos);
	EXPECT_NE(problemWith("flight eagles 2 from 0 5 0 to 0.5 5 0 at 1"), "") << "from and to a metre apart at least";
}

// ---- 8: under the sea --------------------------------------------------------------------------------------------------------------------------------------------------------------

// A bait ball: every fish placed; a strike's hunter is there only around its strike, and fish near its path are pushed away from it.
TEST(FilmAnimals13, ABaitBallSplitsRoundAStrike) {
	namespace sea = cartoon::sealife;
	sea::BaitBall b;
	b.at = {0, -8, 0}, b.count = 300, b.radius = 2.0, b.strikes = {5.0};
	const std::string quiet = sea::ballText(b, 1.0), strike = sea::ballText(b, 5.0);
	EXPECT_EQ(count(quiet, "ObjectInstance \"sea_bait_"), 300u);
	EXPECT_EQ(count(quiet, "sea_shark_"), 0u);
	EXPECT_EQ(count(strike, "sea_shark_"), 1u);
	// the fish's places: the nearest to the ball's middle are further from it at the strike (the hunter is there) than in the quiet
	const auto nearest = [](const std::string &text) {
		const std::regex tr("ConcatTransform \\[(?:\\s+\\S+){12}\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+1\\.00000 \\]\\s+ObjectInstance \"sea_bait");
		double best = 1e9;
		for (std::sregex_iterator it(text.begin(), text.end(), tr), end; it != end; ++it)
			best = std::min(best, std::hypot(std::stod((*it)[1]), std::stod((*it)[2]) + 8.0, std::stod((*it)[3])));
		return best;
	};
	EXPECT_GT(nearest(strike), nearest(quiet) + 0.2);
	// the dolphins' strike has no shark in it
	b.hunter = "dolphins";
	EXPECT_EQ(count(sea::ballText(b, 5.0), "sea_shark_"), 0u);
}

// Jellyfish glow without lighting anything; reef fish come in their colours with the clownfish's bands; on a terrain the heights are above the ground.
TEST(FilmAnimals13, SeaLifeParsesAndDraws) {
	namespace sea = cartoon::sealife;
	sea::Life life;
	life.jellies.push_back({{-1, 0, -1}, {1, 1, 1}, 3, 1, 1.0});
	life.reefs.push_back({{-1, 0, -1}, {1, 1, 1}, 10, 1});
	life.turtles.push_back({{0, 2, 0}, 2, 1, 4.0});
	const std::string text = sea::text(life, 2.0);
	EXPECT_EQ(count(text, "\"bool sampled\" false"), 3u) << "each jellyfish's rim glows, lighting nothing";
	EXPECT_NE(text.find("diffusetransmission"), std::string::npos) << "a jellyfish's bell lets the light through";
	EXPECT_EQ(count(text, "ObjectBegin \"sea_reef_2_"), 8u) << "the clownfish, in each pose";
	const sea::Life lifted = sea::lifted(life, [](double, double) { return -20.0; });
	EXPECT_DOUBLE_EQ(lifted.turtles[0].at.y, -18.0);
	EXPECT_DOUBLE_EQ(lifted.jellies[0].lo.y, -20.0);
	EXPECT_EQ(problemWith("baitball 0 -5 0 count 200 strike 3 strike 6 hunter sharks\nreeffish 30 in -5 -10 -5 5 -8 5\njellyfish 6 in -3 -8 -3 3 -2 3 glow 2\nseaturtles 2 at 0 -6 0"), "");
	EXPECT_NE(problemWith("baitball 0 -5 0 hunter orcas"), "");
	EXPECT_NE(problemWith("jellyfish 6 in -3 -8 -3 3 -2"), "");
}

// ---- 9: the small world and thin-film iridescence ---------------------------------------------------------------------------------------------------------------------------

// The thin film: with no film (or a film of the air's index) it is the bare conductor's Fresnel exactly; it stays within 0 .. 1; a film colours the reflection
// (unequal channels on a grey base) and the colour changes with the angle it is seen at.
TEST(ThinFilm, IsTheBareConductorWithoutAFilmAndColoursWithOne) {
	for (double c : {1.0, 0.8, 0.5, 0.2, 0.05})
		for (const auto &[eta, k] : std::vector<std::pair<double, double>>{{0.2, 3.9}, {1.5, 0.0}, {1.0, 2.0}, {2.8, 3.1}}) {
			const double bare = FrComplex(c, eta, k);
			EXPECT_NEAR(FrThinFilm(c, 550.0, 0.0, 1.45, eta, k), bare, 1e-9) << "no film";
			EXPECT_NEAR(FrThinFilm(c, 550.0, 400.0, 1.0, eta, k), bare, 1e-9) << "a film of air";
			for (double nm : {100.0, 300.0, 700.0}) {
				const double r = FrThinFilm(c, 550.0, nm, 1.45, eta, k);
				EXPECT_GE(r, 0.0);
				EXPECT_LE(r, 1.0 + 1e-9);
			}
		}
	double r0, g0, b0, r1, g1, b1;
	FrThinFilmRGB(1.0, 300.0, 1.56, 1.0, 1.0, 1.0, 0.46, 0.46, 0.46, r0, g0, b0);   // (a dark base: a beetle's)
	FrThinFilmRGB(0.3, 300.0, 1.56, 1.0, 1.0, 1.0, 0.46, 0.46, 0.46, r1, g1, b1);
	EXPECT_GT(std::max({r0, g0, b0}) - std::min({r0, g0, b0}), 0.02) << "coloured";
	EXPECT_GT(std::fabs(r0 / (r0 + g0 + b0) - r1 / (r1 + g1 + b1)) + std::fabs(b0 / (r0 + g0 + b0) - b1 / (r1 + g1 + b1)), 0.02) << "the colour changes with the angle";
	// the shared BxDF uses it
	ConductorBxDF<double> bare{1.5, 1.5, 1.5, 0.0, 0.0, 0.0, 0.1, 0.1}, filmed{1.5, 1.5, 1.5, 0.0, 0.0, 0.0, 0.1, 0.1, 300.0, 1.56};
	double a[3], f[3];
	bare.fresnel(0.9, a[0], a[1], a[2]);
	filmed.fresnel(0.9, f[0], f[1], f[2]);
	EXPECT_NEAR(a[0], FrComplex(0.9, 1.5, 0.0), 1e-12);
	EXPECT_GT(std::fabs(f[0] - a[0]) + std::fabs(f[2] - a[2]), 0.01);
}

// Insects and dew: bees' and dragonflies' wings are fans with the blur's alpha picture; beetles' shells and a dragonfly's body have the film; dew is drops of water.
TEST(FilmAnimals13, InsectsAndDewDraw) {
	namespace macro = cartoon::macro;
	macro::World w;
	macro::Insects bees, dragonflies, beetles;
	bees.kind = "bees", bees.count = 2, bees.lo = {-0.1, 0.05, -0.1}, bees.hi = {0.1, 0.1, 0.1};
	dragonflies.kind = "dragonflies", dragonflies.count = 1, dragonflies.lo = bees.lo, dragonflies.hi = bees.hi;
	beetles.kind = "beetles", beetles.count = 3, beetles.lo = {-0.05, 0.0, -0.05}, beetles.hi = {0.05, 0.0, 0.05};
	w.insects = {bees, dragonflies, beetles};
	macro::Dew d;
	d.count = 20;
	w.dews.push_back(d);
	const auto flat = [](double, double) { return 0.0; };
	const std::string a = macro::text(w, 1.0, flat), b = macro::text(w, 1.6, flat);
	EXPECT_EQ(count(a, "\"texture alpha\" \"rt_wing_blur\""), 2u * 2u + 1u * 4u) << "two wings a bee, four a dragonfly";
	EXPECT_EQ(count(a, "Texture \"rt_wing_blur\""), 1u);
	EXPECT_EQ(count(a, "filmthickness"), 3u + 1u + 8u) << "three beetles' shells, a dragonfly's body and the eight wings";
	EXPECT_NE(a.find("\"dielectric\" \"float eta\" [ 1.33 ]"), std::string::npos) << "dew";
	EXPECT_GE(count(a, "\"sphere\""), 20u) << "a drop or more on each blade";
	EXPECT_NE(a, b) << "they move";
	EXPECT_EQ(problemWith("insects bees 3 in -0.1 0.05 -0.1 0.1 0.1 0.1 size 1.2\ninsects beetles 2 in -0.1 0 -0.1 0.1 0 0.1 film 420\ndew 100 in -0.2 -0.2 0.2 0.2"), "");
	EXPECT_NE(problemWith("insects wasps 3 in 0 0 0 1 1 1"), "");
}

// ---- 10: night, shimmer, calls --------------------------------------------------------------------------------------------------------------------------------------------------

// At night with eye-shine the pupils glow (lighting nothing); a camera trap adds its flash and its look; shimmer writes its words and its time into the look line.
TEST(FilmAnimals13, NightShotsAndShimmer) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("night", "scene s.pbrt\nlength 6\nherd wolves 3 from -5 0 to 5 0\nnight eyeshine 2\ncameratrap ir name TRAIL 7 clock 23:59 temp 4\nshimmer 0.004\n", film), "");
	const std::string s = film.sceneAt(1.0);
	EXPECT_NE(s.find("_shine200"), std::string::npos) << "the shining eyes' own objects";
	EXPECT_NE(s.find("LightSource \"point\""), std::string::npos) << "the flash";
	EXPECT_NE(s.find(" saturation 0 "), std::string::npos) << "infrared: no colour";
	EXPECT_NE(s.find("shimmer 0.00400 shimmer-time 1.0000"), std::string::npos);
	EXPECT_EQ(cartoon::night::stampWords(cartoon::night::Night{0, 0, true, true, "TRAIL 7", "2026-10-30", 23 * 60 + 59, 4.0, 0.0}, 61.0), "TRAIL 7     2026-10-30   00:00:01     +4 C")
		<< "the clock runs on past midnight";
	EXPECT_NE(problemWith("shimmer 0.5"), "");
	EXPECT_NE(problemWith("cameratrap clock 25:00"), "");
	// the shimmer moves the picture near its foot and leaves its top alone
	lens_look::Look look;
	ASSERT_EQ(lens_look::parse("shimmer 0.01 shimmer-time 2", look), "");
	const int W = 64, H = 64;
	std::vector<float> img(W * H * 3);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
			for (int k = 0; k < 3; ++k) img[(y * W + x) * 3 + k] = (x / 4) % 2 ? 1.0f : 0.0f;   // (stripes up and down)
	std::vector<float> out = img;
	lens_look::apply(out.data(), W, H, look);
	double top = 0.0, foot = 0.0;
	for (int x = 0; x < W; ++x) top += std::fabs(out[(2 * W + x) * 3] - img[(2 * W + x) * 3]), foot += std::fabs(out[((H - 3) * W + x) * 3] - img[((H - 3) * W + x) * 3]);
	EXPECT_LT(top, 1e-4);
	EXPECT_GT(foot, 1.0);
}

// Calls: each calling kind has one, loud enough to hear and never broken (no NaN); a rabbit has none.
TEST(FilmAnimals13, AnimalsCall) {
	namespace ss = cartoon::soundscape;
	for (const char *kind : {"lions", "elephants", "wolves", "bears", "foxes", "zebras", "penguins", "horses", "deer"}) {
		SCOPED_TRACE(kind);
		const ss::Call c = ss::callOf(kind, 7u);
		ASSERT_FALSE(c.sound.empty());
		double peak = 0.0;
		bool finite = true;
		for (float v : c.sound) peak = std::max(peak, static_cast<double>(std::fabs(v))), finite = finite && std::isfinite(v);
		EXPECT_TRUE(finite);
		EXPECT_GT(peak, 0.05);
		EXPECT_LT(peak, 4.0);
		EXPECT_GT(c.every, 0.0);
	}
	EXPECT_TRUE(ss::callOf("rabbits", 1u).sound.empty()) << "a rabbit is quiet";
}
