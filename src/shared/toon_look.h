#pragma once
// toon_look.h -- a cartoon look for a rendered picture: cel shading (the light on each surface put into a few flat bands), ink lines (round what stands in front of
// something, along sharp creases, against the sky) and stronger colour. Made after the render from the picture and its passes (aov_pass.h: albedo, normal, depth,
// coverage), for any backend.
//
// Shading: the light at a pixel is the picture over its surface's own colour (the albedo pass), smoothed over the surface (an average with its neighbours that does not
// cross a depth jump or a sharp turn, so the render's noise goes and an edge stays), measured against the bright end of the picture's light (its 95th percentile) and put
// into bands - the darkest `shadow` of the brightest, the others evenly up to it, the steps between them a little soft so that they do not stair-step. The picture there is
// the surface's colour (a texture stays: it is in the albedo) in the banded light, with the light's own colour. A surface that only gives light (a lamp, an emissive sky)
// and the background keep their own.
// Lines: where the depth jumps (its inverse is nearly flat across a plane, so a floor seen edge-on draws none), where the normal turns by more than about 55 degrees, and
// where a surface meets the background; drawn on the nearer side, `lines` pixels wide in a picture 1080 high (as wide in proportion in any other), in `ink`.
//
// Set by a scene file's `# @rt-toon [bands N] [lines W] [ink COLOUR] [saturation S] [shadow S]` line (a film writes one into every frame: film_file.h `toon`), or by
// --toon (the scene's style, or the default one). Off by default: a picture without it is the rendered one.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace toon_look {

struct Style {
	bool on = false;
	int bands = 3;                              // 2..6 bands of light
	double lines = 2.0;                         // the ink lines' width in pixels at a picture 1080 high (0: no lines)
	double ink[3] = {0.012, 0.010, 0.018};      // the lines' colour (linear)
	double saturation = 1.2;                    // 1: as rendered
	double shadow = 0.35;                       // the darkest band's light, as a share of the brightest
};

