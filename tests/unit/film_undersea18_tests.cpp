// film_undersea18_tests.cpp - round 18, cartoons under the sea: more sea characters (film_sea_cast.h), what flows (film_flow.h), submarines (film_subs.h), air under the
// water (film_airpocket.h), towns (film_seatown.h), fast lanes and chases (film_fastlane.h), schools that perform (film_shows.h), giants (film_abyss.h), things from above
// and the deep's places (film_fromabove.h), drawn looks (film_seatoon.h), and the film's glue (film_parse_deep.h, film_seaworld.cpp).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/cartoon_audio.h"
#include "../../src/shared/cartoon/cartoon_films.h"

namespace ff = cartoon::film_file;
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
	cartoon::film_kinds::seacast::install();
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_undersea18_test_" + name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt", std::ios::binary) << kOpenSet;
	std::ofstream(d / (name + ".film"), std::ios::binary) << filmText;
	return film.load((d / (name + ".film")).string());
}
const char *kSea = "scene s.pbrt\nlength 30\nwater sea level 0 waves 0.3\nunderwater\n";
const char *kShot = "shot 0 S\n  key 0 -10 4  0 -12 -6  55\n";
std::string problemWith(const std::string &lines) {
	ff::DataFilm bad;
	return loadFilm("bad", std::string(kSea) + lines + "\n", bad);
}
std::size_t count(const std::string &text, const std::string &what) {
	std::size_t n = 0;
	for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
	return n;
}
double energy(const std::vector<std::int16_t> &pcm, double t0, double t1) {   // (interleaved stereo; t counts 48 kHz frames, the mix's are audio::kRate's - 44.1 kHz)
	double e = 0.0;
	for (std::size_t i = static_cast<std::size_t>(t0 * 48000.0) * 2; i < std::min(pcm.size(), static_cast<std::size_t>(t1 * 48000.0) * 2); ++i) e += static_cast<double>(pcm[i]) * pcm[i];
	return e;
}
const auto flat = [](double, double) { return -15.0; };

}  // namespace

// ---- 1: more of the sea's characters ---------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea18, SeaCastKindsDrawAndAct) {
	cartoon::film_kinds::install();
	cartoon::film_kinds::sea::install();
	cartoon::film_kinds::seacast::install();
	const auto &kinds = ff::characterKinds();
	for (const char *k : {"mermaid", "merman", "seaturtle", "shark", "jellyfish", "seahorse", "puffer"}) {
		ASSERT_TRUE(kinds.count(k)) << k;
		EXPECT_TRUE(kinds.at(k).aquatic) << k << " swims as it is";
		ff::CharacterMoment m;
		m.frame = cartoon::identity();
		EXPECT_FALSE(kinds.at(k).text(m).empty()) << k;
		EXPECT_NE(std::find(kinds.at(k).poses.begin(), kinds.at(k).poses.end(), "puff"), kinds.at(k).poses.end()) << k;
	}
	EXPECT_TRUE(kinds.at("mermaid").flows) << "her hair streams";
	EXPECT_TRUE(kinds.at("jellyfish").flows);
	EXPECT_TRUE(static_cast<bool>(kinds.at("mermaid").headFrame)) << "she takes `hair` strands";
	// a pufferfish blown up on cue: bigger, and spiny
	std::vector<cartoon::rig::Gesture> puff{{"puff", 1.0, 1.3, 9.0, 9.5}};
	ff::CharacterMoment calm, blown;
	calm.frame = blown.frame = cartoon::identity();
	calm.gestures = blown.gestures = &puff;
	calm.t = 0.5, blown.t = 4.0;
	const std::string a = kinds.at("puffer").text(calm), b = kinds.at("puffer").text(blown);
	EXPECT_GT(b.size(), a.size() + 2000) << "its spines out";
	// a mermaid swimming lies level: her lean turns her a long way, treading water it does not
	ff::CharacterMoment treading, swimming;
	treading.frame = swimming.frame = cartoon::identity();
	swimming.swim = 1.0, swimming.going = 1.0;
	const V3 up{0.0, 1.0, 0.0};
	const auto turnOf = [&](const ff::CharacterMoment &m) {
		const cartoon::Mat4 l = kinds.at("mermaid").lean(m);
		return cartoon::transformPoint(l, up) - cartoon::transformPoint(l, {0.0, 0.0, 0.0});
	};
	EXPECT_GT(turnOf(treading).y, 0.95);
	EXPECT_LT(turnOf(swimming).y, 0.3) << "laid level";
}

