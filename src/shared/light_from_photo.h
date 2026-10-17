#pragma once
// light_from_photo.h -- "Light from a photo": a photo of an outdoor scene becomes the settings of the Builder's Sun & sky (scene_sky.h): how high the sun is, which way it is, how hazy.
//
// How it is done, with plain image analysis (no model, no download):
//   * the sky is found as the blue or white region that hangs from the top of the picture (column by column, down to where something else begins);
//   * the camera's tilt comes from where the sky ends: the lower edge of the sky is taken to be the horizon, which a level horizon in the picture tells (the tilt can also be given);
//   * if the sun itself is in the picture (a small clipped white spot inside the sky) its direction is read straight off;
//   * otherwise the Hosek-Wilkie sky (the very model the Builder renders its sky with, hosek_sky.h) is fitted to the brightness of the sky pixels: for each sun height, direction and
//     haze it predicts how bright every direction of the sky is, and the one whose predictions follow the photo's brightness best (up to one overall gain, since the photo's exposure
//     is not known) wins. A sky is brighter towards the sun and near the horizon, and that gradient is what is fitted.
// What it cannot tell: the exposure (the brightness stays the Builder's), the ground's brightness, the sun's direction when the sky is flat (overcast) or only a small piece is seen, and
// a warm sunset sky (it looks for blue and white skies). The result says how well the fitted sky explains the photo, so a poor fit is not passed off as a good one.
//
// Header-only, std-only (plus hosek_sky.h); shared by the Scene Builder (qt_gui/scene_builder_light_photo.cpp) and its tests.

#include "hosek_sky.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace light_from_photo {

constexpr double kPi = 3.14159265358979323846;

struct Options {
	double hfovDegrees = 65.0;   // the photo's horizontal field of view (a phone's main camera is about 65 to 70)
	bool tiltGiven = false;      // else it is read off the horizon
	double tiltDegrees = 0.0;    // the camera looks up (+) or down (-) by this much, when given
};

struct Estimate {
	bool ok = false;
	std::string why;                 // what went wrong (when not ok), or what to be careful of (when ok)
	double sunElevation = 35.0;      // degrees above the horizon
	double sunAzimuth = 0.0;         // degrees to the right of the direction the photo's centre looks in (negative: to the left), as seen from above
	double turbidity = 3.0;          // haze: 1.7 very clear, 3 ordinary, 6 and more hazy
	bool sunSeen = false;            // the sun itself is in the picture
	bool horizonFound = false;       // the camera's tilt was read off the horizon (else it was given or assumed level)
	double tiltDegrees = 0.0;        // the tilt used
	double skyFraction = 0.0;        // the share of the picture that is sky
	double confidence = 0.0;         // 0..1: how much of the sky's brightness variation the fitted sky explains
	bool flatSky = false;            // the sky has almost no gradient: the direction of the sun cannot be told
	bool evenBlue = false;           // ... and it is clearly blue, not grey: a clear sky looked at away from the sun, not an overcast one
};

namespace detail {

inline double decode(double s) { return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); }

struct Picture {
	int w = 0, h = 0;
	std::vector<float> rgb;   // display values 0..1, three per pixel
	const float* at(int x, int y) const { return &rgb[(static_cast<size_t>(y) * w + x) * 3]; }
};

// The photo reduced (by averaging blocks) to at most `maxWidth` pixels across.
inline Picture reduce(const unsigned char* rgb8, int w, int h, int maxWidth) {
	const int block = std::max(1, (w + maxWidth - 1) / maxWidth);
	Picture p;
	p.w = std::max(1, w / block);
	p.h = std::max(1, h / block);
	p.rgb.assign(static_cast<size_t>(p.w) * p.h * 3, 0.0f);
	for (int y = 0; y < p.h; ++y)
		for (int x = 0; x < p.w; ++x) {
			double sum[3] = {0, 0, 0};
			int n = 0;
			for (int dy = 0; dy < block; ++dy)
				for (int dx = 0; dx < block; ++dx) {
					const int sx = x * block + dx, sy = y * block + dy;
					if (sx >= w || sy >= h) continue;
					for (int c = 0; c < 3; ++c) sum[c] += rgb8[(static_cast<size_t>(sy) * w + sx) * 3 + c] / 255.0;
					++n;
				}
			for (int c = 0; c < 3; ++c) p.rgb[(static_cast<size_t>(y) * p.w + x) * 3 + c] = static_cast<float>(sum[c] / std::max(n, 1));
		}
	return p;
}

