// film_universe_tests.cpp - round 10 of the film files: the universe - a set in space, planets, atmospheres, rings and moons, nebulae, galaxies, stars, comets,
// black holes, ships, people in space, its camera moves, its light and sound, and the way down to a planet. (film_spectacle_tests.cpp holds round 9.)

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/cartoons.h"

namespace ff = cartoon::film_file;

namespace {

const char *kOpenSet = R"(# an empty set
LookAt 0 40 200  0 30 0  0 1 0
Camera "perspective" "float fov" [ 40 ]
Film "rgb" "integer xresolution" [ 320 ] "integer yresolution" [ 180 ]
Sampler "halton" "integer pixelsamples" [ 16 ]
WorldBegin
LightSource "infinite" "rgb L" [ 0.2 0.25 0.3 ]
LightSource "distant" "point3 from" [ -4 6 5 ] "point3 to" [ 0 0 0 ] "rgb L" [ 2.2 1.9 1.55 ]
)";

// A film file in a temporary folder of its own with the open set; "" or the problem.
std::string loadFilm(const std::string &name, const std::string &filmText, ff::DataFilm &film) {
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_universe_test_" + name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt", std::ios::binary) << kOpenSet;
	std::ofstream(d / (name + ".film"), std::ios::binary) << filmText;
	return film.load((d / (name + ".film")).string());
}

// The problem a bad line gives, in a film otherwise fine.
std::string problemOf(const std::string &line) {
	ff::DataFilm bad;
	return loadFilm("bad", "scene s.pbrt\nlength 4\n" + line + "\n", bad);
}

std::size_t count(const std::string &text, const std::string &what) {
	std::size_t n = 0;
	for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
	return n;
}

// The numbers after `key` at its first occurrence in `text` from `from` on.
std::vector<double> numbersAfter(const std::string &text, const std::string &key, std::size_t from = 0, int n = 3) {
	std::vector<double> v;
	const std::size_t at = text.find(key, from);
	if (at == std::string::npos) return v;
	std::istringstream in(text.substr(at + key.size()));
	for (int i = 0; i < n; ++i) {
		double x = 0.0;
		if (in >> x) v.push_back(x);
	}
	return v;
}

