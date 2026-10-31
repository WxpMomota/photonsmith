#ifndef TONE_MAP_H
#define TONE_MAP_H
//==============================================================================
// tone_map.h -- Filmic tone mapping and sRGB gamma encoding
//
// pbrt-v4 context
// ---------------
// pbrt-v4 outputs floating-point EXR images and defers tone mapping to a
// separate tool (imgtool).  Because this renderer outputs 8-bit PPM, tone
// mapping must happen in the write path.  We implement two widely-used
// industry operators:
//
//   ACES (default)   -- Narkowicz 2015 approximation of the ACES RRT+ODT.
//                       Same curve used by Unreal Engine 4.  Maps [0, inf)
//                       to [0, 1) with a filmic shoulder and pleasant
//                       highlight rolloff.  Reference:
//                       https://knarkowicz.wordpress.com/2016/01/06/
//                       aces-filmic-tone-mapping-curve/
//
//   Reinhard         -- Simple operator: L' = L / (1 + L).
//                       Preserves hue at low values; faster but less
//                       filmic at high exposure.
//
//   None             -- Clamp only (legacy behavior for scenes without HDR).
//
// sRGB gamma
// ----------
// The standard sRGB OETF is a piecewise function:
//   x <= 0.0031308  ->  12.92 * x
//   x >  0.0031308  ->  1.055 * x^(1/2.4) - 0.055
// This replaces the previous sqrt (gamma-2.0) approximation and more
// accurately matches display hardware.
//==============================================================================

#include <cmath>
#include <string>

#include "tone_map_names.h"

// ---------------------------------------------------------------------------
// sRGB OETF  (linear float -> display-encoded float in [0,1])
//
// Aligned with pbrt-v4 LinearToSRGB (util/color.h):
//   - Linear segment: 12.92 * x  for x <= 0.0031308  (identical constants)
//   - Power segment:  minimax rational polynomial approximation (from enoki)
//     instead of std::pow(x, 1/2.4), which avoids a slow transcendental call
//     while matching the IEC 61966-2-1 sRGB standard to within float precision.
// ---------------------------------------------------------------------------
inline double linear_to_srgb(double x) {
	if (x <= 0.0)       return 0.0;
	if (x >= 1.0)       return 1.0;
	if (x <= 0.0031308) return 12.92 * x;
	// Minimax rational approximation -- mirrors pbrt-v4's enoki-derived polynomial.
	// Evaluated in double for consistency with the rest of our double pipeline.
	// EvaluatePolynomial(s, c0, c1, ..., cn) = c0 + s*(c1 + s*(...))
	double s = std::sqrt(x);
	double p = -0.0016829072605308378 + s * ( 0.03453868659826638
			 + s * ( 0.7642611304733891   + s * ( 2.0041169284241644
			 + s * ( 0.7551545191665577   + s * (-0.016202083165206348)))));
	double q =  4.178892964897981e-7  + s * (-0.00004375359692957097
			 + s * ( 0.03467195408529984  + s * ( 0.6085338522168684
			 + s * ( 1.8970238036421054   + s))));
	return p / q * x;
}

// ---------------------------------------------------------------------------
// ACES filmic tone mapping -- Narkowicz 2015 approximation
// Input:  linear HDR value (any non-negative float)
// Output: tone-mapped value in [0, 1)
//
// Curve: f(x) = x * (a*x + b) / (x * (c*x + d) + e)
// with a=2.51, b=0.03, c=2.43, d=0.59, e=0.14
// ---------------------------------------------------------------------------
inline double aces_narkowicz(double x) {
	if (x <= 0.0) return 0.0;
	const double a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
	double mapped = (x * (a * x + b)) / (x * (c * x + d) + e);
	return mapped < 0.0 ? 0.0 : (mapped > 1.0 ? 1.0 : mapped);
}

// ---------------------------------------------------------------------------
// Reinhard tone mapping
// Input:  linear HDR value
// Output: value in [0, 1)
// ---------------------------------------------------------------------------
inline double reinhard(double x) {
	if (x <= 0.0) return 0.0;
	return x / (1.0 + x);
}

