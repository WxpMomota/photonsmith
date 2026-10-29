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

#include "../../src/shared/cartoon/cartoon_films.h"   // (the films themselves, which these tests reach into)
#include "../../src/shared/lens_look.h"

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

// ---- 11. black holes ------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUniverseBlackHole, DiscIsHotterInsideComingSideBrighterAndTheFarSideBendsOverTheTop) {
	cs::BlackHole b;
	b.name = "maw", b.at = {0.0, 0.0, -30000.0}, b.radius = 2000.0;
	EXPECT_GT(cs::discKelvin(3.0), 2.0 * cs::discKelvin(12.0)) << "hotter inside";
	EXPECT_GT(cs::beaming(3.0, {1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), 1.5) << "gas coming towards the camera looks brighter";
	EXPECT_LT(cs::beaming(3.0, {-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), 0.7) << "and going away dimmer";
	const cs::DiscStream inner = cs::discStream(b, 3, 0.0), later = cs::discStream(b, 3, 5.0);
	EXPECT_GE(inner.r, 3.0);
	EXPECT_LE(inner.r, b.disc);
	EXPECT_GT(later.phi, inner.phi) << "going round";
	// edge-on from the front: the lensed far side is over the shadow's top, the photon ring round it
	const std::string s = cs::blackHoleText(b, 0.0, {0.0, 0.0, 0.0});
	EXPECT_NE(s.find("# its shadow"), std::string::npos);
	EXPECT_NE(s.find("# its photon ring"), std::string::npos);
	EXPECT_GT(count(s, "AreaLightSource"), 260u) << "its streams, their bent images";
	b.jets = 10.0;
	EXPECT_GT(count(cs::blackHoleText(b, 0.0, {0.0, 0.0, 0.0}), "AreaLightSource"), count(s, "AreaLightSource") + 30u) << "its jets' knots";

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("hole", "scene s.pbrt\nlength 4\nspace sun off\nblackhole maw at 0 0 -30000 radius 2000 jets 12\nshot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
	EXPECT_NE(film.sceneAt(1.0).find("# a black hole (film_cosmos.h): maw"), std::string::npos);
	expectProblems({{"blackhole a at 0 0 0", "how big it is"}, {"blackhole a at 0 0 0 radius 1", "radius 10 to 100000"}, {"blackhole a at 0 0 0 radius 500 disc 2", "\"disc\""},
					{"blackhole a at 0 0 0 radius 500 jets 1", "\"jets\""}});
}

// ---- 12. comets -----------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUniverseComet, TailsPointFromTheSunTheDustTailCurvesBackAndTheCometMoves) {
	cs::Comet c;
	c.name = "halley", c.move = {300.0, 0.0, 0.0};
	cartoon::Vec3 X, Y, Z;
	cs::cometFrame(c, {0.0, 0.0, -1.0}, X, Y, Z);
	EXPECT_NEAR(X.z, -1.0, 1e-9) << "the tails point away from the sun";
	EXPECT_NEAR(Y.x, -1.0, 1e-9) << "the dust tail curves back along its path";
	double e = 0.0, eSide = 0.0, eFar = 0.0, eBack = 0.0;
	cartoon::Vec3 colour, ionColour;
	cs::cometGlow(c, 0.0, 0.0, 0.0, e, colour);
	EXPECT_GT(colour.y, colour.x) << "the coma glows green-white";
	cs::cometGlow(c, 0.3, 0.0, 0.0, eFar, ionColour);
	EXPECT_GT(ionColour.z, ionColour.x) << "the straight ion tail is blue";
	cs::cometGlow(c, 0.3, 0.3 * 0.09, 0.0, eSide, colour);   // (on the dust tail's curve)
	EXPECT_GT(colour.x, colour.z) << "the dust tail is warm";
	cs::cometGlow(c, -0.05, 0.0, 0.2, eBack, colour);
	EXPECT_GT(e, 1.5 * eFar) << "brightest at its head";
	EXPECT_LT(eBack, 1e-3) << "nothing ahead of it off to the side";
	// a rock: lumpy, about its size
	const cartoon::Mesh rock = cs::spaceRock(3, 100.0);
	double lo = 1e9, hi = 0.0;
	for (const cartoon::Vec3 &v : rock.verts) lo = std::min(lo, length(v)), hi = std::max(hi, length(v));
	EXPECT_LT(lo, 0.45 * 100.0);
	EXPECT_GT(hi, 0.5 * 100.0 * 0.85);
	EXPECT_LT(hi, 100.0);

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("comet", "scene s.pbrt\nlength 4\nspace\ncomet halley at 0 0 -20000 move 300 0 0 tail 20000\nshot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
	const std::string at0 = film.sceneAt(0.0), at2 = film.sceneAt(2.0);
	EXPECT_NE(at0.find("MediumInterface \"comet_halley\" \"\""), std::string::npos);
	EXPECT_NE(at0, at2) << "it moves";
	expectProblems({{"comet a at 0 0 0 size 5", "\"size\""}, {"comet a at 0 0 0 move 1 2", "\"move\""}, {"comet a at 0 0 0 tail 50", "\"tail\""}});
}

// ---- 13. asteroid belts ---------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUniverseBelt, RocksFillTheBandOrTheBallMostAreSmallAndTheyGoRound) {
	cs::Belt b;
	b.name = "belt", b.at = {0.0, 0.0, 0.0}, b.radius = 20000.0, b.width = 4000.0, b.size = 400.0, b.count = 2000;
	int small = 0;
	for (int k = 0; k < b.count; ++k) {
		const cs::BeltRock r = cs::beltRock(b, k, 0.0);
		const double rr = std::hypot(r.at.x, r.at.z);
		EXPECT_GT(rr, b.radius - b.width) << k;
		EXPECT_LT(rr, b.radius + b.width) << k;
		EXPECT_LT(std::abs(r.at.y), 0.2 * b.width) << "a thin band";
		EXPECT_LE(r.size, b.size);
		small += r.size < 0.3 * b.size ? 1 : 0;
		EXPECT_GE(r.shape, 0);
		EXPECT_LT(r.shape, cs::kBeltShapes);
	}
	EXPECT_GT(small, b.count / 2) << "most are small";
	const cs::BeltRock now = cs::beltRock(b, 5, 0.0), later = cs::beltRock(b, 5, 60.0);
	EXPECT_NEAR(std::hypot(now.at.x, now.at.z), std::hypot(later.at.x, later.at.z), 1e-6) << "going round at its own radius";
	EXPECT_GT(length(now.at - later.at), 100.0);
	b.kind = "field", b.radius = 5000.0;
	for (int k = 0; k < 200; ++k) EXPECT_LT(length(cs::beltRock(b, k, 0.0).at), 5000.0 * 1.0001);

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("belt", "scene s.pbrt\nlength 4\nspace\nasteroids ring at 0 0 -30000 radius 20000 count 300\nshot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
	const std::string s = film.sceneAt(1.0);
	EXPECT_EQ(count(s, "ObjectBegin \"belt_ring_"), static_cast<std::size_t>(cs::kBeltShapes));
	EXPECT_EQ(count(s, "ObjectInstance \"belt_ring_"), 300u);
	expectProblems({{"asteroids a at 0 0 0 count 0", "\"count\""}, {"asteroids a at 0 0 0 kind cloud", "\"kind\""}, {"asteroids a at 0 0 0 radius 5", "\"radius\""}});
}

// ---- 14. dying stars ------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUniverseRemnant, ASupernovaFlashesAndItsShellGrowsAPulsarSweepsAndAPlanetaryNebulaIsAShell) {
	cs::Remnant sn;
	sn.name = "crab", sn.size = 10000.0, sn.blast = 5.0;
	double e = 0.0, eMiddle = 0.0;
	cartoon::Vec3 rim, middle;
	cs::remnantGlow(sn, 0.82, 0.0, 0.0, e, rim);
	cs::remnantGlow(sn, 0.45, 0.0, 0.0, eMiddle, middle);
	EXPECT_GT(e, eMiddle) << "its gas is in a shell";
	const std::string before = cs::remnantText("zz_remnant_test", sn, 2.0), flash = cs::remnantText("zz_remnant_test", sn, 6.0), later = cs::remnantText("zz_remnant_test", sn, 40.0);
	EXPECT_EQ(before.find("Include"), std::string::npos) << "a red giant, nothing blown off yet";
	EXPECT_NE(flash.find("# its flash"), std::string::npos);
	EXPECT_EQ(later.find("# its flash"), std::string::npos) << "faded";
	const auto boxScale = [](const std::string &s) {
		const std::size_t at = s.find("Scale ");
		return at == std::string::npos ? 0.0 : std::atof(s.c_str() + at + 6);
	};
	EXPECT_GT(boxScale(later), 2.0 * boxScale(flash)) << "the shell grows";

	cs::Remnant p = sn;
	p.kind = "pulsar", p.period = 4.0;
	const cartoon::Mesh b0 = cs::pulsarBeams(p, 0.0), b1 = cs::pulsarBeams(p, 1.0);
	ASSERT_EQ(b0.verts.size(), b1.verts.size());
	EXPECT_GT(length(b0.verts[5] - b1.verts[5]), 100.0) << "its beams sweep round";
	EXPECT_NE(cs::remnantText("zz_remnant_test", p, 0.0).find("# its beams"), std::string::npos);

	cs::Remnant pn = sn;
	pn.kind = "planetary";
	cs::remnantGlow(pn, 0.55, 0.0, 0.0, e, rim);
	cs::remnantGlow(pn, 0.2, 0.0, 0.0, eMiddle, middle);
	EXPECT_GT(e, eMiddle);
	EXPECT_GT(middle.y, middle.x) << "teal inside";
	cs::remnantGlow(pn, 0.65, 0.0, 0.0, e, rim);
	EXPECT_GT(rim.x, rim.y) << "red at its rim";
	std::error_code ec;
	std::filesystem::remove_all(cartoon::film_cache::dir("zz_remnant_test"), ec);

	expectProblems({{"remnant a at 0 0 0", "how big it is"}, {"remnant a at 0 0 0 size 500 kind nova", "\"kind\""}, {"remnant a at 0 0 0 size 500 period 0", "\"period\""}});
}

// ---- 15. ships and stations -----------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUniverseShip, CraftFlyNoseFirstTheirEnginesGlowWhileTheyMoveAndWheelsSpin) {
	cs::Ship sh;
	sh.name = "ark", sh.at = {0.0, 0.0, -500.0}, sh.move = {0.0, 0.0, -40.0}, sh.size = 60.0;
	// its nose (+x of its own frame) points the way it goes: -z, in story space too
	const cartoon::Vec3 nose = cartoon::transformPoint(cs::shipPlacement(sh, 0.0), {0.5, 0.0, 0.0}), tail = cartoon::transformPoint(cs::shipPlacement(sh, 0.0), {-0.5, 0.0, 0.0});
	EXPECT_LT(nose.z, tail.z) << "nose first";
	EXPECT_NEAR(length(nose - tail), 60.0, 1e-6);
	EXPECT_NEAR(cartoon::transformPoint(cs::shipPlacement(sh, 10.0), {0.0, 0.0, 0.0}).z, -900.0, 1e-6) << "40 m a second";
	for (const std::string &kind : cs::shipKinds()) {
		sh.kind = kind;
		const std::string moving = cs::shipText(sh, 1.0);
		EXPECT_NE(moving.find("# a " + kind), std::string::npos);
		EXPECT_GE(count(moving, "AreaLightSource"), 2u) << kind << ": its running lights";
	}
	sh.kind = "shuttle";
	cs::Ship still = sh;
	still.move = {0.0, 0.0, 0.0};
	EXPECT_GT(count(cs::shipText(sh, 0.5), "AreaLightSource"), count(cs::shipText(still, 0.5), "AreaLightSource")) << "its engines glow while it moves";
	sh.kind = "wheel";
	EXPECT_NE(cs::shipText(sh, 0.0), cs::shipText(sh, 2.0)) << "the wheel turns";

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("ship", "scene s.pbrt\nlength 4\nspace\nship ark at 0 0 -500 kind freighter size 80 move 0 0 -40 colour 0.8 0.3 0.2 engines on\nshot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
	EXPECT_NE(film.sceneAt(1.0).find("# a freighter (film_cosmos.h): ark"), std::string::npos);
	expectProblems({{"ship a at 0 0 0 kind tug", "\"kind\""}, {"ship a at 0 0 0 colour 2 0 0", "\"colour\""}, {"ship a at 0 0 0 engines maybe", "\"engines\""}, {"ship a at 0 0 0 size 0", "\"size\""}});
}

// ---- 16. astronauts -------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUniverseAstronaut, AstronautsDriftAndTumbleTheirLimbsSwayAndTheirTetherFollowsTheShip) {
	cs::Astronaut a;
	a.name = "kim", a.at = {0.0, 0.0, -6.0}, a.move = {0.1, 0.0, 0.0}, a.floaters = 6;
	const cartoon::Vec3 w0 = cs::astronautWaist(a, 0.0), w10 = cs::astronautWaist(a, 10.0);
	EXPECT_LT(length(w0 - a.at), 0.5) << "its waist is where it is";
	EXPECT_NEAR(w10.x - w0.x, 1.0, 0.5) << "drifting";
	const std::string s0 = cs::astronautText(a, 0.0), s1 = cs::astronautText(a, 1.0);
	EXPECT_NE(s0, s1) << "limbs sway, it tumbles";
	EXPECT_EQ(count(s0, "AreaLightSource"), 2u) << "its two lamps";
	EXPECT_NE(s0.find("Material \"dielectric\""), std::string::npos) << "a globe of water among its floaters";
	EXPECT_EQ(s0.find("# its tether"), std::string::npos);
	const cartoon::Vec3 hook{0.0, 0.0, -20.0};
	EXPECT_NE(cs::astronautText(a, 0.0, &hook).find("# its tether"), std::string::npos);

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("astronaut", "scene s.pbrt\nlength 4\nspace\nship ark at 0 0 -40 kind shuttle size 30 move 0 0 -2\nastronaut kim at 0 0 -10 tether ark floaters 3\nshot 0 A\n key 0 0 0  0 0 -10  50\n", film), "");
	EXPECT_NE(film.sceneAt(1.0).find("# its tether"), std::string::npos);
	expectProblems({{"astronaut a at 0 0 0 tether nobody", "no ship of that name"}, {"astronaut a at 0 0 0 suit 2 1 1", "\"suit\""}, {"astronaut a at 0 0 0 floaters 99", "\"floaters\""}});
}