inline double luminance(const float* c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }

// Whether a pixel looks like sky: blue (or cyan), or bright and nearly colourless (cloud, haze), and not dark.
inline bool skyLike(const float* c) {
	const double r = c[0], g = c[1], b = c[2];
	const double lum = luminance(c), hi = std::max({r, g, b}), lo = std::min({r, g, b});
	const double saturation = (hi - lo) / std::max(hi, 1e-3);
	const bool blue = b >= r * 1.03 && b >= g * 0.9;
	const bool white = lum > 0.55 && saturation < 0.22;
	return (blue || white) && lum > 0.08;
}

// Clipped to (nearly) white: a sun is often warm, so one channel may stop short of full.
inline bool clipped(const float* c) { return std::min({c[0], c[1], c[2]}) >= 0.85f && luminance(c) >= 0.95; }

// For each column of the picture, the row where the sky ends: down from the top while the pixels look like sky, stopping where the brightness drops well below what the sky has been (a blue
// sea is darker than the sky above it; a sky gets brighter towards the horizon, it does not fall off). 0 for a column with no sky at its top.
inline std::vector<int> skyColumnEnds(const Picture& pic) {
	std::vector<int> end(static_cast<size_t>(pic.w), 0);
	for (int x = 0; x < pic.w; ++x) {
		int y = 0, sinceClipped = 1000;
		while (y < pic.h && skyLike(pic.at(x, y))) {
			const double lum = luminance(pic.at(x, y));
			// the brightest the sky was in the last 20 rows (not the whole column: the glare round a sun is far brighter than the sky just past it), leaving out clipped pixels and the
			// rows just after them
			double recent = 0.0;
			for (int k = std::max(0, y - 20); k < y; ++k)
				if (!clipped(pic.at(x, k))) recent = std::max(recent, luminance(pic.at(x, k)));
			if (y >= 3 && sinceClipped > 20 && lum < 0.6 * recent) break;
			sinceClipped = clipped(pic.at(x, y)) ? 0 : sinceClipped + 1;
			++y;
		}
		end[static_cast<size_t>(x)] = y < 3 ? 0 : y;
	}
	return end;
}

}  // namespace detail