// ---- 2: what flows --------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea18, ChainsTrailBehindAndRestWhenStill) {
	cartoon::flow::Wake moving, still;
	for (int k = 0; k < cartoon::flow::kWakeSteps; ++k) {   // (going +z at 1.5 m/s; facing +z)
		moving.frames.push_back(cartoon::translate(0.0, 0.0, -1.5 * cartoon::flow::kWakeStep * k));
		still.frames.push_back(cartoon::identity());
	}
	moving.wet = still.wet = true;
	const std::vector<V3> trail = cartoon::flow::chain(moving, cartoon::identity(), {0.0, 1.6, 0.0}, {0.0, 0.4, -1.0}, 0.6, 8, 0.0, 1);
	ASSERT_EQ(trail.size(), 9u);
	EXPECT_LT(trail.back().z, -0.45) << "streaming behind as it goes";
	EXPECT_NEAR(cartoon::length(trail.back() - trail.front()), 0.6, 0.06) << "it does not stretch";
	const std::vector<V3> rest = cartoon::flow::chain(still, cartoon::identity(), {0.0, 1.6, 0.0}, {0.0, 1.0, 0.0}, 0.6, 8, 0.0, 1);
	EXPECT_GT(rest.back().y, 2.1) << "floating up, as its rest shape says";
	EXPECT_TRUE(cartoon::flow::chain(cartoon::flow::Wake{}, cartoon::identity(), {0, 0, 0}, {0, -1, 0}, 1.0, 4, 0.0, 1).back().y < -0.9) << "nowhere it has been: its rest shape";
}

TEST(FilmUndersea18, GarmentsAreWornAndFlow) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("wear", "scene s.pbrt\nlength 10\ncharacter ann person at 0 0 0\nwalk ann 1 1.2  0 -6\nwear ann cape colour red\nwear ann scarf colour 0.1,0.2,0.8 length 0.8\n" + std::string(kShot), film), "");
	const std::string scene = film.sceneAt(3.0);
	EXPECT_EQ(count(scene, "worn by its character"), 2u);
	EXPECT_NE(problemWith("wear nobody cape").find("no character called nobody"), std::string::npos);
	EXPECT_NE(problemWith("character ann person at 0 -12 0\nwear ann toga").find("wear WHO"), std::string::npos);
}

// ---- 3: submarines ----------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea18, SubsGoTheirWayAndCarryTheirCrew) {
	cartoon::subs::Sub s;
	s.path = {{0.0, -12.0, 0.0}, {20.0, -12.0, 0.0}};
	s.speed = 2.0, s.from = 1.0;
	V3 f;
	EXPECT_NEAR(cartoon::subs::subAt(s, 0.5, &f).x, 0.0, 1e-6) << "waiting until it sets off";
	EXPECT_NEAR(cartoon::subs::subAt(s, 4.0, &f).x, 6.0, 1e-6);
	EXPECT_NEAR(f.x, 1.0, 1e-6);
	EXPECT_LT(cartoon::subs::subAt(s, 100.0).x, 20.0 + 1e-6) << "it stops at its last point";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("sub", std::string(kSea) + "sub round nemo at 0 3 -8 to 12 3 -8 speed 1.5 dive 4\ncharacter ann person at 0 -12 -8\naboard ann nemo\nsay ann 2 \"Dive!\"\n" + kShot, film), "");
	EXPECT_TRUE(film.spec.riders.count("ann") && film.spec.dry.count("ann"));
	V3 at;
	film.momentOf(film.spec.characters[0], 6.0, &at);
	const V3 sub = cartoon::subs::subAt(film.spec.subs[0], film.natureAt(6.0));
	EXPECT_LT(std::hypot(-at.x - sub.x, at.z - sub.z), 0.6) << "in its cabin";
	EXPECT_LT(film.momentOf(film.spec.characters[0], 6.0).swim, 0.5) << "sitting, not swimming";
	const std::string scene = film.sceneAt(6.0);
	EXPECT_GE(count(scene, "LightSource \"spot\""), 2u) << "its searchlights";
	EXPECT_NE(scene.find("thindielectric"), std::string::npos) << "its dome";
	V3 feet, front, chest;
	EXPECT_TRUE(film.wildSubject("nemo", 6.0, feet, front, chest)) << "a shot can follow it";
	EXPECT_NE(problemWith("character ann person at 0 -12 0\naboard ann ghost").find("no boat or sub"), std::string::npos);
}

