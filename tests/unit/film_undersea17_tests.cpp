// film_undersea17_tests.cpp - round 17, cartoons under the sea: the toon look by depth (film_undersea.h, film_sealight.h), the sea's characters (film_sea_kinds.h),
// characters meeting the water (film_world.cpp waterPlay), comic sea life (film_vignettes.h), new places and the ice (film_seabed.h, film_undersea.h), treasure and
// things let go in the water (film_treasure.h, rigid_world.h), a storm and the moon from below (film_sealight.h), and the story kit: the score, the muffled music and
// bubble captions (film_score.h, cartoon_audio.h, film_file.cpp), the starters (scripts/anim/starters).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/cartoon_audio.h"
#include "../../src/shared/cartoon/cartoon_films.h"
#include "../../src/shared/cartoon/film_score.h"
#include "../../src/shared/cartoon/film_sea_kinds.h"
#include "../../src/shared/cartoon/film_sealight.h"
#include "../../src/shared/cartoon/film_treasure.h"
#include "../../src/shared/cartoon/film_vignettes.h"
#include "../../src/shared/cartoon/rigid_world.h"

namespace ff = cartoon::film_file;
namespace undersea = cartoon::undersea;
namespace underwater = cartoon::underwater;
namespace sealight = cartoon::sealight;
namespace vignettes = cartoon::vignettes;
namespace treasure = cartoon::treasure;
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
	cartoon::film_kinds::sea::install();
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_undersea17_test_" + name);
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
const auto flat = [](double, double) { return -15.0; };

}  // namespace

// ---- 1: the cartoon look under the water --------------------------------------------------------------------------------------------------------------------------------

// A toon film under the water: its colour script follows the depth, its lines thin with distance, the caustic net is drawn in soft bands, and there is no marine snow.
TEST(FilmUndersea17, TheToonLookUnderTheWater) {
	EXPECT_NE(undersea::toonDepthLook(2.0), undersea::toonDepthLook(20.0)) << "deeper, bluer";
	EXPECT_LT(sealight::toonNet(0.2), sealight::toonNet(1.0));
	EXPECT_NEAR(sealight::toonNet(0.9), sealight::toonNet(1.1), 0.05) << "a flat band, not a ramp";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("toon", std::string(kSea) + "toon bands 3 lines 2\nshot 0 S\n  key 0 -10 4  0 -12 -4  50\n", film), "");
	EXPECT_NE(film.toonLine(1.0).find("line-depth"), std::string::npos);
	EXPECT_EQ(film.sceneAt(1.0).find("# marine snow"), std::string::npos);
}

// ---- 2: the sea's characters ----------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea17, SeaCharactersActAndSwim) {
	cartoon::film_kinds::install();
	cartoon::film_kinds::sea::install();
	const auto &kinds = ff::characterKinds();
	for (const char *k : {"fish", "crab", "octopus"}) ASSERT_TRUE(kinds.count(k)) << k;
	EXPECT_TRUE(kinds.at("fish").aquatic);
	EXPECT_TRUE(kinds.at("crab").crawls);
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("kinds", std::string(kSea) +
									"character finn fish at 0 -12 -4\ncharacter cora crab at 1 -15 -4\nwalk cora 1.0 0.3  2 -4\ngesture finn \"wave\" 1 1.5 3 3.5\n"
									"gesture cora \"snap\" 2 2.3 4 4.5\nshot 0 S\n  key 0 -12 2  0 -13 -4  50\n",
								film),
			  "");
	EXPECT_GT(cartoon::transformPoint(film.momentOf(*film.character("finn"), 5.0).frame, V3{0, 0, 0}).y, -13.0) << "the fish keeps its height";
	EXPECT_LT(cartoon::transformPoint(film.momentOf(*film.character("cora"), 5.0).frame, V3{0, 0, 0}).y, -14.5) << "the crab stays on the sand";
	EXPECT_FALSE(film.sceneAt(2.5).empty());
}

// ---- 3: characters and the water ----------------------------------------------------------------------------------------------------------------------------------------