// ---- 17. the camera's journeys --------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUniverseSpaceCam, OrbitsKeepTheirDistanceFlybysAreFastestNearestZoomsArePowersOfTenAndJumpsFlash) {
	const auto eyeOf = [](const cs::SpaceCam &c, double u) {
		const camera_keyframes::Key k = cs::spaceCamKey(c, u);
		return cartoon::Vec3{k.pos[0], k.pos[1], k.pos[2]};
	};
	cs::SpaceCam c;
	ASSERT_EQ(cs::spaceCamLine({"spacecam", "orbit", "0", "0", "-30000", "radius", "9000", "degrees", "120"}, c), "");
	EXPECT_NEAR(length(eyeOf(c, 0.0) - c.at), 9000.0, 1e-6);
	EXPECT_NEAR(length(eyeOf(c, 1.0) - c.at), 9000.0, 1e-6);
	EXPECT_NEAR(length(eyeOf(c, 1.0) - eyeOf(c, 0.0)), 2.0 * 9000.0 * std::cos(10.0 * 3.14159265358979 / 180.0) * std::sin(60.0 * 3.14159265358979 / 180.0), 1.0) << "120 degrees round";

	ASSERT_EQ(cs::spaceCamLine({"spacecam", "flyby", "0", "0", "-30000", "radius", "5000"}, c), "");
	EXPECT_NEAR(length(eyeOf(c, 0.5) - c.at), 5000.0, 1.0) << "nearest halfway";
	EXPECT_GT(length(eyeOf(c, 0.0) - c.at), 5.0 * 5000.0) << "in from far away";
	EXPECT_GT(length(eyeOf(c, 0.51) - eyeOf(c, 0.49)), 1.5 * length(eyeOf(c, 0.03) - eyeOf(c, 0.01))) << "fastest where it is nearest";

	ASSERT_EQ(cs::spaceCamLine({"spacecam", "zoom", "0", "0", "0", "from", "1000000", "to", "10"}, c), "");
	EXPECT_NEAR(length(eyeOf(c, 0.0)), 1.0e6, 1e-3);
	EXPECT_NEAR(length(eyeOf(c, 1.0)), 10.0, 1e-6);
	EXPECT_NEAR(length(eyeOf(c, 0.5)), std::sqrt(1.0e6 * 10.0), 1.0) << "the same factor every second";

	ASSERT_EQ(cs::spaceCamLine({"spacecam", "hyperspace", "0", "0", "0", "to", "500000", "0", "0"}, c), "");
	EXPECT_LT(length(eyeOf(c, 0.1)), 1.0);
	EXPECT_LT(length(eyeOf(c, 0.9) - cartoon::Vec3{500000.0, 0.0, 0.0}), 1.0) << "out where it goes";
	EXPECT_EQ(count(cs::hyperspaceText(c, 0.05), "AreaLightSource"), 0u) << "stars, not streaks, before";
	EXPECT_GE(count(cs::hyperspaceText(c, 0.5), "AreaLightSource"), 2u) << "streaks and the flash";

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("journey", "scene s.pbrt\nlength 10\nspace\nplanet home rocky at 0 0 -30000 radius 3000\nshot 0 A\n spacecam orbit 0 0 -30000 radius 9000\n", film), "");
	EXPECT_NE(film.sceneAt(0.0), film.sceneAt(9.0)) << "the camera goes round";
	expectProblems({{"shot 0 A\nspacecam orbit 0 0 0", "how far"}, {"shot 0 A\nspacecam zoom 0 0 0 from 10", "from how far and to how far"}, {"shot 0 A\nspacecam warp 0 0 0", "spacecam orbit"},
					{"shot 0 A\nspacecam hyperspace 0 0 0", "where it comes out"}, {"shot 0 A\n key 0 0 0  0 0 -1  50\nspacecam orbit 0 0 0 radius 5", "has a `spacecam` line and"}});
}

