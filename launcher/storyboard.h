#pragma once
// storyboard.h -- a film drawn before it is rendered, as a story artist would draw it:
//   `ray_tracer --film-storyboard <key> OUT.png [WIDTH] [colour]`: a storyboard sheet - a panel a shot (two or three for a long shot), three to a row, each a drawing of the
//   shot's frame with its number, its time and length under it, and the line of the film's words (its captions) spoken in it.
//   `ray_tracer --film-animatic <key> OUT.mp4 [WIDTH] [FPS] [colour]`: an animatic - every frame of the film drawn so (FPS a second: 12) with the shot and time in a corner, made
//   into a video with the film's soundtrack, to watch the cut, the timing and the sound together before a frame is rendered.
// A drawing is made from the frame's first-hit passes (onion_frame.h's Passes: no lighting, no samples to speak of): its shading in four pencil tones with hatching in the
// darkest, and ink lines where one thing ends and another begins (coverage, object, a step in depth, a fold in the surface); `colour` keeps the flat colours under the ink.
// A frame takes a fraction of a second, so a whole film's animatic takes minutes.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "launcher/onion_frame.h"
#include "launcher/pass_drawing.h"
#include "launcher/video_render.h"   // assembleVideoFile, film_captions::textPicture, cartoon_render::writeSoundtrack

