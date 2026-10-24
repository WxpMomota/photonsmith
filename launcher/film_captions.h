#pragma once
// film_captions.h -- the words a film lays over its video (cartoon::Caption: a line of the story, a title), laid onto the finished frames just before they become the video.
//
// The frames are the video's own pictures (enc_0000.png ...), which a resumed video keeps, so a frame must not get its words twice: the frames this has written are listed in
// captions.done beside them with the size and time of the file it left, and a frame whose file is still that one is skipped. A frame rendered again is a different file, and gets
// its words; a frame filled in with a copy of its neighbour (which keeps the neighbour's time) has the words its neighbour has. A caption's picture is read once, scaled to the frame (an area average, so the small text stays crisp) and laid on with its transparency;
// a missing picture is reported once and skipped.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../src/external/image_writer.h"
#include "../src/external/stb_image.h"
#include "../src/shared/cartoon/cartoons.h"

namespace film_captions {

struct Picture {
	int w = 0, h = 0;
	std::vector<float> rgba;   // straight (not premultiplied) alpha, 0..1, colours in sRGB
};

inline bool load(const std::string &path, Picture &out) {
	int w = 0, h = 0, n = 0;
	unsigned char *px = stbi_load(path.c_str(), &w, &h, &n, 4);
	if (!px) return false;
	out.w = w;
	out.h = h;
	out.rgba.resize(static_cast<std::size_t>(w) * h * 4);
	for (std::size_t i = 0; i < out.rgba.size(); ++i) out.rgba[i] = px[i] / 255.0f;
	stbi_image_free(px);
	return true;
}

// `src` scaled to w x h by averaging the source pixels under each target pixel (premultiplied, so a letter's edge does not darken into its transparent surroundings).
inline Picture scaled(const Picture &src, int w, int h) {
	Picture out;
	out.w = w;
	out.h = h;
	out.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0.0f);
	const double sx = static_cast<double>(src.w) / w, sy = static_cast<double>(src.h) / h;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const double x0 = x * sx, x1 = (x + 1) * sx, y0 = y * sy, y1 = (y + 1) * sy;
			double acc[4] = {0, 0, 0, 0}, area = 0.0;
			for (int yy = static_cast<int>(y0); yy < (std::min)(src.h, static_cast<int>(std::ceil(y1))); ++yy)
				for (int xx = static_cast<int>(x0); xx < (std::min)(src.w, static_cast<int>(std::ceil(x1))); ++xx) {
					const double wgt = ((std::min)(x1, xx + 1.0) - (std::max)(x0, static_cast<double>(xx))) * ((std::min)(y1, yy + 1.0) - (std::max)(y0, static_cast<double>(yy)));
					if (wgt <= 0.0) continue;
					const float *p = &src.rgba[(static_cast<std::size_t>(yy) * src.w + xx) * 4];
					for (int c = 0; c < 3; ++c) acc[c] += p[c] * p[3] * wgt;
					acc[3] += p[3] * wgt;
					area += wgt;
				}
			float *o = &out.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
			if (area > 0.0 && acc[3] > 1e-9) {
				for (int c = 0; c < 3; ++c) o[c] = static_cast<float>(acc[c] / acc[3]);
				o[3] = static_cast<float>(acc[3] / area);
			}
		}
	return out;
}

// How strongly caption `c` shows at time t (0 outside it, rising and falling over its fade).
inline double strength(const cartoon::Caption &c, double t) {
	if (t < c.t0 || t > c.t1) return 0.0;
	const double f = (std::max)(1e-3, c.fade);
	return (std::min)(1.0, (std::min)((t - c.t0) / f, (c.t1 - t) / f));
}

// Lays `pic` (already the right size) on the w x h RGB frame with its top-left corner at (x0, y0), at `alpha`.
inline void over(std::vector<unsigned char> &frame, int w, int h, const Picture &pic, int x0, int y0, double alpha) {
	for (int y = 0; y < pic.h; ++y) {
		const int fy = y0 + y;
		if (fy < 0 || fy >= h) continue;
		for (int x = 0; x < pic.w; ++x) {
			const int fx = x0 + x;
			if (fx < 0 || fx >= w) continue;
			const float *p = &pic.rgba[(static_cast<std::size_t>(y) * pic.w + x) * 4];
			const double a = p[3] * alpha;
			if (a <= 0.0) continue;
			unsigned char *d = &frame[(static_cast<std::size_t>(fy) * w + fx) * 3];
			for (int c = 0; c < 3; ++c) d[c] = static_cast<unsigned char>(std::lround((std::min)(255.0, (std::max)(0.0, d[c] * (1.0 - a) + p[c] * 255.0 * a))));
		}
	}
}

