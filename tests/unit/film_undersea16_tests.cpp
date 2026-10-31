// film_undersea16_tests.cpp - round 16, under the sea: cartoon characters swimming and talking under the water (film_file.cpp, film_world.cpp, film_kinds.h), a boat
// overhead and a sardine run (film_boat.h), hydrothermal vents (film_vents.h), silt and murky water, a wreck's corridor, the sand close up and a camera's housing
// (film_undersea.h, film_seabed.h), set pieces of behaviour (film_behaviour.h), and the sounds of the sea (film_world.cpp soundscape, film_dressing.h).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/cartoon_audio.h"
#include "../../src/shared/cartoon/cartoon_films.h"
#include "../../src/shared/cartoon/film_behaviour.h"
#include "../../src/shared/cartoon/film_boat.h"
#include "../../src/shared/cartoon/film_dressing.h"
#include "../../src/shared/cartoon/film_sealight.h"
#include "../../src/shared/cartoon/film_vents.h"
#include "../../src/shared/lens_look.h"

namespace ff = cartoon::film_file;
namespace boat = cartoon::boat;
namespace vents = cartoon::vents;
namespace behaviour = cartoon::behaviour;
namespace undersea = cartoon::undersea;
namespace underwater = cartoon::underwater;
namespace sealight = cartoon::sealight;
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
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_undersea16_test_" + name);
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
double energy(const std::vector<std::int16_t> &pcm, double a, double b) {
	double e = 0.0;
	const std::size_t i0 = static_cast<std::size_t>(a * cartoon::audio::kRate) * 2, i1 = std::min(pcm.size(), static_cast<std::size_t>(b * cartoon::audio::kRate) * 2);
	for (std::size_t i = i0; i < i1; ++i) e += std::abs(static_cast<double>(pcm[i]));
	return e / std::max<std::size_t>(1, i1 - i0);
}

}  // namespace

// ---- 1: cartoon characters under the water --------------------------------------------------------------------------------------------------------------------------------

// A character under the water swims its walk at its own height (not down on the seabed), and talking it breathes out bubbles from its mouth.
TEST(FilmUndersea16, ACharacterSwimsAndTalksInBubbles) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("swim", std::string(kSea) + "character ann person at 0 -6 0\nwalk ann 1.0 0.5  0 -4\nsay ann 6.0 \"Look at all the fish!\"\nshot 0 S\n  key 3 -6 2  0 -6 -2  50\n", film), "");
	const auto bubbles = [&](double t) { return count(film.sceneAt(t), "# bubbles"); };
	EXPECT_EQ(bubbles(4.0), 0u) << "quiet: no bubbles";
	EXPECT_GT(bubbles(7.0), 0u) << "talking: bubbles from the mouth";
	ff::DataFilm dry;
	ASSERT_EQ(loadFilm("dry", "scene s.pbrt\nlength 12\ncharacter ann person at 0 0 0\nsay ann 6.0 \"Hello there!\"\nshot 0 S\n  key 3 1 2  0 1 -2  50\n", dry), "");
	EXPECT_EQ(count(dry.sceneAt(7.0), "# bubbles"), 0u) << "on land: none";
}

// ---- 2, 3: a boat overhead and a sardine run --------------------------------------------------------------------------------------------------------------------------------

