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
//
// And a painting: `paint SIZE` turns the picture into strokes of paint that follow its edges (kuwahara.h: the anisotropic Kuwahara filter, as Blender's compositor
// Kuwahara node has it), SIZE the strokes' radius in pixels at a picture 1080 high; `paint-sharpness` (0..1), `paint-stretch` (0..2: how far a stroke runs along an
// edge) and `paint-smoothness` (pixels at 1080: how even the strokes' directions are). First of all the steps, so the lens's glow and the grade go over the paint.
//
// And god rays: `sunbeams S` streaks the light of the sun (or a lamp) out across the frame from where it is (sun_beams.h: Blender's compositor Sun Beams node), S how
// much (0..2); `sunbeams-length L` how far (0..1 of the way to the source), `sunbeams-threshold T` the linear luminance that sends beams (1), `sunbeams-at X Y` where the
// source is (0..1 across and down, outside the picture too; without it, the middle of the brightest light).
//
// And the picture on paper or film (print_look.h, made on the finished picture by the program that writes it): `halftone SIZE` (a comic's dots), `watercolor W`,
// `vintage V` (an old film print; `vintage-tone`, and `seed`: the frame's, which a film writes).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "kuwahara.h"
#include "print_look.h"
#include "sun_beams.h"
#include "ppm_text.h"

