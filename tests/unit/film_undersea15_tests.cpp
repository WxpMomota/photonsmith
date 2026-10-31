// film_undersea15_tests.cpp - round 15, under the sea: the caustic net over everything and the sun seen from below (film_sealight.h), a scuba diver and a diver's camera
// (film_diver.h, film_file.cpp), schools that behave and the big animals (film_school.h), reefs, small creatures, night dives and kelp forests (film_reef.h).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/cartoon_films.h"
#include "../../src/shared/cartoon/film_diver.h"
#include "../../src/shared/cartoon/film_reef.h"
#include "../../src/shared/cartoon/film_school.h"
#include "../../src/shared/cartoon/film_sealight.h"

namespace ff = cartoon::film_file;
namespace sealight = cartoon::sealight;
namespace diver = cartoon::diver;
namespace school = cartoon::school;
namespace reef = cartoon::reef;
namespace underwater = cartoon::underwater;
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
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_undersea15_test_" + name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt", std::ios::binary) << kOpenSet;
	std::ofstream(d / (name + ".film"), std::ios::binary) << filmText;
	return film.load((d / (name + ".film")).string());
}
const char *kSea = "scene s.pbrt\nlength 12\nwater sea level 0 waves 0.4\nunderwater\n";
std::string problemWith(const std::string &lines) {
	ff::DataFilm bad;
	return loadFilm("bad", std::string(kSea) + lines + "\n", bad);
}
std::size_t count(const std::string &text, const std::string &what) {
	std::size_t n = 0;
	for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
	return n;
}
const auto flat = [](double, double) { return -10.0; };
const std::string kSunLine = "LightSource \"distant\" \"point3 from\" [ 0.30000 0.80000 0.20000 ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ 2.00000 1.90000 1.80000 ]\n";

}  // namespace

// ---- 1, 2: light ------------------------------------------------------------------------------------------------------------------------------------------------------------

// The caustic slide takes most of the sun (the sun keeps a fifth), aimed down the sun's way bent by the surface; it reaches less far round a camera near the sand (its
// pixels are smaller there); a frame without a sun is left as it was.
TEST(FilmUndersea15, TheCausticNetCarriesMostOfTheSun) {
	const V3 bent = sealight::refractedSun(cartoon::normalize(V3{0.6, 0.5, 0.0}));
	EXPECT_GT(bent.y, 0.5 / std::sqrt(0.61)) << "steeper under the water";
	EXPECT_NEAR(std::hypot(bent.x, bent.z), 0.6 / std::sqrt(0.61) / 1.333, 1e-6) << "Snell's law";
	EXPECT_LT(sealight::reachFor(0.2), sealight::reachFor(10.0));
	EXPECT_EQ(sealight::reachFor(100.0), sealight::kReach);
	const std::string out = sealight::withCaustics(kSunLine, "undersea15_test", 1.0, {0, -4, 0}, 0.0, 10.0);
	EXPECT_NE(out.find("LightSource \"projection\""), std::string::npos);
	EXPECT_NE(out.find("\"rgb L\" [ 0.40000 0.38000 0.36000 ]"), std::string::npos) << "the sun keeps 1 - 0.8 of its light";
	EXPECT_NE(out.find("caustic_slide_"), std::string::npos);
	EXPECT_EQ(sealight::withCaustics("WorldBegin\n", "undersea15_test", 1.0, {0, -4, 0}, 0.0, 10.0), "WorldBegin\n") << "no sun: nothing";
}

// Looking up, the sun is glints on the ripples (lighting nothing) and the look draws them as stars; rain lands on the surface as rings; above the water, neither.
TEST(FilmUndersea15, TheSunAndTheRainFromBelow) {
	const std::string glints = sealight::glintsText(kSunLine, 2.0, {0, -5, 0}, 0.0);
	EXPECT_NE(glints.find("\"bool sampled\" false"), std::string::npos);
	EXPECT_TRUE(sealight::glintsText(kSunLine, 2.0, {0, 3, 0}, 0.0).empty()) << "above the water";
	EXPECT_NE(sealight::glintLook().find("star"), std::string::npos);
	EXPECT_FALSE(sealight::rainText(1.0, 2.0, {0, -3, 0}, 0.0).empty());
	EXPECT_TRUE(sealight::rainText(0.0, 2.0, {0, -3, 0}, 0.0).empty());
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("rain", "scene s.pbrt\nlength 4\nsky 11:00 11:00\nwater sea level 0\nunderwater\nweather rain\nshot 0 S\n  key 0 -4 4  0 -2 -4  60\n", film), "");
	const std::string s = film.sceneAt(1.0);
	EXPECT_NE(s.find("# rain on the surface, from below"), std::string::npos);
	EXPECT_EQ(s.find("\"rgb reflectance\" [ 0.8 0.83 0.88 ]"), std::string::npos) << "no rain falling through the water (film_particles.h's streaks)";
	EXPECT_NE(s.find("# the caustic net over everything"), std::string::npos);
}