// A boat goes its way and eases to a stop for its anchor; drawn, it has its hull, its wake's bubbles, and the anchor falls on its chain and lands in a puff of silt.
TEST(FilmUndersea16, ABoatOverheadDropsItsAnchor) {
	boat::Boat b;
	b.x = 0.0, b.z = 0.0, b.toward = 90.0, b.speed = 2.0, b.t0 = 0.0, b.anchor = 10.0;
	EXPECT_NEAR(boat::boatAt(b, 5.0, 0.0).x, 10.0, 1e-9) << "under way";
	EXPECT_NEAR(boat::boatAt(b, 10.0, 0.0).x, boat::boatAt(b, 30.0, 0.0).x, 1e-9) << "at anchor";
	EXPECT_LT(boat::boatAt(b, 10.0, 0.0).x, 20.0) << "slowing before it";
	const std::string under = boat::boatText(b, 5.0, 0.0, flat, underwater::Current());
	EXPECT_NE(under.find("# a boat"), std::string::npos);
	EXPECT_NE(under.find("# bubbles"), std::string::npos) << "the wake";
	const std::string landed = boat::boatText(b, 20.0, 0.0, flat, underwater::Current());
	EXPECT_NE(landed.find("# silt"), std::string::npos) << "the anchor's landing";
	EXPECT_EQ(problemWith("boat 0 -14 toward 90 length 10 speed 2 during 6 40 anchor 16"), "");
	EXPECT_NE(problemWith("boat 0 -14 length 100"), "") << "60 m at most";
}

// In a sardine run gannets plunge into the water each at its time, dolphins and sharks round the bait ball.
TEST(FilmUndersea16, ASardineRun) {
	boat::Run r;
	r.at = {0.0, -6.0, 0.0}, r.from = 2.0;
	V3 f;
	double since = 0.0;
	EXPECT_GT(boat::gannetAt(r, 0, boat::diveTime(r, 0) - 1.0, 0.0, f, since).y, 0.0) << "in the air before its dive";
	EXPECT_LT(boat::gannetAt(r, 0, boat::diveTime(r, 0) + 0.5, 0.0, f, since).y, 0.0) << "under the water after it";
	const std::string text = boat::runText(r, 8.0, 0.0, underwater::Current());
	EXPECT_NE(text.find("# the sardine run"), std::string::npos);
	EXPECT_EQ(problemWith("sardinerun 0 6 -30 radius 3 gannets 8 dolphins 3 from 22"), "");
	EXPECT_NE(problemWith("sardinerun 0 6 -30 gannets 100"), "");
}

// ---- 4: hydrothermal vents ------------------------------------------------------------------------------------------------------------------------------------------------

// Vents stand on the seabed, each pouring a black plume (a grid medium that absorbs) that spreads before it reaches the surface.
TEST(FilmUndersea16, VentsPourBlackPlumes) {
	vents::Vents v;
	v.count = 2, v.x0 = -4, v.z0 = -4, v.x1 = 4, v.z1 = 4;
	double H = 0.0, R = 0.0;
	const V3 foot = vents::ventAt(v, 0, flat, H, R);
	EXPECT_NEAR(foot.y, -10.0, 1e-9);
	EXPECT_GT(H, 1.0);
	const std::string deep = vents::text("undersea16_test", v, 0, 3.0, flat, 1e30);
	EXPECT_NE(deep.find("# hydrothermal vents"), std::string::npos);
	EXPECT_EQ(count(deep, "\"rgbgrid\""), 2u) << "a plume each";
	// (a sea 2 m over the chimneys' mouths: the plumes stop short of the surface, or there are none)
	const std::string shallow = vents::text("undersea16_test", v, 0, 3.0, flat, -10.0 + H + 2.0);
	static const std::regex scale(R"(Scale \S+ (\S+) \S+\n  MakeNamedMedium)");
	for (std::sregex_iterator it(shallow.begin(), shallow.end(), scale), end; it != end; ++it) EXPECT_LT(std::atof((*it)[1].str().c_str()), 2.0);
	EXPECT_EQ(problemWith("vents 3 in -6 -46 6 -40"), "");
	EXPECT_NE(problemWith("vents 40 in -6 -46 6 -40"), "");
}

// ---- 5: silt and murk ---------------------------------------------------------------------------------------------------------------------------------------------------