namespace lens_look {

struct Look {
	double bloom = 0.0;         // 0..1: the share of the light the lens spreads into a glow (0.1 subtle, 0.3 dreamy)
	double bloomRadius = 0.05;  // the widest glow's reach, as a share of the picture's width
	double flare = 0.0;         // 0..1: a lens flare from the brightest lights (above four times white): a streak sideways and ghosts mirrored through the centre
	double vignette = 0.0;      // 0..1: how much darker the corners are than the centre
	double warmth = 0.0;        // -1..1: towards blue (cool, night) or amber (warm, dawn); the picture's brightness stays the same
	double saturation = 1.0;    // 0 = grey, 1 = as rendered, above 1 = more colourful
	// A star glint (Blender's Glare node, "Streaks"/"Simple Star"): rays from every light brighter than `starThreshold` (linear luminance), `starPoints` of them evenly
	// round (4: a cross, 6, 8 ...), the first at `starAngle` degrees from the right (45 with 4 points: an x), each reaching `starLength` of the picture's width;
	// `star` is how much of the light above the threshold goes into each ray (0: none). The sparkle on a crystal, a gem, an eye, a magic spark.
	double star = 0.0;
	int starPoints = 4;
	double starAngle = 0.0;
	double starLength = 0.08;
	double starThreshold = 1.0;
	// a painting (kuwahara.h): the strokes' radius in pixels at a picture 1080 high (0: none), and the filter's sharpness, stretch and smoothness (kuwahara::Params)
	double paint = 0.0;
	double paintSharpness = 0.5;
	double paintStretch = 1.0;
	double paintSmoothness = 4.0;
	// god rays (sun_beams.h): how much, how far, from what luminance, and from where (sunbeamsAt false: the brightest light's middle)
	double sunbeams = 0.0;
	double sunbeamsLength = 0.35;
	double sunbeamsThreshold = 1.0;
	bool sunbeamsAt = false;
	double sunbeamsX = 0.5, sunbeamsY = 0.5;
	// the grade: c' = (max(0, c * gain * tint + lift))^(1 / gamma), per channel
	double lift[3] = {0.0, 0.0, 0.0};
	double gamma[3] = {1.0, 1.0, 1.0};
	double gain[3] = {1.0, 1.0, 1.0};
	double tint[3] = {1.0, 1.0, 1.0};
	// `tonemap NAME`: the curve that turns the light into the picture's colours (tone_map.h: aces, agx, neutral, reinhard, none); empty: the render's own (--tonemap, aces).
	// A film's choice (its cameras file's `tonemap`, shot_cameras.h); not applied here - the picture writers read it (a --tonemap given on the command line wins).
	std::string tonemap;
	// the picture printed or painted on paper, or shown from an old film (print_look.h: halftone, watercolor, vintage): read here, made by the program that writes the
	// finished picture, last of all, on the picture as shown (not in apply below, which works on the light)
	print_look::Params print;
	bool graded() const {
		for (int k = 0; k < 3; ++k)
			if (lift[k] != 0.0 || gamma[k] != 1.0 || gain[k] != 1.0 || tint[k] != 1.0) return true;
		return false;
	}
	// Heat shimmer (`shimmer A`, at `shimmer-time T` seconds): the air over hot ground bending the light - what a long lens sees across a plain at midday. The picture is
	// pushed about by a field of rising, wavering ripples, A (a share of the picture's height: 0.002 a light shimmer, 0.006 a strong one) at the bottom of the frame where
	// the hot air is, fading to nothing two thirds of the way up. A film writes its time into each frame's look line, so the ripples move from frame to frame.
	double shimmer = 0.0, shimmerTime = 0.0;
	// A dome port half under the water (`waterline Y`: the line's row, a share of the picture's height from the top; `drops D`, at `drops-time T`): the meniscus along
	// the line - a dark wavering edge with a bright one over it - and drops of water running slowly down the dome above it, each a little lens showing the picture behind it
	// turned over (D 1 about seventy of them). film_undersea.h writes these for a camera at the waterline.
	double waterline = -10.0, drops = 0.0, dropsTime = 0.0;
	// A flat port in front of the lens (round 16: an underwater housing): `distort K` bows straight lines outwards towards the edges (pincushion: K above 0), `fringe F`
	// splits the colours there - the red pulled out, the blue in - as the water and the port's glass bend them differently.
	double distort = 0.0, fringe = 0.0;
	bool active() const { return shimmer > 0.0 || waterline > -1.0 || drops > 0.0 || distort != 0.0 || fringe != 0.0 || paint > 0.0 || sunbeams > 0.0 || bloom > 0.0 || flare > 0.0 || star > 0.0 || vignette > 0.0 || warmth != 0.0 || saturation != 1.0 || graded(); }
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
		if (word == "sunbeams-at") {   // two numbers: where the source is on the screen
			double x = 0.0, y = 0.0;
			if (!(in >> x >> y) || !std::isfinite(x) || !std::isfinite(y)) return "\"sunbeams-at\" takes two numbers: across and down, 0..1 (outside the picture too)";
			out.sunbeamsAt = true, out.sunbeamsX = std::clamp(x, -2.0, 3.0), out.sunbeamsY = std::clamp(y, -2.0, 3.0);
			continue;
		}
		if (word == "lut") {   // a .cube colour grade (cube_lut.h): a path, no spaces
			std::string path;
			if (!(in >> path)) return "\"lut\" takes a .cube file";
			out.print.lut = path;
			continue;
		}
		if (word == "tonemap") {
			std::string name;
			in >> name;
			if (name != "aces" && name != "agx" && name != "agx-punchy" && name != "neutral" && name != "reinhard" && name != "none") return "\"tonemap\" takes aces, agx, agx-punchy, neutral, reinhard or none";
			out.tonemap = name;
			continue;
		}
		double v = 0.0;
		if (!(in >> v) || !std::isfinite(v)) return "\"" + word + "\" needs a number after it";
		if (word == "bloom") out.bloom = std::clamp(v, 0.0, 1.0);
		else if (word == "bloom-radius") out.bloomRadius = std::clamp(v, 0.001, 0.5);
		else if (word == "flare") out.flare = std::clamp(v, 0.0, 1.0);
		else if (word == "star") out.star = std::clamp(v, 0.0, 1.0);
		else if (word == "star-points") out.starPoints = std::clamp(static_cast<int>(std::lround(v)), 2, 16);
		else if (word == "star-angle") out.starAngle = v;
		else if (word == "star-length") out.starLength = std::clamp(v, 0.005, 0.5);
		else if (word == "star-threshold") out.starThreshold = std::clamp(v, 0.0, 1000.0);
		else if (word == "vignette") out.vignette = std::clamp(v, 0.0, 1.0);
		else if (word == "shimmer") out.shimmer = std::clamp(v, 0.0, 0.05);
		else if (word == "shimmer-time") out.shimmerTime = v;
		else if (word == "waterline") out.waterline = std::clamp(v, -5.0, 5.0);
		else if (word == "drops") out.drops = std::clamp(v, 0.0, 5.0);
		else if (word == "drops-time") out.dropsTime = v;
		else if (word == "distort") out.distort = std::clamp(v, -0.5, 0.5);
		else if (word == "fringe") out.fringe = std::clamp(v, 0.0, 0.05);
		else if (word == "warmth") out.warmth = std::clamp(v, -1.0, 1.0);
		else if (word == "saturation") out.saturation = std::clamp(v, 0.0, 3.0);
		else if (word == "paint") out.paint = std::clamp(v, 0.0, 40.0);
		else if (word == "paint-sharpness") out.paintSharpness = std::clamp(v, 0.0, 1.0);
		else if (word == "paint-stretch") out.paintStretch = std::clamp(v, 0.0, 2.0);
		else if (word == "paint-smoothness") out.paintSmoothness = std::clamp(v, 0.0, 40.0);
		else if (word == "sunbeams") out.sunbeams = std::clamp(v, 0.0, 2.0);
		else if (word == "sunbeams-length") out.sunbeamsLength = std::clamp(v, 0.0, 1.0);
		else if (word == "sunbeams-threshold") out.sunbeamsThreshold = std::clamp(v, 0.0, 1000.0);
		else if (word == "halftone") out.print.halftone = std::clamp(v, 0.0, 60.0);
		else if (word == "halftone-mix") out.print.halftoneMix = std::clamp(v, 0.0, 1.0);
		else if (word == "watercolor" || word == "watercolour") out.print.watercolor = std::clamp(v, 0.0, 2.0);
		else if (word == "vintage") out.print.vintage = std::clamp(v, 0.0, 2.0);
		else if (word == "vintage-tone") out.print.vintageTone = std::clamp(v, 0.0, 1.0);
		else if (word == "seed") out.print.seed = static_cast<int>(std::clamp(v, -1e9, 1e9));
		else if (word == "lut-mix") out.print.lutMix = std::clamp(v, 0.0, 1.0);
		else return "unknown word \"" + word + "\" (bloom, bloom-radius, flare, star, star-points, star-angle, star-length, star-threshold, vignette, warmth, saturation, lift, "
					"gamma, gain, tint, tonemap, paint, paint-sharpness, paint-stretch, paint-smoothness, sunbeams, sunbeams-length, sunbeams-threshold, sunbeams-at, halftone, "
					"halftone-mix, watercolor, vintage, vintage-tone, seed, lut, lut-mix)";
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

// The picture (w x h x 3) at (x, y) in pixels, bilinear between pixel centres; black outside.
inline void sample(const std::vector<float> &img, int w, int h, double x, double y, double out[3]) {
	out[0] = out[1] = out[2] = 0.0;
	const double fx = x - 0.5, fy = y - 0.5;
	const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
	const double tx = fx - x0, ty = fy - y0;
	for (int j = 0; j < 2; ++j)
		for (int i = 0; i < 2; ++i) {
			const int px = x0 + i, py = y0 + j;
			if (px < 0 || py < 0 || px >= w || py >= h) continue;
			const double wgt = (i ? tx : 1.0 - tx) * (j ? ty : 1.0 - ty);
			const float *p = &img[(static_cast<std::size_t>(py) * static_cast<std::size_t>(w) + static_cast<std::size_t>(px)) * 3];
			for (int k = 0; k < 3; ++k) out[k] += wgt * p[k];
		}
}

// One ray of a star glint: `src`'s light smeared along (dx, dy) (a unit step in pixels), fading to 1% over `lengthPx`, its total kept (the ray's light is the source's).
// Built as Blender's Glare node builds its streaks (after Kawase's "Frame buffer postprocessing effects in DOUBLE-S.T.E.A.L", GDC 2003): passes of four taps each,
// the taps of pass p 4^p pixels apart and weighted by the fade over that distance, so a ray hundreds of pixels long costs sixteen or twenty taps a pixel.
// `atSource` gets the share of the light that stays where it was (the product of the passes' first weights), for taking it out again: a ray is what leaves the light.
inline std::vector<float> ray(const std::vector<float> &src, int w, int h, double dx, double dy, double lengthPx, double &atSource) {
	const double fade = std::pow(0.01, 1.0 / (std::max)(1.0, lengthPx));   // per pixel
	std::vector<float> cur = src, next(src.size());
	atSource = 1.0;
	for (int step = 1; step < lengthPx; step *= 4) {
		double weights[4], total = 0.0;
		for (int j = 0; j < 4; ++j) total += weights[j] = std::pow(fade, static_cast<double>(j * step));
		for (double &wj : weights) wj /= total;
		atSource *= weights[0];
		ppm_text::parallel_ranges(static_cast<std::size_t>(h), [&](std::size_t begin, std::size_t end) {
			for (std::size_t y = begin; y < end; ++y)
				for (int x = 0; x < w; ++x) {
					double acc[3] = {0.0, 0.0, 0.0}, c[3];
					for (int j = 0; j < 4; ++j) {   // the light arriving from behind along the ray
						sample(cur, w, h, x + 0.5 - dx * j * step, y + 0.5 - dy * j * step, c);
						for (int k = 0; k < 3; ++k) acc[k] += weights[j] * c[k];
					}
					for (int k = 0; k < 3; ++k) next[(y * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3 + k] = static_cast<float>(acc[k]);
				}
		});
		cur.swap(next);
	}
	return cur;
}

}  // namespace detail

// Applies `look` to a linear RGB picture (w x h x 3 floats, rows from the top) in place. Non-finite values are treated as black first, as the writers do.
inline void apply(float *rgb, int w, int h, const Look &look) {
	if (!look.active() || w <= 0 || h <= 0) return;
	const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
	for (std::size_t i = 0; i < n * 3; ++i)
		if (!std::isfinite(rgb[i])) rgb[i] = 0.0f;

	if (look.shimmer > 0.0) {   // heat shimmer first: it is in the air, in front of the lens (see Look::shimmer)
		const std::vector<float> src(rgb, rgb + n * 3);
		const double T = look.shimmerTime, A = look.shimmer * h;
		for (int y = 0; y < h; ++y) {
			const double v = (y + 0.5) / h, heat = std::clamp((v - 0.33) / 0.67, 0.0, 1.0);
			if (heat <= 0.0) continue;
			for (int x = 0; x < w; ++x) {
				const double u = (x + 0.5) / h;   // (both in picture heights, so the ripples are round whatever the frame's shape)
				const double rise = v * 60.0 + T * 7.0;   // (the ripples climb as the warm air does)
				const double dx = 0.6 * std::sin(rise + 3.0 * std::sin(u * 11.0 + T * 1.7)) + 0.4 * std::sin(2.3 * rise - u * 23.0 + T * 3.1);
				const double dy = 0.5 * std::sin(1.7 * rise + u * 17.0 - T * 2.3);
				double c[3];
				detail::sample(src, w, h, x + 0.5 + A * heat * heat * dx, y + 0.5 + 0.4 * A * heat * heat * dy, c);
				for (int k = 0; k < 3; ++k) rgb[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3 + k] = static_cast<float>(c[k]);
			}
		}
	}

	if (look.distort != 0.0 || look.fringe != 0.0) {   // a flat port (see Look::distort): each colour looked up a little further out the further from the middle it is
		const std::vector<float> src(rgb, rgb + n * 3);
		const double cx = 0.5 * w, cy = 0.5 * h, half = 0.5 * std::hypot(static_cast<double>(w), static_cast<double>(h));
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				const double dx = (x + 0.5 - cx) / half, dy = (y + 0.5 - cy) / half, r2 = dx * dx + dy * dy;
				for (int k = 0; k < 3; ++k) {
					const double f = 1.0 / (1.0 + look.distort * r2) * (1.0 - (k - 1) * look.fringe * r2);   // (red further out than green, blue nearer in)
					double c[3];
					detail::sample(src, w, h, cx + dx * f * half, cy + dy * f * half, c);
					rgb[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3 + k] = static_cast<float>(c[k]);
				}
			}
	}

