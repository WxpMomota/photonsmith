// film_undersea14_tests.cpp - round 14, under the sea: the surface from below (Snell's window), a camera half above the water, depth and a diver's lamp, the deep's own
// lights, the octopus and cuttlefish, the open water's swimmers, bubbles, the seabed's life, the current, and places to film (film_undersea.h, film_deepsea.h,
// film_seabed.h, film_parse_sea.h, lens_look.h's waterline and drops).

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
#include "../../src/shared/cartoon/film_deepsea.h"
#include "../../src/shared/cartoon/film_seabed.h"
#include "../../src/shared/cartoon/film_undersea.h"
#include "../../src/shared/lens_look.h"

namespace ff = cartoon::film_file;
namespace undersea = cartoon::undersea;
namespace deepsea = cartoon::deepsea;
namespace seabed = cartoon::seabed;
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
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_undersea14_test_" + name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt", std::ios::binary) << kOpenSet;
	std::ofstream(d / (name + ".film"), std::ios::binary) << filmText;
	return film.load((d / (name + ".film")).string());
}
const char *kSea = "scene s.pbrt\nlength 8\nwater sea level 0 waves 0.5\nunderwater\n";
std::string problemWith(const std::string &lines) {
	ff::DataFilm bad;
	return loadFilm("bad", std::string(kSea) + lines + "\n", bad);
}
std::size_t count(const std::string &text, const std::string &what) {
	std::size_t n = 0;
	for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
	return n;
}
// The points of the scene's triangle meshes after the first `marker` comment (until the next comment line): pbrt's.
std::vector<V3> pointsAfter(const std::string &text, const std::string &marker) {
	std::vector<V3> pts;
	const std::size_t at = text.find(marker);
	if (at == std::string::npos) return pts;
	const std::size_t end = text.find("\n# ", at + marker.size());
	const std::string part = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
	static const std::regex P(R"("point3 P" \[ ([^\]]*)\])");
	for (std::sregex_iterator i(part.begin(), part.end(), P), e; i != e; ++i) {
		std::istringstream in((*i)[1].str());
		V3 p;
		while (in >> p.x >> p.y >> p.z) pts.push_back(p);
	}
	return pts;
}
cartoon::water::Sea calmSea() {
	cartoon::water::Sea s;
	s.level = 0.0, s.waves = 0.01, s.fromBelow = true;
	return s;
}

}  // namespace

// ---- 1, 2: the surface from below, half above it ---------------------------------------------------------------------------------------------------------------------------------

// Beyond Snell's window the surface is a mirror: its points are all past the critical angle from the eye (none inside the window), it reaches round the eye, the window
// widens the deeper the eye is, and the mirror casts no shadows.
TEST(FilmUndersea14, TheMirrorBeyondSnellsWindow) {
	const cartoon::water::Sea sea = calmSea();
	double edges[2] = {0.0, 0.0};
	for (int k = 0; k < 2; ++k) {
		const V3 eye{0.0, k == 0 ? -3.0 : -9.0, 0.0};
		const std::string text = undersea::windowText(sea, 1.0, eye);
		ASSERT_FALSE(text.empty());
		EXPECT_NE(text.find("\"bool shadow\" false"), std::string::npos);
		const std::vector<V3> pts = pointsAfter(text, "# the mirror");
		ASSERT_GT(pts.size(), 100u);
		double nearest = 1e9;
		for (const V3 &p : pts) {
			const double r = std::hypot(p.x, p.z), up = p.y - eye.y;
			EXPECT_GE(std::atan2(r, up), std::asin(undersea::kCritical) - 0.2) << "a mirror point inside the window (ripples tilt the water by a few degrees)";
			nearest = std::min(nearest, r);
		}
		edges[k] = nearest;
		EXPECT_NEAR(nearest, -eye.y * std::tan(std::asin(undersea::kCritical)), 0.4 * -eye.y) << "the window's edge where the critical angle is";
	}
	EXPECT_GT(edges[1], 2.5 * edges[0]);
}

