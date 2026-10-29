#pragma once
// colour_script.h -- `ray_tracer --film-colours <key> OUT.png [WIDTH] [gpu]`: a film's colour script, as a production designer pins one up - a small picture of the middle of
// every shot, in order, each over a strip of its five main colours (found by k-means, darkest first) and a band of its average colour, so the film's colour arc - warm to
// cold, day to night, the climax's red - can be seen at a glance and its shots' colours compared. Each picture is a quick render of its own (16 samples, WIDTH pixels wide:
// 240), by a run of this program; `gpu` renders them on the graphics card.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../src/external/image_writer.h"
#include "../src/external/stb_image.h"
#include "../src/shared/cartoon/cartoons.h"

namespace colour_script {

// The `k` main colours of a picture (sRGB 0..1, w x h x 3), darkest first: k-means over a sample of its pixels, started from k spread through it.
inline std::vector<std::array<double, 3>> palette(const std::vector<unsigned char> &px, int w, int h, int k = 5) {
	std::vector<std::array<double, 3>> pts;
	const int step = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(w) * h / 4000.0)));
	for (int y = 0; y < h; y += step)
		for (int x = 0; x < w; x += step) {
			const unsigned char *p = &px[(static_cast<std::size_t>(y) * w + x) * 3];
			pts.push_back({p[0] / 255.0, p[1] / 255.0, p[2] / 255.0});
		}
	std::vector<std::array<double, 3>> c;
	if (pts.empty()) return c;
	std::vector<std::array<double, 3>> sorted = pts;
	std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a[0] + a[1] + a[2] < b[0] + b[1] + b[2]; });
	for (int i = 0; i < k; ++i) c.push_back(sorted[static_cast<std::size_t>((i + 0.5) / k * (sorted.size() - 1))]);   // (spread from dark to light)
	std::vector<int> owner(pts.size(), 0);
	for (int it = 0; it < 12; ++it) {
		for (std::size_t i = 0; i < pts.size(); ++i) {
			double best = 1e9;
			for (int j = 0; j < k; ++j) {
				double d = 0.0;
				for (int a = 0; a < 3; ++a) d += (pts[i][a] - c[static_cast<std::size_t>(j)][a]) * (pts[i][a] - c[static_cast<std::size_t>(j)][a]);
				if (d < best) best = d, owner[i] = j;
			}
		}
		std::vector<std::array<double, 4>> sum(static_cast<std::size_t>(k), {0, 0, 0, 0});
		for (std::size_t i = 0; i < pts.size(); ++i) {
			for (int a = 0; a < 3; ++a) sum[static_cast<std::size_t>(owner[i])][a] += pts[i][a];
			sum[static_cast<std::size_t>(owner[i])][3] += 1.0;
		}
		for (int j = 0; j < k; ++j)
			if (sum[static_cast<std::size_t>(j)][3] > 0.0)
				for (int a = 0; a < 3; ++a) c[static_cast<std::size_t>(j)][a] = sum[static_cast<std::size_t>(j)][a] / sum[static_cast<std::size_t>(j)][3];
	}
	std::sort(c.begin(), c.end(), [](const auto &a, const auto &b) { return 0.2126 * a[0] + 0.7152 * a[1] + 0.0722 * a[2] < 0.2126 * b[0] + 0.7152 * b[1] + 0.0722 * b[2]; });
	return c;
}