// A character swimming fast leaves a wake of bubbles behind it; still, none.
TEST(FilmUndersea17, ASwimmerLeavesAWake) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("wake", std::string(kSea) + "character finn fish at 0 -12 0\nwalk finn 2.0 1.0  0 -8\nshot 0 S\n  key 4 -12 2  0 -12 -4  50\n", film), "");
	EXPECT_EQ(count(film.sceneAt(1.0), "# bubbles"), 0u) << "still";
	EXPECT_GT(count(film.sceneAt(5.0), "# bubbles"), 0u) << "swimming";
}

// ---- 4: comic sea life ----------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea17, ComicSeaLife) {
	vignettes::Puffer p;
	p.at = {0, -14, 0};
	EXPECT_LT(vignettes::puffedAt(p, 1.0, 5.0), 0.1) << "nobody near";
	EXPECT_GT(vignettes::puffedAt(p, 1.0, 0.5), 0.9) << "someone within a metre";
	p.at_ = {3.0};
	EXPECT_GT(vignettes::puffedAt(p, 4.0, 5.0), 0.9) << "at its own time";
	EXPECT_LT(vignettes::puffedAt(p, 9.0, 5.0), 0.1) << "and down again";
	vignettes::Flounder f;
	f.at = 2.0;
	EXPECT_EQ(vignettes::flounderText(f, 1.0, flat, underwater::Current{}).find("# silt"), std::string::npos);
	EXPECT_NE(vignettes::flounderText(f, 2.5, flat, underwater::Current{}).find("# silt"), std::string::npos) << "it bursts off the bottom";
	vignettes::Clownfish c;
	c.at = {0, 0, 0};
	EXPECT_NE(vignettes::clownfishText(c, 1.0, flat, {{10, 0, 0}}), vignettes::clownfishText(c, 1.0, flat, {{0.3, -14.6, 0}})) << "they hide";
	vignettes::Dugong d;
	d.at = {0, 0, 0};
	EXPECT_NE(vignettes::dugongText(d, 3.0, flat).find("# a dugong"), std::string::npos);
	EXPECT_EQ(problemWith("clownfish 0 0 -4 count 3\npufferfish 1 1 -5 at 4\nflounder 2 -6 at 3\nmoray -2 0.3 -7 toward 90 at 5\ndugong 0 -10 radius 5"), "");
	EXPECT_NE(problemWith("clownfish 0 0 -4 count 9"), "");
	EXPECT_NE(problemWith("cleanershrimp nobody"), "") << "a character of the film";
	EXPECT_EQ(problemWith("character finn fish at 0 -12 -4\ncleanershrimp finn from 1 to 5"), "");
}

// ---- 5: places and the ice ----------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea17, NewPlacesAndTheIce) {
	EXPECT_EQ(problemWith("seacave 0 -12 toward 180 radius 6\nruins 10 -20 length 14"), "");
	EXPECT_NE(problemWith("seacave 0 -12 radius 90"), "");
	undersea::IceCap ice;
	EXPECT_TRUE(undersea::iceCapText(ice, {0, -5, 0}, 0.0).empty()) << "none unless asked";
	ice.on = true, ice.holes = 4;
	const std::string text = undersea::iceCapText(ice, {0, -5, 0}, 0.0);
	EXPECT_NE(text.find("# sea ice overhead"), std::string::npos);
	EXPECT_NE(text.find("diffusetransmission"), std::string::npos) << "translucent";
	double cx = 0, cz = 0, r = 0;
	ASSERT_TRUE(undersea::iceHoleOf(ice, 0, 0, cx, cz, r)) << "holes 4: every cell has one";
	EXPECT_TRUE(undersea::inIceHole(ice, cx, cz));
	ice.holes = 0;
	EXPECT_FALSE(undersea::inIceHole(ice, cx, cz));
	EXPECT_EQ(problemWith("icecap thickness 2 holes 1"), "");
	ff::DataFilm dry;
	EXPECT_NE(loadFilm("dry17", "scene s.pbrt\nlength 12\nwater sea level 0\nicecap\n", dry).find("underwater"), std::string::npos) << "ice needs `underwater`";
}