// A camera is under the water, above it, or at it; at it the line across its dome is where the water stands on the dome; a shot's `waterline` floats its camera at the
// surface, and the frame's look draws the line and the drops above it.
TEST(FilmUndersea14, HalfAboveHalfBelow) {
	const cartoon::water::Sea sea = calmSea();
	EXPECT_EQ(undersea::waterlineOf(sea, 0.0, {0, -1, 0}).where, undersea::Waterline::Under);
	EXPECT_EQ(undersea::waterlineOf(sea, 0.0, {0, 1, 0}).where, undersea::Waterline::Above);
	const undersea::Waterline at = undersea::waterlineOf(sea, 0.0, {0, -0.05, 0});
	EXPECT_EQ(at.where, undersea::Waterline::At);
	EXPECT_GT(at.elevation, 0.0) << "the eye a little under the surface: more than half the dome under it";
	const auto eyeY = [](const std::string &h) {
		V3 eye, target;
		double fov = 0.0;
		return film_subjects::cameraOf(h, eye, target, fov) ? eye.y : 1e9;
	};
	const std::string header = "LookAt 0 0.3 5  0 0.3 -5  0 1 0\nCamera \"perspective\"\n";
	EXPECT_NE(undersea::floated(header, sea, 0.0, false), header) << "within 0.6 m: it floats on the waves";
	EXPECT_NEAR(eyeY(undersea::floated(header, sea, 0.0, false)), 0.3, 0.02);
	EXPECT_NEAR(eyeY(undersea::floated(header, sea, 0.0, true)), 0.0, 0.02) << "a `waterline` shot's camera: at the surface";
	EXPECT_EQ(undersea::floated("LookAt 0 3 5  0 3 -5  0 1 0\n", sea, 0.0, false), "LookAt 0 3 5  0 3 -5  0 1 0\n");
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("waterline", std::string(kSea) + "shot 0 Over and under\n  waterline\n  key 0 6 5  0 6 -5  50\n", film), "");
	const std::string s = film.sceneAt(1.0);
	EXPECT_NE(s.find(" waterline "), std::string::npos);
	EXPECT_NE(s.find(" drops 0.6"), std::string::npos);
	EXPECT_EQ(s.find("MediumInterface \"\" \"sea\""), std::string::npos) << "the camera itself is in the air";
	EXPECT_NE(problemWith("waterline"), "") << "a shot's word";
	// the look: the drops and the meniscus above and on the line, the picture well under it untouched
	lens_look::Look look;
	ASSERT_EQ(lens_look::parse("waterline 0.5 drops 2 drops-time 1", look), "");
	const int W = 96, H = 64;
	std::vector<float> img(W * H * 3);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
			for (int k = 0; k < 3; ++k) img[(y * W + x) * 3 + k] = 0.2f + 0.6f * (y < H / 2) + 0.1f * ((x / 6) % 2);
	std::vector<float> out = img;
	lens_look::apply(out.data(), W, H, look);
	double above = 0.0, below = 0.0;
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x) {
			const double d = std::fabs(out[(y * W + x) * 3] - img[(y * W + x) * 3]);
			if (y <= H / 2 + 1) above += d;
			else if (y > H * 3 / 4) below += d;
		}
	EXPECT_GT(above, 1.0);
	EXPECT_LT(below, 1e-4);
}

// ---- 3: depth and light ----------------------------------------------------------------------------------------------------------------------------------------------------

