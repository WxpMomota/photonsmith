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
// And, as Blender's Line Art draws them from the geometry (modifiers/intern/lineart: its "material border" and "intersection" edges), where one material meets another
// (`materials on`) and where one object meets another with no depth jump between them - a character's foot on the floor, two parts that pass through each other
// (`objects on`): from the ID passes (aov_pass.h renderIds; an object is a link group, a film's characters each their own). The silhouette (against the background or
// a depth jump) keeps the full width and the lines inside a shape - creases, materials, objects - are `inner` times as wide, the cartoon convention of a bolder outline.
//
// Cel shading per material, as Blender's NPR materials do it (a Shader to RGB into a Color Ramp in each material): `shadow-tint COLOUR` colours the shadow bands
// (a cartoon's shadows are tinted - purple, blue - not only darker), `highlight H` puts a flat glossy spot where the light is above H of the brightest, in
// `highlight-colour`; and `material NAME ...` or `object NAME ...` starts a ramp of a material's or an object's own (its bands, shadow, tint and highlight: the words
// after it, up to the next), found through the ID passes (aov_pass.h renderIds: a material by its scene name, an object by its link group - a film's character is
// `cast/<name>`). A material's own ramp wins over its object's, and either over the picture's.
// Lines drawn as by hand, after Blender's Freestyle line styles (ink_strokes.h): `line-depth D` thins the far lines (0..1), `wobble W` lets them wander W pixels (at
// 1080), `boil on` draws the wobble afresh every other frame (a film writes the frame's `seed`), `pen ANGLE` makes them thick and thin as a broad pen held at that angle
// (`pen-amount A`, 0..1, how thin along the pen), and `ink-tint T` colours them from the surface they edge (0 the ink, 1 the surface's own colour, darkened).
// Set by a scene file's `# @rt-toon [bands N] [lines W] [ink COLOUR] [saturation S] [shadow S]` line (a film writes one into every frame: film_file.h `toon`), or by
// --toon (the scene's style, or the default one). Off by default: a picture without it is the rendered one.

#include <algorithm>
#include <cmath>
#include <utility>

#include "cryptomatte.h"   // a material's or object's ramp found by its name's ID (the ID passes)
#include "ink_strokes.h"   // lines drawn as by hand
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
	double inner = 1.0;                         // the lines inside a shape (creases, materials, objects) as a share of the silhouette's width
	bool materials = false;                     // lines where materials meet (the ID passes)
	bool objects = false;                       // lines where objects meet with no depth jump (the ID passes)
	ink_strokes::Params strokes;                // the lines as drawn by hand (line-depth, wobble, seed, pen, pen-amount)
	bool boil = false;                          // a film draws the wobble afresh every other frame (its frames' `seed`)
	double inkTint = 0.0;                       // 0..1: the lines coloured from the surface they edge
	// the shading's ramp: the picture's, and the materials' and objects' own
	struct Ramp {
		std::string name;                            // the material's or object's name (empty: the picture's own)
		bool object = false;                         // an object's (its link group), else a material's
		int bands = 3;
		double shadow = 0.35;
		double shadowTint[3] = {1.0, 1.0, 1.0};      // the darkest band's colour cast (linear; 1: none)
		double highlight = 0.0;                      // 0: none; else the share of the brightest light above which the surface is highlight-colour
		double highlightColour[3] = {1.0, 1.0, 1.0}; // (linear)
		bool tinted() const { return shadowTint[0] != 1.0 || shadowTint[1] != 1.0 || shadowTint[2] != 1.0; }
	};
	double shadowTint[3] = {1.0, 1.0, 1.0};
	double highlight = 0.0;
	double highlightColour[3] = {1.0, 1.0, 1.0};
	std::vector<Ramp> ramps;                     // `material NAME ...` / `object NAME ...`
	bool needsIds() const { return materials || objects || !ramps.empty(); }
	// The picture's own ramp.
	Ramp base() const {
		Ramp r;
		r.bands = bands, r.shadow = shadow, r.highlight = highlight;
		for (int k = 0; k < 3; ++k) r.shadowTint[k] = shadowTint[k], r.highlightColour[k] = highlightColour[k];
		return r;
	}
};