void expectProblems(const std::vector<std::pair<std::string, std::string>> &cases) {
	for (const auto &[line, says] : cases) {
		const std::string problem = problemOf(line);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

}  // namespace

// ---- 1. the set in space ---------------------------------------------------------------------------------------------------------------------------------------------------

namespace sp = cartoon::space;

// Stars take their colours from their temperatures; the Milky Way is a band, brighter along it than at its pole; a space film has the starfield for its sky and the sun a
// glowing ball a thousand kilometres off where the line says, and none of its set's own sky lights.
TEST(FilmUniverseSpace, TheStarsHaveTheirColoursTheMilkyWayIsABandAndTheSunIsWhereItIsPut) {
	const cartoon::Vec3 cool = sp::blackbody(3000.0), sun = sp::blackbody(5800.0), hot = sp::blackbody(15000.0);
	EXPECT_GT(cool.x, 2.0 * cool.z) << "a cool star is orange-red";
	EXPECT_GT(hot.z, hot.x) << "a hot one blue-white";
	EXPECT_NEAR(sun.x / sun.z, 1.0, 0.25) << "the sun near white";
	EXPECT_NEAR(0.2126 * sun.x + 0.7152 * sun.y + 0.0722 * sun.z, 1.0, 0.05) << "luminance 1";
	EXPECT_GT(sp::milkyWayAt(sp::galacticCentre(), 1), 5.0 * sp::milkyWayAt(sp::galacticPole(), 1));

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("space", "scene s.pbrt\nlength 4\nspace stars 1 milkyway 1 sun 200 10\nshot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
	const std::string s = film.sceneAt(1.0);
	EXPECT_NE(s.find("# space (film_space.h)"), std::string::npos);
	EXPECT_EQ(count(s, "LightSource \"distant\""), 0u) << "the set's own sun left out";
	EXPECT_EQ(s.find("\"rgb L\" [ 0.2 0.25 0.3 ]"), std::string::npos) << "and its sky";
	EXPECT_EQ(s.find("AreaLightSource", s.find("# space")), std::string::npos) << "the sun is in the sky's picture, not a ball";
	EXPECT_NE(s.find("_sun1_200.00_10.00_"), std::string::npos) << "the picture made for the sun where the line puts it";
	ff::DataFilm dark;
	ASSERT_EQ(loadFilm("nosun", "scene s.pbrt\nlength 4\nspace sun off\nshot 0 A\n key 0 0 0  0 0 -100  50\n", dark), "");
	EXPECT_NE(dark.sceneAt(1.0).find("_sun0_"), std::string::npos);
	expectProblems({{"space\nspace", "one `space` line"}, {"space stars 9", "stars and milkyway 0 to 4"}, {"space\nsky 12:00 12:00", "no `sky` with it"}});
}

// ---- 2. planets ------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace pl = cartoon::planets;

// An ocean world has seas (glossy) and land, a lava world glows along its cracks, a gas giant has no sea; a planet turns with its spin; a film in space is moved so its
// camera is near the origin, objects' definitions kept outside the move.
TEST(FilmUniversePlanets, KindsHaveTheirSurfacesPlanetsSpinAndAFarCameraIsBroughtNearTheOrigin) {
	const auto shares = [](const pl::Planet &p, double &seaShare, double &glowShare) {
		seaShare = glowShare = 0.0;
		int n = 0;
		for (int j = 0; j < 40; ++j)
			for (int i = 0; i < 80; ++i, ++n) {
				double sea = 0.0, glow = 0.0;
				pl::surfaceAt(p, pl::detail::dirOf(i, j, 80, 40), sea, glow);
				seaShare += sea > 0.5, glowShare += glow > 0.3;
			}
		seaShare /= n, glowShare /= n;
	};
	pl::Planet ocean, lava, gas;
	ocean.kind = "ocean", lava.kind = "lava", gas.kind = "gas";
	double sea = 0.0, glow = 0.0;
	shares(ocean, sea, glow);
	EXPECT_GT(sea, 0.2) << "seas";
	EXPECT_LT(sea, 0.9) << "and land";
	shares(lava, sea, glow);
	EXPECT_GT(glow, 0.02) << "glowing rivers of lava";
	shares(gas, sea, glow);
	EXPECT_EQ(sea, 0.0);
	EXPECT_NE(pl::placement(ocean, 0.0), pl::placement(ocean, 30.0)) << "it turns";

	const std::string scene = "LookAt -75000 0 0  -75000 0 -20000  0 1 0\nCamera \"perspective\"\nWorldBegin\nShape \"sphere\"\nObjectBegin \"rock\"\nShape \"sphere\"\nObjectEnd\nObjectInstance \"rock\"\n";
	const std::string moved = cartoon::space::recentred(scene);
	EXPECT_NE(moved.find("LookAt 0.000000 0.000000 0.000000  0.000000 0.000000 -20000.000000"), std::string::npos) << moved;
	EXPECT_NE(moved.find("Translate 75000.0 -0.0 -0.0"), std::string::npos) << moved;
	const std::size_t def = moved.find("ObjectBegin"), inst = moved.find("ObjectInstance");
	EXPECT_LT(moved.rfind("AttributeEnd", def), def) << "the object's definition outside the Translate";
	EXPECT_GT(moved.rfind("Translate", inst), def) << "its instance inside it";
	EXPECT_EQ(cartoon::space::recentred("LookAt 10 0 0  0 0 0  0 1 0\nWorldBegin\n"), "LookAt 10 0 0  0 0 0  0 1 0\nWorldBegin\n") << "near the origin already";

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("planets", "scene s.pbrt\nlength 4\nspace\nplanet home ocean at 0 0 -20000 radius 6000\nplanet big gas at 30000 0 -40000 radius 9000 tilt 5\n"
								 "shot 0 A\n key 0 0 0  0 0 -100  50\n",
						film),
			  "");
	const std::string s = film.sceneAt(1.0);
	EXPECT_EQ(count(s, "# a planet (film_planets.h)"), 2u);
	EXPECT_NE(s.find("\"coateddiffuse\" \"texture reflectance\" \"home_colour\""), std::string::npos) << "an ocean world's seas glossy";
	expectProblems({{"planet a moon at 0 0 0 radius 10", "rocky, desert, ice, ocean, gas or lava"},
					{"planet a rocky at 0 0 0", "where it is and how big"},
					{"planet a rocky at 0 0 0 radius 10\nplanet a ice at 5 0 0 radius 10", "already a planet called a"}});
}

// ---- 3. a planet's air -----------------------------------------------------------------------------------------------------------------------------------------------------

// The air is densest at the ground and thins by e every scale height, nothing above its top; Rayleigh air scatters blue the most; a planet with air has its grid in a
// sphere round it.
TEST(FilmUniverseAir, AirThinsWithHeightScattersBlueAndSurroundsThePlanet) {
	const std::vector<float> d = pl::airDensities();
	const int n = pl::kAirN;
	const double half = 1.0 + pl::kAirTop * pl::kAirScale;
	const auto at = [&](double r) {   // (along +x from the middle)
		const int i = std::clamp(static_cast<int>((r / (2.0 * half) + 0.5) * n), 0, n - 1), j = n / 2, k = n / 2;
		return d[(static_cast<std::size_t>(k) * n + j) * n + i];
	};
	EXPECT_NEAR(at(1.0 - 0.01), 1.0f, 1e-6f) << "at the ground (and just under it)";
	EXPECT_LT(at(1.0 + 4.0 * pl::kAirScale), 0.06f) << "thin four scale heights up";
	EXPECT_LT(at(half - 0.001), 1e-3f) << "next to nothing at its top";
	pl::Planet p;
	p.name = "home", p.radius = 6000.0, p.air = 1.0;
	double g = 0.0;
	const cartoon::Vec3 sigma = pl::airSigma(p, g);
	EXPECT_GT(sigma.z, 2.0 * sigma.x) << "blue scattered most";
	EXPECT_EQ(g, 0.0);
	p.airColour = "orange";
	EXPECT_GT(pl::airSigma(p, g).x, pl::airSigma(p, g).z);

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("air", "scene s.pbrt\nlength 4\nspace\nplanet home ocean at 0 0 -20000 radius 6000 air 1\nplanet bare rocky at 20000 0 -20000 radius 3000\n"
							  "shot 0 A\n key 0 0 0  0 0 -100  50\n",
						film),
			  "");
	const std::string s = film.sceneAt(1.0);
	EXPECT_EQ(count(s, "# its air (film_planets.h)"), 1u) << "only the planet with air";
	EXPECT_NE(s.find("MediumInterface \"air_home\" \"\""), std::string::npos);
	expectProblems({{"planet a rocky at 0 0 0 radius 10 air 20", "aurora 0 to 10"}});
}