// `exposure auto` opens up with depth (and only a little with a lamp); a lamp is a spot on the camera; the painted caustics fade out on a deep bed; ripples and a lamp parse.
TEST(FilmUndersea14, DepthAndALamp) {
	underwater::Water w;
	EXPECT_NEAR(undersea::depthExposure(w, 0.0), 1.0, 1e-9);
	EXPECT_GT(undersea::depthExposure(w, 20.0), 1.5);
	EXPECT_GT(undersea::depthExposure(w, 40.0), undersea::depthExposure(w, 20.0));
	EXPECT_LE(undersea::depthExposure(w, 1000.0), 40.0);
	w.lamp = 1.0;
	EXPECT_LE(undersea::depthExposure(w, 300.0), 2.5);
	EXPECT_NE(undersea::lampText(1.0, {0, -10, 0}, {0, -10, -5}).find("LightSource \"spot\""), std::string::npos);
	EXPECT_EQ(underwater::causticBand(5.0), 0);
	EXPECT_EQ(underwater::causticBand(50.0), 3);
	EXPECT_EQ(underwater::causticContrast(3), 0.0);
	EXPECT_LT(underwater::rippleShade(0.3, 0.2), 1.1);
	EXPECT_GT(underwater::rippleShade(0.3, 0.2), 0.75);
	EXPECT_EQ(problemWith("underwater clarity 2 lamp 0.8 ripples"), "");
	EXPECT_NE(problemWith("underwater lamp -1"), "");
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("lamp", std::string("scene s.pbrt\nlength 4\nwater sea level 0\nunderwater lamp 1 ripples\nhalocline -6 strength 2\nshot 0 S\n  key 0 -8 4  0 -9 -4  50\n"), film), "");
	const std::string s = film.sceneAt(1.0);
	EXPECT_NE(s.find("# a diver's lamp"), std::string::npos);
	EXPECT_NE(s.find("# backscatter"), std::string::npos);
	EXPECT_NE(s.find("_rip"), std::string::npos) << "the rippled sand's picture";
	EXPECT_NE(s.find("\"dielectric\" \"float eta\" [ 1.0240 ]"), std::string::npos) << "the halocline";
}

// ---- 7, 9: bubbles and the current ------------------------------------------------------------------------------------------------------------------------------------------

// A bubble rises at its size's speed, grows as it rises (Boyle), drifts with the current and is gone at the surface; the big ones are caps, flat underneath.
TEST(FilmUndersea14, BubblesRiseGrowAndDrift) {
	EXPECT_LT(undersea::riseSpeed(0.0003), undersea::riseSpeed(0.002));
	EXPECT_NEAR(undersea::riseSpeed(0.002), 0.25, 0.05);
	EXPECT_GT(undersea::riseSpeed(0.02), 0.3);
	underwater::Current still, flowing;
	flowing.on = true, flowing.speed = 0.5, flowing.toward = 90.0;
	undersea::Bubble a, b;
	ASSERT_TRUE(undersea::bubbleAt({0, -20, 0}, 0.003, 30.0, 30.0, 1, 0.0, still, a));
	EXPECT_GT(a.r, 0.003 * 1.05) << "bigger nearer the surface";
	ASSERT_TRUE(undersea::bubbleAt({0, -20, 0}, 0.003, 30.0, 30.0, 1, 0.0, flowing, b));
	EXPECT_NEAR(b.p.x - a.p.x, 15.0, 0.1) << "carried 0.5 m/s for 30 s";
	EXPECT_FALSE(undersea::bubbleAt({0, -1, 0}, 0.003, 30.0, 30.0, 1, 0.0, still, a)) << "gone at the surface";
	const cartoon::Mesh cap = undersea::bubbleMesh(0.015, 0.0);
	double lo = 1e9, hi = -1e9;
	for (const V3 &v : cap.verts) lo = std::min(lo, v.y), hi = std::max(hi, v.y);
	EXPECT_LT(hi - lo, 1.2 * 0.015) << "a cap is flatter than a ball";
	undersea::Stream st;
	st.at = {0, -10, 0}, st.rate = 5;
	std::vector<undersea::Bubble> list;
	undersea::streamBubbles(st, 20.0, 0.0, still, list);
	ASSERT_GT(list.size(), 50u);
	for (const undersea::Bubble &q : list) EXPECT_TRUE(q.p.y >= -10.0 - 1e-9 && q.p.y < 0.0);
	EXPECT_EQ(problemWith("bubbles stream 0 0 0 rate 5\nbubbles breath 0 1 0 every 4\nbubbles net 0 0 at 2 radius 6\nbubbles ring 0 0 0 at 1"), "");
	EXPECT_NE(problemWith("bubbles ring 0 0 0"), "") << "a ring needs its time";
	EXPECT_NE(problemWith("bubbles fizz 0 0 0"), "");
}