// ---- 6: treasure ----------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea17, TreasureAndProps) {
	treasure::Chest ch;
	ch.open = 5.0;
	EXPECT_EQ(treasure::chestText(ch, 4.0, flat, 0.0, underwater::Current{}).find("AreaLightSource"), std::string::npos) << "shut: no glow";
	const std::string open = treasure::chestText(ch, 8.0, flat, 0.0, underwater::Current{});
	EXPECT_NE(open.find("AreaLightSource"), std::string::npos) << "the gold glows";
	EXPECT_NE(open.find("# bubbles"), std::string::npos) << "the air let out";
	treasure::Clam cl;
	cl.shut = {3.0};
	EXPECT_GT(treasure::clamGape(cl, 2.9), 0.5);
	EXPECT_LT(treasure::clamGape(cl, 3.5), 0.01) << "snapped shut";
	EXPECT_GT(treasure::clamGape(cl, 7.5), 0.5) << "and open again";
	treasure::Bottle b;
	b.at = {0, 0.0, 0};
	EXPECT_NE(treasure::bottleText(b, 1.0, 0.0, flat, underwater::Current{}), treasure::bottleText(b, 2.0, 0.0, flat, underwater::Current{})) << "it bobs";
	EXPECT_EQ(problemWith("chest 0 -6 toward 20 open 4\nclam 2 -6 shut 3 shut 8 nopearl\nbottle 0 2 -5\nshells 40 in -3 -8 3 -2"), "");
	EXPECT_NE(problemWith("shells 900 in -3 -8 3 -2"), "");
}

// ---- 7: water physics ---------------------------------------------------------------------------------------------------------------------------------------------------

// In water a thing reaches its terminal speed in a moment: a coin flutters down slowly, an anchor plunges, a ball rises and floats; and the rigid world is slowed.
TEST(FilmUndersea17, ThingsFallAsInWater) {
	EXPECT_NEAR(treasure::fallOf(10.0, 0.0, 2.0, 0.2) / 10.0, 2.0, 0.05) << "terminal speed";
	EXPECT_NEAR(treasure::fallOf(10.0, 8.0, 2.0, 0.2), treasure::fallOf(10.0, 0.0, 2.0, 0.2) + 8.0 * 0.2, 1e-6) << "the entry speed spent in a moment";
	const auto landedAfter = [](const std::string &kind) {
		treasure::Sink s;
		s.kind = kind, s.at = {0, -5, 0}, s.t = 0.0;
		double tilt = 0, landed = 0;
		treasure::sinkAt(s, 1.0, 0.0, flat, underwater::Current{}, tilt, landed);
		return landed;
	};
	EXPECT_GT(landedAfter("coin"), 3.0 * landedAfter("anchor")) << "a coin is slow";
	treasure::Sink ball;
	ball.kind = "ball", ball.at = {0, -10, 0};
	double tilt = 0, landed = 0;
	const V3 at = treasure::sinkAt(ball, 60.0, 0.0, flat, underwater::Current{}, tilt, landed);
	EXPECT_NEAR(at.y, 0.0, 0.1) << "floating at the surface";
	EXPECT_EQ(problemWith("sink coin 0 2 -5 at 1\nsink anchor 0 20 -5 at 3"), "");
	EXPECT_NE(problemWith("sink piano 0 2 -5 at 1"), "");
	EXPECT_NE(problemWith("sink coin 0 2 -5"), "") << "when";
	// the rigid world under the water: a ball dropped falls more slowly with the water's drag
	cartoon::rigid::BodyDef d;
	d.sphere = true, d.radius = 0.1, d.mass = 1.0, d.at = {0, 5, 0}, d.start = 0.0;
	const auto heightAfter = [&](double drag) { return cartoon::rigid::posesAt(cartoon::rigid::simulate({d}, 1.0, 0.0, nullptr, 9.81, drag), 0.8)[0].at.y; };
	EXPECT_GT(heightAfter(1.6), heightAfter(0.0) + 0.3);
}