	if (look.waterline > -1.0 || look.drops > 0.0) {   // a dome port at the waterline (see Look::waterline): the drops on it, then the line's meniscus
		const std::vector<float> src(rgb, rgb + n * 3);
		const double line = (look.waterline > -1.0 ? look.waterline : 1.0) * h, T = look.dropsTime;
		const auto px = [&](int x, int y) { return rgb + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3; };
		const int count = static_cast<int>(std::lround(70.0 * look.drops));
		for (int k = 0; k < count && line > 0.0; ++k) {
			const auto rnd = [k](int salt) {
				unsigned v = static_cast<unsigned>(k) * 747796405u + static_cast<unsigned>(salt) * 2891336453u + 12345u;
				v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
				return ((v >> 22u) ^ v) % 1000003u / 1000003.0;
			};
			const double R = (0.006 + 0.028 * rnd(1) * rnd(1)) * h;
			const double fall = 0.004 * h * T * (0.3 + rnd(4));   // (each runs down at its own pace, the big ones faster)
			const double span = std::max(1.0, line - 2.0 * R);
			const double cy = R + std::fmod(rnd(2) * span + fall * (1.0 + 20.0 * R / h), span), cx = rnd(3) * w;
			for (int y = std::max(0, static_cast<int>(cy - R)); y <= std::min(h - 1, static_cast<int>(cy + R)); ++y)
				for (int x = std::max(0, static_cast<int>(cx - R)); x <= std::min(w - 1, static_cast<int>(cx + R)); ++x) {
					const double dx = x + 0.5 - cx, dy = y + 0.5 - cy, d = std::sqrt(dx * dx + dy * dy) / R;
					if (d >= 1.0) continue;
					double c[3];
					detail::sample(src, w, h, cx - 1.8 * dx, cy - 1.8 * dy, c);   // (a little lens: what is behind it, turned over)
					const double rim = d > 0.8 ? 0.55 + 0.45 * (1.0 - (d - 0.8) / 0.2) : 1.0;
					const double glint = std::exp(-((dx + 0.35 * R) * (dx + 0.35 * R) + (dy + 0.4 * R) * (dy + 0.4 * R)) / (0.02 * R * R));
					float *o = px(x, y);
					for (int q = 0; q < 3; ++q) o[q] = static_cast<float>(c[q] * 1.05 * rim + 1.5 * glint);
				}
		}
		if (look.waterline > -1.0 && line > -0.03 * h && line < 1.03 * h)
			for (int x = 0; x < w; ++x) {
				const double u = (x + 0.5) / h, yl = line + 0.004 * h * std::sin(u * 21.0 + T * 2.3) + 0.0025 * h * std::sin(u * 57.0 - T * 4.1);
				for (int y = std::max(0, static_cast<int>(yl - 0.012 * h)); y <= std::min(h - 1, static_cast<int>(yl + 0.012 * h)); ++y) {
					const double s = (y + 0.5 - yl) / (0.004 * h);   // (above the line negative)
					const double dark = 1.0 - 0.75 * std::exp(-s * s), bright = 0.6 * std::exp(-(s + 1.6) * (s + 1.6) / 0.5);
					float *o = px(x, y);
					for (int q = 0; q < 3; ++q) o[q] = static_cast<float>(o[q] * dark + bright * (0.5 + o[q]));
				}
			}
	}