inline double srgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
inline double linearToSrgb(double c) {
	c = std::clamp(c, 0.0, 1.0);
	return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

// A colour as the toon words write it: #rrggbb or r,g,b from 0 to 1 (sRGB, as a picture's colours are), into linear `out`. False when it is not one.
inline bool colourOf(const std::string &v, double out[3]) {
	double c[3] = {0, 0, 0};
	unsigned r = 0, g = 0, b = 0;
	char extra = 0;
	if (v.size() == 7 && v[0] == '#' && std::sscanf(v.c_str() + 1, "%2x%2x%2x%c", &r, &g, &b, &extra) == 3) {
		c[0] = r / 255.0, c[1] = g / 255.0, c[2] = b / 255.0;
	} else if (std::sscanf(v.c_str(), "%lf,%lf,%lf%c", &c[0], &c[1], &c[2], &extra) != 3 || std::min({c[0], c[1], c[2]}) < 0 || std::max({c[0], c[1], c[2]}) > 1) {
		return false;
	}
	for (int k = 0; k < 3; ++k) out[k] = srgbToLinear(c[k]);
	return true;
}

// The words after `# @rt-toon` (or a film file's `toon`): "" or what is wrong. It turns the look on.
inline std::string parse(const std::string &words, Style &s) {
	std::istringstream in(words);
	s.on = true;
	Style::Ramp *ramp = nullptr;   // after `material NAME` / `object NAME`: the words are that ramp's
	for (std::string w; in >> w;) {
		std::string v;
		if (!(in >> v)) return "\"" + w + "\" needs a value (bands N, lines W, ink COLOUR, saturation S, shadow S, inner I, materials on|off, objects on|off, shadow-tint COLOUR, "
							  "highlight H, highlight-colour COLOUR, material NAME, object NAME)";
		char *end = nullptr;
		const double x = std::strtod(v.c_str(), &end);
		const bool isNumber = end && *end == '\0';
		if (w == "material" || w == "object") {
			Style::Ramp r = s.base();
			r.name = v;
			r.object = w == "object";
			s.ramps.push_back(r);
			ramp = &s.ramps.back();
			continue;
		}
		if (ramp && w != "bands" && w != "shadow" && w != "shadow-tint" && w != "highlight" && w != "highlight-colour")
			return "\"" + w + "\" is the picture's, not a material's: put it before the first `material` or `object` (a ramp takes bands, shadow, shadow-tint, highlight, highlight-colour)";
		if (w == "shadow-tint" || w == "highlight-colour") {
			double c[3];
			if (!colourOf(v, c)) return w + " is a colour: #rrggbb or r,g,b from 0 to 1";
			double *dst = w == "shadow-tint" ? (ramp ? ramp->shadowTint : s.shadowTint) : (ramp ? ramp->highlightColour : s.highlightColour);
			for (int k = 0; k < 3; ++k) dst[k] = c[k];
		} else if (w == "highlight") {
			if (!isNumber || x < 0 || x > 1) return "highlight is the share of the brightest light above which a surface is highlight-colour, from 0 (none) to 1";
			(ramp ? ramp->highlight : s.highlight) = x;
		} else if (w == "bands") {
			if (!isNumber || x < 2 || x > 6 || x != std::floor(x)) return "bands is a whole number from 2 to 6";
			(ramp ? ramp->bands : s.bands) = static_cast<int>(x);
		} else if (w == "lines") {
			if (!isNumber || x < 0 || x > 12) return "lines is a width in pixels from 0 (none) to 12";
			s.lines = x;
		} else if (w == "saturation") {
			if (!isNumber || x < 0 || x > 3) return "saturation is from 0 (grey) to 3";
			s.saturation = x;
		} else if (w == "shadow") {
			if (!isNumber || x < 0 || x > 1) return "shadow is the darkest band's light, from 0 to 1";
			(ramp ? ramp->shadow : s.shadow) = x;
		} else if (w == "inner") {
			if (!isNumber || x < 0 || x > 1) return "inner is the lines inside a shape as a share of the outline's width, from 0 (none) to 1";
			s.inner = x;
		} else if (w == "materials" || w == "objects") {
			if (v != "on" && v != "off") return w + " is on or off";
			(w == "materials" ? s.materials : s.objects) = v == "on";
		} else if (w == "ink") {   // #rrggbb or r,g,b (0..1), as a picture's colours are
			if (!colourOf(v, s.ink)) return "ink is a colour: #rrggbb or r,g,b from 0 to 1";
		} else if (w == "line-depth" || w == "pen-amount" || w == "ink-tint") {
			if (!isNumber || x < 0 || x > 1) return w + " is from 0 to 1";
			(w == "line-depth" ? s.strokes.depthThin : w == "pen-amount" ? s.strokes.penAmount : s.inkTint) = x;
		} else if (w == "wobble") {
			if (!isNumber || x < 0 || x > 20) return "wobble is how far a line wanders, in pixels (at 1080 high) from 0 to 20";
			s.strokes.wobble = x;
		} else if (w == "pen") {
			if (!isNumber || x < 0 || x >= 360) return "pen is the pen's angle in degrees, from 0 to 360";
			s.strokes.pen = x;
		} else if (w == "boil") {
			if (v != "on" && v != "off") return "boil is on or off";
			s.boil = v == "on";
		} else if (w == "seed") {
			if (!isNumber || x != std::floor(x)) return "seed is a whole number";
			s.strokes.seed = static_cast<int>(x);
		} else {
			return "\"" + w + "\": the toon look takes bands, lines, ink, saturation, shadow, inner, materials, objects, line-depth, wobble, boil, seed, pen, pen-amount, ink-tint, "
					"shadow-tint, highlight, highlight-colour, material and object";
		}
	}
	return std::string();
}

// The scene-file line that gives a picture this look.
inline std::string tagOf(const Style &s) {
	char buf[260];
	std::snprintf(buf, sizeof buf, "# @rt-toon bands %d lines %g ink %.4f,%.4f,%.4f saturation %g shadow %g", s.bands, s.lines, linearToSrgb(s.ink[0]), linearToSrgb(s.ink[1]),
				  linearToSrgb(s.ink[2]), s.saturation, s.shadow);
	std::string tag = buf;   // (the line words only when they are not the default: a film without them keeps its frames' text)
	if (s.inner != 1.0) {
		std::snprintf(buf, sizeof buf, " inner %g", s.inner);
		tag += buf;
	}
	if (s.materials) tag += " materials on";
	if (s.objects) tag += " objects on";
	const auto numberWord = [&](const char *word, double v) {
		std::snprintf(buf, sizeof buf, " %s %g", word, v);
		return std::string(buf);
	};
	if (s.strokes.depthThin > 0.0) tag += numberWord("line-depth", s.strokes.depthThin);
	if (s.strokes.wobble > 0.0) tag += numberWord("wobble", s.strokes.wobble);
	if (s.boil) tag += " boil on";
	if (s.strokes.seed != 0) tag += " seed " + std::to_string(s.strokes.seed);
	if (s.strokes.pen >= 0.0) tag += numberWord("pen", s.strokes.pen) + numberWord("pen-amount", s.strokes.penAmount);
	if (s.inkTint > 0.0) tag += numberWord("ink-tint", s.inkTint);
	const auto colourWords = [&](const char *word, const double c[3]) {
		std::snprintf(buf, sizeof buf, " %s %.4f,%.4f,%.4f", word, linearToSrgb(c[0]), linearToSrgb(c[1]), linearToSrgb(c[2]));
		return std::string(buf);
	};
	const auto rampWords = [&](const Style::Ramp &r, const Style::Ramp &over) {   // (the words that differ from `over`)
		std::string t;
		if (r.bands != over.bands) t += " bands " + std::to_string(r.bands);
		if (r.shadow != over.shadow) {
			std::snprintf(buf, sizeof buf, " shadow %g", r.shadow);
			t += buf;
		}
		if (r.tinted() || over.tinted()) t += colourWords("shadow-tint", r.shadowTint);
		if (r.highlight != over.highlight) {
			std::snprintf(buf, sizeof buf, " highlight %g", r.highlight);
			t += buf;
		}
		if (r.highlight > 0.0) t += colourWords("highlight-colour", r.highlightColour);
		return t;
	};
	Style::Ramp plain = s.base();   // (the picture's bands and shadow are written above: only its tint and highlight here)
	plain.highlight = 0.0;
	for (int k = 0; k < 3; ++k) plain.shadowTint[k] = 1.0;
	tag += rampWords(s.base(), plain);
	for (const Style::Ramp &r : s.ramps) tag += std::string(r.object ? " object " : " material ") + r.name + rampWords(r, s.base());
	return tag + "\n";
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

// The look on a picture. `rgb`: w*h*3, linear. `planes`: aov_pass.h's ten, w*h each (albedo R G B, normal X Y Z, depth, u, v, coverage), and for the lines between
// materials and objects two more: id.object and id.material (aov_pass.h renderIds).
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
		// each pixel's ramp: its material's own, else its object's, else the picture's (the ID planes: a ramp's name hashed as they are, cryptomatte.h)
		const Style::Ramp pictureRamp = st.base();
		std::vector<std::pair<float, const Style::Ramp *>> materialRamps, objectRamps;
		for (const Style::Ramp &r : st.ramps) (r.object ? objectRamps : materialRamps).emplace_back(cryptomatte::idToFloat(cryptomatte::murmur3_32(r.name)), &r);
		const bool haveIds = planes.size() >= 12 && planes[10].size() >= n && planes[11].size() >= n;
		const auto rampAt = [&](std::size_t i) -> const Style::Ramp & {
			if (haveIds) {
				for (const auto &[id, r] : materialRamps)
					if (planes[11][i] == id) return *r;
				for (const auto &[id, r] : objectRamps)
					if (planes[10][i] == id) return *r;
			}
			return pictureRamp;
		};
		for (std::size_t i = 0; i < n; ++i) {
			if (!shaded[i]) continue;
			const Style::Ramp &rp = rampAt(i);
			const double L = detail::luminance(light[3 * i], light[3 * i + 1], light[3 * i + 2]);
			const double x = L / ref;
			double steps = 0.0;
			for (int k = 1; k < rp.bands; ++k) steps += smoothstep(static_cast<double>(k) / rp.bands - soft, static_cast<double>(k) / rp.bands + soft, x);
			const double level = rp.shadow + (1.0 - rp.shadow) * steps / (rp.bands - 1);   // (from rp.shadow, the darkest band, to 1)
			const double band = level * ref;
			// the shadow's tint: all of it in the darkest band, none in the brightest
			const double inShadow = rp.shadow < 1.0 ? std::clamp((1.0 - level) / (1.0 - rp.shadow), 0.0, 1.0) : 0.0;
			// the highlight: a flat spot where the light is above its share of the brightest
			const double spot = rp.highlight > 0.0 ? smoothstep(rp.highlight - soft, rp.highlight + soft, x) : 0.0;
			for (int c = 0; c < 3; ++c) {   // the surface's colour in light of the band's brightness and the light's own colour (white where there is none to tell)
				const double tint = L > 1e-6 * ref ? std::min(4.0, light[3 * i + c] / L) : 1.0;
				const double shade = 1.0 + (rp.shadowTint[c] - 1.0) * inShadow;
				const double v = albedo[3 * i + c] * tint * band * shade;
				rgb[3 * i + c] = static_cast<float>(v + (rp.highlightColour[c] * ref - v) * spot);
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
	std::vector<float> edge(n, 0.0f), innerEdge(n, 0.0f);   // the silhouette's, and the lines inside a shape (the same map when they are as wide)
	const bool apart = st.inner != 1.0;
	const bool ids = planes.size() >= 12 && planes[10].size() >= n && planes[11].size() >= n;
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
			double in = smoothstep(0.35, 0.55, turn);
			// where one material, or one object, meets another (the ID passes): drawn on the nearer side, as the depth's lines are
			if (ids && (st.materials || st.objects))
				for (std::size_t j : nb) {
					if (!covered(j) || depth[j] < depth[i]) continue;
					if ((st.materials && planes[11][j] != planes[11][i]) || (st.objects && planes[10][j] != planes[10][i])) in = 1.0;
				}
			if (apart) {
				edge[i] = static_cast<float>(e);
				innerEdge[i] = static_cast<float>(in);
			} else {
				edge[i] = static_cast<float>(std::max(e, in));
			}
		}
	std::vector<float> ink(n, 0.0f);
	// the edge map drawn as round strokes `width` pixels wide (in a picture 1080 high), into `ink`
	const auto stroke = [&](const std::vector<float> &edges, double width) {
		const double radius = std::max(0.75, 0.5 * width * h / 1080.0);   // (at least about a pixel and a half wide: a thinner line is lost)
		const int reach = static_cast<int>(std::ceil(radius + 0.5));
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				double m = ink[static_cast<std::size_t>(y) * w + x];
				for (int dy = -reach; dy <= reach; ++dy)
					for (int dx = -reach; dx <= reach; ++dx) {
						const int xx = x + dx, yy = y + dy;
						if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
						const float e = edges[static_cast<std::size_t>(yy) * w + xx];
						if (e <= 0.0f) continue;
						m = std::max(m, e * std::clamp(radius + 0.5 - std::sqrt(static_cast<double>(dx * dx + dy * dy)), 0.0, 1.0));
					}
				ink[static_cast<std::size_t>(y) * w + x] = static_cast<float>(m);
			}
	};
	if (st.strokes.active()) {   // drawn as by hand (ink_strokes.h): wandering, thinner far off, thick and thin with the pen
		const double radius = std::max(0.75, 0.5 * st.lines * h / 1080.0);
		const std::vector<float> e = ink_strokes::wobbled(edge, w, h, st.strokes);
		ink_strokes::draw(ink, e, ink_strokes::widths(e, depth, w, h, st.strokes), w, h, radius);
		if (apart && st.inner > 0.0) {
			const std::vector<float> in = ink_strokes::wobbled(innerEdge, w, h, st.strokes);
			ink_strokes::draw(ink, in, ink_strokes::widths(in, depth, w, h, st.strokes), w, h, radius * st.inner);
		}
	} else {
		stroke(edge, st.lines);
		if (apart && st.inner > 0.0) stroke(innerEdge, st.lines * st.inner);
	}
	for (std::size_t i = 0; i < n; ++i) {
		double colour[3] = {st.ink[0], st.ink[1], st.ink[2]};
		if (st.inkTint > 0.0 && ink[i] > 0.0f)   // (the surface's own colour, deep: a quarter of its light, a little more saturated)
			for (int c = 0; c < 3; ++c) colour[c] += st.inkTint * (0.25 * rgb[3 * i + c] - colour[c]);
		for (int c = 0; c < 3; ++c) rgb[3 * i + c] = static_cast<float>(rgb[3 * i + c] + (colour[c] - rgb[3 * i + c]) * ink[i]);
	}
}

}  // namespace toon_look