// Silt kicked up billows out and settles, gone after a while; a murky patch is a grid of the sea's own water plus the murk, the same as the sea at its edge (no seam).
TEST(FilmUndersea16, SiltAndMurkyWater) {
	EXPECT_TRUE(undersea::siltText({0, -10, 0}, -1.0, 1.0, 3).empty()) << "not yet";
	EXPECT_NE(undersea::siltText({0, -10, 0}, 2.0, 1.0, 3).find("# silt"), std::string::npos);
	EXPECT_TRUE(undersea::siltText({0, -10, 0}, 40.0, 1.0, 3).empty()) << "settled";
	underwater::Water w;
	undersea::Murk m;
	m.at = {0, -5, 0}, m.radius = 4.0;
	const std::string murk = undersea::murkText(m, 0, w);
	EXPECT_NE(murk.find("\"rgbgrid\""), std::string::npos);
	EXPECT_NE(murk.find("MediumInterface \"murk_0\" \"sea\""), std::string::npos);
	const V3 sea = underwater::absorption(w);
	char corner[64];
	std::snprintf(corner, sizeof corner, "\"rgb sigma_a\" [ %.4f %.4f %.4f ", sea.x, sea.y, sea.z);
	EXPECT_NE(murk.find(corner), std::string::npos) << "the grid's corner is the sea's own water";
	EXPECT_EQ(problemWith("murk 14 3 -20 radius 6 green density 2"), "");
	EXPECT_EQ(problemWith("silt 1 -4 at 2 size 1"), "");
	EXPECT_NE(problemWith("silt 1 -4"), "") << "when";
}

// ---- 6: inside a wreck --------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUndersea16, InsideAWreck) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("wreck", std::string(kSea) + "wreckroom 0 -6 toward 0 length 10\nshot 0 In\n  key 0 -13.5 -2  0 -13.5 -8  70\n", film), "");
	EXPECT_NE(film.sceneAt(1.0).find("wreckroom_"), std::string::npos);
	EXPECT_NE(problemWith("wreckroom 0 -6 length 100"), "");
}

// ---- 7: set pieces of behaviour -----------------------------------------------------------------------------------------------------------------------------------------

// A barracuda tornado circles what it is around as that moves; damselfish rush at an intruder near their patch; a sea lion plays round its friend.
TEST(FilmUndersea16, SetPiecesOfBehaviour) {
	behaviour::Station st;
	st.at = {0, -10, 0};
	EXPECT_NE(behaviour::stationText(st, 2.0, flat).find("# a cleaning station"), std::string::npos);
	behaviour::Tornado tn;
	tn.at = {0, -6, 0};
	const std::string here = behaviour::tornadoText(tn, 0, 3.0, [](double) { return V3{0, -6, 0}; });
	const std::string there = behaviour::tornadoText(tn, 0, 3.0, [](double) { return V3{8, -6, 0}; });
	EXPECT_NE(here.find("# a barracuda tornado"), std::string::npos);
	EXPECT_NE(here, there) << "it follows its diver";
	behaviour::Damsels d;
	d.at = {0, -9.6, 0};
	const std::string calm = behaviour::damselText(d, 3.0, [](double) { return std::vector<V3>{{30, -9, 30}}; });
	const std::string angry = behaviour::damselText(d, 3.0, [](double) { return std::vector<V3>{{0.6, -9.3, 0.4}}; });
	EXPECT_NE(calm.find("# damselfish"), std::string::npos);
	EXPECT_NE(calm, angry) << "they go for an intruder";
	behaviour::Playmate pm;
	pm.at = {0, -5, 0};
	V3 f;
	double roll = 0.0;
	const V3 friendAt{3, -5, 0};
	EXPECT_LT(cartoon::length(behaviour::playmateAt(pm, 6.0, [&](double) { return friendAt; }, f, roll) - friendAt), 4.0) << "round its friend";
	for (const char *line : {"cleaningstation -3 0 -12", "tornado 150 at 0 3 -8 around Ana radius 2.5 height 4", "playmate 2 3 -6 with Ana", "damselfish 6 at -1 0.4 -3 radius 1.5"})
		EXPECT_EQ(problemWith(line), "") << line;
	EXPECT_NE(problemWith("tornado 5 at 0 3 -8"), "") << "10 at least";
}

