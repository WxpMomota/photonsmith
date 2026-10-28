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