// ---------------------------------------------------------------------------
// AgX -- Blender's default view transform since 4.0 (Troy Sobotka's AgX, as
// EaryChow tuned it for Blender: release/datafiles/colormanagement). Unlike a
// curve applied to each channel on its own (ACES above, Reinhard), it works on
// the colour as a whole: a bright saturated light - a glowing crystal, a neon
// sign, a flame - keeps its hue and fades towards white the way film does,
// instead of the per-channel curve's skew (a bright blue goes cyan, a bright
// red orange and then yellow, because the strong channel saturates first).
//
// The colour goes into Rec.2020, through AgX's "inset" matrix (which pulls it
// a little towards grey so that the sigmoid desaturates the brightest light
// gracefully), into a log2 encoding from 12.47 stops below to 4.03 above
// middle grey, through AgX's base contrast sigmoid, out through the "outset"
// matrix, and back to linear (a 2.2 power) Rec.709. The matrices and the
// range are three.js's AgXToneMapping (MIT), which takes them from Blender's
// configuration via Filament; the sigmoid is Benjamin Wrensch's polynomial fit
// of AgX's base contrast (MIT, "Minimal AgX implementation", 2023).
// Input linear Rec.709; output linear [0, 1] for the sRGB encoding.
// ---------------------------------------------------------------------------
// `punchy`: AgX's base contrast is soft and a little pale for a cartoon (Blender pairs it with "Looks"); the punchy look - Wrensch's ASC CDL "Punchy", power 1.35 and
// saturation 1.4 on the sigmoid's output - gives back contrast and colour the way Blender's "AgX - Punchy" does, still without the per-channel curve's hue skew.
inline void agx(double& r, double& g, double& b, bool punchy = false) {
	// Rec.709 -> Rec.2020, then the inset matrix (rows of each)
	const double r2 = 0.6274 * r + 0.3293 * g + 0.0433 * b;
	const double g2 = 0.0691 * r + 0.9195 * g + 0.0113 * b;
	const double b2 = 0.0164 * r + 0.0880 * g + 0.8956 * b;
	// (three.js and Filament write these matrices column by column, GLSL's mat3(col0, col1, col2): each row here sums to 1, so a grey stays grey)
	double c[3] = {
		0.856627153315983 * r2 + 0.0951212405381588 * g2 + 0.0482516061458583 * b2,
		0.137318972929847 * r2 + 0.761241990602591 * g2 + 0.101439036467562 * b2,
		0.11189821299995 * r2 + 0.0767994186031903 * g2 + 0.811302368396859 * b2};
	const double minEv = -12.47393, maxEv = 4.026069;
	for (double& x : c) {
		x = (std::log2(x > 1e-10 ? x : 1e-10) - minEv) / (maxEv - minEv);
		x = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
		const double x2 = x * x, x4 = x2 * x2;
		x = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
	}
	if (punchy) {
		for (double& x : c) x = std::pow(x > 0.0 ? x : 0.0, 1.35);
		const double luma = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
		for (double& x : c) x = luma + 1.4 * (x - luma);
	}
	// the outset matrix, then linear again (the sigmoid's output is display-encoded with a 2.2 power)
	double o[3] = {
		1.1271005818144368 * c[0] - 0.11060664309660323 * c[1] - 0.016493938717834573 * c[2],
		-0.1413297634984383 * c[0] + 1.157823702216272 * c[1] - 0.016493938717834257 * c[2],
		-0.14132976349843826 * c[0] - 0.11060664309660294 * c[1] + 1.2519364065950405 * c[2]};
	for (double& x : o) x = std::pow(x > 0.0 ? x : 0.0, 2.2);
	// Rec.2020 -> Rec.709, and a clamp for what falls outside it
	r = 1.6605 * o[0] - 0.5876 * o[1] - 0.0728 * o[2];
	g = -0.1246 * o[0] + 1.1329 * o[1] - 0.0083 * o[2];
	b = -0.0182 * o[0] - 0.1006 * o[1] + 1.1187 * o[2];
	r = r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r);
	g = g < 0.0 ? 0.0 : (g > 1.0 ? 1.0 : g);
	b = b < 0.0 ? 0.0 : (b > 1.0 ? 1.0 : b);
}