// ---- 4: air under the water -------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea18, AirPocketsBubblesAndHelmets) {
	std::vector<cartoon::undersea::Bubble> spilled;
	cartoon::underwater::Current still;
	const std::string bell = cartoon::airpocket::bellText({{0.0, -10.0, -5.0}, 2.4, 1.0, 1}, 10.0, 0.0, still, spilled);
	EXPECT_NE(bell.find("0.7502"), std::string::npos) << "the air inside it: the water's surface from below";
	cartoon::airpocket::Big b;
	b.at = {0.0, -10.0, 0.0}, b.radius = 0.5, b.t = 1.0;
	V3 at;
	double r = 0.0;
	EXPECT_FALSE(cartoon::airpocket::bigAt(b, 0.5, 0.0, still, at, r)) << "not blown yet";
	ASSERT_TRUE(cartoon::airpocket::bigAt(b, 5.0, 0.0, still, at, r));
	EXPECT_GT(at.y, -10.0);
	EXPECT_GT(r, 0.5) << "it grows as it rises (Boyle)";
	EXPECT_FALSE(cartoon::airpocket::bigAt(b, 300.0, 0.0, still, at, r)) << "gone at the surface";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("air", std::string(kSea) + "character ann person at 0 -12 -6\nhelmet ann\nsay ann 1 \"Hello\"\nblowring ann at 2\nbigbubble 2 2 -6 radius 0.4\n" + kShot, film), "");
	const std::string scene = film.sceneAt(3.0);
	EXPECT_NE(scene.find("# a bubble helmet"), std::string::npos);
	EXPECT_NE(scene.find("# a bubble ring"), std::string::npos);
	EXPECT_NE(scene.find("# a big bubble"), std::string::npos);
	EXPECT_TRUE(film.spec.dry.count("ann")) << "it talks without bubbles";
	EXPECT_GT(film.momentOf(film.spec.characters[0], 3.0).swim, 0.5) << "but still swims";
}

// ---- 5: towns ----------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea18, ATownsTrafficWaitsAtTheSignal) {
	cartoon::seatown::Town w;
	w.traffic = 40;
	V3 f;
	const V3 c{0.0, -15.0, 0.0};
	bool waited = false;
	for (int i = 3; i < w.traffic; i += 4)   // (the street fish: one is waiting at the plaza's edge while it is red)
		for (double t = 6.05; t < 11.5; t += 0.25) {
			const V3 a = cartoon::seatown::fishAt(w, i, t, c, f), b = cartoon::seatown::fishAt(w, i, t + 0.25, c, f);
			if (std::hypot(a.x - b.x, a.z - b.z) < 0.03) waited = true;
		}
	EXPECT_TRUE(waited);
	const std::string text = cartoon::seatown::townText(w, 3.0, flat);
	EXPECT_NE(text.find("AreaLightSource"), std::string::npos) << "its windows lit";
	EXPECT_NE(problemWith("seatown 0 0 radius 200").find("seatown X Z"), std::string::npos);
}

