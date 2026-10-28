#pragma once
// lens_look.h -- what a film camera and its grading add to a picture: bloom (the glow a bright light spreads through the lens), a lens flare (a streak and ghosts of the
// brightest lights), a vignette (darker corners), warmth (a white balance towards amber or blue) and saturation. Applied to the LINEAR picture, after the render and the denoiser and before the tone map, so a bright light's glow
// is as strong as the light really is (a glow added after the tone map is the same for a candle and the sun). Every step is linear in the light except the vignette's and
// the bloom's spreading, both of which keep the picture's total light (the bloom moves light, it does not add any): exposure commutes with all of it.
//
// Set by flags (--bloom, --vignette, --warmth, --saturation) or by a scene file's own line `# @rt-look bloom 0.2 vignette 0.3 warmth 0.1 saturation 1.1` (any of the four,
// in any order), which is how a film gives its stills and videos one look; a flag wins over the file. EXR output stays raw (it is for compositing). Off by default: a picture
// with no look is the rendered one, byte for byte.
//
// A grade too (a shot's own colour, from a film's cameras file: shot_cameras.h): `lift R G B` (added light, lifting the shadows), `gamma G` (c^(1/G): above 1 brightens the
// middle), `gain R G B` (multiplied) and `tint R G B` (a colour cast, scaled to keep the brightness) - each one number or three (red, green, blue).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ppm_text.h"

namespace lens_look {

struct Look {
	double bloom = 0.0;         // 0..1: the share of the light the lens spreads into a glow (0.1 subtle, 0.3 dreamy)
	double bloomRadius = 0.05;  // the widest glow's reach, as a share of the picture's width
	double flare = 0.0;         // 0..1: a lens flare from the brightest lights (above four times white): a streak sideways and ghosts mirrored through the centre
	double vignette = 0.0;      // 0..1: how much darker the corners are than the centre
	double warmth = 0.0;        // -1..1: towards blue (cool, night) or amber (warm, dawn); the picture's brightness stays the same
	double saturation = 1.0;    // 0 = grey, 1 = as rendered, above 1 = more colourful
	// the grade: c' = (max(0, c * gain * tint + lift))^(1 / gamma), per channel
	double lift[3] = {0.0, 0.0, 0.0};
	double gamma[3] = {1.0, 1.0, 1.0};
	double gain[3] = {1.0, 1.0, 1.0};
	double tint[3] = {1.0, 1.0, 1.0};
	bool graded() const {
		for (int k = 0; k < 3; ++k)
			if (lift[k] != 0.0 || gamma[k] != 1.0 || gain[k] != 1.0 || tint[k] != 1.0) return true;
		return false;
	}
	bool active() const { return bloom > 0.0 || flare > 0.0 || vignette > 0.0 || warmth != 0.0 || saturation != 1.0 || graded(); }
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
		if (word == "lift" || word == "gamma" || word == "gain" || word == "tint") {   // one number (all three channels) or three (red, green, blue)
			double v[3];
			int n = 0;
			for (; n < 3; ++n) {
				const std::streampos at = in.tellg();
				std::string tok;
				if (!(in >> tok)) break;
				char *end = nullptr;
				const double x = std::strtod(tok.c_str(), &end);
				if (end == tok.c_str() || *end != '\0' || !std::isfinite(x)) {   // (the next word, not a number: put it back)
					in.clear();
					in.seekg(at);
					break;
				}
				v[n] = x;
			}
			if (n != 1 && n != 3) return "\"" + word + "\" takes one number or three (red, green, blue)";
			if (n == 1) v[1] = v[2] = v[0];
			double *dst = word == "lift" ? out.lift : word == "gamma" ? out.gamma : word == "gain" ? out.gain : out.tint;
			for (int k = 0; k < 3; ++k) dst[k] = word == "lift" ? std::clamp(v[k], -1.0, 1.0) : std::clamp(v[k], 0.01, 10.0);
			continue;
		}
		double v = 0.0;
		if (!(in >> v) || !std::isfinite(v)) return "\"" + word + "\" needs a number after it";
		if (word == "bloom") out.bloom = std::clamp(v, 0.0, 1.0);
		else if (word == "bloom-radius") out.bloomRadius = std::clamp(v, 0.001, 0.5);
		else if (word == "flare") out.flare = std::clamp(v, 0.0, 1.0);
		else if (word == "vignette") out.vignette = std::clamp(v, 0.0, 1.0);
		else if (word == "warmth") out.warmth = std::clamp(v, -1.0, 1.0);
		else if (word == "saturation") out.saturation = std::clamp(v, 0.0, 3.0);
		else return "unknown word \"" + word + "\" (bloom, bloom-radius, flare, vignette, warmth, saturation, lift, gamma, gain, tint)";
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

