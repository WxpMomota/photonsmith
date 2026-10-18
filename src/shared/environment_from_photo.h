#pragma once
// environment_from_photo.h -- the photo as the scene's environment: what the camera saw becomes the part of an equirectangular picture it covers, and the rest (above, below, behind) is filled in from it.
//
// A matched photo (camera_from_photo.h) says where it looks: the camera's frame in the Builder's world. Every direction of the equirectangular picture that falls inside the photo takes the photo's colour (in
// linear light, edge feathered), and every other direction takes the colour spread in from the photo's own pixels by a push-pull fill (the picture is shrunk by averaging, the holes are filled from the
// coarser levels, then it is built back up: a smooth, plausible continuation, nothing invented). The sky light that uses it then lights the scene with what is really around it, and a glass or metal
// object reflects the real ground, sky and horizon instead of a made-up sky. The result is scaled so that its upper half is as bright, on average, as the generated sky it replaces (`targetUpperMean`), so
// the Sun that goes with that sky keeps its proportion to it.
//
// Same convention as scene_sky.h: row 0 is up, theta runs from +Y, phi from +X towards -Z. Header-only, std-only; unit tested.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace environment_from_photo {

constexpr double kPi = 3.14159265358979323846;

// Where the photo's camera looks, in the Builder's world: unit vectors, and the half field of view of the picture as tangents.
struct Frame {
	double right[3] = {1, 0, 0}, up[3] = {0, 1, 0}, forward[3] = {0, 0, -1};
	double tanHalfX = 0.6, tanHalfY = 0.45;
};

// The frame of a camera that is at `position`, looks at `target`, has `up` and a vertical field of view `fovDegrees`, for a picture `aspect` (width / height) across. Unit vectors; right = forward x up.
inline Frame frameOf(const double position[3], const double target[3], const double up[3], double fovDegrees, double aspect) {
	Frame f;
	double fw[3] = {target[0] - position[0], target[1] - position[1], target[2] - position[2]};
	auto norm = [](double v[3]) {
		const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		if (l > 1e-12) { v[0] /= l; v[1] /= l; v[2] /= l; }
	};
	norm(fw);
	double r[3] = {fw[1] * up[2] - fw[2] * up[1], fw[2] * up[0] - fw[0] * up[2], fw[0] * up[1] - fw[1] * up[0]};
	norm(r);
	const double u[3] = {r[1] * fw[2] - r[2] * fw[1], r[2] * fw[0] - r[0] * fw[2], r[0] * fw[1] - r[1] * fw[0]};
	for (int i = 0; i < 3; ++i) { f.forward[i] = fw[i]; f.right[i] = r[i]; f.up[i] = u[i]; }
	f.tanHalfY = std::tan(std::min(170.0, std::max(1.0, fovDegrees)) * kPi / 360.0);
	f.tanHalfX = f.tanHalfY * std::max(0.05, aspect);
	return f;
}

// The pictures made here are named photo_env_<hash>.hdr: a scene's light whose picture has such a name is a photo environment (Render into the photo reads its exposure off it like off a generated sky).
inline std::string fileNamePrefix() { return "photo_env_"; }
inline bool isEnvironmentFile(const std::string& path) {
	const size_t slash = path.find_last_of("/\\");
	return path.compare(slash == std::string::npos ? 0 : slash + 1, fileNamePrefix().size(), fileNamePrefix()) == 0;
}

namespace detail {

inline double decode(double s) { return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); }
inline double luminance(const float* c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }

// The direction of the middle of pixel (col, row) of a width x height equirectangular picture.
inline void direction(int col, int row, int width, int height, double d[3]) {
	const double theta = (row + 0.5) / height * kPi, phi = (col + 0.5) / width * 2.0 * kPi;
	d[0] = std::sin(theta) * std::cos(phi);
	d[1] = std::cos(theta);
	d[2] = -std::sin(theta) * std::sin(phi);
}

}  // namespace detail