// The current: its drift is its flow's integral (so what drifts and what leans agree); a surge swings back and forth about a slow onward flow.
TEST(FilmUndersea14, TheCurrentFlowsAndSurges) {
	underwater::Current c;
	c.on = true, c.speed = 0.4, c.toward = 0.0, c.surge = 6.0;
	for (double t : {0.7, 3.1, 8.9}) {
		const V3 d = (c.drift(t + 1e-4) - c.drift(t - 1e-4)) * (1.0 / 2e-4), v = c.velocity(t);
		EXPECT_NEAR(d.z, v.z, 1e-5);
	}
	EXPECT_NEAR(c.drift(6.0).z, 0.2 * 0.4 * 6.0, 1e-9) << "a whole surge brings the water back, but for its slow onward drift";
	EXPECT_EQ(problemWith("current 0.3 toward 45 surge 8"), "");
	EXPECT_NE(problemWith("current 9"), "");
	// kelp leans with it, reef fish face into it
	const auto flat = [](double, double) { return -10.0; };
	underwater::Kelp k;
	k.count = 3;
	EXPECT_NE(underwater::kelpText(k, 1.0, flat), underwater::kelpText(k, 1.0, flat, {0.5, 0, 0}));
}

// ---- 4, 5, 6: the deep, the octopus and cuttlefish, the open water's swimmers ------------------------------------------------------------------------------------------------

// Each animal of the deep is drawn, its own lights glowing (lighting nothing) and the angler's lure a light; comb jellies' rows are thin films; sparks follow the swimmers.
TEST(FilmUndersea14, TheDeepsOwnLights) {
	deepsea::Life life;
	for (const std::string &kind : deepsea::deepKinds()) {
		deepsea::Group g;
		g.kind = kind, g.count = 4, g.lo = {-2, -20, -2}, g.hi = {2, -18, 2};
		life.groups.push_back(g);
	}
	life.sparks = true;
	const std::string text = deepsea::text("test", life, 2.0, 0.0, underwater::Current());
	for (const char *what : {"# anglerfish", "# lanternfish", "# comb jellies", "# siphonophores", "# sparks in the water"}) EXPECT_NE(text.find(what), std::string::npos) << what;
	EXPECT_GE(count(text, "\"bool sampled\" false"), 3u) << "lanterns, siphonophores and sparks light nothing";
	EXPECT_EQ(count(text, "Shape \"sphere\""), 4u) << "each angler's lure is a light";
	EXPECT_GT(count(text, "filmthickness"), 1u) << "comb rows in rainbows";
	EXPECT_EQ(problemWith("deepsea lanternfish 30 in -2 0 -2 2 2 2\ndeepsea sparks"), "");
	EXPECT_NE(problemWith("deepsea dragons 3 in 0 0 0 1 1 1"), "");
}

// An octopus crawls, stops when it hides (its colour going over to the sand's), shoots off backwards when it jets, and leaves a dark ink cloud (an absorbing grid in the
// sea); cuttlefish wear their passing clouds.
TEST(FilmUndersea14, TheOctopusHidesJetsAndInks) {
	deepsea::Octopus o;
	o.at = {0, -10, 0}, o.toward = 0.0, o.hide = 4.0, o.jets = {10.0}, o.inks = {9.8};
	V3 f;
	const V3 a = deepsea::octopusAt(o, 2.0, f), b = deepsea::octopusAt(o, 4.5, f), c = deepsea::octopusAt(o, 9.0, f), d = deepsea::octopusAt(o, 13.0, f);
	EXPECT_GT(b.z, a.z) << "crawling forward";
	EXPECT_NEAR(c.z, b.z + 0.06 * 0.25, 0.02) << "still once hidden";
	EXPECT_LT(d.z, c.z - 1.5) << "jetted off backwards";
	EXPECT_NEAR(deepsea::hideOf(o, 8.0), 1.0, 1e-9);
	const std::string text = deepsea::octopusText(o, 0, 11.0, underwater::Current());
	EXPECT_NE(text.find("\"rgbgrid\""), std::string::npos) << "ink absorbs: an rgbgrid (a uniformgrid only scatters)";
	EXPECT_NE(text.find("MediumInterface \"ink_0_0\" \"sea\""), std::string::npos);
	deepsea::Cuttle cu;
	cu.at = {0, -10, 0}, cu.count = 2;
	EXPECT_NE(deepsea::cuttleText("test", cu, 1.0).find("cuttle_clouds"), std::string::npos);
	EXPECT_EQ(problemWith("octopus 0 0 0 toward 90 hide 3 jet 6 ink 5.8\ncuttlefish 3 at 0 1 0 radius 1"), "");
	EXPECT_NE(problemWith("octopus 0 0"), "");
}