namespace storyboard {

// Text laid on a picture (sRGB, W wide) with its top left at (x, y): white letters with a soft dark halo (film_captions.h), `size` a share of `scale` pixels, at most `width` wide.
inline void label(std::vector<float> &rgb, int W, int H, int x, int y, const std::string &text, double size, int scale, int width) {
	if (text.empty()) return;
	cartoon::Caption c{};
	c.width = 1.0;
	cartoon::CaptionLine line;
	line.text = text;
	line.font = "NotoSansSC-Regular.ttf";
	line.size = size;
	c.lines.push_back(line);
	const film_captions::Picture pic = film_captions::textPicture(c, std::max(1, width), scale);   // (its size a share of `scale`, its width at most `width`)
	for (int py = 0; py < pic.h; ++py)
		for (int px = 0; px < pic.w; ++px) {
			const int tx = x + px, ty = y + py;
			if (tx < 0 || ty < 0 || tx >= W || ty >= H) continue;
			const float *p = &pic.rgba[(static_cast<std::size_t>(py) * pic.w + px) * 4];
			for (int k = 0; k < 3; ++k) {
				float &d = rgb[(static_cast<std::size_t>(ty) * W + tx) * 3 + k];
				d += (p[k] - d) * p[3];
			}
		}
}

inline std::string clock(double t) {
	char s[32];
	std::snprintf(s, sizeof(s), "%d:%04.1f", static_cast<int>(t / 60.0), std::fmod(t, 60.0));
	return s;
}
// The film's shots: when each starts (the first at 0).
inline std::vector<double> shotsOf(const cartoon::Film &film) {
	std::vector<double> s = film.shotStarts ? film.shotStarts() : std::vector<double>{0.0};
	if (s.empty() || s.front() > 1e-9) s.insert(s.begin(), 0.0);
	return s;
}
// The words of the film's captions shown at time t ("" when none).
inline std::string wordsAt(const std::vector<cartoon::Caption> &captions, double t0, double t1) {
	for (const cartoon::Caption &c : captions)
		if (c.t0 < t1 && c.t1 > t0 && !c.lines.empty()) {
			std::string s;
			for (const cartoon::CaptionLine &l : c.lines) s += (s.empty() ? "" : " ") + l.text;
			return s;
		}
	return std::string();
}
inline bool writePicture(const std::string &path, const std::vector<float> &rgb, int w, int h) {
	std::vector<unsigned char> bytes(rgb.size());
	for (std::size_t i = 0; i < rgb.size(); ++i) bytes[i] = static_cast<unsigned char>(std::lround(std::clamp(rgb[i], 0.0f, 1.0f) * 255.0f));
	return write_png(path.c_str(), w, h, bytes.data());
}

// `--film-storyboard`: 0 when the sheet was written.
inline int runSheet(int argc, char **argv) {
	const char *usage = "usage: ray_tracer --film-storyboard <film key> OUT.png [WIDTH] [colour]";
	const cartoon::Film *film = argc >= 4 ? cartoon::find(argv[2]) : nullptr;
	if (!film || !film->sceneAt) {
		std::cerr << usage << (argc >= 4 ? " (a film's key)" : "") << std::endl;
		return EXIT_FAILURE;
	}
	const std::string out = argv[3];
	int pw = 400;
	bool colour = false;
	for (int i = 4; i < argc; ++i) {
		if (std::string(argv[i]) == "colour" || std::string(argv[i]) == "color") colour = true;
		else pw = std::clamp(std::atoi(argv[i]), 160, 1280);
	}
	const int ph = pw * 9 / 16, strip = std::max(36, ph * 3 / 10), gap = std::max(12, pw / 24), perRow = 3, title = std::max(40, pw / 8);
	// the panels: a shot each, a long shot two or three (one every five seconds or so), each at the middle of its part of the shot
	struct Panel {
		int shot, part;
		double t, t0, t1;
	};
	std::vector<Panel> panels;
	const std::vector<double> starts = shotsOf(*film);
	for (std::size_t s = 0; s < starts.size(); ++s) {
		const double t0 = starts[s], t1 = s + 1 < starts.size() ? starts[s + 1] : film->duration;
		const int parts = std::clamp(static_cast<int>(std::lround((t1 - t0) / 5.0)), 1, 3);
		for (int k = 0; k < parts; ++k)
			panels.push_back({static_cast<int>(s) + 1, parts > 1 ? k + 1 : 0, t0 + (t1 - t0) * (k + 0.5) / parts, t0 + (t1 - t0) * k / parts, t0 + (t1 - t0) * (k + 1) / parts});
	}
	const std::vector<cartoon::Caption> captions = film->captions ? film->captions() : std::vector<cartoon::Caption>{};
	const int cols = std::min(perRow, static_cast<int>(panels.size())), rows = (static_cast<int>(panels.size()) + perRow - 1) / perRow;
	const int W = cols * pw + (cols + 1) * gap, H = title + rows * (ph + strip + gap) + gap;
	std::vector<float> sheet(static_cast<std::size_t>(W) * H * 3, 0.9f);
	label(sheet, W, H, gap, gap / 2, film->name, 0.55, title, W - 2 * gap);
	onion_frame::Passes passes;
	std::vector<float> planes;
	for (std::size_t p = 0; p < panels.size(); ++p) {
		const Panel &panel = panels[p];
		std::cout << "panel " << (p + 1) << " of " << panels.size() << " (shot " << panel.shot << ", " << clock(panel.t) << ")..." << std::endl;
		const int x0 = gap + static_cast<int>(p % perRow) * (pw + gap), y0 = title + static_cast<int>(p / perRow) * (ph + strip + gap);
		if (passes.of(film->sceneAt(panel.t), pw, ph, planes)) {
			const std::vector<float> drawing = pass_drawing::sketch(planes, pw, ph, colour);
			for (int y = 0; y < ph; ++y)
				for (int x = 0; x < pw; ++x)
					for (int c = 0; c < 3; ++c)
						sheet[(static_cast<std::size_t>(y0 + y) * W + x0 + x) * 3 + c] = drawing[(static_cast<std::size_t>(y) * pw + x) * 3 + c];
		} else {
			std::cerr << "  shot " << panel.shot << " could not be drawn" << std::endl;
		}
		for (int y = y0 - 1; y <= y0 + ph + strip; ++y)   // the panel's frame, and the dark strip under it for its words
			for (int x = x0 - 1; x <= x0 + pw; ++x) {
				const bool border = y == y0 - 1 || x == x0 - 1 || x == x0 + pw || y == y0 + ph + strip, under = y >= y0 + ph;
				if ((border || under) && x >= 0 && y >= 0 && x < W && y < H)
					for (int c = 0; c < 3; ++c) sheet[(static_cast<std::size_t>(y) * W + x) * 3 + c] = border ? pass_drawing::kInk : 0.2f;
			}
		char length[32];
		std::snprintf(length, sizeof(length), "%.1f s", panel.t1 - panel.t0);
		const std::string head = "Shot " + std::to_string(panel.shot) + (panel.part ? std::string(1, static_cast<char>('A' + panel.part - 1)) : std::string()) + "   " +
								 clock(panel.t0) + "   " + length;
		label(sheet, W, H, x0 + 2, y0 + ph + 2, head, 0.3, strip, pw / 2);
		label(sheet, W, H, x0 + 2, y0 + ph + strip * 2 / 5, wordsAt(captions, panel.t0, panel.t1), 0.22, strip, pw - 4);
	}
	if (!writePicture(out, sheet, W, H)) {
		std::cerr << "could not write " << out << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << out << "  (" << panels.size() << " panels, " << starts.size() << " shots)" << std::endl;
	return EXIT_SUCCESS;
}

// `--film-animatic`: 0 when the video was made.
inline int runAnimatic(int argc, char **argv) {
	const char *usage = "usage: ray_tracer --film-animatic <film key> OUT.mp4 [WIDTH] [FPS] [colour]";
	const cartoon::Film *film = argc >= 4 ? cartoon::find(argv[2]) : nullptr;
	if (!film || !film->sceneAt) {
		std::cerr << usage << (argc >= 4 ? " (a film's key)" : "") << std::endl;
		return EXIT_FAILURE;
	}
	const std::filesystem::path out = argv[3];
	int w = 480, fps = 12;
	bool colour = false;
	for (int i = 4, numbers = 0; i < argc; ++i) {
		if (std::string(argv[i]) == "colour" || std::string(argv[i]) == "color") colour = true;
		else if (numbers++ == 0) w = std::clamp(std::atoi(argv[i]), 160, 1920) / 2 * 2;
		else fps = std::clamp(std::atoi(argv[i]), 1, 60);
	}
	const int h = w * 9 / 16 / 2 * 2, frames = std::max(1, static_cast<int>(std::lround(film->duration * fps)));
	const std::filesystem::path dir = out.parent_path() / (out.stem().string() + "_animatic_frames");
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	std::filesystem::create_directories(dir, ec);
	const std::vector<double> starts = shotsOf(*film);
	onion_frame::Passes passes;
	std::vector<float> planes;
	std::vector<float> last(static_cast<std::size_t>(w) * h * 3, pass_drawing::kPaper);
	std::cout << "Drawing " << frames << " frames of " << film->name << " (" << w << " x " << h << ", " << fps << " a second)..." << std::endl;
	for (int f = 0; f < frames; ++f) {
		const double t = static_cast<double>(f) / fps;
		std::vector<float> rgb = passes.of(film->sceneAt(t), w, h, planes) ? pass_drawing::sketch(planes, w, h, colour) : last;   // (a frame that cannot be drawn holds the one before)
		last = rgb;
		const int shot = static_cast<int>(std::upper_bound(starts.begin(), starts.end(), t + 1e-9) - starts.begin());
		label(rgb, w, h, 4, 2, "Shot " + std::to_string(shot) + "   " + clock(t), 0.065, h, w / 2);
		if (!writePicture((dir / cartoon_render::encName(f)).string(), rgb, w, h)) {
			std::cerr << "could not write a frame in " << dir.string() << std::endl;
			return EXIT_FAILURE;
		}
		if ((f + 1) % (fps * 5) == 0 || f + 1 == frames) std::cout << "  " << (f + 1) << " of " << frames << std::endl;
	}
	const std::filesystem::path wav = dir / "soundtrack.wav";
	const bool sound = cartoon_render::writeSoundtrack(*film, wav, cartoon_render::soundtrackStretched(*film, frames, fps) ? cartoon_render::videoSecondsOf(frames, fps) : 0.0);
	const int rc = assembleVideoFile(dir, frames, fps, out, sound ? wav : std::filesystem::path());
	if (rc == SUCCESS) std::filesystem::remove_all(dir, ec);
	return rc == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace storyboard