// ---- 4. night sides ----------------------------------------------------------------------------------------------------------------------------------------------------------

// Cities light the land and not the sea or a gas giant; the aurora's oval is drawn only over the night side, and none without `aurora`.
TEST(FilmUniverseNight, CitiesLightTheLandAndAuroraHangsOverTheNightSide) {
	pl::Planet p;
	p.name = "home", p.kind = "ocean", p.cities = 1.0, p.aurora = 1.0, p.radius = 6000.0, p.at = {0.0, 0.0, -20000.0};
	int lit = 0, litAtSea = 0;
	for (int j = 0; j < 60; ++j)
		for (int i = 0; i < 120; ++i) {
			const cartoon::Vec3 o = pl::detail::dirOf(i, j, 120, 60);
			double sea = 0.0, glow = 0.0;
			pl::surfaceAt(p, o, sea, glow);
			const double c = pl::cityLightAt(p, o);
			lit += c > 0.1;
			litAtSea += c > 0.1 && sea > 0.0;
		}
	EXPECT_GT(lit, 20) << "cities";
	EXPECT_EQ(litAtSea, 0) << "none at sea";
	pl::Planet gas = p;
	gas.kind = "gas";
	EXPECT_EQ(pl::cityLightAt(gas, cartoon::normalize(cartoon::Vec3{0.3, 0.2, 0.9})), 0.0);
	const std::string night = pl::auroraText("aurora_test", p, 0.0, {1.0, 0.0, 0.0}), none = pl::auroraText("aurora_test", p, 0.0, {1.0, 0.0, 0.0});
	EXPECT_EQ(count(night, "# aurora (film_planets.h)"), 2u) << "round both poles";
	// every point of it on the night side (away from the sun at +x): pbrt x = -(story x) of the mesh points
	std::vector<int> used;   // (the points its triangles use: the mesh keeps every point round the oval)
	{
		std::istringstream idx(night.substr(night.find("\"integer indices\" [") + 20));
		int k = 0;
		while (idx >> k) used.push_back(k);
	}
	std::vector<double> xs;
	{
		std::istringstream in(night.substr(night.find("\"point3 P\" [") + 12));
		double x = 0.0, y = 0.0, z = 0.0;
		while (in >> x >> y >> z) xs.push_back(x);
	}
	double sunward = -1e30;
	for (int k : used) sunward = std::max(sunward, xs.at(static_cast<std::size_t>(k)));
	EXPECT_LT(sunward, 0.3 * p.radius) << "drawn over the night side";
	p.aurora = 0.0;
	EXPECT_EQ(pl::auroraText("aurora_test", p, 0.0, {1.0, 0.0, 0.0}), "");
	(void)none;
	expectProblems({{"planet a ocean at 0 0 0 radius 10 cities 20", "cities and aurora 0 to 10"}});
}