// ---- 3, 10: a diver, and a diver's camera ----------------------------------------------------------------------------------------------------------------------------

// A diver waits, swims the path at their pace, and stops at its end; breathes bubbles out from the regulator; carries a lamp; makes the OK sign.
TEST(FilmUndersea15, ADiverSwimsBreathesAndSignals) {
	diver::Diver d;
	d.name = "Ana", d.path = {{0, -5, 0}, {10, -5, 0}}, d.at = 2.0, d.speed = 0.5, d.lamp = true, d.oks = {6.0};
	EXPECT_NEAR(diver::diverAt(d, 0.0).x, diver::diverAt(d, 1.5).x, 1e-6) << "waiting";
	EXPECT_NEAR(diver::diverAt(d, 12.0).x, 5.0, 0.3);
	EXPECT_NEAR(diver::diverAt(d, 60.0).x, 10.0, 0.5) << "stopped at the end";
	V3 f;
	diver::diverAt(d, 8.0, &f);
	EXPECT_GT(f.x, 0.9);
	EXPECT_GT(diver::mouthAt(d, 8.0).x, diver::diverAt(d, 8.0).x) << "the regulator ahead of the chest";
	EXPECT_NEAR(diver::okness(d, 7.0), 1.0, 1e-9);
	EXPECT_EQ(diver::okness(d, 10.0), 0.0);
	const std::string text = diver::diverText(d, 9.0, 0.0, underwater::Current());
	EXPECT_NE(text.find("# a diver: Ana"), std::string::npos);
	EXPECT_NE(text.find("LightSource \"spot\""), std::string::npos) << "the lamp";
	EXPECT_NE(text.find("# bubbles"), std::string::npos) << "the breath";
	EXPECT_NE(text.find("\"dielectric\" \"float eta\" [ 1.5 ]"), std::string::npos) << "the mask";
	EXPECT_EQ(problemWith("diver Ana from 0 2 0 to 5 2 -5 via 2 2 -2 at 1 speed 0.4 lamp ok 4 suit red"), "");
	EXPECT_NE(problemWith("diver Ana from 0 2 0"), "") << "where they go";
	EXPECT_NE(problemWith("divecam"), "") << "a shot's word";
}

// A shot can follow a diver by name, and with `divecam` the camera rises and sinks with each breath.
TEST(FilmUndersea15, TheCameraFollowsADiverAndBreathes) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("follow", std::string(kSea) + "diver Ana from -4 3 0 to 4 3 -6 at 0 speed 0.6\nshot 0 With her\n  follow Ana\nshot 6 Breathing\n  divecam 2\n  key 0 2 4  0 2 -4  55\n  key 0 2 4  0 2 -4  55\n", film), "");
	const auto eyeAt = [&](double t) {
		V3 eye, target;
		double fov = 0.0;
		film_subjects::cameraOf(film.headerAt(t), eye, target, fov);
		return eye;
	};
	EXPECT_GT(cartoon::length(eyeAt(4.0) - eyeAt(1.0)), 1.0) << "following her";
	double lo = 1e9, hi = -1e9;
	for (double t = 6.2; t < 11.0; t += 0.25) lo = std::min(lo, eyeAt(t).y), hi = std::max(hi, eyeAt(t).y);
	EXPECT_GT(hi - lo, 0.15) << "rising and sinking with the breath";
}

// ---- 4, 6: schools and the big animals ---------------------------------------------------------------------------------------------------------------------------------