// ---- 8: close up ------------------------------------------------------------------------------------------------------------------------------------------------------

// A camera near the sand sees grains and bits of shell where it looks (none from afar); the caustic slide is centred where the sun's beam reaches the camera, so a camera
// deep down near the sand is inside it.
TEST(FilmUndersea16, TheSandCloseUp) {
	EXPECT_NE(undersea::grainsText({0, -9.7, 0}, {0, -10, -0.8}, flat).find("# sand close up"), std::string::npos);
	EXPECT_TRUE(undersea::grainsText({0, -5, 0}, {0, -10, -6}, flat).empty());
	const std::string sun = "LightSource \"distant\" \"point3 from\" [ 0.50000 0.60000 0.00000 ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ 2.00000 1.90000 1.80000 ]\n";
	const std::string out = sealight::withCaustics(sun, "undersea16_test", 1.0, {0, -19.7, 0}, 0.0, 20.0);
	static const std::regex at(R"(Translate (\S+) (\S+) (\S+)\n  Rotate)");
	std::smatch m;
	ASSERT_TRUE(std::regex_search(out, m, at));
	const V3 d = sealight::refractedSun(cartoon::normalize(V3{0.5, 0.6, 0.0}));
	const V3 slide{std::atof(m[1].str().c_str()), std::atof(m[2].str().c_str()), std::atof(m[3].str().c_str())};
	const V3 aim = slide - d * sealight::kBack;   // (where its middle comes through the surface)
	const V3 down = aim - d * (20.0 / d.y);       // (and reaches the bed)
	EXPECT_LT(std::hypot(down.x, down.z), sealight::reachFor(0.3)) << "the camera inside the slide";
}

// ---- 9: sound -------------------------------------------------------------------------------------------------------------------------------------------------------------

// The reef's crackle while the camera is under the water and the waves' wash while it is above, switching where it crosses; a boat's engine heard under way.
TEST(FilmUndersea16, TheSoundsOfTheSea) {
	cartoon::audio::Mix late(6.0);
	cartoon::dressing::sound::reef(late, 6.0, 1.0, 3.0);
	const std::vector<std::int16_t> pcm = late.finish(0.0, 0.0);
	EXPECT_EQ(energy(pcm, 0.0, 2.9), 0.0) << "a bed that starts at 3 s";
	EXPECT_GT(energy(pcm, 3.5, 5.5), 0.0);
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("sound", "scene s.pbrt\nlength 10\nwater sea level 0 waves 0.3\nunderwater\nambience reef 1\nambience sea 1\n"
							   "shot 0 Under\n  key 0 -5 4  0 -5 -4  50\nshot 5 Over\n  key 0 3 4  0 3 -4  50\n", film), "");
	cartoon::audio::Mix m(10.0);
	film.soundscape(m);
	const std::vector<std::int16_t> s = m.finish(0.0, 0.0);
	EXPECT_GT(energy(s, 1.0, 4.0), 0.0) << "the reef, under";
	EXPECT_GT(energy(s, 6.0, 9.0), 0.0) << "the wash, above";
	ff::DataFilm engine;
	ASSERT_EQ(loadFilm("engine", std::string(kSea) + "boat 0 0 toward 90 speed 2 during 2 12\nshot 0 S\n  key 0 -5 4  0 -5 -4  50\n", engine), "");
	cartoon::audio::Mix e(12.0);
	engine.soundscape(e);
	const std::vector<std::int16_t> es = e.finish(0.0, 0.0);
	EXPECT_LT(energy(es, 0.0, 1.5), 0.2 * energy(es, 4.0, 8.0)) << "the engine starts with the boat";
}

