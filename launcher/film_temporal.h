#pragma once
// film_temporal.h -- temporal denoising of a film's finished frames (--temporal-denoise [R]), the last thing done before the video is assembled.
//
// A film's frames are rendered one by one, in any order, on several workers, so the noise of each is its own: where nothing moves, the picture "boils" from frame to frame. Each
// frame is made again from itself and the frames up to R either side in its shot (never across a cut): a pixel takes a neighbour's pixel in as far as the neighbourhood round it
// (a 5 x 5 mean) is the same in both frames - the same picture under different noise - and not where something moved or the light changed. Still parts get the noise of R*2+1
// frames averaged (R = 2: less than half the noise, so a quarter of the samples look as smooth), moving parts keep their own sharpness.
//
// The frames as rendered are not touched: the denoised ones are written to the frames folder's `temporal` folder, which the video is assembled from; each is remembered with the
// frames it was made from (temporal/sources.txt), so a resumed video makes again only those whose neighbours changed.

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_render.h"
#include "film_captions.h"   // (its picture reading and writing)
#include "marks_frame.h"     // (the print on a picture's file)
#include "../src/shared/lens_look.h"
#include "../src/shared/print_look.h"
#include "../src/shared/cartoon/cartoons.h"

namespace film_temporal {

constexpr const char *kFolder = "temporal";
constexpr const char *kSources = "sources.txt";

// A frame: its pixels (RGB, 0..255) and the 5 x 5 mean round each.
struct Frame {
	int w = 0, h = 0;
	std::vector<float> rgb, mean;
};
inline std::unique_ptr<Frame> load(const std::filesystem::path &p) {
	int w = 0, h = 0, n = 0;
	unsigned char *px = stbi_load(p.string().c_str(), &w, &h, &n, 3);
	if (!px) return nullptr;
	auto f = std::make_unique<Frame>();
	f->w = w;
	f->h = h;
	f->rgb.assign(px, px + static_cast<std::size_t>(w) * h * 3);
	stbi_image_free(px);
	// the means by a summed-area table, per channel
	const int r = 2;
	std::vector<double> sat(static_cast<std::size_t>(w + 1) * (h + 1));
	f->mean.assign(f->rgb.size(), 0.0f);
	for (int c = 0; c < 3; ++c) {
		std::fill(sat.begin(), sat.end(), 0.0);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				sat[static_cast<std::size_t>(y + 1) * (w + 1) + x + 1] = f->rgb[(static_cast<std::size_t>(y) * w + x) * 3 + c] + sat[static_cast<std::size_t>(y) * (w + 1) + x + 1] +
																		  sat[static_cast<std::size_t>(y + 1) * (w + 1) + x] - sat[static_cast<std::size_t>(y) * (w + 1) + x];
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				const int x0 = std::max(0, x - r), x1 = std::min(w, x + r + 1), y0 = std::max(0, y - r), y1 = std::min(h, y + r + 1);
				const double s = sat[static_cast<std::size_t>(y1) * (w + 1) + x1] - sat[static_cast<std::size_t>(y0) * (w + 1) + x1] - sat[static_cast<std::size_t>(y1) * (w + 1) + x0] +
								 sat[static_cast<std::size_t>(y0) * (w + 1) + x0];
				f->mean[(static_cast<std::size_t>(y) * w + x) * 3 + c] = static_cast<float>(s / ((x1 - x0) * (y1 - y0)));
			}
	}
	return f;
}

// How much a neighbour's pixel is taken in: all of it where the two neighbourhoods' means are within a little of each other (noise), none where they differ by `tolerance`.
inline double weightOf(const Frame &self, const Frame &other, std::size_t i, double tolerance) {
	double d = 0.0;
	for (int c = 0; c < 3; ++c) d = std::max(d, static_cast<double>(std::abs(self.mean[i + c] - other.mean[i + c])));
	const double k = std::max(0.0, 1.0 - d / tolerance);
	return k * k;
}
// Frame `self` made again from it and its neighbours (each the same size).
inline std::vector<unsigned char> denoise(const Frame &self, const std::vector<const Frame *> &neighbours, double tolerance = 5.0) {
	std::vector<unsigned char> out(self.rgb.size());
	for (std::size_t p = 0; p < self.rgb.size(); p += 3) {
		double sum[3] = {self.rgb[p], self.rgb[p + 1], self.rgb[p + 2]}, weight = 1.0;
		for (const Frame *n : neighbours) {
			const double w = weightOf(self, *n, p, tolerance);
			if (w <= 0.0) continue;
			for (int c = 0; c < 3; ++c) sum[c] += w * n->rgb[p + c];
			weight += w;
		}
		for (int c = 0; c < 3; ++c) out[p + c] = static_cast<unsigned char>(std::lround(std::min(255.0, sum[c] / weight)));
	}
	return out;
}

namespace detail {
inline std::string stampOf(const std::filesystem::path &p) {
	std::error_code ec;
	const auto size = std::filesystem::file_size(p, ec);
	if (ec) return "-";
	return std::to_string(static_cast<unsigned long long>(size)) + ":" + std::to_string(static_cast<long long>(std::filesystem::last_write_time(p, ec).time_since_epoch().count()));
}
}  // namespace detail