// A school parts round what it keeps clear of and bolts from a hunter's dash: the fish near the hunter's path are further from it than they would be without fright.
TEST(FilmUndersea15, ASchoolPartsAndBolts) {
	school::School s;
	s.kind = "sardines", s.count = 300, s.at = {0, -8, 0}, s.radius = 3.0;
	school::School calm = s;
	s.strikes = {6.0};
	school::prepare(calm, 10.0, nullptr);
	school::prepare(s, 10.0, nullptr);
	ASSERT_EQ(s.dashes.size(), 1u);
	V3 h;
	ASSERT_TRUE(school::hunterAt(s.dashes[0], 6.0, h));
	int close = 0, closeCalm = 0;   // (fish within 1.2 m of where the hunter is as it reaches the middle)
	for (int i = 0; i < s.count; ++i) {
		close += cartoon::length(school::fishAt(s, i, 6.0) - h) < 1.2 ? 1 : 0;
		closeCalm += cartoon::length(school::fishAt(calm, i, 6.0) - h) < 1.2 ? 1 : 0;
	}
	EXPECT_LT(close * 2, closeCalm) << "they bolt from the hunter's path (" << close << " near it, against " << closeCalm << " in a calm school)";
	// keeping clear of the camera sitting in the school's middle
	school::School around = calm;
	school::prepare(around, 10.0, [&](double) { return std::vector<V3>{V3{0, -8, 0}}; });
	double clear = 1e9;
	for (int i = 0; i < around.count; ++i) clear = std::min(clear, cartoon::length(school::fishAt(around, i, 9.0) - V3{0, -8, 0}));
	EXPECT_GT(clear, 1.0) << "a hole round the camera";
	const std::string text = school::schoolText(s, 0, 6.0);
	EXPECT_EQ(count(text, "ObjectInstance"), 300u);
	EXPECT_EQ(problemWith("school sardines 400 at 0 4 -12 radius 4 strike 3 hunter dolphins"), "");
	EXPECT_NE(problemWith("school tuna 40 at 0 0 0"), "");
}
// Each big animal is drawn; a whale shark has its spots.
TEST(FilmUndersea15, TheBigAnimals) {
	for (const std::string &kind : school::bigKinds()) {
		SCOPED_TRACE(kind);
		school::BigFish b;
		b.kind = kind, b.count = 1, b.at = {0, -8, 0};
		EXPECT_NE(school::bigText(b, 2.0).find("trianglemesh"), std::string::npos);
	}
	cartoon::Mesh mouth, spots;
	school::whaleShark(9.0, 0.0, mouth, spots);
	EXPECT_GT(spots.verts.size(), 500u);
	EXPECT_EQ(problemWith("bigfish whaleshark 1 at 0 6 -20 radius 14\nbigfish humpbacks 1 at 0 8 -30"), "");
	EXPECT_NE(problemWith("bigfish krakens 1 at 0 0 0"), "");
}

// ---- 5, 7, 8, 9: reefs, small creatures, the night, kelp ---------------------------------------------------------------------------------------------------------------