// ---- 5. rings --------------------------------------------------------------------------------------------------------------------------------------------------------------

// Rings are ringlets in the planet's equator's plane, thin at the division and the gap, none beyond their edges; a planet without them has none.
TEST(FilmUniverseRings, RingletsSpanTheRingsWithADivisionAndAGap) {
	pl::Planet p;
	p.name = "giant", p.kind = "gas", p.radius = 8000.0, p.ringInner = 1.3, p.ringOuter = 2.4;
	EXPECT_LT(pl::ringDensity(p, 0.62), 0.05) << "the wide division";
	EXPECT_LT(pl::ringDensity(p, 0.92), 0.05) << "the narrow gap";
	double dense = 0.0;
	for (double u = 0.3; u < 0.6; u += 0.01) dense = std::max(dense, pl::ringDensity(p, u));
	EXPECT_GT(dense, 0.4) << "bright bands";
	const std::string r = pl::ringsText(p, 0.0);
	const std::size_t disks = count(r, "Shape \"disk\"");
	EXPECT_GT(disks, 100u);
	EXPECT_LE(disks, static_cast<std::size_t>(pl::kRinglets));
	double lo = 1e30, hi = 0.0;
	for (std::size_t at = r.find("\"float radius\" [ "); at != std::string::npos; at = r.find("\"float radius\" [ ", at + 1)) {
		const std::vector<double> outer = numbersAfter(r, "\"float radius\" [ ", at, 1), inner = numbersAfter(r, "\"float innerradius\" [ ", at, 1);
		hi = std::max(hi, outer[0]), lo = std::min(lo, inner[0]);
	}
	EXPECT_GE(lo, 1.3 * 8000.0 - 1.0);
	EXPECT_LE(hi, 2.4 * 8000.0 + 1.0);
	p.ringOuter = 0.0;
	EXPECT_EQ(pl::ringsText(p, 0.0), "");
	expectProblems({{"planet a gas at 0 0 0 radius 10 rings 3 2", "the outer beyond the inner"}, {"planet a gas at 0 0 0 radius 10 rings 0.5 2", "from 1 out to 10"}});
}