	if (look.flare > 0.0) {
		// A lens flare: the brightest lights (what is above four times white - the sun, a flash; not a lit planet) at a quarter of the size, drawn out into a streak
		// sideways, and ghosts of them - soft discs in the colours of a lens's coatings on the line through the picture's centre - added on top.
		const int fw = (std::max)(1, w / 4), fh = (std::max)(1, h / 4);
		std::vector<float> bright(static_cast<std::size_t>(fw) * static_cast<std::size_t>(fh) * 3, 0.0f);
		for (int y = 0; y < fh; ++y)
			for (int x = 0; x < fw; ++x) {
				double c[3] = {0.0, 0.0, 0.0};
				for (int dy = 0; dy < 4; ++dy)
					for (int dx = 0; dx < 4; ++dx) {
						const std::size_t at = (static_cast<std::size_t>((std::min)(h - 1, y * 4 + dy)) * static_cast<std::size_t>(w) + static_cast<std::size_t>((std::min)(w - 1, x * 4 + dx))) * 3;
						for (int k = 0; k < 3; ++k) c[k] += rgb[at + k] / 16.0;
					}
				const double lum = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2], over = lum > 4.0 ? (lum - 4.0) / lum : 0.0;
				for (int k = 0; k < 3; ++k) bright[(static_cast<std::size_t>(y) * static_cast<std::size_t>(fw) + static_cast<std::size_t>(x)) * 3 + k] = static_cast<float>(c[k] * over);
			}
		std::vector<float> streak = bright;
		for (int pass = 0; pass < 3; ++pass) detail::boxPass(streak, fw, fh, (std::max)(1, fw / 10), true);
		// the ghosts: soft round discs on the line from the lights' centre through the picture's centre, each as bright as the lights' share of the picture spread over it
		double total = 0.0, cu = 0.0, cv = 0.0, flux[3] = {0.0, 0.0, 0.0};
		for (int y = 0; y < fh; ++y)
			for (int x = 0; x < fw; ++x) {
				const float *b = &bright[(static_cast<std::size_t>(y) * static_cast<std::size_t>(fw) + static_cast<std::size_t>(x)) * 3];
				const double e = 0.2126 * b[0] + 0.7152 * b[1] + 0.0722 * b[2];
				total += e, cu += e * (x + 0.5) / fw, cv += e * (y + 0.5) / fh;
				for (int k = 0; k < 3; ++k) flux[k] += b[k] / (static_cast<double>(fw) * fh);
			}
		if (total > 0.0) cu /= total, cv /= total;
		const double scales[4] = {-0.55, -1.25, 0.4, -0.25}, radii[4] = {0.03, 0.06, 0.02, 0.045}, weights[4] = {0.004, 0.003, 0.003, 0.004};
		const double tints[4][3] = {{1.0, 0.55, 0.25}, {0.35, 0.55, 1.0}, {0.55, 1.0, 0.6}, {1.0, 0.4, 0.85}};
		const double aspect = static_cast<double>(h) / w, f = look.flare;
		ppm_text::parallel_ranges(n, [&](std::size_t begin, std::size_t end) {
			for (std::size_t i = begin; i < end; ++i) {
				const double u = (static_cast<double>(i % static_cast<std::size_t>(w)) + 0.5) / w, v = (static_cast<double>(i / static_cast<std::size_t>(w)) + 0.5) / h;
				const std::size_t q = (static_cast<std::size_t>((std::min)(fh - 1, static_cast<int>(v * fh))) * static_cast<std::size_t>(fw) + static_cast<std::size_t>((std::min)(fw - 1, static_cast<int>(u * fw)))) * 3;
				for (int k = 0; k < 3; ++k) {
					double add = 0.05 * streak[q + static_cast<std::size_t>(k)] * (k == 0 ? 0.8 : k == 2 ? 1.25 : 1.0);   // (the streak a little blue)
					if (total > 0.0)
						for (int g = 0; g < 4; ++g) {
							const double du = u - (0.5 + scales[g] * (cu - 0.5)), dv = (v - (0.5 + scales[g] * (cv - 0.5))) * aspect, r = radii[g];
							const double disc = 1.0 - std::clamp((std::sqrt(du * du + dv * dv) - 0.7 * r) / (0.3 * r), 0.0, 1.0);   // (its edge soft)
							if (disc > 0.0) add += weights[g] * tints[g][k] * flux[k] * aspect / (3.14159265358979 * r * r) * disc;   // (its share of the picture: pi r^2 / aspect)
						}
					rgb[i * 3 + k] += static_cast<float>(f * add);
				}
			}
		});
	}

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
	// the grade's tint keeps the brightness (like the warmth); its gain and tint go with the white balance
	if (look.graded()) {
		const double y = 0.2126 * look.tint[0] + 0.7152 * look.tint[1] + 0.0722 * look.tint[2];
		for (int k = 0; k < 3; ++k) gain[k] *= look.gain[k] * look.tint[k] / (y > 0.0 ? y : 1.0);
	}
	const bool lifted = look.lift[0] != 0.0 || look.lift[1] != 0.0 || look.lift[2] != 0.0;
	const bool bent = look.gamma[0] != 1.0 || look.gamma[1] != 1.0 || look.gamma[2] != 1.0;
	const double s = look.saturation, v = look.vignette;
	const double cx = 0.5 * w, cy = 0.5 * h, halfDiag2 = cx * cx + cy * cy;
	ppm_text::parallel_ranges(n, [&](std::size_t begin, std::size_t end) {
		for (std::size_t i = begin; i < end; ++i) {
			double c[3];
			for (int k = 0; k < 3; ++k) c[k] = rgb[i * 3 + k] * gain[k];
			if (lifted)
				for (int k = 0; k < 3; ++k) c[k] = (std::max)(0.0, c[k] + look.lift[k]);
			if (bent)
				for (int k = 0; k < 3; ++k) c[k] = std::pow((std::max)(0.0, c[k]), 1.0 / look.gamma[k]);
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
