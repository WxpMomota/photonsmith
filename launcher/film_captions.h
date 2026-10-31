#pragma once
// film_captions.h -- the words a film lays over its video (cartoon::Caption: a line of the story, a title), laid onto the finished frames just before they become the video.
//
// The frames are the video's own pictures (enc_0000.png ...), which a resumed video keeps, so a frame must not get its words twice: the frames this has written are listed in
// captions.done beside them with the size and time of the file it left and a key of the words it has, and a frame whose file and words are still those is skipped. A frame rendered
// again is a different file, and gets its words; a frame filled in with a copy of its neighbour (which keeps the neighbour's time) has the words its neighbour has. The picture as
// rendered is kept beside a frame with words (clean_NNNN.png), so that a caption file that was edited changes the words of a resumed video without rendering a frame again.
// A caption's words are set in a font at the frame's size (textPicture); a caption that is a picture instead is read once, scaled to the frame (an area average, so the small
// text stays crisp) and laid on with its transparency; a missing picture is reported once and skipped.

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
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC   // (its functions static to each file that includes this: no second definition to clash with)
#include "../src/external/stb_truetype.h"
#include "../src/shared/cartoon/cartoons.h"
#include "../src/shared/cartoon/film_cache.h"

namespace film_captions {

struct Picture {
	int w = 0, h = 0;
	std::vector<float> rgba;   // straight (not premultiplied) alpha, 0..1, colours in sRGB
};

// ---- text: a caption's lines set in a font at the frame's own size --------------------------------------------------------------------------------------------------

// A font file read once (and its face, for a .ttc). `ok` false: it could not be found or read.
struct Font {
	std::vector<unsigned char> data;
	stbtt_fontinfo info{};
	bool ok = false;
};

// Where a font is looked for: as given (a full path); the film's own fonts (<scenes>/film_assets/fonts); the system's; the program's (fonts/ beside it, the GUI's qt_gui/fonts).
inline std::string findFont(const std::string &file) {
	namespace fs = std::filesystem;
	std::error_code ec;
	if (file.empty()) return std::string();
	if (fs::path(file).is_absolute() && fs::exists(file, ec)) return file;
	if (const std::string own = cartoon::film_cache::asset("fonts/" + file); !own.empty()) return own;
	std::vector<fs::path> dirs;
#if defined(_WIN32)
	dirs.push_back(fs::path(pbrt_asset_check::environmentValue("WINDIR").empty() ? "C:/Windows" : pbrt_asset_check::environmentValue("WINDIR")) / "Fonts");
	if (const std::string local = pbrt_asset_check::environmentValue("LOCALAPPDATA"); !local.empty()) dirs.push_back(fs::path(local) / "Microsoft/Windows/Fonts");
#elif defined(__APPLE__)
	for (const char *d : {"/System/Library/Fonts", "/System/Library/Fonts/Supplemental", "/Library/Fonts"}) dirs.push_back(d);
	if (const std::string home = pbrt_asset_check::environmentValue("HOME"); !home.empty()) dirs.push_back(fs::path(home) / "Library/Fonts");
#else
	for (const char *d : {"/usr/share/fonts/truetype", "/usr/share/fonts/opentype", "/usr/share/fonts", "/usr/local/share/fonts"}) dirs.push_back(d);
#endif
	for (const char *d : {"fonts", "qt_gui/fonts", "../qt_gui/fonts", "../../qt_gui/fonts"}) dirs.push_back(d);
	for (const fs::path &d : dirs)
		if (fs::exists(d / file, ec)) return (d / file).string();
	return std::string();
}

// The font for a caption line, read once: the line's own, else Noto Sans SC (it has the Chinese and the Latin letters, and comes with the program), said once when it is used
// instead. Null when there is neither.
inline const Font *fontFor(const std::string &file, int index) {
	static std::map<std::string, Font> fonts;
	static std::set<std::string> said;
	const std::string key = file + "#" + std::to_string(index);
	if (auto it = fonts.find(key); it != fonts.end()) return it->second.ok ? &it->second : nullptr;
	Font &f = fonts[key];
	std::string path = findFont(file);
	int face = index;
	if (path.empty()) {
		path = findFont("NotoSansSC-Regular.ttf");
		face = 0;
		if (said.insert(file).second)
			std::cerr << "WARNING: the caption font \"" << file << "\" was not found" << (path.empty() ? "; the captions that use it are left out." : "; using Noto Sans SC instead.") << std::endl;
	}
	if (path.empty()) return nullptr;
	std::ifstream in(path, std::ios::binary);
	f.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	const int offset = f.data.empty() ? -1 : stbtt_GetFontOffsetForIndex(f.data.data(), face);
	f.ok = offset >= 0 && stbtt_InitFont(&f.info, f.data.data(), offset) != 0;
	if (!f.ok && said.insert(path).second) std::cerr << "WARNING: the caption font " << path << " could not be read; the captions that use it are left out." << std::endl;
	return f.ok ? &f : nullptr;
}

// The code points of UTF-8 text (a malformed byte stands for itself).
inline std::vector<int> codePoints(const std::string &s) {
	std::vector<int> out;
	for (std::size_t i = 0; i < s.size();) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		const int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;   // (the bytes that follow it)
		if (n < 0 || i + static_cast<std::size_t>(n) >= s.size() + (n == 0 ? 1u : 0u)) {
			out.push_back(c);
			++i;
			continue;
		}
		int cp = n == 0 ? c : c & (0x3F >> n);
		for (int k = 1; k <= n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
		out.push_back(cp);
		i += static_cast<std::size_t>(n) + 1;
	}
	return out;
}

// How wide text is in a font at `scale` (pixels), with its kerning.
inline double textWidth(const Font &f, const std::vector<int> &cps, float scale) {
	double w = 0.0;
	for (std::size_t i = 0; i < cps.size(); ++i) {
		int advance = 0, bearing = 0;
		stbtt_GetCodepointHMetrics(&f.info, cps[i], &advance, &bearing);
		w += advance * scale;
		if (i + 1 < cps.size()) w += scale * stbtt_GetCodepointKernAdvance(&f.info, cps[i], cps[i + 1]);
	}
	return w;
}

// A caption's lines set at the size they will have on a w x h frame: centred, each line wrapped at its spaces where it is wider than the caption's width, white-on-dark legible
// with a soft dark halo round the letters (as the caption pictures had). An empty picture when no line could be set.
inline Picture textPicture(const cartoon::Caption &c, int w, int h) {
	struct Row {
		const Font *font;
		float scale;
		std::vector<int> cps;
		double width, ascent, height;
		const cartoon::CaptionLine *style;
	};
	std::vector<Row> rows;
	const double maxWidth = (std::max)(0.05, c.width) * w;
	for (const cartoon::CaptionLine &line : c.lines) {
		const Font *font = fontFor(line.font, line.fontIndex);
		if (!font) continue;
		const double px = (std::max)(4.0, line.size * h);
		const float scale = stbtt_ScaleForPixelHeight(&font->info, static_cast<float>(px));
		int ascent = 0, descent = 0, gap = 0;
		stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &gap);
		// (the words of the line, put back together into rows no wider than `limit`)
		const std::vector<int> all = codePoints(line.text);
		const auto wrap = [&](double limit) {
			std::vector<std::vector<int>> out;
			std::vector<int> row, word;
			for (std::size_t i = 0; i <= all.size(); ++i) {
				if (i < all.size() && all[i] != ' ') {
					word.push_back(all[i]);
					continue;
				}
				std::vector<int> trial = row;
				if (!trial.empty()) trial.push_back(' ');
				trial.insert(trial.end(), word.begin(), word.end());
				if (!row.empty() && textWidth(*font, trial, scale) > limit) {
					out.push_back(row);
					row = word;
				} else {
					row = trial;
				}
				word.clear();
			}
			if (!row.empty()) out.push_back(row);
			return out;
		};
		// as few rows as the caption's width allows, and those as even as can be (no word left alone on the last row): the narrowest limit that needs no more rows
		std::vector<std::vector<int>> lineRows = wrap(maxWidth);
		if (lineRows.size() > 1) {
			double lo = maxWidth / static_cast<double>(lineRows.size()), hi = maxWidth;
			for (int step = 0; step < 16; ++step) {
				const double mid = 0.5 * (lo + hi);
				(wrap(mid).size() <= lineRows.size() ? hi : lo) = mid;
			}
			lineRows = wrap(hi);
		}
		for (const std::vector<int> &r : lineRows) rows.push_back({font, scale, r, textWidth(*font, r, scale), ascent * scale, (ascent - descent) * scale, &line});
	}
	if (rows.empty()) return Picture{};
	// the block: the rows one under another, a gap between them; room round it for the halo
	const double blur = 0.0044 * h, grow = (std::max)(1.0, 0.00124 * h);
	const int pad = static_cast<int>(std::ceil(blur * 3.0 + grow)) + 2;
	// (between two lines of the caption, its gap; between the rows a long line was wrapped into, a little leading)
	const auto gapAfter = [&](std::size_t i) { return (rows[i].style == rows[i + 1].style ? 0.12 : c.lineGap) * (std::max)(rows[i].height, rows[i + 1].height); };
	double blockW = 0.0, blockH = 0.0;
	for (std::size_t i = 0; i < rows.size(); ++i) {
		blockW = (std::max)(blockW, rows[i].width);
		blockH += rows[i].height + (i + 1 < rows.size() ? gapAfter(i) : 0.0);
	}
	Picture pic;
	pic.w = static_cast<int>(std::ceil(blockW)) + 2 * pad;
	pic.h = static_cast<int>(std::ceil(blockH)) + 2 * pad;
	std::vector<float> alpha(static_cast<std::size_t>(pic.w) * pic.h, 0.0f);
	std::vector<cartoon::Vec3> colour(alpha.size(), cartoon::Vec3{1.0, 1.0, 1.0});
	double top = pad;
	for (std::size_t r = 0; r < rows.size(); ++r) {
		const Row &row = rows[r];
		double x = pad + (blockW - row.width) / 2.0;
		const int baseline = static_cast<int>(std::lround(top + row.ascent));
		const double extra = row.style->weight * h / 1080.0;   // (a little weight: the glyph laid again a fraction of a pixel to the right)
		for (std::size_t i = 0; i < row.cps.size(); ++i) {
			int advance = 0, bearing = 0;
			stbtt_GetCodepointHMetrics(&row.font->info, row.cps[i], &advance, &bearing);
			const float shift = static_cast<float>(x - std::floor(x));
			int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			stbtt_GetCodepointBitmapBoxSubpixel(&row.font->info, row.cps[i], row.scale, row.scale, shift, 0.0f, &x0, &y0, &x1, &y1);
			const int gw = x1 - x0, gh = y1 - y0;
			if (gw > 0 && gh > 0) {
				std::vector<unsigned char> glyph(static_cast<std::size_t>(gw) * gh);
				stbtt_MakeCodepointBitmapSubpixel(&row.font->info, glyph.data(), gw, gh, gw, row.scale, row.scale, shift, 0.0f, row.cps[i]);
				for (int gy = 0; gy < gh; ++gy)
					for (int gx = 0; gx < gw; ++gx) {
						const int px = static_cast<int>(std::floor(x)) + x0 + gx, py = baseline + y0 + gy;
						if (px < 0 || py < 0 || px >= pic.w || py >= pic.h) continue;
						float a = glyph[static_cast<std::size_t>(gy) * gw + gx] / 255.0f;
						if (extra > 0.0 && gx > 0) a = (std::max)(a, static_cast<float>((std::min)(1.0, extra)) * glyph[static_cast<std::size_t>(gy) * gw + gx - 1] / 255.0f);
						float &d = alpha[static_cast<std::size_t>(py) * pic.w + px];
						if (a > d) {
							d = a;
							colour[static_cast<std::size_t>(py) * pic.w + px] = row.style->colour;
						}
					}
			}
			x += advance * row.scale;
			if (i + 1 < row.cps.size()) x += row.scale * stbtt_GetCodepointKernAdvance(&row.font->info, row.cps[i], row.cps[i + 1]);
		}
		top += row.height + (r + 1 < rows.size() ? gapAfter(r) : 0.0);
	}
	// the halo: the letters grown a pixel, blurred (three box blurs make a Gaussian), at 62%, in a warm near-black
	std::vector<float> halo(alpha.size(), 0.0f);
	const int g = static_cast<int>(std::lround(grow));
	for (int y = 0; y < pic.h; ++y)
		for (int x = 0; x < pic.w; ++x) {
			float m = 0.0f;
			for (int dy = -g; dy <= g; ++dy)
				for (int dx = -g; dx <= g; ++dx) {
					const int yy = y + dy, xx = x + dx;
					if (yy >= 0 && xx >= 0 && yy < pic.h && xx < pic.w) m = (std::max)(m, alpha[static_cast<std::size_t>(yy) * pic.w + xx]);
				}
			halo[static_cast<std::size_t>(y) * pic.w + x] = m;
		}
	const int radius = (std::max)(1, static_cast<int>(std::lround(blur * 0.87)));   // (a box of this half-width, three times, spreads like a Gaussian of `blur`)
	std::vector<float> tmp(halo.size());
	for (int pass = 0; pass < 3; ++pass) {
		for (int y = 0; y < pic.h; ++y)
			for (int x = 0; x < pic.w; ++x) {
				double s = 0.0;
				int n = 0;
				for (int k = -radius; k <= radius; ++k)
					if (x + k >= 0 && x + k < pic.w) s += halo[static_cast<std::size_t>(y) * pic.w + x + k], ++n;
				tmp[static_cast<std::size_t>(y) * pic.w + x] = static_cast<float>(s / n);
			}
		for (int y = 0; y < pic.h; ++y)
			for (int x = 0; x < pic.w; ++x) {
				double s = 0.0;
				int n = 0;
				for (int k = -radius; k <= radius; ++k)
					if (y + k >= 0 && y + k < pic.h) s += tmp[static_cast<std::size_t>(y + k) * pic.w + x], ++n;
				halo[static_cast<std::size_t>(y) * pic.w + x] = static_cast<float>(s / n);
			}
	}
	pic.rgba.assign(alpha.size() * 4, 0.0f);
	const cartoon::Vec3 shadow{8.0 / 255.0, 6.0 / 255.0, 4.0 / 255.0};
	for (std::size_t i = 0; i < alpha.size(); ++i) {
		const double ta = alpha[i], ha = 0.62 * halo[i] * (1.0 - ta), a = ta + ha;
		if (a <= 0.0) continue;
		const cartoon::Vec3 c3 = (colour[i] * ta + shadow * ha) * (1.0 / a);
		pic.rgba[i * 4 + 0] = static_cast<float>(c3.x);
		pic.rgba[i * 4 + 1] = static_cast<float>(c3.y);
		pic.rgba[i * 4 + 2] = static_cast<float>(c3.z);
		pic.rgba[i * 4 + 3] = static_cast<float>(a);
	}
	return pic;
}