// Reads the sky of a photo (`rgb8`, w x h, three 8-bit sRGB values per pixel).
inline Estimate estimate(const unsigned char* rgb8, int w, int h, const Options& options = Options()) {
	Estimate out;
	if (!rgb8 || w < 16 || h < 16) {
		out.why = "The photo is too small.";
		return out;
	}
	const detail::Picture pic = detail::reduce(rgb8, w, h, 192);
	const double focal = (pic.w / 2.0) / std::tan(std::min(160.0, std::max(10.0, options.hfovDegrees)) * kPi / 360.0);   // pixels
	const double cx = pic.w / 2.0, cy = pic.h / 2.0;

	// ---- the sky: down each column from the top (detail::skyColumnEnds)
	const std::vector<int> end = detail::skyColumnEnds(pic);
	size_t skyPixels = 0;
	for (const int e : end) skyPixels += static_cast<size_t>(e);
	out.skyFraction = static_cast<double>(skyPixels) / (static_cast<double>(pic.w) * pic.h);
	if (out.skyFraction < 0.03) {
		out.why = "No sky was found: it looks for a blue or white sky that reaches the top of the picture.";
		return out;
	}

	// ---- the camera's tilt: the lower edge of the sky is the horizon
	double tilt = options.tiltDegrees;
	if (options.tiltGiven) {
		out.horizonFound = false;
	} else {
		std::vector<int> edges;
		for (int x = 0; x < pic.w; ++x)
			if (end[static_cast<size_t>(x)] > 0 && end[static_cast<size_t>(x)] < pic.h - 1) edges.push_back(end[static_cast<size_t>(x)]);
		if (edges.size() >= static_cast<size_t>(0.3 * pic.w)) {
			std::sort(edges.begin(), edges.end());
			const double row = edges[static_cast<size_t>(0.85 * (edges.size() - 1))];   // (the lowest sky seen: buildings and trees only raise the skyline)
			tilt = std::atan2(row - cy, focal) * 180.0 / kPi;   // a horizon below the centre: the camera looks up
			out.horizonFound = true;
		} else {
			tilt = 0.0;
			out.why = "The horizon is not in the picture, so the camera is taken to look level; give the tilt if it does not.";
		}
	}
	tilt = std::min(60.0, std::max(-60.0, tilt));
	out.tiltDegrees = tilt;
	const double sinT = std::sin(tilt * kPi / 180.0), cosT = std::cos(tilt * kPi / 180.0);

	// the direction of a pixel in the room's frame: x to the right, y up, z straight ahead along the photo's centre direction, level
	const auto direction = [&](double u, double v, double d[3]) {
		const double x = (u - cx) / focal, y = -(v - cy) / focal, z = 1.0;
		const double yy = y * cosT + z * sinT, zz = -y * sinT + z * cosT;
		const double n = std::sqrt(x * x + yy * yy + zz * zz);
		d[0] = x / n;
		d[1] = yy / n;
		d[2] = zz / n;
	};

	// ---- the sun itself: one small clipped spot in the sky (white cloud tops give several, and are not it)
	struct Blob {
		size_t area;
		double u, v;
		bool touches;   // reaches the edge of the picture or the end of the sky (the glow at a horizon, a cloud's edge): a sun floats inside the sky
	};
	std::vector<Blob> blobs;
	std::vector<char> seen(static_cast<size_t>(pic.w) * pic.h, 0);
	for (int y0 = 0; y0 < pic.h; ++y0)
		for (int x0 = 0; x0 < pic.w; ++x0) {
			const size_t i0 = static_cast<size_t>(y0) * pic.w + x0;
			if (seen[i0] || y0 >= end[static_cast<size_t>(x0)] || !detail::clipped(pic.at(x0, y0))) continue;
			std::vector<std::pair<int, int>> stack{{x0, y0}};
			seen[i0] = 1;
			size_t area = 0;
			double su = 0, sv = 0;
			bool touches = false;
			while (!stack.empty()) {
				const auto [x, y] = stack.back();
				stack.pop_back();
				++area;
				su += x + 0.5;
				sv += y + 0.5;
				const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
				for (int k = 0; k < 4; ++k) {
					if (nx[k] < 0 || ny[k] < 0 || nx[k] >= pic.w || ny[k] >= pic.h || ny[k] >= end[static_cast<size_t>(nx[k])]) { touches = true; continue; }
					const size_t j = static_cast<size_t>(ny[k]) * pic.w + nx[k];
					if (seen[j] || !detail::clipped(pic.at(nx[k], ny[k]))) continue;
					seen[j] = 1;
					stack.push_back({nx[k], ny[k]});
				}
			}
			if (area >= 2) blobs.push_back({area, su / area, sv / area, touches});   // (single pixels are noise and glints)
		}
	std::sort(blobs.begin(), blobs.end(), [](const Blob& a, const Blob& b) { return a.area > b.area; });
	double sunDir[3] = {0, 0, 0};
	if (!blobs.empty() && !blobs[0].touches && blobs[0].area <= 0.06 * pic.w * pic.h && (blobs.size() == 1 || blobs[1].area * 4 <= blobs[0].area)) {
		const Blob& b = blobs[0];
		// the glow of a sun is the same all round it: the brightness on rings round the spot is even (a clipped cloud is lumpy) and falls off outwards
		const double radius = std::max(1.5, std::sqrt(b.area / kPi));
		double ringMean[2] = {0, 0};
		bool glow = true;
		for (int ring = 0; ring < 2 && glow; ++ring) {
			const double r = (ring == 0 ? 3.0 : 5.0) * radius;
			double sum = 0.0, sq = 0.0;
			int n = 0;
			for (int k = 0; k < 36; ++k) {
				const int px = static_cast<int>(b.u + r * std::cos(k * kPi / 18.0)), py = static_cast<int>(b.v + r * std::sin(k * kPi / 18.0));
				if (px < 0 || py < 0 || px >= pic.w || py >= pic.h || py >= end[static_cast<size_t>(px)]) continue;
				const double lum = detail::luminance(pic.at(px, py));
				sum += lum;
				sq += lum * lum;
				++n;
			}
			if (n < 8) {   // (the ring is mostly outside the picture or the sky: a big glare near an edge; the inner ring has to be there, the outer one need not)
				if (ring == 0) glow = false;
				break;
			}
			ringMean[ring] = sum / n;
			const double variance = std::max(0.0, sq / n - ringMean[ring] * ringMean[ring]);
			if (std::sqrt(variance) > 0.45 * std::max(ringMean[ring], 1e-3)) glow = false;
		}
		if (glow && (ringMean[1] == 0.0 || ringMean[0] >= ringMean[1])) {
			direction(b.u, b.v, sunDir);
			out.sunSeen = true;
		}
	}

	// ---- the samples the sky is fitted to: sky pixels above the horizon, not clipped, not at the sky's ragged lower edge
	struct Sample {
		double theta, d[3], logY;
	};
	std::vector<Sample> samples;
	const size_t stride = std::max<size_t>(1, skyPixels / 900);
	size_t counter = 0;
	double redSum = 0.0, blueSum = 0.0;   // (of the sky pixels kept: how blue the sky is is blue over red)
	for (int x = 0; x < pic.w; ++x)
		for (int y = 0; y < end[static_cast<size_t>(x)] - 3; ++y) {
			if (counter++ % stride != 0) continue;
			const float* c = pic.at(x, y);
			if (detail::clipped(c)) continue;
			Sample s;
			direction(x + 0.5, y + 0.5, s.d);
			if (s.d[1] < 0.01) continue;
			s.theta = std::acos(std::min(1.0, s.d[1]));
			const double lum = detail::decode(c[0]) * 0.2126 + detail::decode(c[1]) * 0.7152 + detail::decode(c[2]) * 0.0722;
			s.logY = std::log(std::max(lum, 1e-4));
			samples.push_back(s);
			redSum += c[0];
			blueSum += c[2];
		}
	if (samples.size() < 60) {
		out.why = "Too little sky to read the light from.";
		return out;
	}
	double mean0 = 0.0, var0 = 0.0;
	for (const Sample& s : samples) mean0 += s.logY;
	mean0 /= static_cast<double>(samples.size());
	for (const Sample& s : samples) var0 += (s.logY - mean0) * (s.logY - mean0);
	var0 /= static_cast<double>(samples.size());

	// how far the fitted sky is from the photo's brightness, with the best overall gain (the photo's exposure is not known): the variance of the log ratio
	const auto misfit = [&](const hosek_sky::Model& model, double elevationDeg, double azimuthDeg) {
		const double el = elevationDeg * kPi / 180.0, az = azimuthDeg * kPi / 180.0;
		const double sx = std::cos(el) * std::sin(az), sy = std::sin(el), sz = std::cos(el) * std::cos(az);
		double mean = 0.0, sq = 0.0;
		for (const Sample& s : samples) {
			const double gamma = std::acos(std::min(1.0, std::max(-1.0, s.d[0] * sx + s.d[1] * sy + s.d[2] * sz)));
			double xyz[3];
			model.xyz(s.theta, gamma, xyz);
			const double r = s.logY - std::log(std::max(xyz[1], 1e-12));
			mean += r;
			sq += r * r;
		}
		const double n = static_cast<double>(samples.size());
		mean /= n;
		return sq / n - mean * mean;
	};

	double bestElevation = 35.0, bestAzimuth = 0.0, bestTurbidity = 3.0, bestError = 1e30;
	const auto consider = [&](double elevation, double azimuth, double turbidity, const hosek_sky::Model& model) {
		const double e = misfit(model, elevation, azimuth);
		if (e < bestError) {
			bestError = e;
			bestElevation = elevation;
			bestAzimuth = azimuth;
			bestTurbidity = turbidity;
		}
	};
	const double turbidities[] = {1.8, 2.5, 3.5, 5.0, 7.0, 10.0};
	if (out.sunSeen) {
		// the sun is where it was seen; only the haze (and a little slack in the direction) is left to find
		const double elevation = std::asin(std::min(1.0, std::max(-1.0, sunDir[1]))) * 180.0 / kPi;
		const double azimuth = std::atan2(sunDir[0], sunDir[2]) * 180.0 / kPi;
		for (const double t : turbidities) consider(std::max(0.5, elevation), azimuth, t, hosek_sky::Model(t, 0.3, std::max(0.5, elevation) * kPi / 180.0));
		bestElevation = std::max(0.5, elevation);
		bestAzimuth = azimuth;
	} else {
		for (const double t : turbidities)
			for (double e = 3.0; e <= 88.0; e += 5.0) {
				const hosek_sky::Model model(t, 0.3, e * kPi / 180.0);
				for (double a = -180.0; a < 180.0; a += 10.0) consider(e, a, t, model);
			}
		// refine around the best
		const double e0 = bestElevation, a0 = bestAzimuth, t0 = bestTurbidity;
		for (double t = std::max(1.7, t0 - 1.0); t <= std::min(10.0, t0 + 1.0) + 1e-9; t += 0.5)
			for (double e = std::max(0.5, e0 - 5.0); e <= std::min(89.0, e0 + 5.0) + 1e-9; e += 1.0) {
				const hosek_sky::Model model(t, 0.3, e * kPi / 180.0);
				for (double a = a0 - 10.0; a <= a0 + 10.0 + 1e-9; a += 2.0) consider(e, a, t, model);
			}
	}
	while (bestAzimuth > 180.0) bestAzimuth -= 360.0;
	while (bestAzimuth < -180.0) bestAzimuth += 360.0;

	out.sunElevation = bestElevation;
	out.sunAzimuth = bestAzimuth;
	out.turbidity = bestTurbidity;
	out.confidence = var0 > 1e-9 ? std::min(1.0, std::max(0.0, 1.0 - bestError / var0)) : 0.0;
	const double blueness = blueSum / std::max(redSum, 1e-3);
	if (var0 < 0.015 && out.skyFraction >= 0.15) {   // a sky with almost no gradient (a thin strip of sky says nothing of the kind)
		out.flatSky = true;
		out.sunElevation = 45.0;
		out.sunAzimuth = 0.0;
		out.confidence = 0.0;
		if (blueness >= 1.25) {   // clearly blue: a clear sky seen away from the sun, not an overcast one
			out.evenBlue = true;
			out.turbidity = 3.0;
			out.why = "The sky is blue and evenly bright (a clear sky seen away from the sun), so the sun's direction cannot be told: it is set high with ordinary haze. Change Sun height and Sun direction if you know them.";
		} else {
			out.turbidity = 10.0;
			out.why = "The sky is even (overcast): the sun's direction cannot be told, so it is set high and the haze to its most.";
		}
	} else if (out.skyFraction < 0.15 && !out.sunSeen) {
		out.confidence = std::min(out.confidence, 0.3);
		out.why = "Only a small part of the picture is sky, so the fit rests on little: check the result by eye (a warm sunset or sunrise sky is not found as sky at all).";
	} else if (out.confidence < 0.35 && !out.sunSeen) {
		out.why = "The fitted sky explains little of the photo's sky (confidence " + std::to_string(static_cast<int>(out.confidence * 100)) + "%): check the result by eye.";
	}
	out.ok = true;
	return out;
}

}  // namespace light_from_photo