	if (look.paint > 0.0) {   // the painting first: the lens and the grade go over it (sizes are given at a picture 1080 high)
		const double scale = h / 1080.0;
		kuwahara::apply(rgb, w, h, kuwahara::Params{look.paint * scale, look.paintSharpness, look.paintStretch, look.paintSmoothness * scale});
	}

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

	if (look.star > 0.0) {
		// A star glint, at half the picture's size: the light above the threshold (a pixel's luminance over it, in that pixel's colour) sent out along each ray and added
		// on top, without what stays at the light itself (detail::ray). Before the bloom, so the rays glow a little too, as in a lens.
		const int sw = (std::max)(1, w / 2), sh = (std::max)(1, h / 2);
		std::vector<float> bright(static_cast<std::size_t>(sw) * static_cast<std::size_t>(sh) * 3, 0.0f);
		for (int y = 0; y < sh; ++y)
			for (int x = 0; x < sw; ++x) {
				double c[3] = {0.0, 0.0, 0.0};
				for (int dy = 0; dy < 2; ++dy)
					for (int dx = 0; dx < 2; ++dx) {
						const std::size_t at = (static_cast<std::size_t>((std::min)(h - 1, y * 2 + dy)) * static_cast<std::size_t>(w) + static_cast<std::size_t>((std::min)(w - 1, x * 2 + dx))) * 3;
						for (int k = 0; k < 3; ++k) c[k] += rgb[at + k] / 4.0;
					}
				const double lum = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2], over = lum > look.starThreshold ? (lum - look.starThreshold) / lum : 0.0;
				for (int k = 0; k < 3; ++k) bright[(static_cast<std::size_t>(y) * static_cast<std::size_t>(sw) + static_cast<std::size_t>(x)) * 3 + k] = static_cast<float>(c[k] * over);
			}
		std::vector<double> rays(bright.size(), 0.0);
		const double lengthPx = look.starLength * sw;
		for (int p = 0; p < look.starPoints; ++p) {
			const double a = (look.starAngle + 360.0 * p / look.starPoints) * 3.14159265358979 / 180.0;
			double atSource = 0.0;
			const std::vector<float> one = detail::ray(bright, sw, sh, std::cos(a), -std::sin(a), lengthPx, atSource);   // (degrees anticlockwise from the right; rows go down)
			for (std::size_t i = 0; i < rays.size(); ++i) rays[i] += one[i] - atSource * bright[i];
		}
		const std::vector<float> half(rays.begin(), rays.end());
		const double s = look.star;
		ppm_text::parallel_ranges(static_cast<std::size_t>(h), [&](std::size_t begin, std::size_t end) {
			for (std::size_t y = begin; y < end; ++y)
				for (int x = 0; x < w; ++x) {
					double c[3];
					detail::sample(half, sw, sh, (x + 0.5) * sw / w, (y + 0.5) * sh / h, c);
					for (int k = 0; k < 3; ++k) rgb[(y * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3 + k] += static_cast<float>(s * (std::max)(0.0, c[k]));
				}
		});
	}

	if (look.sunbeams > 0.0)   // god rays from the light (before the bloom, so they glow a little too)
		sun_beams::apply(rgb, w, h, sun_beams::Params{look.sunbeams, look.sunbeamsLength, look.sunbeamsThreshold, look.sunbeamsAt, look.sunbeamsX, look.sunbeamsY});

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