// ---- 6: fast lanes and chases -----------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea18, ALaneCarriesItsRiderAndAChaserFollows) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("lane", std::string(kSea) + "fastlane eac at -20 3 -8 to 0 3 -8 to 20 3 -8 speed 3\ncharacter mira mermaid at -18 -12 -8\nride mira eac from 2 to 9\n"
												  "character finn fish at 0 -12 -4\nwalk finn 1 1.0  6 -4  6 -10\ncharacter bruce shark at -3 -12 -4\nchase bruce after finn lag 1.5\n" + kShot, film), "");
	ASSERT_EQ(film.spec.lanes.size(), 1u);
	EXPECT_GT(film.spec.lanes[0].total(), 39.0);
	V3 a, b;
	film.momentOf(film.spec.characters[0], 4.0, &a);
	film.momentOf(film.spec.characters[0], 6.0, &b);
	EXPECT_NEAR(-b.x - (-a.x), 6.6, 0.8) << "swept along at the lane's speed";
	EXPECT_GT(film.momentOf(film.spec.characters[0], 5.0).swim, 0.5);
	V3 finn, bruce;
	film.momentOf(film.spec.characters[1], 3.0, &finn);
	film.momentOf(film.spec.characters[2], 4.5, &bruce);
	EXPECT_LT(cartoon::length(finn - bruce), 1e-6) << "where its quarry was 1.5 s before";
	EXPECT_NE(problemWith("character mira mermaid at 0 -12 0\nride mira nowhere from 1").find("no fast lane"), std::string::npos);
	EXPECT_NE(problemWith("character a fish at 0 -12 0\ncharacter b fish at 1 -12 0\ncharacter c fish at 2 -12 0\nchase a after b\nchase b after c").find("chasing someone too"), std::string::npos);
}

TEST(FilmUndersea18, AChaseEndsWhenItsLineSays) {   // (`to T` is a time, not a place as a lane's is)
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("chaseto", std::string(kSea) + "character finn fish at 0 -12 -4\nwalk finn 1 1.0  6 -4  6 -10\ncharacter bruce shark at -3 -12 -4\nchase bruce after finn from 2 to 5 lag 1\n" + kShot, film), "");
	ASSERT_EQ(film.spec.chases.size(), 1u);
	EXPECT_NEAR(film.spec.chases[0].from, 2.0, 1e-9);
	EXPECT_NEAR(film.spec.chases[0].to, 5.0, 1e-9);
	V3 finn, bruce;
	film.momentOf(film.spec.characters[0], 3.0, &finn);
	film.momentOf(film.spec.characters[1], 4.0, &bruce);
	EXPECT_LT(cartoon::length(finn - bruce), 1e-6) << "during the chase: where its quarry was";
	film.momentOf(film.spec.characters[1], 9.0, &bruce);
	EXPECT_NEAR(bruce.x, 3.0, 1e-6) << "after it: back to its own way (it has no walk of its own: where it started, story space)";
}

// ---- 7: schools that perform ------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea18, SchoolsMakeShapesAndWords) {
	EXPECT_TRUE(cartoon::shows::writable("HELLO!"));
	EXPECT_FALSE(cartoon::shows::writable("hello world"));
	EXPECT_FALSE(cartoon::shows::writable("ABCDEFGHIJKLM"));
	cartoon::shows::Show heart;
	heart.shape = "heart";
	double width = 0.0;
	for (int i = 0; i < 200; ++i) width = std::max(width, std::abs(cartoon::shows::shapePoint(heart, i, 200).x));
	EXPECT_GT(width, 0.35);
	EXPECT_LT(width, 0.65);
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("show", std::string(kSea) + "school sardines 120 at 0 6 -12 radius 2\nschoolshow 1 ring at 2 for 10 size 4 toward 0 centre 0 7 -12\nchoir 1 at 1 for 6\n" + kShot, film), "");
	const cartoon::school::School &sc = film.schoolsSwum()[0];
	double off = 0.0;   // (mid-show, every fish on the ring: 2 m from its middle, in the plane facing +z)
	for (int i = 0; i < sc.count; ++i) {
		const V3 p = cartoon::school::fishAt(sc, i, film.natureAt(7.0));
		off += std::abs(std::hypot(p.x - 0.0, p.y - (-8.0)) - 2.0) + std::abs(p.z - (-12.0));
	}
	EXPECT_LT(off / sc.count, 0.25);
	V3 v;
	cartoon::school::fishAt(sc, 0, film.natureAt(7.0), &v);
	EXPECT_GT(cartoon::length(v), 0.05) << "a fish in a shape still faces its way";
	EXPECT_NE(problemWith("schoolshow 2 heart at 1").find("no school 2"), std::string::npos);
}

