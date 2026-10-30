// film_space11_tests.cpp - round 11, the space films: orbits and burns (film_orbits.h), space battles (film_battle.h), alien characters (film_aliens.h), the real night
// sky (film_starsky.h), weather on planets (film_planets.h), low gravity (`gravity`, footprints), floating water and clutter (film_zerog.h), interiors (film_interior.h),
// holograms and screens (film_screens.h) and an explainer's drawings (film_explain.h).

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/cartoon_films.h"   // (the films themselves, which these tests reach into)
#include "../../src/shared/cartoon/film_starsky.h"
#include "../../src/shared/cartoon/prop_physics.h"

namespace ff = cartoon::film_file;
using V3 = cartoon::Vec3;

namespace {

const char *kGround = R"(# a floor
LookAt 0 1.2 4  0 0.4 0  0 1 0
Camera "perspective" "float fov" [ 40 ]
Film "rgb" "integer xresolution" [ 320 ] "integer yresolution" [ 180 ]
WorldBegin
LightSource "distant" "point3 from" [ 3 6 4 ] "point3 to" [ 0 0 0 ] "rgb L" [ 2 2 2 ]
AttributeBegin
  Material "diffuse" "rgb reflectance" [ 0.5 0.5 0.5 ]
  Shape "trianglemesh" "integer indices" [ 0 1 2  0 2 3 ] "point3 P" [ -20 0 -20   20 0 -20   20 0 20   -20 0 20 ]
AttributeEnd
)";

std::string loadFilm(const std::string &name, const std::string &filmText, ff::DataFilm &film) {
	cartoon::film_kinds::install();   // (the person kind and the aliens: installed when the film registry is first built, which a test run on its own may not have done)
	cartoon::film_kinds::aliens::install();
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_space11_test_" + name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt", std::ios::binary) << kGround;
	std::ofstream(d / (name + ".film"), std::ios::binary) << filmText;
	return film.load((d / (name + ".film")).string());
}
std::string problemWith(const std::string &lines) {
	ff::DataFilm bad;
	return loadFilm("bad", "scene s.pbrt\nlength 4\n" + lines + "\n", bad);
}
double angleBetween(V3 a, V3 b) { return std::acos(std::clamp(cartoon::dot(cartoon::normalize(a), cartoon::normalize(b)), -1.0, 1.0)) * 180.0 / cartoon::kPi; }

}  // namespace

// ---- orbits ----
TEST(FilmSpace11Orbits, ACircleKeepsItsRadiusAndAQuarterPeriodIsAQuarterTurn) {
	cartoon::cosmos::Orbit o;
	o.on = true, o.radius = 500.0, o.period = 40.0, o.centre = {10.0, 0.0, 0.0};
	for (double t = 0.0; t < 40.0; t += 3.7) EXPECT_NEAR(cartoon::length(cartoon::cosmos::orbitPoint(o, t) - o.centre), 500.0, 1e-6);
	EXPECT_NEAR(angleBetween(cartoon::cosmos::orbitPoint(o, 0.0) - o.centre, cartoon::cosmos::orbitPoint(o, 10.0) - o.centre), 90.0, 1e-6);
	EXPECT_EQ(cartoon::cosmos::orbitState(o, 7.0).burn, 0.0);   // (no transfer: no burn)
}

TEST(FilmSpace11Orbits, AnEllipseReachesItsNearestAndFarthest) {
	cartoon::cosmos::Orbit o;
	o.on = true, o.radius = 400.0, o.apo = 1200.0, o.period = 30.0;
	double lo = 1e9, hi = 0.0;
	for (double t = 0.0; t < 200.0; t += 0.05) {
		const double r = cartoon::length(cartoon::cosmos::orbitPoint(o, t));
		lo = std::min(lo, r), hi = std::max(hi, r);
	}
	EXPECT_NEAR(lo, 400.0, 2.0);
	EXPECT_NEAR(hi, 1200.0, 2.0);
}