// ---- 10: a camera's housing ---------------------------------------------------------------------------------------------------------------------------------------------

// Through a flat port the field of view is a third narrower and the picture bends and fringes at its edges; a red or magenta filter warms the water's colour back.
TEST(FilmUndersea16, AnUnderwaterCamerasHousing) {
	const std::string narrowed = undersea::throughFlatPort("LookAt 0 0 0 0 0 -1 0 1 0\nCamera \"perspective\" \"float fov\" [ 60 ]\n");
	static const std::regex fov(R"("float fov" \[ (\S+) \])");
	std::smatch m;
	ASSERT_TRUE(std::regex_search(narrowed, m, fov));
	EXPECT_NEAR(std::tan(0.5 * std::atof(m[1].str().c_str()) * cartoon::kPi / 180.0), std::tan(cartoon::kPi / 6.0) / 1.333, 1e-4);
	underwater::Water w;
	EXPECT_EQ(undersea::housingLook(w), "") << "a dome port, no filter: as it was";
	w.port = "flat", w.filter = "red";
	lens_look::Look look;
	EXPECT_EQ(lens_look::parse(undersea::housingLook(w), look), "");
	EXPECT_GT(look.distort, 0.0);
	EXPECT_GT(look.fringe, 0.0);
	EXPECT_GT(look.gain[0], look.gain[2]) << "red back";
	EXPECT_EQ(problemWith("underwater port flat filter magenta"), "");
	EXPECT_NE(problemWith("underwater port square"), "");
}

// A camera at the waterline floats with the waves always a little under the surface at its lens (at nine tenths of the wave it was above it in a trough).
TEST(FilmUndersea16, AWaterlineCameraStaysUnderTheSurface) {
	cartoon::water::Sea sea;
	sea.level = 0.0, sea.waves = 0.8;
	for (double t = 0.0; t < 20.0; t += 0.37) {
		const std::string h = undersea::floated("LookAt 3 0 2  3 0 -8  0 1 0\n", sea, t, true);
		V3 eye, target;
		double f = 0.0;
		ASSERT_TRUE(film_subjects::cameraOf(h, eye, target, f));
		const undersea::Waterline wl = undersea::waterlineOf(sea, t, {-eye.x, eye.y, eye.z});
		EXPECT_GT(wl.surface, eye.y) << t;
		EXPECT_EQ(wl.where, undersea::Waterline::At) << t;
	}
}

// ---- the whole sea ----------------------------------------------------------------------------------------------------------------------------------------------------

// A film with every round-16 line: each is in its frame.
TEST(FilmUndersea16, AWholeDive) {
	ff::DataFilm film;
	const std::string text = std::string("scene s.pbrt\nlength 30\nsky 11:00 11:00\nwater sea level 0 waves 0.4\nunderwater clarity 2 port flat\n") +
							 "diver Ana from -4 3 -2 to 4 3 -8 at 0\nboat 0 -10 toward 90 during 0 20 anchor 4\nsardinerun 0 7 -20 from 0\nvents 2 in -6 -30 6 -26\n"
							 "murk 8 5 -12 radius 4\nsilt 0 -3 at 1\ncleaningstation -2 0 -6\ntornado 40 at 0 4 -6 around Ana\nplaymate 2 5 -4 with Ana\n"
							 "damselfish 4 at -1 0.4 -3\nshot 0 Dive\n  key 0 -12 4  0 -13 -6  55\n";   // (heights on the lines: above the plain floor 15 m down)
	ASSERT_EQ(loadFilm("dive", text, film), "");
	const std::string a = film.sceneAt(8.0);
	for (const char *what : {"# a boat", "# the sardine run", "# hydrothermal vents", "# murky water", "# silt", "# a cleaning station", "# a barracuda tornado",
							 "# a playful sea lion", "# damselfish"})
		EXPECT_NE(a.find(what), std::string::npos) << what;
}