// Each swimmer is drawn; a stingray lies still until it rises and then glides off in a cloud of sand; otters float at the surface; penguins trail bubbles.
TEST(FilmUndersea14, TheOpenWatersSwimmers) {
	for (const std::string &kind : deepsea::swimmerKinds()) {
		SCOPED_TRACE(kind);
		deepsea::Swimmers g;
		g.kind = kind, g.count = 2, g.at = {0, -8, 0}, g.radius = 4, g.rise = 3.0;
		const std::string text = deepsea::swimmersText("test", g, 5.0, 0.0, underwater::Current());
		EXPECT_NE(text.find("trianglemesh"), std::string::npos);
		V3 f;
		double roll = 0.0;
		const V3 p = deepsea::swimmerAt(g, 0, 5.0, f, roll, 0.0);
		if (kind == "otters") EXPECT_NEAR(p.y, -0.07, 1e-9);
		if (kind == "penguins") EXPECT_NE(text.find("# bubbles"), std::string::npos);
		if (kind == "stingrays") {
			EXPECT_EQ(deepsea::swimmerAt(g, 0, 1.0, f, roll, 0.0).x, deepsea::swimmerAt(g, 0, 2.0, f, roll, 0.0).x) << "still, buried";
			EXPECT_GT(length(deepsea::swimmerAt(g, 0, 8.0, f, roll, 0.0) - deepsea::swimmerAt(g, 0, 1.0, f, roll, 0.0)), 1.0) << "risen and gone";
		}
	}
	EXPECT_EQ(problemWith("swimmers mantas 2 at 0 5 -10 radius 8\nswimmers stingrays 2 at 0 0 0 rise 3"), "");
	EXPECT_NE(problemWith("swimmers whales 2 at 0 0 0"), "");
	EXPECT_NE(problemWith("swimmers mantas 2 at 0 0 0 rise 3"), "") << "only a stingray rises from the sand";
}

// ---- 8, 10: the seabed and its places ---------------------------------------------------------------------------------------------------------------------------------------

// Every kind of seabed life is drawn; anemones have their clownfish; garden eels sink into their holes as the camera comes close.
TEST(FilmUndersea14, LifeOnTheSeabed) {
	const auto flat = [](double, double) { return -10.0; };
	for (const std::string &kind : seabed::kinds()) {
		SCOPED_TRACE(kind);
		seabed::Patch p;
		p.kind = kind, p.count = 4, p.x0 = -2, p.z0 = -2, p.x1 = 2, p.z1 = 2;
		seabed::World w;
		w.patches.push_back(p);
		EXPECT_NE(seabed::text("test", w, 1.0, flat, {0.2, 0, 0}, {0, -5, 20}).find("trianglemesh"), std::string::npos);
	}
	seabed::Patch eels;
	eels.kind = "gardeneels", eels.count = 10, eels.x0 = -1, eels.z0 = -1, eels.x1 = 1, eels.z1 = 1;
	const std::string far = seabed::gardenEelText(eels, 1.0, flat, {0, 0, 0}, {0, -9, 20}), close = seabed::gardenEelText(eels, 1.0, flat, {0, 0, 0}, {0, -9, 0.5});
	EXPECT_GT(far.size(), close.size() + 1000) << "out of their holes far off, in them close by";
	seabed::Patch anemones;
	anemones.kind = "anemones", anemones.count = 2;
	EXPECT_NE(seabed::anemoneText(anemones, 1.0, flat, {0, 0, 0}).find("0.95000 0.42000 0.05000"), std::string::npos) << "orange clownfish";
	EXPECT_EQ(problemWith("seabed anemones 6 in -3 -6 3 -3\nseabed gardeneels 40 in 4 -8 10 -2 seed 3"), "");
	EXPECT_NE(problemWith("seabed kelp 6 in 0 0 1 1"), "");
}