// The environment picture (outWidth x outHeight x 3 floats, linear) made from the photo (rgb8, w x h x 3, sRGB) seen from `frame`. `targetUpperMean` > 0 scales it so that the mean luminance of the upper half
// (directions above the horizon) is that; 0 leaves the photo's own scale (0..1). Empty for a photo or size that is not usable.
inline std::vector<float> make(const unsigned char* rgb8, int w, int h, const Frame& frame, int outWidth, int outHeight, double targetUpperMean = 0.0) {
	if (!rgb8 || w < 2 || h < 2 || outWidth < 8 || outHeight < 4) return {};
	const size_t n = static_cast<size_t>(outWidth) * outHeight;
	std::vector<float> value(n * 3, 0.0f), weight(n, 0.0f);
	for (int row = 0; row < outHeight; ++row)
		for (int col = 0; col < outWidth; ++col) {
			double d[3];
			detail::direction(col, row, outWidth, outHeight, d);
			const double z = d[0] * frame.forward[0] + d[1] * frame.forward[1] + d[2] * frame.forward[2];
			if (z <= 1e-3) continue;
			const double x = (d[0] * frame.right[0] + d[1] * frame.right[1] + d[2] * frame.right[2]) / z / frame.tanHalfX;
			const double y = (d[0] * frame.up[0] + d[1] * frame.up[1] + d[2] * frame.up[2]) / z / frame.tanHalfY;
			if (x < -1.0 || x > 1.0 || y < -1.0 || y > 1.0) continue;
			const double px = (x + 1.0) * 0.5 * w - 0.5, py = (1.0 - y) * 0.5 * h - 0.5;
			const int x0 = std::min(w - 1, std::max(0, static_cast<int>(std::floor(px)))), y0 = std::min(h - 1, std::max(0, static_cast<int>(std::floor(py))));
			const int x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1);
			const double tx = std::min(1.0, std::max(0.0, px - x0)), ty = std::min(1.0, std::max(0.0, py - y0));
			const size_t i = static_cast<size_t>(row) * outWidth + col;
			for (int c = 0; c < 3; ++c) {
				const double top = rgb8[(static_cast<size_t>(y0) * w + x0) * 3 + c] * (1.0 - tx) + rgb8[(static_cast<size_t>(y0) * w + x1) * 3 + c] * tx;
				const double bottom = rgb8[(static_cast<size_t>(y1) * w + x0) * 3 + c] * (1.0 - tx) + rgb8[(static_cast<size_t>(y1) * w + x1) * 3 + c] * tx;
				value[i * 3 + c] = static_cast<float>(detail::decode((top * (1.0 - ty) + bottom * ty) / 255.0));
			}
			const double edge = std::min(std::min(1.0 - std::fabs(x), 1.0 - std::fabs(y)), 0.06) / 0.06;   // (the outer 3% ramps: a seam is not an edge)
			weight[i] = static_cast<float>(edge * edge * (3.0 - 2.0 * edge));
		}

	// push-pull: the pyramid of premultiplied colour and weight, then the holes filled from the coarser levels back up
	struct Level { int w, h; std::vector<float> p, a; };
	std::vector<Level> pyramid;
	pyramid.push_back({outWidth, outHeight, std::vector<float>(n * 3), weight});
	for (size_t i = 0; i < n; ++i)
		for (int c = 0; c < 3; ++c) pyramid[0].p[i * 3 + c] = value[i * 3 + c] * weight[i];
	while (pyramid.back().w > 4 && pyramid.back().h > 2) {
		const Level& f = pyramid.back();
		Level c{std::max(2, f.w / 2), std::max(1, f.h / 2), {}, {}};
		c.p.assign(static_cast<size_t>(c.w) * c.h * 3, 0.0f);
		c.a.assign(static_cast<size_t>(c.w) * c.h, 0.0f);
		for (int y = 0; y < c.h; ++y)
			for (int x = 0; x < c.w; ++x) {
				float p[3] = {0, 0, 0}, a = 0.0f;
				int count = 0;
				for (int dy = 0; dy < 2; ++dy)
					for (int dx = 0; dx < 2; ++dx) {
						const int fx = std::min(f.w - 1, 2 * x + dx), fy = std::min(f.h - 1, 2 * y + dy);
						const size_t fi = static_cast<size_t>(fy) * f.w + fx;
						a += f.a[fi];
						for (int k = 0; k < 3; ++k) p[k] += f.p[fi * 3 + k];
						++count;
					}
				const size_t ci = static_cast<size_t>(y) * c.w + x;
				c.a[ci] = a / count;
				for (int k = 0; k < 3; ++k) c.p[ci * 3 + k] = p[k] / count;
			}
		pyramid.push_back(std::move(c));
	}
	std::vector<float> filled;   // the level being built, premultiplied by nothing: plain colour
	{
		const Level& top = pyramid.back();
		const size_t cells = static_cast<size_t>(top.w) * top.h;
		double mean[3] = {0.3, 0.3, 0.3}, total = 0.0, sum[3] = {0, 0, 0};   // (the photo's own average colour fills what even the coarsest level does not reach)
		for (size_t i = 0; i < cells; ++i) {
			total += top.a[i];
			for (int k = 0; k < 3; ++k) sum[k] += top.p[i * 3 + k];
		}
		if (total > 1e-9)
			for (int k = 0; k < 3; ++k) mean[k] = sum[k] / total;
		filled.assign(cells * 3, 0.0f);
		for (size_t i = 0; i < cells; ++i) {
			const float a = std::min(1.0f, top.a[i]);
			for (int k = 0; k < 3; ++k) filled[i * 3 + k] = static_cast<float>(top.p[i * 3 + k] + (1.0f - a) * mean[k]);
		}
	}
	for (size_t l = pyramid.size() - 1; l-- > 0;) {
		const Level& f = pyramid[l];
		const Level& c = pyramid[l + 1];
		std::vector<float> next(static_cast<size_t>(f.w) * f.h * 3);
		for (int y = 0; y < f.h; ++y)
			for (int x = 0; x < f.w; ++x) {
				// the coarser colour at this pixel, bilinear (the picture wraps round in x)
				const double fx = (x + 0.5) * c.w / f.w - 0.5, fy = (y + 0.5) * c.h / f.h - 0.5;
				const int x0 = static_cast<int>(std::floor(fx)), y0 = std::min(c.h - 1, std::max(0, static_cast<int>(std::floor(fy))));
				const int y1 = std::min(c.h - 1, y0 + 1);
				const double tx = fx - x0, ty = std::min(1.0, std::max(0.0, fy - y0));
				const int xa = ((x0 % c.w) + c.w) % c.w, xb = (((x0 + 1) % c.w) + c.w) % c.w;
				const size_t fi = static_cast<size_t>(y) * f.w + x;
				const float a = std::min(1.0f, f.a[fi]);
				for (int k = 0; k < 3; ++k) {
					const double top = filled[(static_cast<size_t>(y0) * c.w + xa) * 3 + k] * (1.0 - tx) + filled[(static_cast<size_t>(y0) * c.w + xb) * 3 + k] * tx;
					const double bottom = filled[(static_cast<size_t>(y1) * c.w + xa) * 3 + k] * (1.0 - tx) + filled[(static_cast<size_t>(y1) * c.w + xb) * 3 + k] * tx;
					const double up = top * (1.0 - ty) + bottom * ty;
					next[fi * 3 + k] = static_cast<float>(f.p[fi * 3 + k] + (1.0 - a) * up);   // = a * own colour + (1 - a) * what is spread in
				}
			}
		filled = std::move(next);
	}

	if (targetUpperMean > 0.0) {   // the scale: the upper half as bright as the sky it replaces
		double sum = 0.0;
		long count = 0;
		for (int row = 0; row < outHeight / 2; ++row)
			for (int col = 0; col < outWidth; ++col) {
				sum += detail::luminance(&filled[(static_cast<size_t>(row) * outWidth + col) * 3]);
				++count;
			}
		const double mean = count ? sum / count : 0.0;
		if (mean > 1e-9) {
			const float scale = static_cast<float>(targetUpperMean / mean);
			for (float& v : filled) v *= scale;
		}
	}
	return filled;
}

}  // namespace environment_from_photo