// A reef: its still corals made once (PLY files), the soft ones swaying; bleached it is white; fluorescent its polyps glow.
TEST(FilmUndersea15, AReef) {
	reef::Reef r;
	r.count = 30, r.seed = 3;
	const std::string a = reef::reefText("undersea15_test", r, 0, 1.0, flat, {0, 0, 0}), b = reef::reefText("undersea15_test", r, 0, 2.0, flat, {0, 0, 0});
	EXPECT_GE(count(a, "Shape \"plymesh\""), 3u);
	EXPECT_NE(a, b) << "the soft corals sway";
	r.bleached = true, r.fluoresce = true;
	const std::string c = reef::reefText("undersea15_test", r, 0, 1.0, flat, {0, 0, 0});
	EXPECT_NE(c.find("0.92000 0.92000 0.90000"), std::string::npos) << "white";
	EXPECT_NE(c.find("\"bool sampled\" false"), std::string::npos) << "glowing polyps";
	EXPECT_EQ(problemWith("reef 60 in -8 -14 8 -4 bleached\nunderwater lamp 1 blue"), "");
	EXPECT_NE(problemWith("reef 60 in -8 -14 8"), "");
}
// The small creatures are drawn; a nudibranch creeps.
TEST(FilmUndersea15, SmallCreatures) {
	for (const std::string &kind : reef::critterKinds()) {
		SCOPED_TRACE(kind);
		reef::Critters c;
		c.kind = kind, c.count = 2;
		EXPECT_NE(reef::crittersText(c, 1.0, flat).find("trianglemesh"), std::string::npos);
	}
	reef::Critters n;
	n.kind = "nudibranchs", n.count = 1;
	EXPECT_NE(reef::crittersText(n, 0.0, flat), reef::crittersText(n, 30.0, flat));
	EXPECT_EQ(problemWith("critters seahorses 2 in -1 -1 1 1"), "");
	EXPECT_NE(problemWith("critters squid 2 in -1 -1 1 1"), "");
}
// At night the water lights up behind what moves; the corals spawn, the bundles rising; the reef fish sleep.
TEST(FilmUndersea15, ANightDive) {
	const std::string glow = reef::glowText({[](double t) { return V3{t, -5, 0}; }}, 3.0, 1.0);
	EXPECT_NE(glow.find("\"bool sampled\" false"), std::string::npos);
	reef::Reef r;
	r.count = 10;
	const std::string early = reef::spawnText({r}, {}, 2.0, 1.0, 9.0, flat, underwater::Current()), late = reef::spawnText({r}, {}, 8.0, 1.0, 9.0, flat, underwater::Current());
	EXPECT_GT(count(late, "\"sphere\""), count(early, "\"sphere\""));
	EXPECT_TRUE(reef::spawnText({r}, {}, 0.5, 1.0, 9.0, flat, underwater::Current()).empty()) << "before it starts";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("night", std::string(kSea) + "reeffish 10 in -2 1 -4 2 2 -2\nnightdive glow 2 spawn 1 9\nreef 20 in -3 -5 3 -2\nshot 0 S\n  key 0 -12 4  0 -13 -4  50\n", film), "");
	const std::string s = film.sceneAt(4.0);
	EXPECT_NE(s.find("# coral spawning"), std::string::npos);
	EXPECT_NE(problemWith("nightdive spawn 5 2"), "") << "spawning ends after it starts";
}
// A kelp forest reaches the surface and lies along it in a canopy.
TEST(FilmUndersea15, AKelpForestReachesTheSurface) {
	reef::KelpForest k;
	k.count = 5;
	const std::string text = reef::kelpForestText(k, 1.0, 0.0, flat, {0, 0, 0});
	double top = -1e9;
	static const std::regex P(R"("point3 P" \[ ([^\]]*)\])");
	for (std::sregex_iterator i(text.begin(), text.end(), P), e; i != e; ++i) {
		std::istringstream in((*i)[1].str());
		V3 p;
		while (in >> p.x >> p.y >> p.z) top = std::max(top, p.y);
	}
	EXPECT_NEAR(top, -0.1, 0.4) << "up to the surface";
	EXPECT_EQ(problemWith("kelpforest 30 in -20 -30 -8 -14"), "");
}

// ---- the whole sea ----------------------------------------------------------------------------------------------------------------------------------------------------

// A film with every round-15 line: each is in its frame.
TEST(FilmUndersea15, AWholeDive) {
	ff::DataFilm film;
	const std::string text = std::string("scene s.pbrt\nlength 10\nsky 11:00 11:00\nwater sea level 0 waves 0.4\nunderwater clarity 2\n") +
							 "diver Ana from -4 3 -2 to 4 3 -8 at 0 lamp\nschool sardines 200 at 0 4 -10 strike 5\nbigfish whaleshark 1 at 0 6 -20\nreef 30 in -6 -12 6 -4\n"
							 "critters nudibranchs 2 in -0.5 -2.5 0.5 -2\nkelpforest 6 in -12 -20 -6 -12\nshot 0 Dive\n  divecam\n  key 0 -12 4  0 -13 -6  55\n  key 1 -12 3  0 -13 -6  55\n";
	ASSERT_EQ(loadFilm("dive", text, film), "");
	const std::string a = film.sceneAt(3.0);
	for (const char *what : {"# a diver: Ana", "# a school of sardines", "# whaleshark", "# a coral reef", "# nudibranchs", "# a kelp forest", "# the caustic net over everything"})
		EXPECT_NE(a.find(what), std::string::npos) << what;
}