// A wreck, an arch, a cenote and a reef wall are each made once and placed in every frame; wrong numbers are refused.
TEST(FilmUndersea14, PlacesToFilm) {
	const auto flat = [](double, double) { return -15.0; };
	seabed::World w;
	for (const char *kind : {"wreck", "arch", "cenote", "reefwall"}) {
		seabed::Place p;
		p.kind = kind, p.length = kind == std::string("wreck") ? 20.0 : 12.0, p.height = 8.0, p.radius = 5.0, p.depth = 10.0, p.size = 0.5;
		w.places.push_back(p);
	}
	const std::string a = seabed::text("undersea14_test", w, 1.0, flat, {0, 0, 0}, {0, -10, 30}), b = seabed::text("undersea14_test", w, 2.0, flat, {0, 0, 0}, {0, -10, 30});
	EXPECT_GE(count(a, "Shape \"plymesh\""), 5u) << "the hull, its ribs, the arch, the cenote, the wall";
	EXPECT_EQ(a, b) << "places stand still";
	EXPECT_NE(a.find("# a wreck"), std::string::npos);
	EXPECT_EQ(problemWith("wreck 0 -20 toward 60 length 28 list 18\narch -20 0 size 1.2\ncenote 0 40 radius 10 depth 20\nreefwall 20 0 length 30 height 15"), "");
	EXPECT_NE(problemWith("wreck 0 0 length 500"), "");
	EXPECT_NE(problemWith("cenote 0 0 toward 30"), "") << "a cenote is round";
}

// ---- the whole sea in a film -------------------------------------------------------------------------------------------------------------------------------------------------

// A film with every round-14 line: each is in its frame, the frame changes as nature's time goes on, and the sea is heard as a reef, not as waves.
TEST(FilmUndersea14, AWholeReefFilm) {
	ff::DataFilm film;
	const std::string text = std::string("scene s.pbrt\nlength 10\nwater sea level 0 waves 0.4\nunderwater clarity 2 ripples\ncurrent 0.2 toward 90\n") +
							 "bubbles stream 1 0 -3 rate 6\nbubbles ring 0 1 -2 at 1\ndeepsea lanternfish 10 in -3 1 -6 3 3 -2\noctopus 0 0 -2 jet 5 ink 4.8\n"
							 "cuttlefish 1 at 1 1 -3\nswimmers mantas 1 at 0 6 -10\nseabed anemones 2 in -2 -4 2 -2\nwreck 6 -12 length 15\n"
							 "shot 0 Reef\n  key 0 -13 4  0 -14 -4  50\n";
	ASSERT_EQ(loadFilm("reef", text, film), "");
	const std::string a = film.sceneAt(2.0), b = film.sceneAt(6.0);
	for (const char *what : {"# the mirror beyond Snell's window", "# bubbles", "# a bubble ring", "# lanternfish", "# an octopus", "# ink", "# cuttlefish", "# mantas", "# anemones", "# a wreck"})
		EXPECT_NE((a + b).find(what), std::string::npos) << what;
	EXPECT_NE(a, b);
	EXPECT_TRUE(std::any_of(film.spec.ambience.begin(), film.spec.ambience.end(), [](const cartoon::dressing::Ambience &x) { return x.kind == "reef"; }));
	EXPECT_FALSE(std::any_of(film.spec.ambience.begin(), film.spec.ambience.end(), [](const cartoon::dressing::Ambience &x) { return x.kind == "sea"; }));
}