// (round 17) A caption in a bubble, for a line spoken under the water: the words inside a round, glassy bubble - a faint blue fill, a bright rim, a highlight up on its left -
// with three small bubbles rising to it from below, where the speaker is. `frameH`: the frame's height (the rim's width follows it).
inline Picture bubbled(const Picture &text, int frameH) {
	if (text.w <= 0) return text;
	const double rx = 0.5 * text.w * 1.18 + 0.01 * frameH, ry = 0.5 * text.h * 1.5 + 0.01 * frameH, tail = 0.9 * ry, rim = (std::max)(1.5, 0.0028 * frameH);
	Picture out;
	out.w = static_cast<int>(std::ceil(2.0 * rx + 2.0 * rim)) + 4;
	out.h = static_cast<int>(std::ceil(2.0 * ry + 2.0 * rim + tail)) + 4;
	out.rgba.assign(static_cast<std::size_t>(out.w) * out.h * 4, 0.0f);
	const double cx = out.w / 2.0, cy = rim + 2.0 + ry;
	struct Ball {
		double x, y, rx, ry;
	};
	const Ball balls[] = {{cx, cy, rx, ry}, {cx - 0.35 * rx, cy + ry + 0.3 * tail, 0.16 * ry, 0.16 * ry}, {cx - 0.45 * rx, cy + ry + 0.62 * tail, 0.1 * ry, 0.1 * ry},
						  {cx - 0.5 * rx, cy + ry + 0.86 * tail, 0.06 * ry, 0.06 * ry}};
	// (straight alpha laid over straight alpha)
	const auto lay = [&out](std::size_t i, cartoon::Vec3 c, double a) {
		float *d = &out.rgba[i * 4];
		const double da = d[3], oa = a + da * (1.0 - a);
		if (oa <= 0.0) return;
		for (int k = 0; k < 3; ++k) d[k] = static_cast<float>(((k == 0 ? c.x : k == 1 ? c.y : c.z) * a + d[k] * da * (1.0 - a)) / oa);
		d[3] = static_cast<float>(oa);
	};
	for (int y = 0; y < out.h; ++y)
		for (int x = 0; x < out.w; ++x) {
			const std::size_t i = static_cast<std::size_t>(y) * out.w + x;
			for (const Ball &b : balls) {
				const double dx = (x + 0.5 - b.x) / b.rx, dy = (y + 0.5 - b.y) / b.ry, d = std::sqrt(dx * dx + dy * dy);
				const double fromRim = (d - 1.0) * (std::min)(b.rx, b.ry);   // (pixels outside the rim; negative inside)
				if (d < 1.0) lay(i, {0.72, 0.88, 1.0}, 0.22 * (0.6 + 0.4 * d * d));   // (the fill, a little stronger towards the rim)
				const double ring = 1.0 - std::abs(fromRim) / rim;
				if (ring > 0.0) lay(i, {0.92, 0.97, 1.0}, 0.85 * ring);
				const double ang = std::atan2(dy, dx), shine = std::exp(-std::pow((ang + 2.3) / 0.35, 2.0)) * (d > 0.7 && d < 0.88 ? 1.0 : 0.0);   // (up on its left)
				if (shine > 0.02) lay(i, {1.0, 1.0, 1.0}, 0.6 * shine);
			}
		}
	const int x0 = static_cast<int>(std::lround(cx - text.w / 2.0)), y0 = static_cast<int>(std::lround(cy - text.h / 2.0));
	for (int y = 0; y < text.h; ++y)
		for (int x = 0; x < text.w; ++x) {
			const int ox = x0 + x, oy = y0 + y;
			if (ox < 0 || oy < 0 || ox >= out.w || oy >= out.h) continue;
			const float *s = &text.rgba[(static_cast<std::size_t>(y) * text.w + x) * 4];
			if (s[3] > 0.0f) lay(static_cast<std::size_t>(oy) * out.w + ox, {s[0], s[1], s[2]}, s[3]);
		}
	return out;
}