// ---- 8: giants in the dark --------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea18, GiantsRiseGlowAndCanBeFollowed) {
	cartoon::abyss::Kraken k;
	k.at = {0.0, -10.0, -30.0}, k.glow = 5.0;
	EXPECT_EQ(cartoon::abyss::krakenText(k, 2.0).find("AreaLightSource"), std::string::npos) << "dark until it glows";
	EXPECT_NE(cartoon::abyss::krakenText(k, 8.0).find("AreaLightSource"), std::string::npos);
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("giants", std::string(kSea) + "seaserpent nessie at 20 3 -18 to -20 3 -18 length 25 speed 4\nhugeeye 4 3 -12 open 1\nreveal 0 8 -40 at 2\nwhalefall -6 -10\n" + kShot, film), "");
	V3 feet, front, chest, later;
	ASSERT_TRUE(film.wildSubject("nessie", 2.0, feet, front, chest));
	film.wildSubject("nessie", 4.0, later, front, chest);
	EXPECT_NEAR(-(later.x - feet.x), -8.0, 0.5) << "swimming along its way at 4 m/s (pbrt's x falls)";
	const std::string scene = film.sceneAt(5.0);
	for (const char *what : {"# a sea serpent", "# a huge eye", "# a light that reveals", "# a whale fall"}) EXPECT_NE(scene.find(what), std::string::npos) << what;
}

// ---- 9: things from above, the deep's places ---------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea18, HooksNetsAnchorsAndTheDeepsPlaces) {
	cartoon::fromabove::Hook h;
	h.at = 1.0, h.depth = 3.0, h.yank = 40.0;
	EXPECT_NEAR(cartoon::fromabove::hookY(h, 3.0, 0.0, -15.0), -1.0, 1e-6) << "going down at half a metre a second";
	EXPECT_LT(cartoon::fromabove::hookY(h, 35.0, 0.0, -15.0), -11.5) << "jigging over the bottom";
	EXPECT_GT(cartoon::fromabove::hookY(h, 43.0, 0.0, -15.0), 0.0) << "snatched up";
	double landed = 1e30;
	cartoon::fromabove::anchorY({0.0, 0.0, 1.0}, 2.0, 0.0, -15.0, landed);
	EXPECT_GT(landed, 3.0);
	EXPECT_LT(landed, 20.0);
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("above", std::string(kSea) + "school sardines 150 at 3 8 -12 radius 2\nnet 3 -12 at 1 size 7 school 1 lift 15\nhook -2 -6 at 0\nanchor 5 -8 at 1\n"
												   "brinepool -6 -16 radius 4\npillowlava 8 -16 radius 3\nturbidity -10 -20 toward 90 at 3\nboat rowboat dinghy at -2 -8\n" + kShot, film), "")
		<< "a boat floats over an underwater film";
	const cartoon::school::School &sc = film.schoolsSwum()[0];
	double before = -1e30, after = -1e30;
	for (int i = 0; i < sc.count; ++i) before = std::max(before, cartoon::school::fishAt(sc, i, film.natureAt(14.0)).y), after = std::max(after, cartoon::school::fishAt(sc, i, film.natureAt(22.0)).y);
	EXPECT_GT(after, before + 2.0) << "hauled up in the net";
	const std::string scene = film.sceneAt(20.0);
	for (const char *what : {"# a net", "# a hook and line", "# an anchor", "# a brine pool", "# pillow lava", "# an underwater avalanche"}) EXPECT_NE(scene.find(what), std::string::npos) << what;
	EXPECT_NE(problemWith("brinicle 0 0 at 1").find("needs an `icecap`"), std::string::npos);
	EXPECT_NE(problemWith("net 0 0 at 1 school 3").find("no such school"), std::string::npos);
}

