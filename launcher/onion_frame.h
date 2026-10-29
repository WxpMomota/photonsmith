#pragma once
// onion_frame.h -- `ray_tracer --film-onion <key> T0 T1 OUT.png [WIDTH HEIGHT] [EVERY]`: an animator's sheet of a stretch of a film, as Blender's onion skinning and motion
// paths show one - the frame in the middle of T0..T1 in a quick flat shading, faint ghosts of the characters every EVERY frames before it (red) and after it (green), and
// each character's path over every frame of the stretch as a line with a dot a frame (dots far apart: fast; bunched: slow - the spacing, its easing). All seen by the
// middle frame's camera, so the ghosts line up with the set. Made from the scene's first-hit passes on the CPU (cpu_render_aov_planes: colour, normal, coverage and the
// object IDs that pick out each `cast/NAME`), not a render: seconds for a stretch of a few seconds, to check timing, arcs and spacing before rendering anything.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../cpu_renderer/cpu_interface.h"
#include "../src/external/image_writer.h"
#include "../src/shared/cartoon/cartoons.h"
#include "../src/shared/cryptomatte.h"
#include "pass_drawing.h"

namespace onion_frame {

// The `cast/NAME` groups of a frame's scene text, in the order they first appear.
inline std::vector<std::string> castOf(const std::string &text) {
	std::vector<std::string> names;
	for (std::size_t at = text.find("\"cast/"); at != std::string::npos; at = text.find("\"cast/", at + 1)) {
		const std::size_t end = text.find('"', at + 1);
		if (end == std::string::npos) break;
		const std::string name = text.substr(at + 1, end - at - 1);
		if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
	}
	return names;
}

// A frame's first-hit passes on the CPU (cpu_render_aov_planes: 12 planes of w x h - albedo 0-2, normal 3-5, depth 6, uv 7-8, coverage 9, object ID 10), each scene written to
// a file of its own (two sheets made at once must not rewrite each other's scene) and loaded into the one registered scene again.
class Passes {
public:
	Passes()
		: file_(std::filesystem::temp_directory_path() /
				("photonsmith_passes_" + std::to_string(std::random_device{}()) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".pbrt")) {}
	~Passes() {
		std::error_code ec;
		std::filesystem::remove(file_, ec);
	}
	Passes(const Passes &) = delete;
	Passes &operator=(const Passes &) = delete;
	// The passes of a scene's text; false when they cannot be made.
	bool of(const std::string &text, int w, int h, std::vector<float> &planes) {
		planes.assign(static_cast<std::size_t>(w) * h * 12, 0.0f);
		std::ofstream(file_, std::ios::binary) << text;
		if (!*id_) {
			if (cpu_register_scene_file(file_.string().c_str(), id_, static_cast<int>(sizeof id_)) != 0) return false;
		} else if (!cpu_reload_scene_file(id_)) {
			return false;
		}
		return cpu_render_aov_planes(w, h, 4, id_, 0.0, 0.0, 0.0, 0, planes.data(), 1) == 0;
	}

private:
	std::filesystem::path file_;
	char id_[64] = "";
};

// A frame's scene seen by another frame's camera: `header` (that frame's, up to WorldBegin) and the frame's world.
inline std::string withHeader(const std::string &text, const std::string &header) {
	const std::size_t world = text.find("WorldBegin");
	return world == std::string::npos ? text : header + text.substr(world + std::string("WorldBegin").size());
}

// The command: 0 when the sheet was written.
inline int run(int argc, char **argv) {
	const char *usage = "usage: ray_tracer --film-onion <film key> T0 T1 OUT.png [WIDTH HEIGHT] [EVERY]";
	if (argc < 6) {
		std::cerr << usage << std::endl;
		return EXIT_FAILURE;
	}
	const cartoon::Film *film = cartoon::find(argv[2]);
	const double t0 = std::atof(argv[3]), t1 = std::atof(argv[4]);
	const std::string out = argv[5];
	const int w = argc >= 8 ? std::max(16, std::atoi(argv[6])) : 960, h = argc >= 8 ? std::max(16, std::atoi(argv[7])) : 540;
	const int every = argc >= 9 ? std::max(1, std::atoi(argv[8])) : 3;
	if (!film || !film->sceneAt || !film->headerAt || !(t1 > t0)) {
		std::cerr << usage << " (a film's key, and T1 after T0)" << std::endl;
		return EXIT_FAILURE;
	}
	const int fps = std::max(1, film->fps);
	const long f0 = std::lround(t0 * fps), f1 = std::lround(t1 * fps), middle = (f0 + f1) / 2;
	// every frame's scene, seen by the middle frame's camera: that frame's header, the frame's world
	const std::string header = film->headerAt(static_cast<double>(middle) / fps);
	Passes passes;
	const std::size_t n = static_cast<std::size_t>(w) * h;
	std::vector<float> planes;
	std::vector<std::string> cast;
	const auto passesOf = [&](long f) {   // the frame's passes; false when they cannot be made
		const std::string text = withHeader(film->sceneAt(static_cast<double>(f) / fps), header);
		for (const std::string &c : castOf(text))
			if (std::find(cast.begin(), cast.end(), c) == cast.end()) cast.push_back(c);
		return passes.of(text, w, h, planes);
	};
	const auto plane = [&](int p, std::size_t i) { return planes[static_cast<std::size_t>(p) * n + i]; };
	// the middle frame, in a flat shading
	if (!passesOf(middle)) {
		std::cerr << "the film's frame could not be made into passes" << std::endl;
		return EXIT_FAILURE;
	}
	std::vector<float> rgb = pass_drawing::flatShade(planes, w, h);
	const std::vector<float> middleRgb = rgb, middleId(planes.begin() + static_cast<std::ptrdiff_t>(10 * n), planes.begin() + static_cast<std::ptrdiff_t>(11 * n));
	std::vector<float> ids;   // (the characters' object IDs: cryptomatte.h, as the passes hash `cast/NAME`)
	const auto inCast = [&](float id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
	// every frame: each character's middle on the screen (its path), and on the ghost frames its shape laid on faintly
	std::vector<std::vector<std::pair<double, double>>> paths(64);
	for (long f = f0; f <= f1; ++f) {
		if (!passesOf(f)) continue;
		for (const std::string &c : cast)
			if (!inCast(cryptomatte::idToFloat(cryptomatte::murmur3_32(c)))) ids.push_back(cryptomatte::idToFloat(cryptomatte::murmur3_32(c)));
		for (std::size_t k = 0; k < ids.size() && k < paths.size(); ++k) {
			double sx = 0.0, sy = 0.0, count = 0.0;
			for (std::size_t i = 0; i < n; ++i)
				if (plane(9, i) > 0.5f && plane(10, i) == ids[k]) sx += static_cast<double>(i % w) + 0.5, sy += static_cast<double>(i / w) + 0.5, count += 1.0;
			if (count > 0.0) paths[k].push_back({sx / count, sy / count});
		}
		const long away = f - middle;
		if (away == 0 || away % every != 0) continue;
		const double fade = std::max(0.15, 0.55 - 0.1 * (std::labs(away) / every - 1));
		const double tint[3] = {away < 0 ? 0.95 : 0.25, away < 0 ? 0.28 : 0.8, away < 0 ? 0.22 : 0.32};   // (before red, after green: Blender's)
		for (int y = 1; y + 1 < h; ++y)
			for (int x = 1; x + 1 < w; ++x) {
				const std::size_t i = static_cast<std::size_t>(y) * w + x;
				if (plane(9, i) <= 0.5f || !inCast(plane(10, i))) continue;
				bool edge = false;   // (its outline drawn stronger than its fill)
				for (const std::size_t j : {i - 1, i + 1, i - static_cast<std::size_t>(w), i + static_cast<std::size_t>(w)}) edge = edge || plane(9, j) <= 0.5f || plane(10, j) != plane(10, i);
				const double a = edge ? std::min(1.0, 1.6 * fade) : 0.45 * fade;
				for (int c = 0; c < 3; ++c) rgb[3 * i + c] = static_cast<float>(rgb[3 * i + c] + (tint[c] - rgb[3 * i + c]) * a);
			}
	}
	// the middle frame's characters over their ghosts
	for (const std::string &c : cast)
		if (!inCast(cryptomatte::idToFloat(cryptomatte::murmur3_32(c)))) ids.push_back(cryptomatte::idToFloat(cryptomatte::murmur3_32(c)));
	for (std::size_t i = 0; i < n; ++i)
		if (inCast(middleId[i]))
			for (int c = 0; c < 3; ++c) rgb[3 * i + c] = middleRgb[3 * i + c];
	// the paths: a line through each character's middles, and a dot at each frame
	const auto dot = [&](double cx, double cy, double r, const double col[3]) {
		for (int y = static_cast<int>(cy - r - 1); y <= static_cast<int>(cy + r + 1); ++y)
			for (int x = static_cast<int>(cx - r - 1); x <= static_cast<int>(cx + r + 1); ++x) {
				if (x < 0 || y < 0 || x >= w || y >= h) continue;
				const double a = std::clamp(r + 0.5 - std::hypot(x + 0.5 - cx, y + 0.5 - cy), 0.0, 1.0);
				for (int c = 0; c < 3; ++c) rgb[(static_cast<std::size_t>(y) * w + x) * 3 + c] += static_cast<float>((col[c] - rgb[(static_cast<std::size_t>(y) * w + x) * 3 + c]) * a);
			}
	};
	const double line[3] = {0.98, 0.78, 0.12}, mark[3] = {0.1, 0.1, 0.12};
	const double scale = h / 540.0;
	for (const auto &path : paths) {
		for (std::size_t k = 1; k < path.size(); ++k) {
			const double len = std::hypot(path[k].first - path[k - 1].first, path[k].second - path[k - 1].second);
			for (double s = 0.0; s <= len; s += 0.5) {
				const double u = len > 0.0 ? s / len : 0.0;
				dot(path[k - 1].first + u * (path[k].first - path[k - 1].first), path[k - 1].second + u * (path[k].second - path[k - 1].second), 1.0 * scale, line);
			}
		}
		for (const auto &p : path) dot(p.first, p.second, 2.2 * scale, mark), dot(p.first, p.second, 1.3 * scale, line);
	}
	std::vector<unsigned char> bytes(n * 3);
	for (std::size_t i = 0; i < n * 3; ++i) bytes[i] = static_cast<unsigned char>(std::lround(std::clamp(rgb[i], 0.0f, 1.0f) * 255.0f));
	if (!write_png(out.c_str(), w, h, bytes.data())) {
		std::cerr << "could not write " << out << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << out << "  (frames " << f0 << " to " << f1 << ", the middle " << middle << "; ghosts every " << every << "; " << cast.size() << " character(s))" << std::endl;
	return EXIT_SUCCESS;
}

}  // namespace onion_frame