// The captions with text as a subtitle file (SubRip, .srt) for the video: the same words, for a player to show or a site to read. False when there are none or it cannot be written.
inline bool writeSrt(const std::vector<cartoon::Caption> &captions, const std::filesystem::path &path) {
	std::string out;
	int n = 0;
	const auto stamp = [](double t) {
		const long long ms = std::llround((std::max)(0.0, t) * 1000.0);
		char b[32];
		std::snprintf(b, sizeof b, "%02lld:%02lld:%02lld,%03lld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
		return std::string(b);
	};
	for (const cartoon::Caption &c : captions) {
		if (c.lines.empty()) continue;
		out += std::to_string(++n) + "\n" + stamp(c.t0) + " --> " + stamp(c.t1) + "\n";
		for (const cartoon::CaptionLine &l : c.lines) out += l.text + "\n";
		out += "\n";
	}
	if (n == 0) return false;
	std::ofstream f(path, std::ios::binary);
	f << out;
	return static_cast<bool>(f);
}

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
// A frame's picture before its words were laid on (clean_NNNN.png beside enc_NNNN.png), kept so that changed words can be laid again without rendering the frame again.
constexpr const char *kCleanPrefix = "clean_";

// What is written on a frame: the words, their look and place and how strongly each shows, as a short hash ("none": nothing). Two frames with the same key look the same.
inline std::string wordsKey(const std::vector<std::pair<const cartoon::Caption *, double>> &here) {
	if (here.empty()) return "none";
	std::string s;
	char b[160];
	for (const auto &[c, a] : here) {
		std::snprintf(b, sizeof b, "%s|%.4f|%.6g|%.6g|%.6g|%d\n", c->image.c_str(), a, c->centreY, c->width, c->lineGap, c->bubble ? 1 : 0);
		s += b;
		for (const cartoon::CaptionLine &l : c->lines) {
			std::snprintf(b, sizeof b, "|%d|%.6g|%.6g|%.6g|%.6g|%.6g|", l.fontIndex, l.size, l.colour.x, l.colour.y, l.colour.z, l.weight);
			s += l.text + "|" + l.font + b + "\n";
		}
	}
	unsigned long long hash = 1469598103934665603ULL;   // (FNV-1a)
	for (const unsigned char ch : s) hash = (hash ^ ch) * 1099511628211ULL;
	std::snprintf(b, sizeof b, "%016llx", hash);
	return b;
}

// Lays the film's captions on frames 0 .. count-1 in `dir` (enc_NNNN.png). Returns how many frames got words (or lost them).
//
// Each frame is recorded with the stamp (size and time) of the file this left and the key of its words. A frame whose file and words are both as recorded is left alone; one rendered
// again is a clean picture and gets its words; one whose words have changed (the caption file was edited) gets them again on its clean picture, or that picture back when it has none now.
inline int apply(const cartoon::Film &film, const std::filesystem::path &dir, int count) {
	if (!film.captions) return 0;
	const std::vector<cartoon::Caption> captions = film.captions();
	namespace fs = std::filesystem;
	const fs::path record = dir / kRecord;
	struct Done {
		std::string stamp, key;   // key "" (a record from before keys): taken to be the words it has now
	};
	std::map<int, Done> done;
	std::set<std::string> captioned;   // the stamps of frames with words: a frame that failed is a copy of its neighbour with the neighbour's time (cartoon_render.h), and has its words if that had
	{
		std::ifstream in(record);
		std::string line;
		while (std::getline(in, line)) {
			std::istringstream parts(line);
			int frame = 0;
			std::string size, when, key;
			if (!(parts >> frame >> size >> when)) continue;
			parts >> key;
			done[frame] = {" " + size + " " + when, key};
			if (key != "none") captioned.insert(done[frame].stamp);
		}
	}
	if (captions.empty() && done.empty()) return 0;
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
	int laid = 0, stale = 0;
	for (int i = 0; i < count; ++i) {
		const double t = film.duration * i / (std::max)(1, count);
		std::vector<std::pair<const cartoon::Caption *, double>> here;
		for (const cartoon::Caption &c : captions)
			if (const double a = strength(c, t); a > 0.0) here.push_back({&c, a});
		const std::string key = wordsKey(here);
		char name[32], cleanName[32];
		std::snprintf(name, sizeof name, "enc_%04d.png", i);
		std::snprintf(cleanName, sizeof cleanName, "%s%04d.png", kCleanPrefix, i);
		const fs::path file = dir / name, clean = dir / cleanName;
		std::error_code ec;
		if (!fs::exists(file, ec)) continue;
		const std::string stamp = stampOf(file);
		const auto it = done.find(i);
		const bool asRecorded = it != done.end() && it->second.stamp == stamp;
		if (asRecorded && (it->second.key.empty() || it->second.key == key)) continue;
		// the picture to lay the words on: the file itself when it has none (a new render, or a frame recorded without words), else the clean picture kept beside it
		const bool fileIsClean = !asRecorded || it->second.key == "none";
		if (!asRecorded && here.empty()) continue;
		if (!asRecorded && captioned.count(stamp)) {   // (a copy of a frame that has its words)
			log << i << stamp << " " << key << "\n";
			continue;
		}
		if (!fileIsClean && !fs::exists(clean, ec)) {   // (words laid before clean pictures were kept: render it again to change them)
			++stale;
			continue;
		}
		if (here.empty()) {   // (its words are gone: its clean picture back)
			fs::rename(clean, file, ec);
			if (ec) continue;
			log << i << stampOf(file) << " none\n";
			log.flush();
			++laid;
			continue;
		}
		const fs::path source = fileIsClean ? file : clean;
		int w = 0, h = 0, n = 0;
		unsigned char *px = stbi_load(source.string().c_str(), &w, &h, &n, 3);
		if (!px) continue;
		std::vector<unsigned char> frame(px, px + static_cast<std::size_t>(w) * h * 3);
		stbi_image_free(px);
		for (const auto &[c, a] : here) {
			if (!c->lines.empty()) {   // text: set at this frame's size, once a caption and size
				const std::string key = "text|" + std::to_string(c - captions.data()) + "|" + std::to_string(w) + "|" + std::to_string(h);
				if (!sized.count(key)) sized[key] = c->bubble ? bubbled(textPicture(*c, w, h), h) : textPicture(*c, w, h);
				const Picture &pic = sized[key];
				if (pic.w > 0) over(frame, w, h, pic, (w - pic.w) / 2, static_cast<int>(std::lround(c->centreY * h - pic.h / 2.0)), a);
				continue;
			}
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
		// (written beside it and renamed over it: a run stopped in the middle of the write must not leave half a frame that a resumed video would keep; a clean file becomes the clean
		// picture first, and a run stopped between the two renames leaves the frame missing, so a resumed video renders it again)
		const fs::path tmp = dir / (std::string(name) + ".tmp.png");
		if (!write_png(tmp.string().c_str(), w, h, frame.data())) continue;
		if (fileIsClean) fs::rename(file, clean, ec);
		if (!ec) fs::rename(tmp, file, ec);
		if (ec) {
			fs::remove(tmp, ec);
			continue;
		}
		log << i << stampOf(file) << " " << key << "\n";
		log.flush();
		++laid;
	}
	if (stale > 0)
		std::cerr << "WARNING: " << stale << " frames keep the words they were given before (no clean picture of them was kept); render them again to change their words." << std::endl;
	return laid;
}

}  // namespace film_captions
