#pragma once
// film_transitions.h -- a film's dissolves, fades and wipes (cartoon::Film::transitions), made on its finished frames when the video is assembled, before its captions are laid on.
//
// A frame in a dissolve or a wipe is two pictures: the film's own (the camera of the shot it is in) and the other side's (Film::sceneAcross: the camera of the shot on the other
// side of the cut), rendered into the frames folder's `across` folder (video_render.h renders only the frames that need it). The two are blended in linear light - a dissolve
// evenly, a wipe with a soft edge coming in from the left - and a fade takes the frame to black and up again. The frame's own picture is kept beside it (plain_NNNN.png) and
// what was made is recorded (transitions.done), so that a resumed video, or one whose transitions were changed, is made again from the pictures as rendered.

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_render.h"
#include "film_captions.h"   // (its picture reading and writing)
#include "../src/shared/cartoon/cartoons.h"

namespace film_transitions {

constexpr const char *kRecord = "transitions.done";
constexpr const char *kPlainPrefix = "plain_";
constexpr const char *kAcrossFolder = "across";

// What a frame at time t is in: none (a plain frame), or a transition and how far through it is (0..1: the incoming shot's share for a dissolve or a wipe, the brightness for a fade).
struct At {
	const cartoon::Transition *in = nullptr;
	double amount = 0.0;
	bool beforeCut = false;
};
inline At at(const std::vector<cartoon::Transition> &transitions, double t) {
	At a;
	for (const cartoon::Transition &tr : transitions) {
		const double h = 0.5 * tr.length;
		if (t < tr.at - h || t > tr.at + h) continue;
		a.in = &tr;
		a.beforeCut = t < tr.at;
		if (tr.kind == cartoon::Transition::Fade) a.amount = a.beforeCut ? 1.0 - cartoon::smoothstep(tr.at - h, tr.at, t) : cartoon::smoothstep(tr.at, tr.at + h, t);
		else a.amount = cartoon::smoothstep(tr.at - h, tr.at + h, t);
		return a;
	}
	return a;
}
inline double timeOf(const cartoon::Film &film, int frame, int count) { return film.duration * frame / std::max(1, count); }

// The frames whose other side must be rendered (those in a dissolve or a wipe), one flag a frame.
inline std::vector<char> acrossFrames(const cartoon::Film &film, int count) {
	std::vector<char> out(static_cast<std::size_t>(std::max(0, count)), 0);
	if (!film.transitions || !film.sceneAcross) return out;
	const std::vector<cartoon::Transition> transitions = film.transitions();
	for (int i = 0; i < count; ++i) {
		const At a = at(transitions, timeOf(film, i, count));
		out[static_cast<std::size_t>(i)] = a.in && a.in->kind != cartoon::Transition::Fade;
	}
	return out;
}
inline bool needsAcross(const cartoon::Film &film, int count) {
	for (char c : acrossFrames(film, count))
		if (c) return true;
	return false;
}

namespace detail {
inline double toLinear(unsigned char v) {
	const double c = v / 255.0;
	return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
inline unsigned char toSrgb(double c) {
	c = std::max(0.0, std::min(1.0, c));
	const double v = c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
	return static_cast<unsigned char>(std::lround(v * 255.0));
}
inline bool loadRgb(const std::filesystem::path &p, std::vector<unsigned char> &px, int &w, int &h) {
	int n = 0;
	unsigned char *data = stbi_load(p.string().c_str(), &w, &h, &n, 3);
	if (!data) return false;
	px.assign(data, data + static_cast<std::size_t>(w) * h * 3);
	stbi_image_free(data);
	return true;
}
inline std::string stampOf(const std::filesystem::path &p) {
	std::error_code ec;
	const auto size = std::filesystem::file_size(p, ec);
	if (ec) return "-";
	const auto when = std::filesystem::last_write_time(p, ec);
	return std::to_string(static_cast<unsigned long long>(size)) + ":" + std::to_string(static_cast<long long>(when.time_since_epoch().count()));
}
inline std::string plainName(int frame) {
	char name[32];
	std::snprintf(name, sizeof(name), "%s%04d.png", kPlainPrefix, frame);
	return name;
}
// The frames the captions pass has recorded, with the stamp of the frame it left (film_captions.h's record): a frame of ours with words on it is still ours.
inline std::map<int, std::string> captionedStamps(const std::filesystem::path &dir) {
	std::map<int, std::string> out;
	std::ifstream in(dir / "captions.done");
	std::string line;
	while (std::getline(in, line)) {
		std::istringstream parts(line);
		int frame = 0;
		std::string size, when;
		if (parts >> frame >> size >> when) out[frame] = size + ":" + when;
	}
	return out;
}
}  // namespace detail

// The picture of a frame in a transition: `own` is the frame's picture, `across` the other side's (for a dissolve or a wipe), both w x h RGB.
inline std::vector<unsigned char> blend(const At &a, const std::vector<unsigned char> &own, const std::vector<unsigned char> &across, int w, int h) {
	std::vector<unsigned char> out(own.size());
	const bool fade = a.in->kind == cartoon::Transition::Fade;
	// the outgoing and incoming pictures: before the cut the frame's own is the outgoing shot's
	const std::vector<unsigned char> &out0 = fade || a.beforeCut ? own : across, &in1 = fade || a.beforeCut ? across : own;
	const double soft = 0.04 * w, edge = a.amount * (w + soft) - soft;   // (a wipe's edge, from the left)
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 3;
			double k = a.amount;
			if (a.in->kind == cartoon::Transition::Wipe) k = 1.0 - cartoon::smoothstep(edge, edge + soft, x + 0.5);
			for (int c = 0; c < 3; ++c) {
				if (fade) out[i + c] = detail::toSrgb(detail::toLinear(own[i + c]) * a.amount);
				else out[i + c] = detail::toSrgb(detail::toLinear(out0[i + c]) * (1.0 - k) + detail::toLinear(in1[i + c]) * k);
			}
		}
	return out;
}

// Makes the transitions on the finished frames of `dir` (enc_NNNN.png, and across/enc_NNNN.png for a dissolve or a wipe); returns how many frames it made. A frame whose other
// side is missing (its render failed) is left as a cut.
inline int apply(const cartoon::Film &film, const std::filesystem::path &dir, int count) {
	namespace fs = std::filesystem;
	std::map<int, std::pair<std::string, std::string>> done;   // frame -> (its stamp as we left it, the other side's stamp)
	{
		std::ifstream in(dir / kRecord);
		std::string line;
		while (std::getline(in, line)) {
			std::istringstream parts(line);
			int frame = 0;
			std::string own, across;
			if (parts >> frame >> own >> across) done[frame] = {own, across};
		}
	}
	const std::vector<cartoon::Transition> transitions = film.transitions ? film.transitions() : std::vector<cartoon::Transition>();
	if (transitions.empty() && done.empty()) return 0;
	const std::map<int, std::string> captioned = detail::captionedStamps(dir);
	std::ofstream log(dir / kRecord, std::ios::app);
	int made = 0, missing = 0;
	std::error_code ec;
	for (int i = 0; i < count; ++i) {
		const fs::path file = dir / cartoon_render::encName(i), plain = dir / detail::plainName(i), across = dir / kAcrossFolder / cartoon_render::encName(i);
		const std::string stamp = detail::stampOf(file);
		const auto it = done.find(i);
		const bool ours = it != done.end() && it->second.first != "none" && (stamp == it->second.first || (captioned.count(i) && captioned.at(i) == stamp));   // (as we left it, maybe with its words)
		const At a = at(transitions, timeOf(film, i, count));
		if (!a.in) {
			if (ours && fs::exists(plain, ec)) {   // (no longer in a transition: its own picture back)
				fs::rename(plain, file, ec);
				log << i << " none -\n";
			}
			continue;
		}
		const bool needsOther = a.in->kind != cartoon::Transition::Fade;
		const std::string acrossStamp = needsOther ? detail::stampOf(across) : "-";
		if (needsOther && acrossStamp == "-") {
			++missing;
			continue;
		}
		if (ours && it->second.second == acrossStamp) continue;   // (made as it is now)
		if (!ours) fs::copy_file(file, plain, fs::copy_options::overwrite_existing, ec);   // (a frame as rendered: kept)
		if (ec) continue;
		std::vector<unsigned char> own, other;
		int w = 0, h = 0, w2 = 0, h2 = 0;
		if (!detail::loadRgb(plain, own, w, h)) continue;
		if (needsOther && (!detail::loadRgb(across, other, w2, h2) || w2 != w || h2 != h)) {
			++missing;
			continue;
		}
		const std::vector<unsigned char> picture = blend(a, own, needsOther ? other : own, w, h);
		const fs::path tmp = dir / (cartoon_render::encName(i) + ".tmp.png");
		if (!write_png(tmp.string().c_str(), w, h, picture.data())) continue;
		fs::rename(tmp, file, ec);
		if (ec) {
			fs::remove(tmp, ec);
			continue;
		}
		log << i << " " << detail::stampOf(file) << " " << acrossStamp << "\n";
		log.flush();
		++made;
	}
	if (missing > 0) std::cerr << "WARNING: " << missing << " frame(s) of a dissolve or a wipe have no picture of the other side (its render failed): they are left as a cut." << std::endl;
	return made;
}

}  // namespace film_transitions