// ---- 8: the storm and the moon from below -------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea17, AStormAndTheMoonFromBelow) {
	EXPECT_TRUE(sealight::foamText(0.0, 1.0, {0, -5, 0}, 0.0, 90).empty());
	EXPECT_NE(sealight::foamText(1.0, 1.0, {0, -5, 0}, 0.0, 90).find("# foam"), std::string::npos);
	EXPECT_EQ(sealight::foamText(1.0, 1.0, {0, -5, 0}, 0.0, 90), sealight::foamText(1.0, 1.0, {0, -5, 0}, 0.0, 90));
	const std::string sun = "LightSource \"distant\" \"point3 from\" [ 0.3 0.9 0.1 ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ 4 4 4 ]\n";
	sealight::Sun s;
	ASSERT_TRUE(sealight::findSun(sealight::dimSun(sun, 0.5), s));
	EXPECT_NEAR(s.L.x, 2.0, 1e-4);
	for (double t = 0.0; t < 20.0; t += 0.5) EXPECT_GE(sealight::stormLight(1.0, t), 0.44);
	EXPECT_TRUE(sealight::underFlashText(0.0, {0, 1, 0}).empty());
	EXPECT_NE(sealight::underFlashText(1.0, {100, 1400, -2000}).find("distant"), std::string::npos);
	sealight::Moon m;
	m.on = true;
	const std::string moon = sealight::moonText(m, {0, -5, 0});
	ASSERT_TRUE(sealight::findSun(moon, s)) << "the sea's caustics take the moon as the sun";
	EXPECT_LT(s.L.y, 0.2);
	EXPECT_EQ(problemWith("moonlight elevation 40 toward 120 bright 2"), "");
	EXPECT_NE(problemWith("moonlight elevation 140"), "");
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("storm", std::string(kSea) + "storm rain strength 1 from 0\nshot 0 S\n  key 0 -5 4  0 -6 -4  50\n", film), "");
	EXPECT_TRUE(film.spec.current.on) << "a storm overhead rocks the water below";
	EXPECT_GT(film.spec.current.surge, 0.0);
}

// ---- 9 and 10: the music, captions and starters -----------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea17, MusicHeardFromUnderTheWater) {
	cartoon::audio::Mix m(1.0);
	std::vector<float> tone(cartoon::audio::kRate);
	for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = static_cast<float>(std::sin(2.0 * 3.14159265 * 5000.0 * i / cartoon::audio::kRate));
	m.put(0.0, tone, 0.5, 0.0, 0.0);
	cartoon::audio::Mix muffled = m;
	muffled.lowPass(std::vector<float>(200, 900.0f));
	const auto loud = [](const cartoon::audio::Mix &x) {
		const std::vector<std::int16_t> pcm = x.finish(0.0, 0.0, 0.89, 0.0);
		double e = 0;
		for (std::size_t i = pcm.size() / 4; i < pcm.size() / 2; ++i) e += std::abs(static_cast<double>(pcm[i]));
		return e;
	};
	EXPECT_LT(loud(muffled), 0.3 * loud(m)) << "the highs gone";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("music", std::string(kSea) + "character finn fish at 0 -12 -4\nsay finn 1 \"Hello!\"\nshot 0 S\n  key 0 -10 4  0 -12 -4  50\n", film), "");
	const std::vector<float> fc = film.musicCutoffs();
	ASSERT_FALSE(fc.empty());
	EXPECT_LT(fc[300], 1500.0f) << "the camera is under the water";
	const std::string score = cartoon::score::draft(film);
	EXPECT_NE(score.find("send 0.9"), std::string::npos) << "the sea's style: deep in the reverb";
	EXPECT_NE(score.find("spread 0.07"), std::string::npos) << "bubbles of notes";
	const std::vector<cartoon::Caption> caps = film.lineCaptions();
	ASSERT_FALSE(caps.empty());
	EXPECT_TRUE(caps[0].bubble) << "a line said under the water, in a bubble";
}

// Each starter film loads and its frames are made.
TEST(FilmUndersea17, TheStartersLoad) {
	const std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "scripts" / "anim" / "starters";
	ASSERT_TRUE(std::filesystem::exists(dir)) << dir;
	int n = 0;
	for (const auto &e : std::filesystem::directory_iterator(dir)) {
		if (e.path().extension() != ".film") continue;
		std::ifstream in(e.path(), std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		for (const auto &[from, to] : std::vector<std::pair<std::string, std::string>>{{"{{key}}", "zz-starter"}, {"{{name}}", "Zz Starter"}, {"film_assets/wild/set.pbrt", "s.pbrt"}})
			for (std::size_t at; (at = text.find(from)) != std::string::npos;) text.replace(at, from.size(), to);
		ff::DataFilm film;
		ASSERT_EQ(loadFilm("starter_" + e.path().stem().string(), text, film), "") << e.path();
		EXPECT_FALSE(film.sceneAt(film.spec.length / 2.0).empty());
		++n;
	}
	EXPECT_GE(n, 3);
}
