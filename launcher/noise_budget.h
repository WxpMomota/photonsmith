#pragma once
// noise_budget.h -- `--noise-target N`: a film's samples given to its shots by how noisy each is, so that every shot comes out about equally clean for the samples spent. Before
// the frames are rendered, each shot's middle frame is rendered twice at a quarter of the size with a quarter of the samples (two seeds: the same picture under different
// noise); half the difference of the two, against the picture's brightness, is that shot's noise at those samples. Noise falls as the square root of the samples, so the shot
// needs (noise / N)^2 times the test's samples: a foggy night shot gets more, a plain daylight one fewer (from a quarter to four times the samples asked for). The shares
// are written beside the frames (shot_samples.txt) and every frame's run is handed the file (--shot-samples), which multiplies the samples of the frames of each shot.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_render.h"
#include "shot_samples.h"
#include "../src/shared/output_crop.h"

namespace noise_budget {

// The relative noise of two renders of one picture under different seeds (sRGB 0..maxValue, as PPM samples): half their difference's root mean square over the mean.
inline double noiseOf(const std::vector<int> &a, const std::vector<int> &b, int maxValue) {
	if (a.size() != b.size() || a.empty()) return -1.0;
	double diff = 0.0, mean = 0.0;
	for (std::size_t i = 0; i < a.size(); ++i) {
		const double x = a[i] / static_cast<double>(maxValue), y = b[i] / static_cast<double>(maxValue);
		diff += (x - y) * (x - y), mean += 0.5 * (x + y);
	}
	mean /= static_cast<double>(a.size());
	return std::sqrt(diff / static_cast<double>(a.size()) / 2.0) / std::max(0.02, mean);
}

// The table for `film` at `target` noise: each shot's test renders (in `dir`), at `spp` samples asked for. Says what it found.
inline shot_samples::Table measure(const cartoon::Film &film, const LaunchArgs &args, const std::string &exe, int width, int height, int spp, int depth, bool gpu, const std::filesystem::path &dir,
					 double target) {
	shot_samples::Table out;
	std::vector<double> starts = film.shotStarts ? film.shotStarts() : std::vector<double>{0.0};
	if (starts.empty()) starts.push_back(0.0);
	const int w = std::max(32, width / 4), h = std::max(18, height / 4), testSpp = std::max(4, spp / 4);
	std::filesystem::create_directories(dir);
	std::cout << "\nMeasuring each shot's noise (--noise-target " << target << "): two renders a shot at " << w << " x " << h << ", " << testSpp << " samples..." << std::endl;
	for (std::size_t s = 0; s < starts.size(); ++s) {
		const double t0 = starts[s], t1 = s + 1 < starts.size() ? starts[s + 1] : film.duration, t = 0.5 * (t0 + t1);
		std::vector<int> pics[2];
		int pw = 0, ph = 0, maxValue = 255;
		bool ok = true;
		for (int k = 0; k < 2 && ok; ++k) {
			LaunchArgs a = args;
			a.seed = 7919 * (k + 1) + static_cast<long long>(s);
			a.consumed_tokens.push_back("--seed");
			a.consumed_tokens.push_back(std::to_string(a.seed));
			const std::filesystem::path pic = dir / ("noise_" + std::to_string(s) + "_" + std::to_string(k) + ".ppm");
			std::filesystem::remove(pic);
			ok = cartoon_render::renderFrame(film, t, dir / ("noise_" + std::to_string(s) + ".pbrt"), exe, a, w, h, testSpp, depth, pic.string(), 0, 0.0, gpu) == 0 &&
				 output_crop::readPpm(pic.string(), pw, ph, maxValue, pics[k]);
		}
		const double noise = ok ? noiseOf(pics[0], pics[1], maxValue) : -1.0;
		double factor = 1.0;
		if (noise > 0.0) factor = std::clamp(testSpp * (noise / target) * (noise / target) / std::max(1, spp), 0.25, 4.0);
		out.starts.push_back(t0), out.factors.push_back(factor);
		std::printf("  shot %zu at %.2f s: noise %.4f -> %.2fx the samples (%d)\n", s + 1, t0, noise, factor, static_cast<int>(std::lround(spp * factor)));
	}
	return out;
}

}  // namespace noise_budget