// ---- 10: drawn looks ---------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea18, DrawnLooksWipesWobblesRaysAndSparks) {
	std::vector<cartoon::undersea::Bubble> wall;
	cartoon::seatoon::wipeBubbles(5.0, 3.0, {0, 0, 0}, {0, 0, -1}, 40.0, wall);
	EXPECT_TRUE(wall.empty()) << "before the wipe";
	cartoon::seatoon::wipeBubbles(5.0, 5.0, {0, 0, 0}, {0, 0, -1}, 40.0, wall);
	EXPECT_GT(wall.size(), 150u) << "the wall across the frame at the cut";
	EXPECT_TRUE(cartoon::seatoon::wobbleLook({{2.0, 6.0, 0.5}}, 1.0).empty());
	EXPECT_NE(cartoon::seatoon::wobbleLook({{2.0, 6.0, 0.5}}, 4.0).find("shimmer"), std::string::npos);
	cartoon::seatoon::Sparkle s;
	s.on = true;
	EXPECT_TRUE(cartoon::seatoon::sparkleText(s, {{{0, -5, 0}, {0, -5, 0}, {0, -5, 0}}}, 3.0).empty()) << "only what moves stirs it";
	EXPECT_NE(cartoon::seatoon::sparkleText(s, {{{0, -5, 0}, {0.3, -5, 0}, {0.6, -5, 0}}}, 3.0).find("# bubbles (film_undersea.h)"), std::string::npos) << "drawn without ink, as the bubbles are";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("toon", std::string(kSea) + "sky 11:00 11:00\ntoon bands 3 lines 2\ncharacter finn fish at 0 -12 -5\nwalk finn 1 3.0  6 -5\nspeedlines finn\ngulp finn at 2\n"
												  "drawnrays\nseasparkle\nbubblewipe at 4\nwaterwobble during 5 8\n" + kShot, film), "");
	EXPECT_NE(film.sceneAt(4.0).find("# bubbles"), std::string::npos);
	EXPECT_NE(film.sceneAt(6.0).find("shimmer"), std::string::npos);
	EXPECT_NE(problemWith("character finn fish at 0 -12 0\ngulp nobody at 1").find("no character called nobody"), std::string::npos);
}

// ---- the round as a whole ----------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea18, TheRoundsLinesNeedTheSeaAndAreHeard) {
	ff::DataFilm dry;
	EXPECT_NE(loadFilm("dry", "scene s.pbrt\nlength 5\nseatown 0 0\n", dry).find("needs an `underwater` line"), std::string::npos);
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("heard", std::string(kSea) + "sub robot rov at 0 2 -6 toward 180\nshot 0 S\n  key 0 -12 2  0 -12 -6  55\n", film), "");
	cartoon::audio::Mix m(10.0);
	film.deepSounds(m);
	EXPECT_GT(energy(m.finish(0.0, 0.0), 1.0, 9.0), 0.0) << "its motor and its sonar";
}

// A line takes its own words only (another line's from a shared list was taken and did nothing), and what nature does is heard when it is seen: in a timelapse, an anchor
// going in at nature's 20 s splashes at film time 5 s.
TEST(FilmUndersea18, OwnWordsAndNaturesTimeHeard) {
	EXPECT_EQ(problemWith("character finn person at 0 -12 -6\ngulp finn at 2"), "");
	EXPECT_NE(problemWith("character finn person at 0 -12 -6\ngulp finn at 2 size 3"), "") << "a gulp has no size";
	EXPECT_EQ(problemWith("divingbell 0 -10 -8 size 2"), "");
	EXPECT_NE(problemWith("divingbell 0 -10 -8 rise 2"), "") << "a bell does not rise";
	EXPECT_EQ(problemWith("kraken 0 -10 -20 reach 4 -10 -14 from 4"), "");
	EXPECT_NE(problemWith("kraken 0 -10 -20 from 4"), "") << "`from` is for its reach";
	EXPECT_NE(problemWith("hook 0 -6 at 1 colour 1 0 0"), "") << "a hook has no colour";
	EXPECT_EQ(problemWith("reveal 0 8 -40 at 2 colour 0.5 0.7 1.0"), "");
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("timelapse", std::string(kSea) + "timelapse 4\nanchor 0 -8 at 20\nshot 0 S\n  key 0 -10 4  0 -12 -6  55\n", film), "");
	cartoon::audio::Mix m(12.0);
	film.deepSounds(m);
	const std::vector<std::int16_t> pcm = m.finish(0.0, 0.0);
	const double seen = film.filmTimeOf(20.0);   // (where the picture shows it go in: nature's 20 s)
	EXPECT_NEAR(film.natureAt(seen), 20.0, 1e-3);
	EXPECT_LT(seen, 8.0) << "a timelapse: well before film time 20";
	const double s = static_cast<double>(cartoon::audio::kRate) / 48000.0;   // (energy() counts 48 kHz frames; the mix is kRate's)
	EXPECT_EQ(energy(pcm, 0.0, (seen - 0.02) * s), 0.0) << "nothing before it";
	EXPECT_GT(energy(pcm, seen * s, (seen + 0.6) * s), 0.0) << "its splash, then";
}