// The look's print (print_look.h: halftone, watercolor, vintage) of frame `i` of `count`: its header's `# @rt-look` line (a film's cameras file writes one, with the frame's
// seed) and then the run's own --look words. The frames were rendered without it (--defer-print) and get it here, after the denoising, which would average a vintage film's
// grain and dust away.
inline print_look::Params printOf(const cartoon::Film &film, int i, int count, const std::string &lookWords) {
	lens_look::Look look;
	if (film.headerAt) {
		const std::string head = film.headerAt(film.duration * i / std::max(1, count));
		const std::size_t at = head.find("# @rt-look");
		if (at != std::string::npos) {
			const std::size_t from = at + std::string("# @rt-look").size(), end = head.find('\n', from);
			lens_look::parse(head.substr(from, end == std::string::npos ? std::string::npos : end - from), look);
		}
	}
	if (!lookWords.empty()) lens_look::parse(lookWords, look);
	return look.print;
}

// Denoises the `count` finished frames of `dir` into dir/temporal, with `radius` frames either side, and puts each frame's print on after (printOf); returns how many it made
// (the others were made already from the same frames). The folder then holds every frame the video is assembled from.
inline int apply(const cartoon::Film &film, const std::filesystem::path &dir, int count, int radius, const std::string &lookWords = std::string()) {
	namespace fs = std::filesystem;
	const fs::path out = dir / kFolder;
	fs::create_directories(out);
	std::map<int, std::string> made;   // frame -> what it was made from
	{
		std::ifstream in(out / kSources);
		std::string line;
		while (std::getline(in, line)) {
			const std::size_t space = line.find(' ');
			if (space != std::string::npos) made[std::stoi(line.substr(0, space))] = line.substr(space + 1);
		}
	}
	// the shot of each frame: never blended across a cut
	const std::vector<double> starts = film.shotStarts ? film.shotStarts() : std::vector<double>{0.0};
	const auto shotOf = [&](int f) {
		const double t = film.duration * f / std::max(1, count);
		int s = 0;
		for (std::size_t k = 0; k < starts.size(); ++k)
			if (t >= starts[k]) s = static_cast<int>(k);
		return s;
	};
	std::map<int, std::unique_ptr<Frame>> loaded;   // a window of frames round the one being made
	std::ofstream log(out / kSources, std::ios::app);
	int n = 0;
	for (int i = 0; i < count; ++i) {
		std::string sources = std::to_string(radius);
		std::vector<int> used;
		for (int j = i - radius; j <= i + radius; ++j)
			if (j >= 0 && j < count && shotOf(j) == shotOf(i)) {
				used.push_back(j);
				sources += " " + std::to_string(j) + "=" + detail::stampOf(dir / cartoon_render::encName(j));
			}
		const fs::path target = out / cartoon_render::encName(i);
		const print_look::Params print = printOf(film, i, count, lookWords);
		if (print.active()) {   // (a changed print makes the frame again)
			char words[160];
			std::snprintf(words, sizeof words, " print %g %g %g %g %g %d", print.halftone, print.halftoneMix, print.watercolor, print.vintage, print.vintageTone, print.seed);
			sources += words;
		}
		if (made.count(i) && made[i] == sources && fs::exists(target)) continue;
		for (auto it = loaded.begin(); it != loaded.end();) it = it->first < i - radius ? loaded.erase(it) : std::next(it);
		for (int j : used)
			if (!loaded.count(j)) loaded[j] = load(dir / cartoon_render::encName(j));
		// (a frame that cannot be read or written denoised goes in as it is - with its print, which it lacks: a hole stopped ffmpeg at it, and the video and its sound came out short)
		const auto asItIs = [&] {
			std::error_code copied;
			fs::copy_file(dir / cartoon_render::encName(i), target, fs::copy_options::overwrite_existing, copied);
			std::string why;
			if (!copied && print.active()) marks_frame::print(target.string(), print, why);
		};
		const Frame *self = loaded[i].get();
		if (!self) {
			asItIs();
			continue;
		}
		std::vector<const Frame *> neighbours;
		for (int j : used)
			if (j != i && loaded[j] && loaded[j]->w == self->w && loaded[j]->h == self->h) neighbours.push_back(loaded[j].get());
		std::vector<unsigned char> picture = denoise(*self, neighbours);
		if (print.active()) {   // the print, on the denoised picture
			std::vector<float> shown(picture.size());
			for (std::size_t k = 0; k < picture.size(); ++k) shown[k] = picture[k] / 255.0f;
			print_look::apply(shown.data(), self->w, self->h, print);
			for (std::size_t k = 0; k < picture.size(); ++k) picture[k] = static_cast<unsigned char>(std::lround(std::clamp(shown[k], 0.0f, 1.0f) * 255.0f));
		}
		const fs::path tmp = out / (cartoon_render::encName(i) + ".tmp.png");
		if (!write_png(tmp.string().c_str(), self->w, self->h, picture.data())) {
			asItIs();
			continue;
		}
		std::error_code ec;
		fs::rename(tmp, target, ec);
		if (ec) {
			asItIs();
			continue;
		}
		log << i << " " << sources << "\n";
		++n;
	}
	return n;
}

}  // namespace film_temporal