// ---- 6. moons and eclipses ------------------------------------------------------------------------------------------------------------------------------------------------

// A moon goes round its planet at its orbit's distance, half its period apart on opposite sides; a body over the sun covers it all, beside it none, part way between;
// in totality there is a corona, near the end the diamond ring; a narrow shot of the sun draws it as a ball.
TEST(FilmUniverseMoons, MoonsGoRoundAndEclipsesShowTheCoronaAndTheDiamondRing) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("moons", "scene s.pbrt\nlength 4\nspace sun 180 0\nplanet home ocean at 1000 0 2000 radius 600\nmoon luna rocky of home radius 150 orbit 2000 period 80\n"
								"shot 0 A\n key 0 0 0  0 0 -100  50\n",
						film),
			  "");
	const std::vector<pl::Planet> &all = film.spec.planets;
	ASSERT_EQ(all.size(), 2u);
	EXPECT_EQ(all[1].around, "home");
	const cartoon::Vec3 a = pl::centreAt(all, 1, 0.0), b = pl::centreAt(all, 1, 40.0);
	EXPECT_NEAR(cartoon::length(a - all[0].at), 2000.0, 1e-6);
	EXPECT_NEAR(cartoon::length(b - all[0].at), 2000.0, 1e-6);
	EXPECT_NEAR(cartoon::length(a - b), 4000.0, 1e-3) << "opposite sides half a period apart";

	const cartoon::Vec3 sun{0.0, 0.0, -1.0}, eye{0.0, 0.0, 0.0};
	const double s = 0.5 * 0.53 * cartoon::kPi / 180.0;
	cartoon::Vec3 away;
	EXPECT_DOUBLE_EQ(pl::sunCovered(eye, {0.0, 0.0, -60000.0}, 300.0, sun, s, away), 1.0) << "right over it, a little bigger: total";
	EXPECT_DOUBLE_EQ(pl::sunCovered(eye, {3000.0, 0.0, -60000.0}, 300.0, sun, s, away), 0.0) << "beside it";
	const double part = pl::sunCovered(eye, {300.0, 0.0, -60000.0}, 300.0, sun, s, away);
	EXPECT_GT(part, 0.2);
	EXPECT_LT(part, 0.9);
	std::vector<pl::Planet> eclipser(1);
	eclipser[0].name = "dark", eclipser[0].radius = 300.0, eclipser[0].at = {0.0, 0.0, -60000.0};
	EXPECT_NE(pl::eclipseText("eclipse_test", eclipser, 0.0, eye, sun, 0.53).find("# the sun's corona"), std::string::npos) << "in totality";
	eclipser[0].at = {2000.0, 0.0, -60000.0};
	EXPECT_EQ(pl::eclipseText("eclipse_test", eclipser, 0.0, eye, sun, 0.53), "");
	// near the end: the diamond ring (search the offset that leaves 0.4% of the sun)
	double x = 0.0;
	for (double t = 0.0; t < 600.0; t += 0.25) {
		eclipser[0].at = {t, 0.0, -60000.0};
		if (pl::sunCovered(eye, eclipser[0].at, 300.0, sun, s, away) < 0.996) {
			x = t;
			break;
		}
	}
	eclipser[0].at = {x, 0.0, -60000.0};
	EXPECT_NE(pl::eclipseText("eclipse_test", eclipser, 0.0, eye, sun, 0.53).find("# the diamond ring"), std::string::npos);
	cartoon::space::Space sp1;
	sp1.sunBearing = 180.0, sp1.sunElevation = 0.0;
	EXPECT_TRUE(cartoon::space::narrowOnTheSun(sp1, {0.0, 0.0, 0.0}, {0.0, 0.0, -100.0}, 6.0));
	EXPECT_FALSE(cartoon::space::narrowOnTheSun(sp1, {0.0, 0.0, 0.0}, {0.0, 0.0, -100.0}, 40.0)) << "a wide shot keeps the sun in the sky's picture";
	expectProblems({{"planet a rocky at 0 0 0 radius 10\nmoon b rocky of c radius 2 orbit 30", "no planet or moon called c"},
					{"planet a rocky at 0 0 0 radius 10\nmoon b rocky of a radius 2", "its orbit"}});
}