TEST(FilmSpace11Orbits, ATransferRisesToItsNewCircleAndBurnsTwiceGoingDownItFlipsFirst) {
	cartoon::cosmos::Orbit up;
	up.on = true, up.radius = 500.0, up.period = 20.0, up.to = 1500.0, up.at = 5.0;
	const double second = cartoon::cosmos::secondBurn(up);
	EXPECT_NEAR(second - 5.0, 0.5 * 20.0 * std::pow(1000.0 / 500.0, 1.5), 1e-6);   // (half a turn on the transfer ellipse, by Kepler's third law)
	EXPECT_NEAR(cartoon::length(cartoon::cosmos::orbitPoint(up, second + 7.0)), 1500.0, 1e-6);
	EXPECT_NEAR(cartoon::length(cartoon::cosmos::orbitPoint(up, second)), 1500.0, 1.0);   // (the ellipse's far end touches the new circle)
	EXPECT_GT(cartoon::cosmos::orbitState(up, 5.0).burn, 0.99);
	EXPECT_GT(cartoon::cosmos::orbitState(up, second).burn, 0.99);
	EXPECT_EQ(cartoon::cosmos::orbitState(up, 0.5 * (5.0 + second)).burn, 0.0);
	EXPECT_FALSE(cartoon::cosmos::orbitState(up, 5.0).retrograde);
	cartoon::cosmos::Orbit down = up;
	down.radius = 1500.0, down.to = 500.0;
	EXPECT_TRUE(cartoon::cosmos::orbitState(down, 5.0).retrograde);
	EXPECT_NEAR(cartoon::cosmos::orbitState(down, 5.0).flip, 1.0, 1e-9);   // (turned round to fire ahead by the time the burn comes)
	EXPECT_EQ(cartoon::cosmos::orbitState(down, 0.0).flip, 0.0);
	EXPECT_NEAR(cartoon::cosmos::flipTurn(0.5), 0.5, 1e-9);
	EXPECT_EQ(cartoon::cosmos::flipTurn(0.0), 0.0);
	EXPECT_EQ(cartoon::cosmos::flipTurn(1.0), 1.0);
}

TEST(FilmSpace11Orbits, AShipOnAnOrbitFlipsWithThrustersAndFiresItsEngineOnlyForItsBurns) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("orbit", "scene s.pbrt\nlength 40\nspace\nplanet home rocky at 0 0 0 radius 300\nship sat at 0 0 0\norbit sat around home radius 1500 period 30 to 600 at 12\n"
							   "shot 0 A\n key 0 3000 -3000  0 0 0  50\n", film), "");
	const cartoon::cosmos::Ship &s = film.spec.ships.at(0);
	ASSERT_TRUE(s.orbit.on);
	V3 push;
	EXPECT_EQ(cartoon::cosmos::shipBurning(s, 2.0), 0.0);
	EXPECT_GT(cartoon::cosmos::shipBurning(s, 12.0), 0.9);
	EXPECT_EQ(cartoon::cosmos::shipThrusters(s, 2.0, push), 0.0);   // (coasting round its circle: nothing puffs)
	double most = 0.0;
	for (double t = 7.0; t < 12.0; t += 0.05) most = std::max(most, cartoon::cosmos::shipThrusters(s, t, push));
	EXPECT_GT(most, 0.0);   // (the flip before the first burn down starts and stops with puffs)
	EXPECT_NE(problemWith("ship sat at 0 0 0\norbit sat radius 100"), "");
	EXPECT_NE(problemWith("orbit nobody at 0 0 0 radius 100"), "");
	EXPECT_NE(problemWith("ship sat at 0 0 0\norbit sat at 0 0 0 radius 100 apoapsis 50"), "");
}