// ---------------------------------------------------------------------------
// Khronos PBR Neutral -- the Khronos Group's tone mapper for showing a
// product's true colours (Blender offers it as "Khronos PBR Neutral"): below
// 0.76 the picture is unchanged, apart from a small offset that takes the
// darkest colours' floor down; above it the brightest channel is compressed
// and the colour moved towards white just enough to keep its hue. Bright
// cartoon colours stay as saturated as they were painted. From the
// specification's reference code (Khronos, Apache 2.0):
// https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral
// ---------------------------------------------------------------------------
inline void pbr_neutral(double& r, double& g, double& b) {
	const double startCompression = 0.8 - 0.04, desaturation = 0.15;
	const double x = r < g ? (r < b ? r : b) : (g < b ? g : b);
	const double offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	r -= offset, g -= offset, b -= offset;
	const double peak = r > g ? (r > b ? r : b) : (g > b ? g : b);
	if (peak >= startCompression) {
		const double d = 1.0 - startCompression;
		const double newPeak = 1.0 - d * d / (peak + d - startCompression);
		r *= newPeak / peak, g *= newPeak / peak, b *= newPeak / peak;
		const double t = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
		r += (newPeak - r) * t, g += (newPeak - g) * t, b += (newPeak - b) * t;
	}
	r = r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r);
	g = g < 0.0 ? 0.0 : (g > 1.0 ? 1.0 : g);
	b = b < 0.0 ? 0.0 : (b > 1.0 ? 1.0 : b);
}

// ---------------------------------------------------------------------------
// Filmic -- Blender's view transform from 2.80 to 3.6 ("Filmic", base contrast,
// no look), which scenes made in those versions were lit for: a gentle toe that
// keeps night scenes readable, a long shoulder (middle grey 0.18 shows at about
// 0.51, white is reached 6 stops above it), and bright saturated light washing
// out towards white. Not Blender's lookup tables: our own small model, fitted
// to Blender 5.2's Filmic measured on greys and on 12 hues x 4 saturations
// from 12 stops below middle grey to 12 above (largest error 0.005 on greys,
// 0.04 on the brightest saturated colours, rms 0.01).
//   * In Filmic's log encoding (12.47 stops below middle grey to 12.53 above,
//     0..1), each channel is pulled towards the largest by
//     0.865 * smoothstep(0.639, 0.997, largest)^0.2247: colours past about 3.5
//     stops over grey lose saturation, the way the transform's gamut LUT does.
//   * Each channel then goes through a generalised logistic in stops s
//     (log2(x / 0.18)): lo + (hi - lo) / (1 + e^(-k (s - s0)))^(1/nu), which
//     is display-encoded (sRGB) already.
// Input linear Rec.709; output linear [0, 1] for the sRGB encoding (the curve's
// display value decoded, so that the writer's encoding gives it back).
// ---------------------------------------------------------------------------
inline void filmic_blender(double& r, double& g, double& b) {
	const double lo = 12.473931188, range = 25.0;   // the log encoding: log2 x from -12.47 to +12.53
	double v[3] = {r, g, b};
	double vmax = 0.0;
	for (double& x : v) {
		x = (std::log2(x > 1e-12 ? x : 1e-12) + lo) / range;
		x = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
		vmax = x > vmax ? x : vmax;
	}
	double t = (vmax - 0.63918) / (0.99729 - 0.63918);
	t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
	const double d = 0.86483 * std::pow(t * t * (3.0 - 2.0 * t), 0.22471);
	double out[3];
	for (int i = 0; i < 3; ++i) {
		const double s = (v[i] + (vmax - v[i]) * d) * range - lo - std::log2(0.18);
		double y = -0.011947 + (1.038885 + 0.011947) / std::pow(1.0 + std::exp(-0.563328 * (s - 0.657732)), 1.0 / 1.243626);
		y = y < 0.0 ? 0.0 : (y > 1.0 ? 1.0 : y);
		out[i] = y <= 0.04045 ? y / 12.92 : std::pow((y + 0.055) / 1.055, 2.4);   // display -> linear, undone by linear_to_srgb
	}
	r = out[0], g = out[1], b = out[2];
}

// ---------------------------------------------------------------------------
// ToneMapMode -- selects the active operator
// ---------------------------------------------------------------------------
enum class ToneMapMode {
	ACES,       // Narkowicz ACES RRT+ODT approximation (default)
	Reinhard,   // Simple Reinhard operator
	None,       // No tone mapping -- clamp only (legacy behavior)
	AgX,        // Blender's AgX (agx() above): hue-keeping highlights
	AgXPunchy,  // the same with the punchy look: more contrast and colour
	Neutral,    // Khronos PBR Neutral (pbr_neutral() above): the colours as painted
	Filmic      // Blender's Filmic (filmic_blender() above): scenes made in Blender 2.8 - 3.6 look as their makers saw them
};

