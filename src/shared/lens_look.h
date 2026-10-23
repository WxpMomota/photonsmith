#pragma once
// lens_look.h -- what a film camera and its grading add to a picture: bloom (the glow a bright light spreads through the lens), a vignette (darker corners), warmth (a
// white balance towards amber or blue) and saturation. Applied to the LINEAR picture, after the render and the denoiser and before the tone map, so a bright light's glow
// is as strong as the light really is (a glow added after the tone map is the same for a candle and the sun). Every step is linear in the light except the vignette's and
// the bloom's spreading, both of which keep the picture's total light (the bloom moves light, it does not add any): exposure commutes with all of it.
//
// Set by flags (--bloom, --vignette, --warmth, --saturation) or by a scene file's own line `# @rt-look bloom 0.2 vignette 0.3 warmth 0.1 saturation 1.1` (any of the four,
// in any order), which is how a film gives its stills and videos one look; a flag wins over the file. EXR output stays raw (it is for compositing). Off by default: a picture
// with no look is the rendered one, byte for byte.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ppm_text.h"

namespace lens_look {

struct Look {
	double bloom = 0.0;         // 0..1: the share of the light the lens spreads into a glow (0.1 subtle, 0.3 dreamy)
	double bloomRadius = 0.05;  // the widest glow's reach, as a share of the picture's width
	double vignette = 0.0;      // 0..1: how much darker the corners are than the centre
	double warmth = 0.0;        // -1..1: towards blue (cool, night) or amber (warm, dawn); the picture's brightness stays the same
	double saturation = 1.0;    // 0 = grey, 1 = as rendered, above 1 = more colourful
	bool active() const { return bloom > 0.0 || vignette > 0.0 || warmth != 0.0 || saturation != 1.0; }
};

// Values that were set (a flag, a scene line) override `base`; a negative bloom/vignette or a saturation below 0 means "not given" in `given`.
struct Given {
	double bloom = -1.0, vignette = -1.0, warmth = -2.0, saturation = -1.0;
};
inline Look merged(Look base, const Given &g) {
	if (g.bloom >= 0.0) base.bloom = g.bloom;
	if (g.vignette >= 0.0) base.vignette = g.vignette;
	if (g.warmth >= -1.0) base.warmth = g.warmth;
	if (g.saturation >= 0.0) base.saturation = g.saturation;
	return base;
}

// Parses "bloom 0.2 vignette 0.3 ..." into `out` (unmentioned values are left as they are). Returns what was wrong, or "" when all of it was understood.
inline std::string parse(const std::string &text, Look &out) {
	std::istringstream in(text);
	std::string word;
	while (in >> word) {
		double v = 0.0;
		if (!(in >> v) || !std::isfinite(v)) return "\"" + word + "\" needs a number after it";
		if (word == "bloom") out.bloom = std::clamp(v, 0.0, 1.0);
		else if (word == "bloom-radius") out.bloomRadius = std::clamp(v, 0.001, 0.5);
		else if (word == "vignette") out.vignette = std::clamp(v, 0.0, 1.0);
		else if (word == "warmth") out.warmth = std::clamp(v, -1.0, 1.0);
		else if (word == "saturation") out.saturation = std::clamp(v, 0.0, 3.0);
		else return "unknown word \"" + word + "\" (bloom, bloom-radius, vignette, warmth, saturation)";
	}
	return "";
}

// A scene file's `# @rt-look ...` line (looked for before WorldBegin, like the other @rt- header lines). False when the file has none; `problem` says what could not be read.
inline bool fromSceneFile(const std::string &path, Look &out, std::string &problem) {
	std::ifstream f(path);
	std::string line;
	const std::string tag = "# @rt-look";
	while (std::getline(f, line)) {
		if (line.rfind("WorldBegin", 0) == 0) break;
		if (line.rfind(tag, 0) == 0) {
			problem = parse(line.substr(tag.size()), out);
			return true;
		}
	}
	return false;
}

namespace detail {

// One box blur of half-width r along rows (stride 1) or columns, in place, for the three channels; the edges repeat the border pixel.
inline void boxPass(std::vector<float> &img, int w, int h, int r, bool rows) {
	if (r <= 0) return;
	const int lines = rows ? h : w, len = rows ? w : h;
	const std::size_t step = rows ? 3 : static_cast<std::size_t>(w) * 3;
	ppm_text::parallel_ranges(static_cast<std::size_t>(lines), [&](std::size_t begin, std::size_t end) {
		std::vector<float> src(static_cast<std::size_t>(len) * 3);
		for (std::size_t l = begin; l < end; ++l) {
			const std::size_t base = rows ? l * static_cast<std::size_t>(w) * 3 : l * 3;
			for (int i = 0; i < len; ++i)
				for (int c = 0; c < 3; ++c) src[static_cast<std::size_t>(i) * 3 + c] = img[base + static_cast<std::size_t>(i) * step + c];
			for (int c = 0; c < 3; ++c) {
				const auto at = [&](int i) { return src[static_cast<std::size_t>(std::clamp(i, 0, len - 1)) * 3 + c]; };
				double sum = 0.0;
				for (int i = -r; i <= r; ++i) sum += at(i);
				const double inv = 1.0 / (2 * r + 1);
				for (int i = 0; i < len; ++i) {
					img[base + static_cast<std::size_t>(i) * step + c] = static_cast<float>(sum * inv);
					sum += at(i + r + 1) - at(i - r);
				}
			}
		}
	}, 1);
}

// A Gaussian of standard deviation `sigma` pixels, as three box blurs in each direction.
inline void gaussian(std::vector<float> &img, int w, int h, double sigma) {
	const int r = (std::max)(0, static_cast<int>(std::lround((std::sqrt(4.0 * sigma * sigma + 1.0) - 1.0) / 2.0)));
	for (int pass = 0; pass < 3; ++pass) {
		boxPass(img, w, h, r, true);
		boxPass(img, w, h, r, false);
	}
}

}  // namespace detail

// Applies `look` to a linear RGB picture (w x h x 3 floats, rows from the top) in place. Non-finite values are treated as black first, as the writers do.
inline void apply(float *rgb, int w, int h, const Look &look) {
	if (!look.active() || w <= 0 || h <= 0) return;
	const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
	for (std::size_t i = 0; i < n * 3; ++i)
		if (!std::isfinite(rgb[i])) rgb[i] = 0.0f;

	if (look.bloom > 0.0) {
		// The glow is three Gaussians of the whole picture (a tight halo, a middle one and a wide haze, like a real lens's veiling glare), mixed in with weight `bloom`.
		const double widest = look.bloomRadius * w;
		const double sigmas[3] = {widest / 16.0, widest / 4.0, widest};
		const double weights[3] = {0.5, 0.3, 0.2};
		std::vector<double> glow(n * 3, 0.0);
		for (int k = 0; k < 3; ++k) {
			std::vector<float> blurred(rgb, rgb + n * 3);
			detail::gaussian(blurred, w, h, (std::max)(0.5, sigmas[k]));
			for (std::size_t i = 0; i < n * 3; ++i) glow[i] += weights[k] * blurred[i];
		}
		const double b = look.bloom;
		for (std::size_t i = 0; i < n * 3; ++i) rgb[i] = static_cast<float>((1.0 - b) * rgb[i] + b * glow[i]);
	}

	// Warmth: a white balance (amber gains red and loses blue), scaled so that white keeps its brightness.
	double gain[3] = {1.0, 1.0, 1.0};
	if (look.warmth != 0.0) {
		gain[0] = 1.0 + 0.15 * look.warmth;
		gain[1] = 1.0 + 0.03 * look.warmth;
		gain[2] = 1.0 - 0.20 * look.warmth;
		const double y = 0.2126 * gain[0] + 0.7152 * gain[1] + 0.0722 * gain[2];
		for (double &g : gain) g /= y;
	}
	const double s = look.saturation, v = look.vignette;
	const double cx = 0.5 * w, cy = 0.5 * h, halfDiag2 = cx * cx + cy * cy;
	ppm_text::parallel_ranges(n, [&](std::size_t begin, std::size_t end) {
		for (std::size_t i = begin; i < end; ++i) {
			double c[3];
			for (int k = 0; k < 3; ++k) c[k] = rgb[i * 3 + k] * gain[k];
			if (s != 1.0) {
				const double y = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
				for (double &ck : c) ck = (std::max)(0.0, y + s * (ck - y));
			}
			if (v > 0.0) {
				// Darker towards the corners: 1 at the centre, 1 - v in a corner, falling off like a lens (slowly at first).
				const double px = (static_cast<double>(i % static_cast<std::size_t>(w)) + 0.5) - cx, py = (static_cast<double>(i / static_cast<std::size_t>(w)) + 0.5) - cy;
				const double r2 = (px * px + py * py) / halfDiag2;
				const double f = 1.0 - v * r2 * r2 * (3.0 - 2.0 * std::sqrt(r2));
				for (double &ck : c) ck *= (std::max)(0.0, f);
			}
			for (int k = 0; k < 3; ++k) rgb[i * 3 + k] = static_cast<float>(c[k]);
		}
	});
}

}  // namespace lens_look