inline double srgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
inline double linearToSrgb(double c) {
	c = std::clamp(c, 0.0, 1.0);
	return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

// The words after `# @rt-toon` (or a film file's `toon`): "" or what is wrong. It turns the look on.
inline std::string parse(const std::string &words, Style &s) {
	std::istringstream in(words);
	s.on = true;
	for (std::string w; in >> w;) {
		std::string v;
		if (!(in >> v)) return "\"" + w + "\" needs a value (bands N, lines W, ink COLOUR, saturation S, shadow S)";
		char *end = nullptr;
		const double x = std::strtod(v.c_str(), &end);
		const bool isNumber = end && *end == '\0';
		if (w == "bands") {
			if (!isNumber || x < 2 || x > 6 || x != std::floor(x)) return "bands is a whole number from 2 to 6";
			s.bands = static_cast<int>(x);
		} else if (w == "lines") {
			if (!isNumber || x < 0 || x > 12) return "lines is a width in pixels from 0 (none) to 12";
			s.lines = x;
		} else if (w == "saturation") {
			if (!isNumber || x < 0 || x > 3) return "saturation is from 0 (grey) to 3";
			s.saturation = x;
		} else if (w == "shadow") {
			if (!isNumber || x < 0 || x > 1) return "shadow is the darkest band's light, from 0 to 1";
			s.shadow = x;
		} else if (w == "ink") {   // #rrggbb or r,g,b (0..1), as a picture's colours are
			double c[3] = {0, 0, 0};
			unsigned r = 0, g = 0, b = 0;
			char extra = 0;
			if (v.size() == 7 && v[0] == '#' && std::sscanf(v.c_str() + 1, "%2x%2x%2x%c", &r, &g, &b, &extra) == 3) {
				c[0] = r / 255.0, c[1] = g / 255.0, c[2] = b / 255.0;
			} else if (std::sscanf(v.c_str(), "%lf,%lf,%lf%c", &c[0], &c[1], &c[2], &extra) != 3 || std::min({c[0], c[1], c[2]}) < 0 || std::max({c[0], c[1], c[2]}) > 1) {
				return "ink is a colour: #rrggbb or r,g,b from 0 to 1";
			}
			for (int k = 0; k < 3; ++k) s.ink[k] = srgbToLinear(c[k]);
		} else {
			return "\"" + w + "\": the toon look takes bands, lines, ink, saturation and shadow";
		}
	}
	return std::string();
}

// The scene-file line that gives a picture this look.
inline std::string tagOf(const Style &s) {
	char buf[200];
	std::snprintf(buf, sizeof buf, "# @rt-toon bands %d lines %g ink %.4f,%.4f,%.4f saturation %g shadow %g\n", s.bands, s.lines, linearToSrgb(s.ink[0]), linearToSrgb(s.ink[1]),
				  linearToSrgb(s.ink[2]), s.saturation, s.shadow);
	return buf;
}

// A scene file's `# @rt-toon` line (before its WorldBegin): true when it has one (and `problem` says what is wrong with it, if anything).
inline bool fromSceneFile(const std::string &path, Style &out, std::string &problem) {
	std::ifstream f(path);
	const std::string tag = "# @rt-toon";
	for (std::string line; std::getline(f, line);) {
		if (line.rfind("WorldBegin", 0) == 0) break;
		if (line.rfind(tag, 0) == 0) {
			problem = parse(line.substr(tag.size()), out);
			return true;
		}
	}
	return false;
}

namespace detail {
inline double smoothstep(double a, double b, double x) {
	const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}
inline double luminance(double r, double g, double b) { return 0.2126 * r + 0.7152 * g + 0.0722 * b; }
}  // namespace detail

// The look on a picture. `rgb`: w*h*3, linear. `planes`: aov_pass.h's ten, w*h each (albedo R G B, normal X Y Z, depth, u, v, coverage).
inline void apply(std::vector<float> &rgb, int w, int h, const std::vector<std::vector<float>> &planes, const Style &st) {
	using detail::smoothstep;
	const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
	if (!st.on || planes.size() < 10 || rgb.size() < n * 3) return;
	for (const std::vector<float> &p : planes)
		if (p.size() < n) return;
	const std::vector<float> &cover = planes[9], &depth = planes[6];
	const auto covered = [&](std::size_t i) { return cover[i] > 0.5f && depth[i] > 0.0f; };
	// ---- the light, smoothed over each surface and banded ----
	// The surface's colour is the albedo pass with its speckles taken out (the median of its 3 x 3 neighbours on the same surface: a glossy surface's albedo is noisy, a
	// texture's edge stays). The light's colour at a pixel is the picture over that colour, its fireflies cut to the picture's 99th percentile, averaged with its neighbours on
	// the same surface (no depth jump, the normal within about 25 degrees) along the rows and the columns, twice, which takes the render's noise out without crossing an
	// edge; and the picture is then the surface's colour in the banded light: flat, whatever the noise was.
	std::vector<unsigned char> shaded(n, 0);   // 0: not shaded (the background, a surface that only gives light)
	for (std::size_t i = 0; i < n; ++i) shaded[i] = covered(i) && detail::luminance(planes[0][i], planes[1][i], planes[2][i]) >= 0.02;
	const auto sameSurface = [&](std::size_t i, std::size_t j) {
		if (!shaded[j]) return false;
		const double a = 1.0 / depth[i], b = 1.0 / depth[j];
		const double dot = planes[3][i] * planes[3][j] + planes[4][i] * planes[4][j] + planes[5][i] * planes[5][j];
		return std::abs(a - b) <= 0.03 * a && dot > 0.9;
	};
	std::vector<float> albedo(n * 3, 0.0f);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const std::size_t i = static_cast<std::size_t>(y) * w + x;
			if (!shaded[i]) continue;
			float values[3][9];
			int count = 0;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx) {
					const int xx = x + dx, yy = y + dy;
					if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
					const std::size_t j = static_cast<std::size_t>(yy) * w + xx;
					if (j != i && !sameSurface(i, j)) continue;
					for (int c = 0; c < 3; ++c) values[c][count] = planes[static_cast<std::size_t>(c)][j];
					++count;
				}
			for (int c = 0; c < 3; ++c) {
				std::nth_element(values[c], values[c] + count / 2, values[c] + count);
				albedo[3 * i + c] = std::max(0.02f, values[c][count / 2]);
			}
		}
	std::vector<float> light(n * 3, 0.0f), lit;
	for (std::size_t i = 0; i < n; ++i)
		if (shaded[i]) {
			for (int c = 0; c < 3; ++c) light[3 * i + c] = rgb[3 * i + c] / albedo[3 * i + c];
			lit.push_back(static_cast<float>(detail::luminance(light[3 * i], light[3 * i + 1], light[3 * i + 2])));
		}
	if (!lit.empty()) {
		const auto percentile = [&](double q) {
			std::vector<float> v = lit;
			const std::size_t at = static_cast<std::size_t>(q * static_cast<double>(v.size() - 1));
			std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(at), v.end());
			return static_cast<double>(v[at]);
		};
		const double ceiling = std::max(1e-6, percentile(0.99));
		for (std::size_t i = 0; i < n; ++i) {
			if (!shaded[i]) continue;
			const double L = detail::luminance(light[3 * i], light[3 * i + 1], light[3 * i + 2]);
			if (L > ceiling)
				for (int c = 0; c < 3; ++c) light[3 * i + c] = static_cast<float>(light[3 * i + c] * ceiling / L);
		}
		const int blur = std::max(2, h / 90);
		for (int pass = 0; pass < 4; ++pass) {   // the rows, the columns, and again
			std::vector<float> out = light;
			for (int y = 0; y < h; ++y)
				for (int x = 0; x < w; ++x) {
					const std::size_t i = static_cast<std::size_t>(y) * w + x;
					if (!shaded[i]) continue;
					double sum[3] = {0, 0, 0}, count = 0;
					for (int k = -blur; k <= blur; ++k) {
						const int xx = pass % 2 == 0 ? x + k : x, yy = pass % 2 == 0 ? y : y + k;
						if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
						const std::size_t j = static_cast<std::size_t>(yy) * w + xx;
						if (j != i && !sameSurface(i, j)) continue;
						for (int c = 0; c < 3; ++c) sum[c] += light[3 * j + c];
						count += 1.0;
					}
					for (int c = 0; c < 3; ++c) out[3 * i + c] = static_cast<float>(sum[c] / count);
				}
			light.swap(out);
		}
		lit.clear();
		for (std::size_t i = 0; i < n; ++i)
			if (shaded[i]) lit.push_back(static_cast<float>(detail::luminance(light[3 * i], light[3 * i + 1], light[3 * i + 2])));
		const double ref = std::max(1e-6, percentile(0.95));
		const double soft = 0.04;
		for (std::size_t i = 0; i < n; ++i) {
			if (!shaded[i]) continue;
			const double L = detail::luminance(light[3 * i], light[3 * i + 1], light[3 * i + 2]);
			const double x = L / ref;
			double steps = 0.0;
			for (int k = 1; k < st.bands; ++k) steps += smoothstep(static_cast<double>(k) / st.bands - soft, static_cast<double>(k) / st.bands + soft, x);
			const double band = (st.shadow + (1.0 - st.shadow) * steps / (st.bands - 1)) * ref;
			for (int c = 0; c < 3; ++c) {   // the surface's colour in light of the band's brightness and the light's own colour (white where there is none to tell)
				const double tint = L > 1e-6 * ref ? std::min(4.0, light[3 * i + c] / L) : 1.0;
				rgb[3 * i + c] = static_cast<float>(albedo[3 * i + c] * tint * band);
			}
		}
	}
	// ---- the colour ----
	if (st.saturation != 1.0)
		for (std::size_t i = 0; i < n; ++i) {
			const double y = detail::luminance(rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2]);
			for (int c = 0; c < 3; ++c) rgb[3 * i + c] = static_cast<float>(std::max(0.0, y + (rgb[3 * i + c] - y) * st.saturation));
		}
	// ---- the lines ----
	if (st.lines <= 0.0) return;
	std::vector<float> edge(n, 0.0f);
	const auto inv = [&](int x, int y) {
		const std::size_t i = static_cast<std::size_t>(y) * w + x;
		return covered(i) ? 1.0 / depth[i] : 0.0;
	};
	for (int y = 1; y + 1 < h; ++y)
		for (int x = 1; x + 1 < w; ++x) {
			const std::size_t i = static_cast<std::size_t>(y) * w + x;
			if (!covered(i)) continue;
			const std::size_t nb[4] = {i - 1, i + 1, i - static_cast<std::size_t>(w), i + static_cast<std::size_t>(w)};
			double e = 0.0;
			for (std::size_t j : nb)
				if (!covered(j)) e = 1.0;   // against the background
			// the depth: a jump in its inverse, drawn on the nearer side
			const double c = inv(x, y), l = inv(x - 1, y), r = inv(x + 1, y), u = inv(x, y - 1), d = inv(x, y + 1);
			const double bend = std::max(std::abs(l + r - 2.0 * c), std::abs(u + d - 2.0 * c)) / c;
			if (c >= 0.5 * std::min(l + r, u + d)) e = std::max(e, smoothstep(0.04, 0.12, bend));
			// a crease
			double turn = 0.0;
			for (std::size_t j : nb) {
				if (!covered(j)) continue;
				const double dot = planes[3][i] * planes[3][j] + planes[4][i] * planes[4][j] + planes[5][i] * planes[5][j];
				turn = std::max(turn, 1.0 - dot);
			}
			e = std::max(e, smoothstep(0.35, 0.55, turn));
			edge[i] = static_cast<float>(e);
		}
	const double radius = std::max(0.75, 0.5 * st.lines * h / 1080.0);   // (at least about a pixel and a half wide: a thinner line is lost)
	const int reach = static_cast<int>(std::ceil(radius + 0.5));
	std::vector<float> ink(n, 0.0f);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			double m = 0.0;
			for (int dy = -reach; dy <= reach; ++dy)
				for (int dx = -reach; dx <= reach; ++dx) {
					const int xx = x + dx, yy = y + dy;
					if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
					const float e = edge[static_cast<std::size_t>(yy) * w + xx];
					if (e <= 0.0f) continue;
					m = std::max(m, e * std::clamp(radius + 0.5 - std::sqrt(static_cast<double>(dx * dx + dy * dy)), 0.0, 1.0));
				}
			ink[static_cast<std::size_t>(y) * w + x] = static_cast<float>(m);
		}
	for (std::size_t i = 0; i < n; ++i)
		for (int c = 0; c < 3; ++c) rgb[3 * i + c] = static_cast<float>(rgb[3 * i + c] + (st.ink[c] - rgb[3 * i + c]) * ink[i]);
}

}  // namespace toon_look