// ---- battles ----
TEST(FilmSpace11Battle, BoltsReachTheirTargetAShieldStopsThemAndAnExplodedShipIsGone) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("battle", "scene s.pbrt\nlength 10\nspace\nship a at 0 0 0 kind fighter size 20\nship b at 0 0 600 kind freighter size 40\n"
								"laser a at 1 to b count 3 every 0.5 speed 300\nshield b radius 30\nexplode b at 6\nshot 0 A\n key 300 200 -400  0 0 300  50\n", film), "");
	const std::vector<cartoon::battle::Bolt> bolts = cartoon::battle::boltsOf(film.spec.battle, film.spec.ships);
	ASSERT_EQ(bolts.size(), 3u);
	for (const auto &b : bolts) {
		EXPECT_NEAR(b.t1 - b.t0, cartoon::length(b.end - b.start) / 300.0, 1e-6);
		EXPECT_TRUE(b.shield != nullptr);
		EXPECT_NEAR(cartoon::length(b.end - b.shieldCentre), 30.0, 1.0);   // (stopped on the shield's skin)
	}
	EXPECT_NEAR(cartoon::battle::goneAt(film.spec.battle, "b"), 6.0, 1e-9);
	EXPECT_EQ(film.spec.ships.at(1).goneAt, 6.0);
	EXPECT_NE(problemWith("laser nobody at 1"), "");
	EXPECT_NE(problemWith("ship a at 0 0 0\nexplode a"), "");
}

// ---- aliens ----
TEST(FilmSpace11Aliens, EachKindDrawsAndGestures) {
	for (const char *kind : {"blob", "squid", "hoverbot"}) {
		ff::DataFilm film;
		ASSERT_EQ(loadFilm(std::string("alien_") + kind, std::string("scene s.pbrt\nlength 4\ncharacter zed ") + kind + " at 0 0 0 facing 0\ngesture zed \"wave\" 0.5 0.9 2 2.4\n", film), "") << kind;
		const std::string a = film.sceneAt(0.5), b = film.sceneAt(1.6);
		EXPECT_NE(a.find("Shape"), std::string::npos) << kind;
		EXPECT_NE(a, b) << kind;   // (it moves: hops, sways or hovers, and waves)
	}
}

// ---- the real sky ----
TEST(FilmSpace11Sky, TheCatalogueLoadsAndThePlanetsAreWhereTheyWereOnTheFirstDayOf2000) {
	const auto &stars = cartoon::starsky::catalogue();
	if (stars.empty()) GTEST_SKIP() << "the star catalogue (film_assets/library/stars/bsc5.txt) is not here";
	EXPECT_GT(stars.size(), 9000u);
	EXPECT_GT(cartoon::starsky::figures().size(), 100u);
	const double jd = cartoon::starsky::julianDate(2000, 1, 1, 12.0);
	EXPECT_NEAR(jd, 2451545.0, 1e-9);
	for (const auto &p : cartoon::starsky::planetsOn(jd)) {   // (JPL Horizons: Jupiter RA 1h36m Dec +8.6; Saturn RA 2h35m Dec +12.6)
		if (p.name == "jupiter") { EXPECT_NEAR(p.ra, 24.0, 2.0); EXPECT_NEAR(p.dec, 8.6, 2.0); }
		if (p.name == "saturn") { EXPECT_NEAR(p.ra, 38.7, 2.0); EXPECT_NEAR(p.dec, 12.6, 2.0); }
	}
	double ra = 0.0, dec = 0.0;
	cartoon::starsky::sunOn(jd, ra, dec);
	EXPECT_NEAR(ra, 281.3, 1.0);   // (the sun in Sagittarius at the new year, well south)
	EXPECT_NEAR(dec, -23.0, 1.0);
	double bad = 0.0;
	EXPECT_FALSE(cartoon::starsky::parseDate("1700-01-01", bad));
	EXPECT_TRUE(cartoon::starsky::parseDate("1969-07-20", bad));
}

TEST(FilmSpace11Sky, TheSkyFrameRunsRightAscensionEastwardToTheLeft) {
	cartoon::starsky::Frame f;   // (the pole up, RA 0 straight ahead along +z)
	const V3 ahead = f.world(0.0, 0.0), east = f.world(90.0, 0.0), pole = f.world(0.0, 90.0);
	EXPECT_NEAR(ahead.z, 1.0, 1e-9);
	EXPECT_NEAR(pole.y, 1.0, 1e-9);
	EXPECT_NEAR(east.x, -1.0, 1e-9);   // (looking along +z, pbrt's +x is on the right: RA grows to the left, as the real sky's)
	EXPECT_NE(problemWith("space constellations"), "");   // (constellations need the real stars)
	EXPECT_EQ(problemWith("space stars real constellations date 1969-07-20"), "");
}