// The record of the frames that have their words, beside them (a resumed video keeps it: video_render.h).
constexpr const char *kRecord = "captions.done";
// Lays the film's captions on frames 0 .. count-1 in `dir` (enc_NNNN.png). Returns how many frames got words.
inline int apply(const cartoon::Film &film, const std::filesystem::path &dir, int count) {
	if (!film.captions) return 0;
	const std::vector<cartoon::Caption> captions = film.captions();
	if (captions.empty()) return 0;
	namespace fs = std::filesystem;
	const fs::path record = dir / kRecord;
	std::map<int, std::string> done;   // frame -> "size time" of the file this left
	std::set<std::string> captioned;   // those stamps: a frame that failed is a copy of its neighbour with the neighbour's time (cartoon_render.h), and has its words if that had
	{
		std::ifstream in(record);
		int frame = 0;
		std::string stamp;
		while (in >> frame && std::getline(in, stamp)) done[frame] = stamp, captioned.insert(stamp);
	}
	const auto stampOf = [](const fs::path &p) {
		std::error_code ec;
		const auto size = fs::file_size(p, ec);
		const auto when = fs::last_write_time(p, ec);
		return " " + std::to_string(static_cast<unsigned long long>(size)) + " " + std::to_string(static_cast<long long>(when.time_since_epoch().count()));
	};
	std::map<std::string, Picture> originals;
	std::map<std::string, bool> missing;
	std::map<std::string, Picture> sized;   // by "image|w|h"
	std::ofstream log(record, std::ios::app);
	int laid = 0;
	for (int i = 0; i < count; ++i) {
		const double t = film.duration * i / (std::max)(1, count);
		std::vector<std::pair<const cartoon::Caption *, double>> here;
		for (const cartoon::Caption &c : captions)
			if (const double a = strength(c, t); a > 0.0) here.push_back({&c, a});
		if (here.empty()) continue;
		char name[32];
		std::snprintf(name, sizeof name, "enc_%04d.png", i);
		const fs::path file = dir / name;
		std::error_code ec;
		if (!fs::exists(file, ec)) continue;
		const std::string stamp = stampOf(file);
		if (auto it = done.find(i); it != done.end() && it->second == stamp) continue;
		if (captioned.count(stamp)) {   // (a copy of a frame that has its words)
			log << i << stamp << "\n";
			continue;
		}
		int w = 0, h = 0, n = 0;
		unsigned char *px = stbi_load(file.string().c_str(), &w, &h, &n, 3);
		if (!px) continue;
		std::vector<unsigned char> frame(px, px + static_cast<std::size_t>(w) * h * 3);
		stbi_image_free(px);
		for (const auto &[c, a] : here) {
			if (missing[c->image]) continue;
			if (!originals.count(c->image)) {
				Picture p;
				if (c->image.empty() || !load(c->image, p)) {
					std::cerr << "WARNING: the caption picture \"" << c->image << "\" could not be read; it is left out." << std::endl;
					missing[c->image] = true;
					continue;
				}
				originals[c->image] = std::move(p);
			}
			const Picture &src = originals[c->image];
			const int pw = (std::max)(1, static_cast<int>(std::lround(c->width * w)));
			const int ph = (std::max)(1, static_cast<int>(std::lround(static_cast<double>(src.h) * pw / src.w)));
			const std::string key = c->image + "|" + std::to_string(pw) + "|" + std::to_string(ph);
			if (!sized.count(key)) sized[key] = scaled(src, pw, ph);
			over(frame, w, h, sized[key], (w - pw) / 2, static_cast<int>(std::lround(c->centreY * h - ph / 2.0)), a);
		}
		// (written beside it and renamed over it: a run stopped in the middle of the write must not leave half a frame that a resumed video would keep)
		const fs::path tmp = dir / (std::string(name) + ".tmp.png");
		if (!write_png(tmp.string().c_str(), w, h, frame.data())) continue;
		fs::rename(tmp, file, ec);
		if (ec) {
			fs::remove(tmp, ec);
			continue;
		}
		log << i << stampOf(file) << "\n";
		log.flush();
		++laid;
	}
	return laid;
}

}  // namespace film_captions