// ---- 7. nebulae -----------------------------------------------------------------------------------------------------------------------------------------------------------

namespace cs = cartoon::cosmos;

// A nebula's gas glows red outside and teal near its middle, its dust absorbs and gives no light, nothing beyond its edge; its pillars are columns of dust; a film's
// nebula is a glowing grid in a sphere, with its young stars.
TEST(FilmUniverseNebula, GasGlowsDustDarkensAndPillarsStand) {
	cs::Nebula n;
	n.name = "veil", n.size = 20000.0;
	double gasLight = 0.0, outside = 0.0, dustLight = 0.0;
	int dusty = 0;
	for (int k = 0; k < 24; ++k)
		for (int j = 0; j < 24; ++j)
			for (int i = 0; i < 24; ++i) {
				const double x = (i + 0.5) / 24, y = (j + 0.5) / 24, z = (k + 0.5) / 24;
				const cs::Voxel v = cs::nebulaVoxel(n, x, y, z);
				const double r = 2.0 * std::sqrt((x - 0.5) * (x - 0.5) + (y - 0.5) * (y - 0.5) + (z - 0.5) * (z - 0.5));
				if (r > 1.45) outside += v.sa.x + v.le.x;
				gasLight += v.le.x + v.le.y + v.le.z;
				if (v.sa.x > 20.0 / n.size) ++dusty, dustLight += v.le.x;
			}
	EXPECT_GT(gasLight, 10.0) << "glowing gas";
	EXPECT_EQ(outside, 0.0) << "nothing past its edge";
	EXPECT_GT(dusty, 0) << "dust lanes";
	EXPECT_LT(dustLight, 0.3 * dusty) << "dust gives little light";
	const cs::Voxel middle = cs::nebulaVoxel(n, 0.5, 0.5, 0.5);
	if (middle.le.y + middle.le.x > 0.0) EXPECT_GT(middle.le.y / (middle.le.x + 1e-9), 0.3) << "teal towards the middle";
	cs::Nebula towers = n;
	towers.kind = "pillars";
	int column = 0;
	const int ts = towers.seed * 113;
	const double px = 0.3 + 0.06 * (cartoon::hash01(ts, 5) - 0.5), pz = 0.5 + 0.12 * (cartoon::hash01(ts, 6) - 0.5);   // (the first pillar's axis, as nebulaVoxel places it)
	for (double y = 0.3; y < 0.5; y += 0.02) column += cs::nebulaVoxel(towers, px + 0.05 * std::sin(y * 7.0), y, pz).sa.x > 20.0 / n.size;
	EXPECT_GT(column, 5) << "a pillar of dust";

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("nebula", "scene s.pbrt\nlength 4\nspace\nnebula veil at 0 0 -30000 size 20000 kind dark\nshot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
	const std::string s = film.sceneAt(1.0);
	EXPECT_NE(s.find("MediumInterface \"nebula_veil\" \"\""), std::string::npos);
	EXPECT_EQ(count(s, "AreaLightSource"), 4u) << "its young stars";
	expectProblems({{"nebula a at 0 0 0", "how big it is"}, {"nebula a at 0 0 0 size 10", "size 100 to 200000"}, {"nebula a at 0 0 0 size 500 kind glowing", "\"kind\""}});
}

// ---- 8. galaxies ----------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUniverseGalaxy, ArmsWindBulgeGlowsAndEllipticalsFade) {
	cs::Galaxy g;
	g.name = "swirl", g.size = 50000.0;
	// the bulge is warm and bright; out in the disc the light comes and goes round each ring (the arms), bluer on them
	const cs::Voxel middle = cs::galaxyVoxel(g, 0.0, 0.0, 0.0);
	EXPECT_GT(middle.le.x, middle.le.z) << "a warm bulge";
	double lo = 1e9, hi = 0.0, blueOnArm = 0.0;
	for (int k = 0; k < 72; ++k) {
		const double th = 2.0 * 3.14159265358979 * k / 72.0;
		const cs::Voxel v = cs::galaxyVoxel(g, 0.5 * std::cos(th), 0.5 * std::sin(th), 0.0);
		const double light = v.le.x + v.le.y + v.le.z, glow = v.sa.x * g.size;
		if (glow * light > hi) hi = glow * light, blueOnArm = v.le.z / (v.le.x + 1e-9);
		lo = std::min(lo, glow * light);
	}
	EXPECT_GT(hi, 3.0 * lo + 1e-6) << "arms";
	EXPECT_GT(blueOnArm, 0.8) << "young blue stars on the arms";
	EXPECT_LT(cs::galaxyVoxel(g, 0.0, 0.0, 0.3).sa.x, 0.05 * middle.sa.x) << "a thin disc";
	cs::Galaxy barred = g;
	barred.kind = "barred";
	EXPECT_GT(cs::galaxyVoxel(barred, 0.18, 0.0, 0.0).sa.x, 2.0 * cs::galaxyVoxel(barred, 0.0, 0.18, 0.0).sa.x) << "a bar along u";
	cs::Galaxy oval = g;
	oval.kind = "elliptical";
	const double c = cs::galaxyVoxel(oval, 0.0, 0.0, 0.0).sa.x, half = cs::galaxyVoxel(oval, 0.4, 0.0, 0.0).sa.x;
	EXPECT_GT(c, 5.0 * half) << "brightest in its middle";
	EXPECT_EQ(cs::galaxyVoxel(oval, 1.0, 0.0, 0.0).sa.x, 0.0) << "fades out inside its box";
	double u = 0.0, v = 0.0, h = 0.0;
	cs::Galaxy edge = g;
	edge.tilt = 90.0;
	cs::galaxyFrame(edge, cartoon::Vec3{0.0, 0.5, 0.0}, u, v, h);
	EXPECT_NEAR(std::abs(h), 0.5, 1e-9) << "edge on, up is out of the disc";

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("galaxy", "scene s.pbrt\nlength 4\nspace\ngalaxy swirl at 0 0 -60000 size 50000 tilt 55\nshot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
	EXPECT_NE(film.sceneAt(1.0).find("MediumInterface \"galaxy_swirl\" \"\""), std::string::npos);
	expectProblems({{"galaxy a at 0 0 0", "how big it is"}, {"galaxy a at 0 0 0 size 10", "size 100 to 400000"}, {"galaxy a at 0 0 0 size 500 kind round", "\"kind\""},
					{"galaxy a at 0 0 0 size 500 tilt 120", "\"tilt\""}});
}

// ---- 9. stars close up ----------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUniverseStar, ColourFollowsTemperatureSpotsAreDarkAndItGlows) {
	cs::Star sun;
	sun.name = "sol";
	cs::Star red = sun, blue = sun;
	red.kind = "red", blue.kind = "blue";
	EXPECT_DOUBLE_EQ(sun.kelvin(), 5800.0);
	cs::Star own = sun;
	own.temperature = 7000.0;
	EXPECT_DOUBLE_EQ(own.kelvin(), 7000.0) << "its own temperature wins";
	const cartoon::Vec3 r = sp::blackbody(red.kelvin()), b = sp::blackbody(blue.kelvin());
	EXPECT_GT(r.x, r.z) << "a red star is red";
	EXPECT_GT(b.z, b.x) << "a blue star is blue";
	// the surface: about 1 on average, its spots darker
	double sum = 0.0, darkest = 1e9;
	int n = 0;
	for (int j = 1; j < 60; ++j)
		for (int i = 0; i < 120; ++i) {
			const double th = 3.14159265358979 * j / 60.0, ph = 2.0 * 3.14159265358979 * i / 120.0;
			const double v = cs::starSurfaceAt(sun, {std::sin(th) * std::cos(ph), std::sin(th) * std::sin(ph), std::cos(th)});
			sum += v, ++n, darkest = std::min(darkest, v);
		}
	EXPECT_NEAR(sum / n, 1.0, 0.25);
	EXPECT_LT(darkest, 0.5) << "sunspots";
	// the glow: light just above the surface, less further out, none inside or past its box
	EXPECT_GT(cs::starGlowVoxel(sun, 1.1, 0.0, 0.0).le.x, 4.0 * cs::starGlowVoxel(sun, 2.0, 0.0, 0.0).le.x);
	EXPECT_EQ(cs::starGlowVoxel(sun, 0.5, 0.0, 0.0).le.x, 0.0);
	EXPECT_EQ(cs::starGlowVoxel(sun, 3.1, 0.0, 0.0).le.x, 0.0);

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("star", "scene s.pbrt\nlength 4\nspace sun off\nstar vega at 0 0 -30000 radius 5000 kind blue\nshot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
	const std::string s = film.sceneAt(1.0);
	EXPECT_NE(s.find("MediumInterface \"glow_vega\" \"\""), std::string::npos);
	EXPECT_NE(s.find("\"string filename\" [ \""), std::string::npos) << "its surface picture";
	expectProblems({{"star a at 0 0 0", "how big it is"}, {"star a at 0 0 0 radius 1", "radius 10 to 200000"}, {"star a at 0 0 0 radius 500 kind green", "\"kind\""},
					{"star a at 0 0 0 radius 500 temperature 900", "\"temperature\""}});
}

// ---- 10. flares and prominences -------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUniverseFlares, ProminencesLoopOffTheLimbAndFlaresBurstThenFade) {
	cs::Star sun;
	sun.name = "sol", sun.radius = 1000.0, sun.prominences = 6;
	// a prominence: its ends on the surface, its middle well above it
	const cartoon::Mesh m = cs::prominence(sun, 2, 0.0);
	ASSERT_FALSE(m.verts.empty());
	double lowest = 1e9, highest = 0.0;
	for (const cartoon::Vec3 &v : m.verts) {
		const double r = std::sqrt(v.x * v.x + v.y * v.y + (v.z - sun.at.z) * (v.z - sun.at.z));   // (its at.x is 0: story x is pbrt's mirrored)
		lowest = std::min(lowest, r), highest = std::max(highest, r);
	}
	EXPECT_LT(lowest, 1.0 * sun.radius) << "rooted in the surface";
	EXPECT_GT(highest, 1.1 * sun.radius) << "looping above it";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("flares", "scene s.pbrt\nlength 90\nspace sun off\nstar sol at 0 0 -30000 radius 5000 prominences 8 flare 10\nshot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
	EXPECT_NE(film.sceneAt(1.0).find("# its prominences"), std::string::npos);
	EXPECT_EQ(film.sceneAt(5.0).find("# a flare"), std::string::npos) << "not before its time";
	EXPECT_NE(film.sceneAt(12.0).find("# a flare"), std::string::npos);
	EXPECT_EQ(film.sceneAt(80.0).find("# a flare"), std::string::npos) << "over a minute later it is gone";
	expectProblems({{"star a at 0 0 0 radius 500 prominences 99", "\"prominences\""}});
}