// ---- weather ----
TEST(FilmSpace11Weather, BandsFlowEastAndWestAndAStillPlanetDoesNotDrift) {
	cartoon::planets::Planet p;
	p.kind = "gas", p.flow = 1.0, p.spin = 600.0;
	EXPECT_NE(cartoon::planets::bandDrift(p, 0.0, 100.0), 0.0);
	EXPECT_LT(cartoon::planets::bandDrift(p, 0.0, 100.0) * cartoon::planets::bandDrift(p, std::sin(0.45), 100.0), 0.0);   // (neighbouring jets go opposite ways)
	p.flow = 0.0;
	EXPECT_EQ(cartoon::planets::bandDrift(p, 0.3, 100.0), 0.0);
	EXPECT_NE(problemWith("space\nplanet a rocky at 0 0 -5000 radius 100 storms 3"), "");   // (storms are a gas giant's)
	EXPECT_NE(problemWith("space\nplanet a gas at 0 0 -5000 radius 100 clouds 0.5"), "");   // (a cloud layer is a solid world's)
	EXPECT_EQ(problemWith("space\nplanet a ocean at 0 0 -5000 radius 100 clouds 0.5 lightning 1"), "");
}

// ---- low gravity ----
TEST(FilmSpace11Gravity, JumpsHangLongerAndBouncesRiseSlower) {
	ff::DataFilm earth, moon;
	ASSERT_EQ(loadFilm("g_earth", "scene s.pbrt\nlength 4\n", earth), "");
	ASSERT_EQ(loadFilm("g_moon", "scene s.pbrt\nlength 4\ngravity 1.62\nfootprints\n", moon), "");
	EXPECT_EQ(moon.spec.gravity, 1.62);
	ff::WalkLine w;
	w.height = 0.5;
	EXPECT_NEAR(moon.flightOf(w) / earth.flightOf(w), std::sqrt(9.81 / 1.62), 1e-9);
	cartoon::physics::Bounce b;
	b.from = {0.0, 2.0, 0.0};
	cartoon::physics::Bounce slow = b;
	slow.gravity = 1.62;
	EXPECT_LT(cartoon::physics::at(b, 0.5).at.y, cartoon::physics::at(slow, 0.5).at.y);   // (on the moon it has fallen less)
	EXPECT_NE(problemWith("gravity -1"), "");
	EXPECT_NE(problemWith("footprints nobody"), "");
}

// ---- floating ----
TEST(FilmSpace11ZeroG, ADropRingsAboutItsSizeSplitsIntoTwoHalvesAndClutterStaysInItsBox) {
	cartoon::zerog::Blob b;
	b.size = 0.05;
	double lo = 1e9, hi = 0.0;
	for (double t = 0.0; t < 2.0; t += 0.01) {
		const double r = cartoon::zerog::ringingRadius({0.0, 1.0, 0.0}, b.size, t, 1);
		lo = std::min(lo, r), hi = std::max(hi, r);
	}
	EXPECT_GT(hi, 0.05);
	EXPECT_LT(lo, 0.05);
	EXPECT_GT(lo, 0.03);   // (it wobbles, not more)
	b.split = 1.0;
	const std::string before = cartoon::zerog::blobText(b, 0.2), after = cartoon::zerog::blobText(b, 1.5);
	auto count = [](const std::string &s) { std::size_t n = 0; for (std::size_t p = s.find("Shape"); p != std::string::npos; p = s.find("Shape", p + 1)) ++n; return n; };
	EXPECT_EQ(count(before), 1u);
	EXPECT_EQ(count(after), 2u);
	cartoon::zerog::Drift d;
	d.centre = {1.0, 2.0, 3.0}, d.half = {0.5, 0.4, 0.3}, d.count = 20, d.speed = 0.7;
	for (double t = 0.0; t < 30.0; t += 0.37)
		for (int k = 0; k < d.count; ++k) {
			const V3 p = cartoon::zerog::driftPlace(d, k, t);
			EXPECT_LE(std::abs(p.x - 1.0), 0.5 + 1e-9);
			EXPECT_LE(std::abs(p.y - 2.0), 0.4 + 1e-9);
			EXPECT_LE(std::abs(p.z - 3.0), 0.3 + 1e-9);
		}
	EXPECT_NE(problemWith("blob w"), "");
	EXPECT_NE(problemWith("blob w on nobody"), "");
	EXPECT_NE(problemWith("drift junk 500 at 0 0 0"), "");
}

