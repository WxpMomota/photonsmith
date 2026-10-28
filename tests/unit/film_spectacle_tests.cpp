// film_spectacle_tests.cpp - nature round 9 of film files: spectacle - flying through clouds and fog banks, eruptions, ocean giants, bioluminescence, ice, the wind,
// fur, the sky's optics, caustics and the ground close up. (film_nature_tests.cpp holds rounds 6 and 8.)

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
)";

// A film file in a temporary folder of its own with the open set; "" or the problem.
std::string loadFilm(const std::string &name, const std::string &filmText, ff::DataFilm &film) {
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_spectacle_test_" + name);
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

}  // namespace

// ---- 1. flying through clouds; fog banks ---------------------------------------------------------------------------------------------------------------------------------

namespace clouds = cartoon::clouds;

// A deck of cloud is one layer over the whole area: flat-bottomed, closed over about its cover's share of the sky with gaps between, fading out at the area's edge, in one
// grid whose sphere holds the camera; it drifts. A fog bank lies on the land - its box from below the lowest ground under it - is densest low down, drifts, and sinks away
// as it burns off.
TEST(FilmSpectacleClouds, ADeckCoversTheSkyWithGapsAndFogBanksLieOnTheLandAndBurnOff) {
	const std::vector<float> d = clouds::deckDensities(0.75, 3);
	ASSERT_EQ(d.size(), static_cast<std::size_t>(clouds::kDeckNx) * clouds::kDeckNy * clouds::kDeckNz);
	const auto at = [&](const std::vector<float> &g, int i, int j, int k) { return g[(static_cast<std::size_t>(k) * clouds::kDeckNy + j) * clouds::kDeckNx + i]; };
	int closed = 0, columns = 0;
	double bottom = 0.0, body = 0.0, edge = 0.0;
	for (int k = 10; k < clouds::kDeckNz - 10; ++k)
		for (int i = 10; i < clouds::kDeckNx - 10; ++i) {
			++columns;
			if (at(d, i, clouds::kDeckNy / 3, k) > 0.2f) ++closed;
			bottom += at(d, i, 0, k);
			body += at(d, i, clouds::kDeckNy / 3, k);
		}
	for (int k = 0; k < clouds::kDeckNz; ++k) edge += at(d, 0, clouds::kDeckNy / 3, k);
	const double share = double(closed) / columns;
	EXPECT_GT(share, 0.45) << "a deck at cover 0.75 closes most of the sky";
	EXPECT_LT(share, 0.95) << "with gaps for the sun";
	EXPECT_LT(bottom, 0.4 * body) << "a flat base";
	EXPECT_LT(edge, 1.0) << "fading out at the area's edge";
	const double lessShare = [&] {
		const std::vector<float> e = clouds::deckDensities(0.3, 3);
		int c = 0;
		for (int k = 10; k < clouds::kDeckNz - 10; ++k)
			for (int i = 10; i < clouds::kDeckNx - 10; ++i) c += at(e, i, clouds::kDeckNy / 3, k) > 0.2f;
		return double(c) / columns;
	}();
	EXPECT_LT(lessShare, share - 0.2) << "less cover, more sky";

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("deck", "scene s.pbrt\nlength 10\nterrain hills size 600 height 90 seed 2 far off\nclouds deck base 140 thick 120 area 3000 wind 90 speed 5\n"
						"fogbank at 0 0 size 300 depth 25 density 0.05 during 2 8 wind 0 speed 2\nshot 0 A\n key 0 160 150  0 140 0\n",
						film),
			  "");
	ASSERT_EQ(film.spec.cloudLayers.size(), 1u);
	EXPECT_EQ(film.spec.cloudLayers[0].kind, "deck");
	EXPECT_DOUBLE_EQ(film.spec.cloudLayers[0].thick, 120.0);
	ASSERT_EQ(film.spec.fogBanks.size(), 1u);
	const std::string a = film.sceneAt(4.0), b = film.sceneAt(6.0);
	ASSERT_NE(a.find("MediumInterface \"deck_1\" \"\""), std::string::npos);
	EXPECT_EQ(count(a, "Include \""), 2u) << "one grid for the deck, one for the bank";
	// the deck's sphere holds the camera (160 m up at 150 m out, the deck's box 140 to 260 m over a 3 km square)
	const std::size_t deckSphere = a.find("Material \"interface\"");
	const std::vector<double> centre = numbersAfter(a, "Translate ", a.rfind("AttributeBegin", deckSphere));
	const std::vector<double> radius = numbersAfter(a, "\"float radius\" [ ", deckSphere, 1);
	ASSERT_EQ(centre.size(), 3u);
	ASSERT_EQ(radius.size(), 1u);
	EXPECT_LT(std::hypot(0.0 - centre[0], 160.0 - centre[1], 150.0 - centre[2]), radius[0]);
	EXPECT_NEAR(centre[0], 20.0, 1e-6) << "drifted 20 m with the wind in 4 s";
	// the fog bank: its box starts below the ground at its centre, and sinks away as it burns off
	const std::shared_ptr<const cartoon::terrain::Field> land = cartoon::terrain::fieldOf(film.spec.terrain);
	const auto bankBox = [](const std::string &s) {
		const std::size_t inc = s.find("fogbank_0");
		const std::size_t begin = s.rfind("AttributeBegin", s.rfind("Include", inc));
		std::vector<double> v = numbersAfter(s, "Translate ", begin);
		const std::vector<double> sc = numbersAfter(s, "Scale ", begin);
		v.insert(v.end(), sc.begin(), sc.end());
		return v;
	};
	const std::vector<double> boxA = bankBox(a), boxLate = bankBox(film.sceneAt(7.8));
	ASSERT_EQ(boxA.size(), 6u);
	EXPECT_LT(boxA[1], land->heightAt(0.0, 8.0)) << "from below the ground under it";
	EXPECT_NEAR(boxA[3], 300.0, 1e-6);
	ASSERT_EQ(boxLate.size(), 6u);
	EXPECT_LT(boxLate[4], boxA[4]) << "sinking as it burns off";
	EXPECT_EQ(film.sceneAt(1.0).find("fogbank_0"), std::string::npos) << "not yet settled in";
	EXPECT_EQ(film.sceneAt(9.0).find("fogbank_0"), std::string::npos) << "burnt off";
	EXPECT_NE(a, b);
	// densest low down
	const std::vector<float> f = clouds::bankDensities(1);
	double low = 0.0, high = 0.0;
	for (int k = 0; k < clouds::kBankNz; ++k)
		for (int i = 0; i < clouds::kBankNx; ++i) {
			low += f[(static_cast<std::size_t>(k) * clouds::kBankNy + 1) * clouds::kBankNx + i];
			high += f[(static_cast<std::size_t>(k) * clouds::kBankNy + clouds::kBankNy - 1) * clouds::kBankNx + i];
		}
	EXPECT_GT(low, 5.0 * high);
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"fogbank size 100", "where the fog lies"},
																						{"fogbank at 0 0 depth 0", "depth (2 to 2000 m)"},
																						{"fogbank at 0 0 during 5 2", "T1 after T0"},
																						{"clouds cumulus thick 100", "a deck thick"}}) {
		const std::string problem = problemOf(line);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

// ---- 2. an eruption --------------------------------------------------------------------------------------------------------------------------------------------------------

namespace fire = cartoon::fire;

namespace {
// The box (corner, then size) of the grid medium `name` in a frame's text.
std::vector<double> gridBox(const std::string &s, const std::string &name) {
	const std::size_t at = s.find("MakeNamedMedium \"" + name + "\"");
	if (at == std::string::npos) return {};
	const std::size_t begin = s.rfind("AttributeBegin", at);
	std::vector<double> v = numbersAfter(s, "Translate ", begin);
	const std::vector<double> sc = numbersAfter(s, "Scale ", begin);
	v.insert(v.end(), sc.begin(), sc.end());
	return v;
}
}  // namespace

// An ash column climbs fast and slows, spreading into its umbrella only once it is up; lightning flickers in it from part way up, ash falls round the camera once the
// umbrella is over, and a pyroclastic flow races down from the rim the way it is sent, its cloud reaching further and growing, kept below the column's grid.
TEST(FilmSpectacleVolcano, AnAshColumnClimbsIntoAnUmbrellaWithLightningAshAndPyroclasticFlows) {
	double top = 0.0, umbrella = 0.0, top2 = 0.0, umbrella2 = 0.0, top3 = 0.0, umbrella3 = 0.0;
	fire::columnShape(2400.0, 2.0, top, umbrella);
	fire::columnShape(2400.0, 0.5 * fire::columnRise(2400.0), top2, umbrella2);
	fire::columnShape(2400.0, 3.0 * fire::columnRise(2400.0), top3, umbrella3);
	EXPECT_LT(top, top2);
	EXPECT_GT(top2 - top, 0.0);
	EXPECT_NEAR(top3, 2400.0, 1e-6) << "up to its height";
	EXPECT_EQ(umbrella, 0.0);
	EXPECT_EQ(umbrella2, 0.0) << "no umbrella while it is still climbing";
	EXPECT_GT(umbrella3, 0.3 * 2400.0);

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("volcano", "scene s.pbrt\nlength 60\nterrain hills size 4000 height 60 seed 2\n"
								  "volcano at 0 -600 height 300 radius 600 erupting 1 60 column 2400 wind 90 lightning 6 ashfall 1.5 flow 10 toward 180 flow 20\n"
								  "shot 0 A\n key 0 200 1500  0 600 -600\n",
						film),
			  "");
	ASSERT_EQ(film.spec.volcanoes.size(), 1u);
	const fire::Volcano &v = film.spec.volcanoes[0];
	const std::shared_ptr<const cartoon::terrain::Field> land = cartoon::terrain::fieldOf(film.spec.terrain);
	EXPECT_GT(v.rimY, land->heightAt(0.0, -600.0)) << "the rim stands above the crater's floor";
	ASSERT_EQ(film.spec.lightnings.size(), 1u);
	EXPECT_EQ(film.spec.lightnings[0].strikes.size(), 6u);
	EXPECT_NEAR(film.spec.lightnings[0].base, v.rimY + 0.35 * 2400.0, 1e-6);
	EXPECT_GE(film.spec.lightnings[0].t0, 1.0 + 0.5 * fire::columnRise(2400.0) - 1e-9);
	for (const cartoon::skyfx::Strike &st : film.spec.lightnings[0].strikes) EXPECT_LT(std::hypot(st.x, st.z + 600.0), 0.12 * 600.0 + 1e-6) << "over the crater";
	ASSERT_EQ(std::count_if(film.spec.weather.begin(), film.spec.weather.end(), [](const cartoon::particles::Weather &w) { return w.kind == "ash"; }), 1);
	// the flows: from the rim, downhill, the first toward 180 degrees (pbrt's -z)
	ASSERT_EQ(v.flows.size(), 2u);
	for (const fire::PyroclasticFlow &f : v.flows) {
		ASSERT_GT(f.path.size(), 20u);
		EXPECT_LT(f.path.back().y, f.path.front().y - 150.0) << "down the volcano";
	}
	EXPECT_LT(v.flows[0].path[20].z, -600.0 - 0.1 * 600.0) << "toward 180: down the far (-z) side";

	const std::string early = film.sceneAt(4.0), mid = film.sceneAt(14.0), late = film.sceneAt(30.0);
	EXPECT_NE(early.find("ashcolumn_0"), std::string::npos);
	EXPECT_EQ(early.find("plume_0"), std::string::npos) << "the column instead of the plume";
	EXPECT_EQ(early.find("pyroflow_0_0"), std::string::npos) << "not yet";
	EXPECT_NE(mid.find("pyroflow_0_0"), std::string::npos);
	EXPECT_EQ(mid.find("pyroflow_0_1"), std::string::npos);
	EXPECT_NE(late.find("pyroflow_0_1"), std::string::npos);
	const std::vector<double> c0 = gridBox(early, "ashcolumn_0"), c1 = gridBox(late, "ashcolumn_0");
	ASSERT_EQ(c0.size(), 6u);
	ASSERT_EQ(c1.size(), 6u);
	EXPECT_NEAR(c0[1], v.rimY, 1e-3) << "from the rim up";
	EXPECT_GT(c1[4], 2.0 * c0[4]) << "climbing";
	EXPECT_GT(c1[3], 2.0 * c0[3]) << "its umbrella spreading";
	const std::vector<double> f0 = gridBox(mid, "pyroflow_0_0"), f1 = gridBox(late, "pyroflow_0_0");
	ASSERT_EQ(f0.size(), 6u);
	ASSERT_EQ(f1.size(), 6u);
	EXPECT_GT(f1[3] * f1[5], 1.5 * f0[3] * f0[5]) << "reaching further";
	EXPECT_LE(f1[1] + f1[4], v.rimY + 1e-3) << "kept below the column's grid";
	EXPECT_EQ(film.sceneAt(4.0).find("pyroflow"), std::string::npos);
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{
			 {"terrain hills\nvolcano at 0 0 column 100", "300 to 20000"},
			 {"terrain hills\nvolcano at 0 0 lightning 4", "give the volcano a `column H`"},
			 {"terrain hills\nvolcano at 0 0 erupting 2 8 flow 20", "while the volcano erupts"},
			 {"terrain hills\nvolcano at 0 0 toward 90", "after `flow T`"},
			 {"terrain hills\nvolcano at 0 0 ashfall 9", "up to 4"}}) {
		const std::string problem = problemOf(line);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
	ff::DataFilm ash;
	EXPECT_EQ(loadFilm("ash", "scene s.pbrt\nlength 4\nweather ash 2\n", ash), "") << "ash is a weather of its own too";
}