// ---- 18. light in space ---------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUniverseLight, TheSunFlaresTheLensAPlanetLightsItsNeighboursAndTheLookIsInTheFrame) {
	// a flare: a dim picture is left alone; one with a sun in it gets a ghost across the centre and a streak beside the sun
	const int w = 160, h = 90;
	std::vector<float> dim(static_cast<std::size_t>(w) * h * 3, 0.5f), sun = dim;
	for (int y = 20; y < 28; ++y)
		for (int x = 120; x < 128; ++x)
			for (int k = 0; k < 3; ++k) sun[(static_cast<std::size_t>(y) * w + x) * 3 + k] = 400.0f;
	lens_look::Look look;
	ASSERT_EQ(lens_look::parse("flare 1", look), "");
	std::vector<float> a = dim;
	lens_look::apply(a.data(), w, h, look);
	EXPECT_EQ(a, dim) << "nothing bright enough to flare";
	std::vector<float> b = sun;
	lens_look::apply(b.data(), w, h, look);
	const auto px = [&](const std::vector<float> &img, int x, int y) { return img[(static_cast<std::size_t>(y) * w + x) * 3 + 1]; };
	// the sun's centre is at (124, 24); the ghost of scale -0.55 at (80 - 0.55 * 44, 45 + 0.55 * 21) = (56, 57)
	EXPECT_GT(px(b, 56, 57), px(sun, 56, 57) + 0.05f) << "a ghost across the centre";
	EXPECT_GT(px(b, 100, 24), px(sun, 100, 24) + 0.05f) << "a streak beside the sun";
	EXPECT_NEAR(px(b, 56, 10), px(sun, 56, 10), 0.02f) << "and not everywhere";

	// planetshine: a lit planet beside the camera lights it; a planet showing its night side does not
	cartoon::planets::Planet p;
	p.name = "home", p.at = {0.0, 0.0, -10000.0}, p.radius = 4000.0;
	const std::string lit = cs::planetshineText({p}, 0.0, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 1.0), dark = cs::planetshineText({p}, 0.0, {0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, 1.0);
	EXPECT_NE(lit.find("LightSource \"spot\""), std::string::npos);
	EXPECT_EQ(dark.find("LightSource"), std::string::npos);

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("light", "scene s.pbrt\nlength 4\nspace sun 0 0 flare 0.4 bloom 0.2 planetshine 2\nplanet home rocky at 0 0 -10000 radius 4000\nshot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
	const std::string s = film.sceneAt(1.0);
	EXPECT_EQ(s.rfind("# @rt-look", 0), 0u) << "the look is the frame's first line";
	EXPECT_NE(s.find("flare 0.400 bloom 0.200"), std::string::npos);
	EXPECT_NE(s.find("# planetshine from home"), std::string::npos);
	expectProblems({{"space flare 2", "flare and bloom 0 to 1"}, {"space planetshine -1", "planetshine 0 to 10"}});
}

// ---- 19. sound in space ---------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUniverseSound, SpaceHumsEnginesRumbleAsTheyPassABlastBoomsAndSilenceIsAsked) {
	const auto energy = [](const cartoon::audio::Mix &m, double from, double to, double *left = nullptr, double *right = nullptr) {
		const std::vector<std::int16_t> pcm = m.finish(0.0, 0.0, 0.89, 0.0);   // (levels compared between mixes: none brought to a loudness)
		double l = 0.0, r = 0.0;
		for (std::size_t i = static_cast<std::size_t>(from * cartoon::audio::kRate); i < static_cast<std::size_t>(to * cartoon::audio::kRate) && 2 * i + 1 < pcm.size(); ++i)
			l += double(pcm[2 * i]) * pcm[2 * i], r += double(pcm[2 * i + 1]) * pcm[2 * i + 1];
		if (left) *left = l;
		if (right) *right = r;
		return l + r;
	};
	const auto mixOf = [](const std::string &key, const std::string &lines, double seconds) {
		ff::DataFilm film;
		EXPECT_EQ(loadFilm(key, "scene s.pbrt\nlength " + std::to_string(seconds) + "\n" + lines + "shot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
		cartoon::audio::Mix m(seconds);
		film.soundscape(m);
		return m;
	};
	EXPECT_EQ(energy(mixOf("hush", "space hum 0\n", 6.0), 0.0, 6.0), 0.0) << "silence when asked";
	const double hum = energy(mixOf("hum", "space\n", 6.0), 0.0, 6.0);
	EXPECT_GT(hum, 0.0) << "the hum of space";
	// a shuttle passing from left to right, 30 m in front: loudest as it passes, on the left first
	double l0 = 0.0, r0 = 0.0, l1 = 0.0, r1 = 0.0;
	const cartoon::audio::Mix pass = mixOf("pass", "space hum 0\nship ark at 300 0 -30 kind shuttle size 20 move -60 0 0\n", 10.0);
	energy(pass, 0.0, 2.0, &l0, &r0);
	energy(pass, 8.0, 10.0, &l1, &r1);
	EXPECT_GT(energy(pass, 4.0, 6.0), 3.0 * energy(pass, 0.0, 2.0)) << "loudest as it passes";
	EXPECT_GT(l0, 2.0 * r0) << "on the left first (pbrt's +x)";
	EXPECT_GT(r1, 2.0 * l1) << "then on the right";
	// a supernova blows up at 3 s: nothing before, a boom after
	const cartoon::audio::Mix blast = mixOf("blast", "space hum 0\nremnant crab at 0 0 -40000 size 12000 blast 3\n", 8.0);
	EXPECT_EQ(energy(blast, 0.0, 2.9), 0.0);
	EXPECT_GT(energy(blast, 3.0, 6.0), 0.0) << "the boom";
}

// ---- 20. re-entry and landing ---------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmUniverseDescent, ACraftBurnsInOpensItsChutesFiresItsRocketsAndLands) {
	cs::Descent d;
	ASSERT_EQ(cs::descentLine({"descent", "pod", "from", "0", "20000", "-60000", "to", "0", "0", "-2000", "start", "0", "land", "30", "size", "10"}, d), "");
	EXPECT_LT(length(cs::descentAt(d, cs::descentU(d, 0.0)) - d.from), 1e-6);
	EXPECT_LT(length(cs::descentAt(d, cs::descentU(d, 30.0)) - d.to), 1e-6) << "down where it lands";
	EXPECT_LT(length(cs::descentAt(d, cs::descentU(d, 40.0)) - d.to), 1e-6) << "and stays";
	EXPECT_GT(cs::descentU(d, 3.0), 3.0 / 30.0) << "fast at first, slowing as it comes";
	EXPECT_GT(cs::descentHeat(d, cs::descentU(d, 2.0)), 0.5) << "re-entry's fire early";
	EXPECT_EQ(cs::descentHeat(d, cs::descentU(d, 25.0)), 0.0);
	const std::string burning = cs::descentText(d, 2.0), chutes = cs::descentText(d, 20.0), landing = cs::descentText(d, 28.5);
	EXPECT_NE(burning.find("# its re-entry fire"), std::string::npos);
	EXPECT_EQ(chutes.find("# its re-entry fire"), std::string::npos);
	const std::string canopy = "[ 0.95000 0.45000 0.12000 ]", dust = "[ 0.45000 0.40000 0.34000 ]";
	EXPECT_NE(chutes.find(canopy), std::string::npos) << "its parachutes";
	EXPECT_EQ(burning.find(canopy), std::string::npos);
	cs::Descent bare = d;
	bare.chutes = false;
	EXPECT_EQ(cs::descentText(bare, 20.0).find(canopy), std::string::npos) << "no chutes when asked";
	EXPECT_NE(landing.find(dust), std::string::npos) << "dust as it lands";
	EXPECT_EQ(cs::descentText(d, 40.0).find(dust), std::string::npos) << "then it rests";

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("descent", "scene s.pbrt\nlength 30\nspace hum 0\ndescent pod from 0 20000 -60000 to 0 0 -2000 start 0 land 30 kind shuttle size 10\nshot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
	EXPECT_NE(film.sceneAt(2.0).find("# a descent (film_voyage.h): pod"), std::string::npos);
	cartoon::audio::Mix m(30.0);
	film.soundscape(m);
	const std::vector<std::int16_t> pcm = m.finish(0.0, 0.0, 0.89, 0.0);
	double e = 0.0;
	for (std::int16_t v : pcm) e += double(v) * v;
	EXPECT_GT(e, 0.0) << "it roars as it burns and thumps as it lands";
	expectProblems({{"descent pod from 0 1 0 to 0 0 0 start 5 land 2", "\"land\" before \"start\""}, {"descent pod from 0 1 0 to 0 0 0", "when it starts and when it lands"},
					{"descent pod from 0 1 0 to 0 0 0 start 0 land 9 chutes maybe", "\"chutes\""}});
}

// ---- found in review (2026-10-28) -----------------------------------------------------------------------------------------------------------------------------------------

// A galaxy's grid box holds its whole disc, whatever its tilt: the rim of the disc, taken back from the disc's frame (galaxyFrame) into the box's, is inside it.
TEST(FilmUniverseReview, AGalaxysBoxHoldsItsWholeDiscAtEveryTilt) {
	for (double tilt : {0.0, 20.0, 60.0, 90.0}) {
		cs::Galaxy g;
		g.tilt = tilt, g.turn = 30.0;
		const cartoon::Vec3 half = cs::galaxyHalf(g);
		const double ti = tilt * cartoon::kPi / 180.0, tu = g.turn * cartoon::kPi / 180.0;
		for (int k = 0; k < 64; ++k) {
			const double a = 2.0 * cartoon::kPi * k / 64.0, u = 0.95 * std::cos(a), v = 0.95 * std::sin(a), h = 0.0;
			const double x = u * std::cos(tu) - v * std::sin(tu), y1 = u * std::sin(tu) + v * std::cos(tu);
			const double y = y1 * std::cos(ti) - h * std::sin(ti), z = y1 * std::sin(ti) + h * std::cos(ti);
			double uu = 0.0, vv = 0.0, hh = 0.0;
			cs::galaxyFrame(g, {x, y, z}, uu, vv, hh);
			ASSERT_NEAR(uu, u, 1e-9) << "the inverse is right";
			EXPECT_LE(std::abs(x), half.x + 1e-9) << "tilt " << tilt;
			EXPECT_LE(std::abs(y), half.y + 1e-9) << "tilt " << tilt << ": the disc's rim is outside the box";
			EXPECT_LE(std::abs(z), half.z + 1e-9) << "tilt " << tilt << ": the disc's rim is outside the box";
		}
	}
}

// In a narrow shot of the sun, the sun's ball is along the sun's direction from the camera, wherever the camera is.
TEST(FilmUniverseReview, TheSunsBallIsAlongTheSunFromTheCamera) {
	cartoon::space::Space s;
	s.on = true, s.sunBearing = 180.0, s.sunElevation = 0.0;
	const cartoon::Vec3 eye{40000.0, 0.0, 0.0};   // (story space: far from the origin)
	const std::string text = cartoon::space::spaceText("review_sunball", s, true, eye);
	const std::size_t at = text.find("Translate ", text.find("the sun as a ball"));
	ASSERT_NE(at, std::string::npos);
	double px = 0.0, py = 0.0, pz = 0.0;
	std::istringstream(text.substr(at + 10)) >> px >> py >> pz;   // (pbrt's)
	const cartoon::Vec3 d = cartoon::space::sunDirection(s), toBall = normalize(cartoon::Vec3{-px, py, pz} - eye);   // (both into story space)
	EXPECT_GT(dot(toBall, cartoon::Vec3{-d.x, d.y, d.z}), std::cos(0.01 * cartoon::kPi / 180.0)) << "the ball is the sun's direction from the camera, not from the origin";
}

// A black hole spinning the other way: its jets' knots stay between the pole and the jets' end, with sizes, however long the film.
TEST(FilmUniverseReview, AJetSpinningTheOtherWayKeepsItsKnotsInPlace) {
	cs::BlackHole b;
	b.jets = 18.0, b.spin = -2.0;
	const std::string text = cs::blackHoleText(b, 300.0, {0.0, 0.0, 0.0});
	int spheres = 0;
	for (std::size_t at = text.find("\"float radius\" [ "); at != std::string::npos; at = text.find("\"float radius\" [ ", at + 1)) {
		++spheres;
		EXPECT_GE(std::atof(text.c_str() + at + 17), 0.0) << "a knot with a negative size";
	}
	EXPECT_GT(spheres, 40) << "the jets' knots";
}

// A planet with a negative spin turns slowly the other way, not once a second.
TEST(FilmUniverseReview, ANegativeSpinTurnsAPlanetSlowlyTheOtherWay) {
	pl::Planet p;
	p.spin = -600.0;
	EXPECT_NE(pl::placement(p, 60.0).find("Rotate -36.0000 0 1 0"), std::string::npos) << pl::placement(p, 60.0);
}

// A star's glow is baked per radius: another radius is another file, not the old one again.
TEST(FilmUniverseReview, AStarsGlowIsMadeAgainForAnotherRadius) {
	cs::Star a, b;
	a.name = b.name = "reviewstar", b.radius = 0.5 * a.radius;
	const auto glowFile = [](const std::string &text) {
		const std::size_t at = text.find("starglow_");
		return at == std::string::npos ? std::string() : text.substr(at, text.find(".pbrt", at) - at);
	};
	const std::string fa = glowFile(cs::starText("review_star", a, 0.0)), fb = glowFile(cs::starText("review_star", b, 0.0));
	ASSERT_FALSE(fa.empty());
	EXPECT_NE(fa, fb);
}

// A belt narrower in radius than its band is wide (`radius 2000`, the default width 4000): no rock comes to the middle, and none whirls round thousands of times too fast.
TEST(FilmUniverseReview, ARingBeltNarrowerThanItsBandKeepsItsRocksOutOfTheMiddle) {
	cs::Belt b;
	b.radius = 2000.0;
	for (int k = 0; k < 400; ++k) {
		const cs::BeltRock r0 = cs::beltRock(b, k, 10.0), r1 = cs::beltRock(b, k, 10.0 + 1.0 / 24.0);
		EXPECT_GT(length(r0.at - b.at), 0.25 * b.radius) << "rock " << k;
		EXPECT_LT(length(r1.at - r0.at), 0.05 * b.radius) << "rock " << k << " moved too far in a frame";
	}
}

// A total eclipse: the corona is there, and the diamond ring is not (it is the last sliver going, before totality).
TEST(FilmUniverseReview, TotalityHasTheCoronaButNoDiamondRing) {
	pl::Planet p;
	p.name = "blocker", p.at = {0.0, 0.0, -1000.0}, p.radius = 100.0;
	const std::string text = pl::eclipseText("review_eclipse", {p}, 0.0, {0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, 0.53);
	EXPECT_NE(text.find("corona"), std::string::npos);
	EXPECT_EQ(text.find("diamond ring"), std::string::npos);
}

// A descent onto a terrain (no `space` line) is heard, and a blast or a landing before the film began is not heard at its start.
TEST(FilmUniverseReview, ADescentOverLandIsHeardAndWhatWasOverBeforeTheFilmIsNot) {
	const auto mixOf = [](const std::string &key, const std::string &lines, double seconds) {
		ff::DataFilm film;
		EXPECT_EQ(loadFilm(key, "scene s.pbrt\nlength " + std::to_string(seconds) + "\n" + lines + "shot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
		cartoon::audio::Mix m(seconds);
		film.soundscape(m);
		const std::vector<std::int16_t> pcm = m.finish(0.0, 0.0, 0.89, 0.0);
		double e = 0.0;
		for (std::int16_t v : pcm) e += double(v) * v;
		return e;
	};
	EXPECT_GT(mixOf("landing", "terrain hills size 200 height 20 seed 1 far off\ndescent pod from 0 2000 -3000 to 0 0 -300 start 0 land 8 size 10\n", 10.0), 0.0) << "a landing on land roars and thumps";
	EXPECT_EQ(mixOf("pastblast", "space hum 0\nremnant crab at 0 0 -40000 size 12000 blast -200\n", 4.0), 0.0) << "a supernova that blew up long before";
	EXPECT_EQ(mixOf("pastlanding", "space hum 0\ndescent pod from 0 2000 -3000 to 0 0 -300 start -60 land -30 size 10\n", 4.0), 0.0) << "a craft that landed before the film";
}