// ---------------------------------------------------------------------------
// Parses a --tonemap flag value ("aces"/"reinhard"/"none") into a
// ToneMapMode. Shared by both the CPU (camera.h) and GPU
// (gpu/optix/optix_interface.cpp) render paths so the two can't drift on
// what a given name means. Unrecognized names fail (return false) rather
// than silently defaulting here - callers decide their own fallback
// behavior and warning text, matching sampler_kind_from_name()'s own
// contract (camera.h).
// ---------------------------------------------------------------------------
inline bool tone_map_mode_from_name(const std::string& name, ToneMapMode& out) {
	if (name == "aces")     { out = ToneMapMode::ACES;     return true; }
	if (name == "reinhard") { out = ToneMapMode::Reinhard; return true; }
	if (name == "none")     { out = ToneMapMode::None;     return true; }
	if (name == "agx")      { out = ToneMapMode::AgX;      return true; }
	if (name == "agx-punchy") { out = ToneMapMode::AgXPunchy; return true; }
	if (name == "neutral")  { out = ToneMapMode::Neutral;  return true; }
	if (name == "filmic")   { out = ToneMapMode::Filmic;   return true; }
	return false;
}

// ---------------------------------------------------------------------------
// tone_map_mode_display_name -- human-readable label for a [TECH] startup
// log line. Shared by both the CPU (cpu_interface.cpp) and GPU
// (optix_interface.cpp) render paths so their startup banners can't drift
// on what a given mode is called, the display-string counterpart to
// tone_map_mode_from_name() above.
// ---------------------------------------------------------------------------
inline const char* tone_map_mode_display_name(ToneMapMode mode) {
	switch (mode) {
		case ToneMapMode::Reinhard: return "Reinhard L/(1+L)";
		case ToneMapMode::None:     return "None (clamp only)";
		case ToneMapMode::AgX:      return "AgX (Blender)";
		case ToneMapMode::AgXPunchy: return "AgX Punchy";
		case ToneMapMode::Neutral:  return "Khronos PBR Neutral";
		case ToneMapMode::Filmic:   return "Filmic (Blender 2.8-3.6)";
		default:                    return "ACES Narkowicz 2015";
	}
}

// ---------------------------------------------------------------------------
// apply_tone_map -- apply the selected operator to a single channel
// ---------------------------------------------------------------------------
inline double apply_tone_map(double x, ToneMapMode mode) {
	switch (mode) {
		case ToneMapMode::ACES:     return aces_narkowicz(x);
		case ToneMapMode::Reinhard: return reinhard(x);
		case ToneMapMode::AgX:
		case ToneMapMode::AgXPunchy: {   // (one channel alone: as a grey of that brightness)
			double r = x, g = x, b = x;
			agx(r, g, b, mode == ToneMapMode::AgXPunchy);
			return g;
		}
		case ToneMapMode::Neutral: {
			double r = x, g = x, b = x;
			pbr_neutral(r, g, b);
			return g;
		}
		case ToneMapMode::Filmic: {
			double r = x, g = x, b = x;
			filmic_blender(r, g, b);
			return g;
		}
		default:                    return x < 0.0 ? 0.0 : x;
	}
}

// ---------------------------------------------------------------------------
// apply_tone_map_rgb -- the operator on a whole colour, in place: AgX and PBR
// Neutral need the three channels together; the others act on each alone.
// What every picture writer calls (camera write_color, the GPU's output).
// ---------------------------------------------------------------------------
inline void apply_tone_map_rgb(double& r, double& g, double& b, ToneMapMode mode) {
	if (mode == ToneMapMode::AgX || mode == ToneMapMode::AgXPunchy) return agx(r, g, b, mode == ToneMapMode::AgXPunchy);
	if (mode == ToneMapMode::Neutral) return pbr_neutral(r, g, b);
	if (mode == ToneMapMode::Filmic) return filmic_blender(r, g, b);
	r = apply_tone_map(r, mode);
	g = apply_tone_map(g, mode);
	b = apply_tone_map(b, mode);
}

#endif