// ---- interiors ----
TEST(FilmSpace11Interiors, AnAirlockShutsItsInnerDoorWarnsThenOpensItsOuterDoorWithAPuffOfMist) {
	cartoon::interior::Interior in;
	in.kind = "airlock", in.cycle = 2.0;
	double inner = 0, outer = 0, warn = 0, vent = 0;
	cartoon::interior::airlockState(in, 1.0, inner, outer, warn, vent);
	EXPECT_EQ(inner, 1.0); EXPECT_EQ(outer, 0.0); EXPECT_EQ(warn, 0.0);
	cartoon::interior::airlockState(in, 5.0, inner, outer, warn, vent);
	EXPECT_EQ(inner, 0.0); EXPECT_EQ(outer, 0.0); EXPECT_EQ(warn, 1.0); EXPECT_EQ(vent, 0.0);
	cartoon::interior::airlockState(in, 8.5, inner, outer, warn, vent);
	EXPECT_EQ(outer, 1.0);
	EXPECT_GT(vent, 0.0);
	EXPECT_LT(vent, 1.0);
	for (const char *kind : {"corridor", "cockpit", "airlock", "ring"}) {
		in.kind = kind;
		EXPECT_NE(cartoon::interior::interiorText(in, 1.0).find("Shape"), std::string::npos) << kind;
	}
	EXPECT_NE(problemWith("interior kitchen at 0 0 0"), "");
	EXPECT_NE(problemWith("interior corridor"), "");
	EXPECT_NE(problemWith("interior corridor at 0 0 0 cycle 3"), "");   // (a cycle is an airlock's)
}

// ---- holograms and screens ----
TEST(FilmSpace11Screens, AHologramIsCutIntoScanlinesAndGlowsWithoutBeingALight) {
	cartoon::Mesh tri;
	tri.verts = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};
	tri.normals = {{0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}};
	tri.tris = {{0, 1, 2}};
	cartoon::Mesh out;
	cartoon::screens::detail::scanlines(tri, cartoon::identity(), 0.0, 0.1, 0.5, 0.0, out);
	double area = 0.0;
	for (const auto &t : out.tris) {
		area += 0.5 * cartoon::length(cartoon::cross(out.verts[t[1]] - out.verts[t[0]], out.verts[t[2]] - out.verts[t[0]]));
		for (int k = 0; k < 3; ++k) EXPECT_LE(std::fmod(out.verts[t[k]].y + 1e-9, 0.1), 0.05 + 2e-9);   // (every piece inside a lit slice)
	}
	EXPECT_NEAR(area, 0.2625, 1e-9);   // (the lit halves of its slices: their widths 1 - y summed over [0.1k, 0.1k + 0.05])
	for (const char *kind : {"globe", "ship", "orbits", "map"}) {
		cartoon::screens::Hologram h;
		h.kind = kind;
		const std::string text = cartoon::screens::hologramText(h, 1.0);
		EXPECT_NE(text.find("\"bool sampled\" false"), std::string::npos) << kind;
	}
	EXPECT_NE(problemWith("hologram h cube at 0 0 0"), "");
	EXPECT_NE(problemWith("hologram h globe"), "");
	EXPECT_NE(problemWith("screen tv at 0 0 0"), "");   // (what it shows)
	EXPECT_NE(problemWith("screen tv at 0 0 0 show picture"), "");
	EXPECT_NE(problemWith("screen tv at 0 0 0 show picture missing.png"), "");
	EXPECT_EQ(problemWith("screen tv at 0 1 0 show radar range 500"), "");
}