// ---- 3. the sea's giants ---------------------------------------------------------------------------------------------------------------------------------------------------

namespace giants = cartoon::giants;

// A humpback's body has its long pectorals and broad flukes; it rolls up through the surface (its back out, then down) and blows as it comes over; in a breach it is flung
// clear of the sea nearly upright and crashes back, a burst of spray at each end and foam after. Dolphins leap clear of the water in arcs, a splash where each goes out
// and back in.
TEST(FilmSpectacleSea, AWhaleRollsBlowsAndBreachesAndDolphinsPorpoise) {
	const auto extent = [](const giants::Body &b, int axis, bool hi) {
		double v = hi ? -1e30 : 1e30;
		for (const cartoon::Mesh *m : {&b.dark, &b.light})
			for (const auto &tr : m->tris)
				for (int k : tr) {
					const cartoon::Vec3 p = m->verts[static_cast<std::size_t>(k)];
					const double c = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
					v = hi ? std::max(v, c) : std::min(v, c);
				}
		return v;
	};
	const giants::Body &whale = giants::whaleBody();
	ASSERT_FALSE(whale.dark.tris.empty());
	ASSERT_FALSE(whale.light.tris.empty()) << "white pectorals and throat";
	EXPECT_GT(extent(whale, 0, true), 0.45) << "its head";
	EXPECT_LT(extent(whale, 0, false), -0.5) << "its flukes";
	EXPECT_GT(extent(whale, 2, true), 0.33) << "pectorals a third of its length";
	EXPECT_LT(extent(whale, 2, false), -0.33);
	const giants::Body &dolphin = giants::dolphinBody();
	EXPECT_GT(extent(dolphin, 1, true), 0.15) << "a dorsal fin";

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("giants", "scene s.pbrt\nlength 40\nwater sea level 0 waves 0.5\n"
								 "whale at 0 -150 toward 90 length 14 speed 2 breach 20\ndolphins 5 at 20 -60 toward 270 speed 6\nshot 0 A\n key 0 8 60  0 2 -150  50\n",
						film),
			  "");
	ASSERT_EQ(film.spec.whales.size(), 1u);
	ASSERT_EQ(film.spec.pods.size(), 1u);
	const giants::Whale &w = film.spec.whales[0];
	const std::vector<double> none, breach = {20.0};
	// rolling: over a dive cycle its back comes out of the water and goes back under; most of the time it is under
	int out = 0, n = 0;
	for (double t = 0.0; t < 16.0; t += 0.25, ++n) {
		const giants::Frame back = giants::whaleFrames(w, t, 0.0, none, 0.0)(0.0);
		out += back.at.y + 0.11 * w.length > 0.0;
	}
	EXPECT_GT(out, 0) << "its back rolls out";
	EXPECT_LT(out, n / 2) << "and it is under most of the time";
	const std::vector<double> blows = giants::blowTimes(w, 0.0, 40.0, breach);
	EXPECT_GE(blows.size(), 1u);
	for (double b : blows) {
		EXPECT_GT(giants::blowhole(w, b, 0.0, breach, 0.0).y, 0.0) << "it blows with its blowhole out of the water";
		EXPECT_TRUE(b < 20.0 - 6.0 || b > 20.0 + giants::breachCrash(14.0) + 8.0) << "not round the breach";
	}
	// the breach: clear of the sea nearly upright at its top, back under after the crash
	const double air = giants::breachCrash(14.0);
	EXPECT_GT(air, 2.0);
	EXPECT_LT(air, 5.0);
	const auto f = giants::whaleFrames(w, 20.0 + 0.45 * air, 0.0, breach, 0.0);
	EXPECT_GT(f(-0.4 * w.length).at.y, -1.0) << "most of it out of the water";
	EXPECT_GT(f(0.0).forward.y, 0.8) << "nearly upright";
	EXPECT_LT(giants::whaleFrames(w, 20.0 + air + 1.5, 0.0, breach, 0.0)(0.0).at.y, 0.0) << "back under";
	const std::string up = film.sceneAt(20.2), crash = film.sceneAt(20.0 + air + 0.3), calm = film.sceneAt(19.0);
	EXPECT_GT(count(up, "Shape \"sphere\""), count(calm, "Shape \"sphere\"") + 100) << "spray as it bursts out";
	EXPECT_GT(count(crash, "Shape \"sphere\""), count(calm, "Shape \"sphere\"") + 300) << "a crash of spray";
	EXPECT_EQ(count(calm, "# bubbles"), 0u) << "bubbles only under water";
	// dolphins: each leaps clear every few seconds
	const giants::Dolphins &d = film.spec.pods[0];
	for (int i = 0; i < d.count; ++i) {
		double top = -1e30;
		for (double t = 0.0; t < 6.0; t += 0.05) top = std::max(top, giants::dolphinFrames(d, i, t, 0.0, giants::kDolphinLength)(0.0).at.y);
		EXPECT_GT(top, 0.6) << "dolphin " << i << " leaps clear";
		std::vector<double> o, in;
		giants::leapTimes(d, i, 10.0, o, in);
		EXPECT_GE(o.size(), 2u);
		EXPECT_GE(in.size(), 2u);
	}
	EXPECT_NE(film.sceneAt(5.0), film.sceneAt(5.5));
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"water sea level 0\nwhale toward 90", "where it is"},
																						{"water sea level 0\nwhale at 0 0 length 90", "length 4 to 30"},
																						{"water sea level 0\nwhale at 0 0 during 2 8 breach 12", "while it is there"},
																						{"water sea level 0\ndolphins 0 at 0 0", "1 to 40 dolphins"},
																						{"whale at 0 0", "needs a `water sea` line"}}) {
		const std::string problem = problemOf(line);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

// ---- 4. living light ------------------------------------------------------------------------------------------------------------------------------------------------------

namespace glow = cartoon::glow;

// Fireflies flash briefly every few seconds, each in its own time - or, `together`, drifting into step until the swarm flashes as one; fungi glow in clusters; with
// `bioluminescence` the sea's foam and the spray of a breach give out light.
TEST(FilmSpectacleGlow, FirefliesFlashAndFallIntoStepFungiGlowAndTheStirredSeaShines) {
	glow::Fireflies f;
	f.count = 40;
	int lit = 0, samples = 0;
	for (int k = 0; k < f.count; ++k) {
		int flashes = 0;
		bool was = false;
		for (double t = 0.0; t < 10.0; t += 0.02, ++samples) {
			const bool on = glow::flash(f, k, t) > 0.2;
			lit += on;
			flashes += on && !was;
			was = on;
		}
		EXPECT_GE(flashes, 2) << "firefly " << k << " flashes every few seconds";
	}
	EXPECT_LT(double(lit) / samples, 0.15) << "brief flashes";
	const auto inStep = [&](const glow::Fireflies &s, double around) {   // (the share of the swarm lit when the most are)
		double best = 0.0;
		for (double t = around; t < around + 3.0; t += 0.02) {
			int on = 0;
			for (int k = 0; k < s.count; ++k) on += glow::flash(s, k, t) > 0.3;
			best = std::max(best, double(on) / s.count);
		}
		return best;
	};
	glow::Fireflies sync = f;
	sync.together = true;
	EXPECT_LT(inStep(sync, 0.0), 0.5) << "each in its own time at first";
	EXPECT_GT(inStep(sync, 40.0), 0.9) << "all as one after half a minute";
	EXPECT_LT(inStep(f, 40.0), 0.5) << "without `together`, never in step";

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("glow", "scene s.pbrt\nlength 30\nwater sea level 0 waves 1\nbioluminescence strength 2 colour teal\n"
							   "fireflies 50 in -10 -10 10 10 height 3 together\nfungi 12 in -5 -5 5 5 colour green brightness 2\n"
							   "whale at 0 -60 toward 90 breach 10\nshot 0 A\n key 0 3 30  0 1 -60  50\n",
						film),
			  "");
	ASSERT_NE(film.spec.seas[0].glowing(), nullptr);
	EXPECT_NEAR(film.spec.seas[0].glow.y, cartoon::glow::colourOf("teal").y * 1.2, 1e-9);
	const std::string a = film.sceneAt(10.5);
	const std::size_t spray = a.find("# spray (film_giants.h)");
	ASSERT_NE(spray, std::string::npos);
	EXPECT_LT(a.find("AreaLightSource", spray), a.find("Shape \"sphere\"", spray)) << "the spray glows";
	const std::size_t flies = a.find("# fireflies"), fungi = a.find("# glowing fungi");
	ASSERT_NE(flies, std::string::npos);
	ASSERT_NE(fungi, std::string::npos);
	ASSERT_LT(flies, fungi);
	const std::size_t glowing = count(a.substr(flies, fungi - flies), "AreaLightSource");
	EXPECT_GT(glowing, 0u);
	EXPECT_LE(glowing, 50u) << "one light a firefly";
	const std::size_t caps = count(a.substr(fungi), "\"rgb L\"");
	EXPECT_GE(caps, 12u * 3u);
	EXPECT_LE(caps, 12u * 11u);
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"bioluminescence", "needs a `water` line"},
																						{"water sea level 0\nbioluminescence colour pink", "blue|green|teal"},
																						{"fireflies 0 in 0 0 1 1", "1 to 2000 fireflies"},
																						{"fireflies 10 in 0 0 1 1 height 50", "\"height\""},
																						{"fungi 10 in 0 0 1 1 colour red", "\"colour\""}}) {
		const std::string problem = problemOf(line);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

// ---- 5. ice ----------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace ice = cartoon::ice;

// A glacier is thick in its middle and thins to nothing at its edges, crevassed more towards its front; its front calves - a slab that stands, topples out and lies in
// the water, with a burst of spray; an iceberg floats mostly under the water; sea ice covers as much of the sea as it is told; a frozen waterfall hangs in icicles, silent.
TEST(FilmSpectacleIce, AGlacierThinsToItsEdgesCrevassesAndCalvesBergsFloatLowAndFallsFreeze) {
	ice::Glacier g;
	g.length = 600.0, g.width = 160.0, g.thick = 40.0;
	const auto flat = [](double, double) { return 0.0; };
	EXPECT_GT(ice::glacierTop(g, 300.0, 0.0, flat), 25.0) << "thick in its middle";
	EXPECT_LT(ice::glacierTop(g, 300.0, 79.5, flat), 0.25 * ice::glacierTop(g, 300.0, 0.0, flat)) << "thin at its edges";
	double head = 0.0, front = 0.0;
	for (double b = -70.0; b <= 70.0; b += 1.0)
		for (double a = 0.0; a < 100.0; a += 1.0) head += ice::crevasse(g, a, b), front += ice::crevasse(g, 500.0 + a, b);
	EXPECT_GT(front, 3.0 * head + 1.0) << "crevassed towards its front";
	const std::string text = ice::glacierText(g, 0.0, flat, -1e30);
	EXPECT_NE(text.find("\"subsurface\""), std::string::npos) << "ice";
	EXPECT_NE(text.find("0.86 0.88 0.9"), std::string::npos) << "snow on top";
	// the calving slab: standing, then out from the front and down
	const auto reach = [](const std::string &s, int axis) {   // (the furthest point along an axis: 0 = z, out from the front of a glacier flowing toward 0; 1 = y)
		std::istringstream in(s.substr(s.find("\"point3 P\" [") + 12));
		double best = -1e30, x = 0.0, y = 0.0, z = 0.0;
		while (in >> x >> y >> z) best = std::max(best, axis == 0 ? z : y);
		return best;
	};
	const std::string standing = ice::slabText(g, 0.0, -1.0, flat, 0.0, 0.0), toppling = ice::slabText(g, 0.0, 2.0, flat, 0.0, 2.0), lying = ice::slabText(g, 0.0, 8.0, flat, 0.0, 8.0);
	EXPECT_GT(reach(toppling, 0), reach(standing, 0) + 10.0) << "toppling out over the water";
	EXPECT_LT(reach(lying, 1), 0.5 * reach(standing, 1)) << "lying in the water";
	// an iceberg: most of it under the water
	const cartoon::Mesh &berg = ice::bergShape(3);
	double lo = 0.0, hi = 0.0;
	for (const cartoon::Vec3 &v : berg.verts) lo = std::min(lo, v.y), hi = std::max(hi, v.y);
	EXPECT_GT(-lo, 2.0 * hi) << "a deep keel under a low berg";

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("ice", "scene s.pbrt\nlength 30\nterrain hills size 2000 height 30 seed 2\nwater sea level -5 waves 0.3\nglacier from 0 300 toward 180 length 400 width 120 calve 10\n"
							  "iceberg at 50 -200 size 30\nseaice cover 0.5 floe 8\nwaterfall at -300 40 300 drop 20 frozen\nshot 0 A\n key 0 20 -150  0 10 0  50\n",
						film),
			  "");
	const std::string before = film.sceneAt(9.0), after = film.sceneAt(10.0 + ice::kCalveFall + 0.4);
	EXPECT_NE(before.find("# a calving slab"), std::string::npos) << "the slab stands in the front";
	EXPECT_EQ(before.find("# spray"), std::string::npos);
	EXPECT_NE(after.find("# spray"), std::string::npos) << "a burst of spray as it hits the water";
	EXPECT_NE(after.find("# an iceberg"), std::string::npos);
	EXPECT_NE(after.find("# sea ice"), std::string::npos);
	EXPECT_NE(after.find("# a frozen waterfall"), std::string::npos);
	EXPECT_EQ(after.find("# a waterfall (film_water.h)"), std::string::npos) << "no running water";
	EXPECT_TRUE(film.spec.emitters.empty()) << "a frozen fall throws up no spray";
	// more cover, more floes
	ice::SeaIce few, many;
	few.cover = 0.2, many.cover = 0.8;
	cartoon::water::Sea sea;
	const std::string a = ice::seaIceText(few, sea, 0.0, {0.0, 5.0, 0.0}), b = ice::seaIceText(many, sea, 0.0, {0.0, 5.0, 0.0});
	EXPECT_GT(b.size(), 2.5 * a.size());
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"glacier from 0 0 toward 0", "needs a `terrain` line"},
																						{"iceberg at 0 0", "needs a `water` line"},
																						{"water sea level 0\nseaice cover 2", "cover 0 to 1"},
																						{"terrain hills\nglacier from 0 0 width 1", "width 10 to 5000"},
																						{"waterfall at 0 0 0 frozen drop 0", "\"drop\""}}) {
		const std::string problem = problemOf(line);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

// ---- 6. the wind -----------------------------------------------------------------------------------------------------------------------------------------------------------

namespace wnd = cartoon::wind;

// One `wind` line is taken by everything that moves with the air and did not say otherwise; a steady wind leans the plants even between gusts. A tornado forms,
// crosses from one point to the other and ropes out; its foot churns dust on land and spray over water.
TEST(FilmSpectacleWind, OneWindMovesEverythingAndATornadoCrossesTheLand) {
	cartoon::dressing::Gusts steady;
	steady.steady = 0.8, steady.dx = 1.0, steady.dz = 0.0;
	EXPECT_GT(cartoon::dressing::gustAt(steady, 3.0, 4.0, 1.0).x, 0.5) << "a steady lean with no gusts";

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("wind", "scene s.pbrt\nlength 40\nterrain hills size 3000 height 40 seed 2\nwater sea level -50\nwind 90 10\nclouds cumulus\nclouds wisps wind 0 speed 2\n"
							   "fogbank at 0 0\nweather rain\nvolcano at 0 -600 column 1000\nfire at 5 0 5\ntornado from -300 0 to 300 0 during 5 35 width 30 height 400\n"
							   "shot 0 A\n key 0 30 500  0 100 0  50\n",
						film),
			  "");
	const ff::Spec &s = film.spec;
	ASSERT_EQ(s.cloudLayers.size(), 2u);
	EXPECT_DOUBLE_EQ(s.cloudLayers[0].wind, 90.0);
	EXPECT_DOUBLE_EQ(s.cloudLayers[0].speed, 15.0) << "faster aloft";
	EXPECT_DOUBLE_EQ(s.cloudLayers[1].wind, 0.0) << "its own wind";
	EXPECT_DOUBLE_EQ(s.cloudLayers[1].speed, 2.0);
	EXPECT_DOUBLE_EQ(s.fogBanks[0].wind, 90.0);
	EXPECT_DOUBLE_EQ(s.fogBanks[0].speed, 3.0);
	EXPECT_DOUBLE_EQ(s.seas[0].wind, 90.0);
	EXPECT_DOUBLE_EQ(s.volcanoes[0].wind, 90.0);
	ASSERT_FALSE(s.weather.empty());
	EXPECT_NEAR(s.weather[0].windX, 10.0, 1e-9) << "rain slants with it";
	EXPECT_NEAR(s.weather[0].windZ, 0.0, 1e-9);
	EXPECT_GT(s.gusts.strength, 1.0);
	EXPECT_NEAR(s.gusts.dx, -1.0, 1e-9) << "gusts along it (story space)";
	EXPECT_GT(s.gusts.steady, 0.5);
	// smoke from the fire bends downwind: its column's box is shifted to +x of the fire
	const std::string a = film.sceneAt(20.0);
	EXPECT_NE(a.find("smoke_0"), std::string::npos);
	// the tornado: forming, crossing, gone
	const wnd::Tornado &k = s.tornadoes[0];
	EXPECT_EQ(wnd::strength(k, 4.0), 0.0);
	EXPECT_GT(wnd::strength(k, 20.0), 0.99);
	EXPECT_LT(wnd::footAt(k, 8.0).x, wnd::footAt(k, 30.0).x - 300.0) << "crossing from -300 to 300";
	EXPECT_NE(a.find("MakeNamedMedium \"funnel_0\""), std::string::npos);
	EXPECT_EQ(film.sceneAt(3.0).find("funnel_0"), std::string::npos);
	const auto flat = [](double, double) { return 0.0; };
	const std::string land = wnd::tornadoText(k, 0, 20.0, 20.0, flat, -1e30), sea = wnd::tornadoText(k, 0, 20.0, 20.0, flat, 5.0);
	EXPECT_NE(land.find("\"dust_0\""), std::string::npos) << "dust and debris on land";
	EXPECT_NE(sea.find("\"spray_0\""), std::string::npos) << "a waterspout over the sea";
	EXPECT_EQ(sea.find("\"dust_0\""), std::string::npos);
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"wind 90", "`wind DEGREES SPEED`"},
																						{"wind 90 100", "0 to 60 m/s"},
																						{"wind 90 5\nwind 0 5", "one `wind` line"},
																						{"tornado from 0 0 to 1 1 during 0 5", "at least 12 s"},
																						{"tornado from 0 0 to 1 1 width 0", "width 2 to 1000"}}) {
		const std::string problem = problemOf(line);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}
