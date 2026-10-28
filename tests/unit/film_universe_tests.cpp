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