inline int run(int argc, char **argv) {
	const char *usage = "usage: ray_tracer --film-colours <film key> OUT.png [WIDTH] [gpu]";
	if (argc < 4) {
		std::cerr << usage << std::endl;
		return EXIT_FAILURE;
	}
	const cartoon::Film *film = cartoon::find(argv[2]);
	if (!film || !film->sceneAt) {
		std::cerr << usage << " (a film's key)" << std::endl;
		return EXIT_FAILURE;
	}
	const std::string out = argv[3];
	int tw = 240;
	bool gpu = false;
	for (int i = 4; i < argc; ++i) {
		if (std::string(argv[i]) == "gpu") gpu = true;
		else tw = std::clamp(std::atoi(argv[i]), 64, 960);
	}
	const int th = tw * 9 / 16, bar = std::max(8, th / 4), band = std::max(4, th / 10), gap = 4;
	std::vector<double> starts = film->shotStarts ? film->shotStarts() : std::vector<double>{0.0};
	if (starts.empty()) starts.push_back(0.0);
	const std::filesystem::path dir = std::filesystem::temp_directory_path() /
		("photonsmith_colours_" + std::to_string(std::random_device{}()) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(dir);
	const std::string exe = argv[0];
	const int perRow = 8, n = static_cast<int>(starts.size()), cols = std::min(n, perRow), rows = (n + perRow - 1) / perRow;
	const int cellH = th + gap + bar + gap + band, W = cols * tw + (cols + 1) * gap, H = rows * cellH + (rows + 1) * gap * 3;
	std::vector<unsigned char> sheet(static_cast<std::size_t>(W) * H * 3, 24);
	for (int s = 0; s < n; ++s) {
		const double t0 = starts[static_cast<std::size_t>(s)], t1 = s + 1 < n ? starts[static_cast<std::size_t>(s) + 1] : film->duration;
		const double t = 0.5 * (t0 + t1);
		const std::filesystem::path pic = dir / ("shot" + std::to_string(s) + ".png");
		std::string command = "\"" + exe + "\" " + (gpu ? "--gpu" : "--cpu") + " --height " + std::to_string(th) + " --output \"" + pic.string() + "\" " + std::to_string(tw) +
							  " 16 4 \"cartoon:" + std::string(film->key) + "@" + std::to_string(t) + "\" > \"" + (dir / "log.txt").string() + "\" 2>&1";
#ifdef _WIN32
		command = "\"" + command + "\"";
#endif
		std::cout << "shot " << (s + 1) << " of " << n << " (" << t << " s)..." << std::endl;
		int w = 0, h = 0, comp = 0;
		unsigned char *px = std::system(command.c_str()) == 0 ? stbi_load(pic.string().c_str(), &w, &h, &comp, 3) : nullptr;
		if (!px) {
			std::cerr << "  shot " << (s + 1) << " could not be rendered (see " << (dir / "log.txt").string() << ")" << std::endl;
			continue;
		}
		const std::vector<unsigned char> img(px, px + static_cast<std::size_t>(w) * h * 3);
		stbi_image_free(px);
		const int x0 = gap + (s % perRow) * (tw + gap), y0 = gap * 3 + (s / perRow) * (cellH + gap * 3);
		const auto put = [&](int x, int y, const double c[3]) {
			if (x < 0 || y < 0 || x >= W || y >= H) return;
			for (int a = 0; a < 3; ++a) sheet[(static_cast<std::size_t>(y) * W + x) * 3 + a] = static_cast<unsigned char>(std::lround(std::clamp(c[a], 0.0, 1.0) * 255.0));
		};
		double mean[3] = {0, 0, 0};
		for (int y = 0; y < th; ++y)
			for (int x = 0; x < tw; ++x) {
				const int sx = std::min(w - 1, x * w / tw), sy = std::min(h - 1, y * h / th);
				const unsigned char *p = &img[(static_cast<std::size_t>(sy) * w + sx) * 3];
				const double c[3] = {p[0] / 255.0, p[1] / 255.0, p[2] / 255.0};
				put(x0 + x, y0 + y, c);
				for (int a = 0; a < 3; ++a) mean[a] += c[a] / (static_cast<double>(tw) * th);
			}
		const std::vector<std::array<double, 3>> pal = palette(img, w, h);
		for (std::size_t j = 0; j < pal.size(); ++j)   // the palette strip, then the average band
			for (int y = 0; y < bar; ++y)
				for (int x = static_cast<int>(j * tw / pal.size()); x < static_cast<int>((j + 1) * tw / pal.size()); ++x) put(x0 + x, y0 + th + gap + y, pal[j].data());
		for (int y = 0; y < band; ++y)
			for (int x = 0; x < tw; ++x) put(x0 + x, y0 + th + gap + bar + gap + y, mean);
	}
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	if (!write_png(out.c_str(), W, H, sheet.data())) {
		std::cerr << "could not write " << out << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << out << "  (" << n << " shot(s))" << std::endl;
	return EXIT_SUCCESS;
}

}  // namespace colour_script