TEST(FilmSpace11Screens, ARadarShowsTheShipsInRangeAndTheirBlipsFadeAfterTheBeamPasses) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("radar", "scene s.pbrt\nlength 6\nspace\nship near at 100 0 0\nship far at 9000 0 0\nscreen tv at 0 1 0 show radar range 1000\nshot 0 A\n key 0 1 -3  0 1 0  50\n", film), "");
	const cartoon::screens::Screen &sc = film.spec.screens.at(0);
	const std::string text = cartoon::screens::screenText(sc, 0.05, "space11_test", film.spec.ships);
	std::size_t blips = 0;
	for (std::size_t p = text.find("Shape \"trianglemesh\""); p != std::string::npos; p = text.find("Shape \"trianglemesh\"", p + 1)) ++blips;
	// (the frame, the face, six beam lines and one blip: the far ship is out of range)
	EXPECT_EQ(blips, 9u);
}

// ---- an explainer's drawings ----
TEST(FilmSpace11Explain, LabelsKeepTheirSizeInTheFrameAndPopUpOnTime) {
	cartoon::explain::View v;
	v.eye = {0.0, 0.0, 0.0}, v.target = {0.0, 0.0, 1.0}, v.fov = 40.0;
	EXPECT_NEAR(v.frameHeightAt({0.0, 0.0, 200.0}) / v.frameHeightAt({0.0, 0.0, 100.0}), 2.0, 1e-9);
	EXPECT_NEAR(v.right().x, 1.0, 1e-9);   // (pbrt's +x on the right looking along +z)
	EXPECT_NEAR(v.up().y, 1.0, 1e-9);
	EXPECT_EQ(cartoon::explain::detail::shown(0.5, 1.0, 5.0), 0.0);
	EXPECT_EQ(cartoon::explain::detail::shown(3.0, 1.0, 5.0), 1.0);
	EXPECT_EQ(cartoon::explain::detail::shown(5.5, 1.0, 5.0), 0.0);
	EXPECT_EQ(cartoon::explain::detail::grouped(12742.0), "12,742");
	EXPECT_EQ(cartoon::explain::detail::grouped(139820.0), "139,820");
	EXPECT_EQ(cartoon::explain::detail::grouped(469.0), "469");
	ASSERT_NE(cartoon::explain::knownBody("jupiter"), nullptr);
	EXPECT_NEAR(cartoon::explain::knownBody("Jupiter")->radiusKm / cartoon::explain::knownBody("EARTH")->radiusKm, 10.97, 0.01);
	EXPECT_EQ(cartoon::explain::knownBody("Vulcan"), nullptr);
}

TEST(FilmSpace11Explain, TheFilmChecksWhatItsDrawingsName) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("explain", "scene s.pbrt\nlength 8\nspace\nplanet home ocean at 0 0 0 radius 1000\nmoon luna rocky of home radius 250 orbit 2600 period 40\n"
								 "ship probe at 0 0 0\norbit probe around home radius 1500 period 12\nlabel \"Home\" on home from 1 to 6\norbitline probe dashed\norbitline luna\n"
								 "scalebar 2000 at 0 -1600 0\ncompare Earth Mars home at 0 0 -3500 height 1200\nshot 0 A\n key 0 3500 -6500  0 0 0  45\n", film), "");
	EXPECT_EQ(film.spec.explain.labels.size(), 1u);
	EXPECT_EQ(film.spec.explain.orbits.size(), 2u);
	EXPECT_TRUE(film.spec.explain.orbits[0].dashed);
	const std::string a = film.sceneAt(3.0), b = film.sceneAt(0.2);
	EXPECT_NE(a.find("# an orbit line (film_explain.h): luna"), std::string::npos);
	EXPECT_NE(a.find("# a label (film_explain.h): Home"), std::string::npos);
	EXPECT_EQ(b.find("# a label (film_explain.h): Home"), std::string::npos);   // (not yet)
	EXPECT_NE(problemWith("label \"X\" on nobody"), "");
	EXPECT_NE(problemWith("label \"X\""), "");
	EXPECT_NE(problemWith("label \"X\" at 0 0 0 from 5 to 2"), "");
	EXPECT_NE(problemWith("ship p at 0 0 0\norbitline p"), "");   // (not on an orbit)
	EXPECT_NE(problemWith("compare Earth Vulcan at 0 0 0"), "");
	EXPECT_NE(problemWith("scalebar -5 at 0 0 0"), "");
}
