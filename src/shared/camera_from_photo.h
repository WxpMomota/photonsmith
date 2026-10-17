#pragma once
// camera_from_photo.h -- "Camera from a photo": the lens, the tilt and the roll of the camera that took a photo, as a Scene Builder camera that sees a rendered scene the same way.
//
// What is read, with no model and no download:
//   * the lens, from the photo's own EXIF data when it has it (the 35 mm equivalent focal length a phone or camera writes into a JPEG: exifFocalLength35mm()), else it is given;
//   * the tilt (looking up or down) and the roll (a tilted horizon) from the horizon, which is taken to be the lower edge of the sky: a line is fitted through the sky's lower edge in every
//     column (buildings and trees only raise some of it, so the points that sit above the line are dropped), the line's slope is the roll, and how far it is from the middle of the picture is the
//     tilt. The sky is found by light_from_photo.h's sky finder (a blue or white sky hanging from the top of the picture).
// What it cannot tell: where the camera stood (only its height can be given), anything when the horizon is not in the picture (give the tilt and roll), and a horizon hidden by buildings or
// trees all along (the line then follows the skyline).
//
// makeCamera() puts the result in a Builder camera: the Builder camera's field of view is vertical, so the photo's horizontal field of view is converted for the photo's proportions; the camera
// keeps the heading it has, is put at the given height, and is turned up or down and rolled as the photo's was.
//
// Header-only, std-only; shared by the Scene Builder (qt_gui/scene_builder_camera_photo.cpp) and its tests.

#include "light_from_photo.h"
#include "scene_model.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace camera_from_photo {

constexpr double kPi = 3.14159265358979323846;

// ---- the lens, from EXIF ------------------------------------------------------------------------------------------------------------------------------------------------------------------

// The 35 mm equivalent focal length (EXIF tag FocalLengthIn35mmFilm) of a JPEG file's bytes, or 0 when the file is not a JPEG with EXIF data, is damaged, or does not say. Every read is
// bounds-checked: the bytes come from a file nobody vouches for.
inline double exifFocalLength35mm(const unsigned char* data, size_t size) {
	if (!data || size < 12 || data[0] != 0xFF || data[1] != 0xD8) return 0.0;
	size_t pos = 2;
	while (pos + 4 <= size) {
		if (data[pos] != 0xFF) return 0.0;
		const unsigned char marker = data[pos + 1];
		if (marker == 0xFF) { ++pos; continue; }   // (padding)
		if (marker == 0xD9 || marker == 0xDA) return 0.0;   // the end, or the picture itself: no EXIF before it
		if (marker >= 0xD0 && marker <= 0xD7) { pos += 2; continue; }
		const size_t length = (static_cast<size_t>(data[pos + 2]) << 8) | data[pos + 3];
		if (length < 2 || pos + 2 + length > size) return 0.0;
		if (marker == 0xE1 && length >= 2 + 6 + 8 && std::string(reinterpret_cast<const char*>(data + pos + 4), 6) == std::string("Exif\0\0", 6)) {
			const unsigned char* tiff = data + pos + 10;
			const size_t n = length - 2 - 6;   // the TIFF block's size
			bool little;
			if (tiff[0] == 'I' && tiff[1] == 'I') little = true;
			else if (tiff[0] == 'M' && tiff[1] == 'M') little = false;
			else return 0.0;
			const auto u16 = [&](size_t at, bool& ok) -> uint32_t {
				if (at + 2 > n) { ok = false; return 0; }
				return little ? (tiff[at] | (tiff[at + 1] << 8)) : ((tiff[at] << 8) | tiff[at + 1]);
			};
			const auto u32 = [&](size_t at, bool& ok) -> uint32_t {
				if (at + 4 > n) { ok = false; return 0; }
				return little ? (static_cast<uint32_t>(tiff[at]) | (static_cast<uint32_t>(tiff[at + 1]) << 8) | (static_cast<uint32_t>(tiff[at + 2]) << 16) | (static_cast<uint32_t>(tiff[at + 3]) << 24))
				              : ((static_cast<uint32_t>(tiff[at]) << 24) | (static_cast<uint32_t>(tiff[at + 1]) << 16) | (static_cast<uint32_t>(tiff[at + 2]) << 8) | static_cast<uint32_t>(tiff[at + 3]));
			};
			bool ok = true;
			if (u16(2, ok) != 42 || !ok) return 0.0;
			// the value of tag `wanted` in the directory at `dir` (a SHORT or LONG that fits in the entry), or false
			const auto find = [&](uint32_t dir, uint32_t wanted, uint32_t& value) -> bool {
				bool good = true;
				const uint32_t count = u16(dir, good);
				if (!good || count > 512) return false;
				for (uint32_t i = 0; i < count; ++i) {
					const size_t entry = static_cast<size_t>(dir) + 2 + static_cast<size_t>(i) * 12;
					const uint32_t tag = u16(entry, good);
					const uint32_t type = u16(entry + 2, good);
					if (!good) return false;
					if (tag != wanted) continue;
					if (type == 3) value = u16(entry + 8, good);        // SHORT
					else if (type == 4) value = u32(entry + 8, good);   // LONG
					else return false;
					return good;
				}
				return false;
			};
			uint32_t exifDir = 0, focal = 0;
			const uint32_t ifd0 = u32(4, ok);
			if (!ok || !find(ifd0, 0x8769, exifDir)) return 0.0;
			if (!find(exifDir, 0xA405, focal)) return 0.0;
			return focal > 0 && focal < 2000 ? static_cast<double>(focal) : 0.0;
		}
		pos += 2 + length;
	}
	return 0.0;
}

// The horizontal field of view (degrees) of a lens of `focal35mm` (35 mm equivalent) for a picture w x h: the frame's diagonal is 43.27 mm whatever its proportions or orientation.
inline double hfovFromFocal35(double focal35mm, int w, int h) {
	if (!(focal35mm > 0.0) || w <= 0 || h <= 0) return 0.0;
	const double diagonal = std::atan(43.2666 / (2.0 * focal35mm));   // half the diagonal field of view
	const double across = std::tan(diagonal) * w / std::sqrt(static_cast<double>(w) * w + static_cast<double>(h) * h);
	return 2.0 * std::atan(across) * 180.0 / kPi;
}

// ---- the tilt and the roll, from the horizon ------------------------------------------------------------------------------------------------------------------------------------------------

struct Options {
	double hfovDegrees = 65.0;   // the photo's horizontal field of view
	bool tiltGiven = false;      // else read off the horizon
	double tiltDegrees = 0.0;    // the camera looks up (+) or down (-) by this much, when given
	bool rollGiven = false;
	double rollDegrees = 0.0;    // the camera is rolled counter-clockwise (+) or clockwise (-) as seen from behind it, when given
};

struct Estimate {
	bool ok = false;
	std::string why;             // what went wrong (when not ok), or what to be careful of (when ok)
	bool horizonFound = false;   // the tilt and roll were read off the horizon (else they were given, or are 0)
	double tiltDegrees = 0.0;    // looking up (+) or down (-)
	double rollDegrees = 0.0;    // rolled counter-clockwise (+), as seen from behind the camera: the horizon then runs down to the right in the picture
	double skyFraction = 0.0;
	double horizonRowFraction = 0.5;   // where the horizon crosses the middle of the picture, as a share of its height from the top (0.5: through the middle)
	double lineSpread = 0.0;     // how far the sky's lower edge strays from the fitted line, in pixels of the reduced picture (small: a clean horizon)
};

namespace detail {

// A line row = a + b * x through points (x, row), by least squares.
struct Line {
	double a = 0.0, b = 0.0;
	bool ok = false;
};

inline Line fit(const std::vector<double>& xs, const std::vector<double>& rows, const std::vector<char>& use) {
	double sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0;
	for (size_t i = 0; i < xs.size(); ++i)
		if (use[i]) {
			sx += xs[i];
			sy += rows[i];
			sxx += xs[i] * xs[i];
			sxy += xs[i] * rows[i];
			n += 1.0;
		}
	Line l;
	const double denominator = n * sxx - sx * sx;
	if (n < 8 || std::fabs(denominator) < 1e-9) return l;
	l.b = (n * sxy - sx * sy) / denominator;
	l.a = (sy - l.b * sx) / n;
	l.ok = true;
	return l;
}

}  // namespace detail

// Reads the horizon of a photo (`rgb8`, w x h, three 8-bit sRGB values per pixel).
inline Estimate estimate(const unsigned char* rgb8, int w, int h, const Options& options = Options()) {
	Estimate out;
	if (!rgb8 || w < 16 || h < 16) {
		out.why = "The photo is too small.";
		return out;
	}
	out.tiltDegrees = options.tiltGiven ? options.tiltDegrees : 0.0;
	out.rollDegrees = options.rollGiven ? options.rollDegrees : 0.0;
	if (options.tiltGiven && options.rollGiven) {
		out.ok = true;
		return out;
	}
	const light_from_photo::detail::Picture pic = light_from_photo::detail::reduce(rgb8, w, h, 192);
	const std::vector<int> end = light_from_photo::detail::skyColumnEnds(pic);
	size_t skyPixels = 0;
	for (const int e : end) skyPixels += static_cast<size_t>(e);
	out.skyFraction = static_cast<double>(skyPixels) / (static_cast<double>(pic.w) * pic.h);
	const double focal = (pic.w / 2.0) / std::tan(std::min(160.0, std::max(10.0, options.hfovDegrees)) * kPi / 360.0);   // pixels of the reduced picture
	const double cx = pic.w / 2.0, cy = pic.h / 2.0;

	std::vector<double> xs, rows;
	for (int x = 0; x < pic.w; ++x)
		if (end[static_cast<size_t>(x)] > 0 && end[static_cast<size_t>(x)] < pic.h - 1) {
			xs.push_back(x + 0.5);
			rows.push_back(end[static_cast<size_t>(x)]);
		}
	const auto fail = [&](const char* why) {
		out.ok = true;   // (the camera can still be made from what was given and level)
		out.why = why;
		return out;
	};
	if (out.skyFraction < 0.03) return fail("No sky was found (it looks for a blue or white sky that reaches the top of the picture), so the tilt and roll are not read: give them if the camera was not level.");
	if (xs.size() < static_cast<size_t>(0.4 * pic.w)) return fail("The horizon is not in the picture (the sky reaches the bottom, or is cut up by buildings or trees), so the tilt and roll are not read: give them if the camera was not level.");

	// the sky's lower edge is the horizon where nothing stands up into it; what does (buildings, trees) only raises the edge, so start from the lower half of the points and drop the ones that sit
	// above the line by more than the line's own spread
	std::vector<double> sorted = rows;
	std::sort(sorted.begin(), sorted.end());
	const double median = sorted[sorted.size() / 2];
	std::vector<char> use(xs.size(), 0);
	for (size_t i = 0; i < xs.size(); ++i) use[i] = rows[i] >= median ? 1 : 0;
	detail::Line line = detail::fit(xs, rows, use);
	double spread = 1.0;
	for (int pass = 0; pass < 6 && line.ok; ++pass) {
		double sq = 0.0;
		int n = 0;
		for (size_t i = 0; i < xs.size(); ++i)
			if (use[i]) {
				const double r = rows[i] - (line.a + line.b * xs[i]);
				sq += r * r;
				++n;
			}
		spread = std::max(0.75, std::sqrt(sq / std::max(n, 1)));
		for (size_t i = 0; i < xs.size(); ++i) {
			const double r = rows[i] - (line.a + line.b * xs[i]);
			use[i] = std::fabs(r) <= 2.0 * spread ? 1 : 0;
		}
		const detail::Line next = detail::fit(xs, rows, use);
		if (!next.ok) break;
		line = next;
	}
	int inliers = 0;
	double minX = 1e9, maxX = -1e9;
	for (size_t i = 0; i < xs.size(); ++i)
		if (use[i]) {
			++inliers;
			minX = std::min(minX, xs[i]);
			maxX = std::max(maxX, xs[i]);
		}
	if (!line.ok || inliers < static_cast<int>(0.3 * pic.w) || maxX - minX < 0.4 * pic.w) return fail("The sky's lower edge is too ragged to be a horizon (buildings or trees along it), so the tilt and roll are not read: give them if the camera was not level.");

	out.horizonFound = true;
	out.lineSpread = spread;
	const double slope = line.b;                                             // rows per pixel: positive when the horizon runs down to the right
	const double distance = (line.a + slope * cx - cy) / std::sqrt(1.0 + slope * slope);   // from the middle of the picture to the line, across it: positive when the line is below the middle
	out.horizonRowFraction = (line.a + slope * cx) / pic.h;
	if (!options.rollGiven) out.rollDegrees = std::max(-45.0, std::min(45.0, std::atan(slope) * 180.0 / kPi));
	if (!options.tiltGiven) out.tiltDegrees = std::max(-60.0, std::min(60.0, std::atan2(distance, focal) * 180.0 / kPi));
	// a horizon that is not a clean straight line (hills, dunes, trees or roofs along it) may not be the horizon at all
	const double dropped = 1.0 - static_cast<double>(inliers) / static_cast<double>(xs.size());   // the share of the sky's lower edge that was not on the line
	if (spread > 1.5 || dropped > 0.35) out.why = "The sky's lower edge is not a clean straight line (hills, trees or buildings along it), so the tilt and roll may be off: check them by eye, or give them.";
	else if (out.skyFraction < 0.12) out.why = "Only a small part of the picture is sky, so the horizon rests on little: check the tilt and roll by eye, or give them.";
	out.ok = true;
	return out;
}

// ---- the camera --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// The Builder camera that sees what a camera of horizontal field of view `hfovDegrees` tilted by `tiltDegrees` and rolled by `rollDegrees` (counter-clockwise seen from behind it) saw, in
// a picture photoW x photoH: `current`'s heading is kept, its position is moved to `heightMetres` above the ground (scene units are metres), and its lens radius and sensor are left alone.
inline scene_doc::Camera makeCamera(const scene_doc::Camera& current, double hfovDegrees, int photoW, int photoH, double tiltDegrees, double rollDegrees, double heightMetres) {
	scene_doc::Camera cam = current;
	double hx = current.target.x - current.position.x, hz = current.target.z - current.position.z;
	const double flat = std::sqrt(hx * hx + hz * hz);
	if (flat < 1e-9) { hx = 0.0; hz = -1.0; }
	else { hx /= flat; hz /= flat; }
	const double t = tiltDegrees * kPi / 180.0, r = rollDegrees * kPi / 180.0;
	const double fx = hx * std::cos(t), fy = std::sin(t), fz = hz * std::cos(t);                // where it looks
	const double ux = -hx * std::sin(t), uy = std::cos(t), uz = -hz * std::sin(t);              // its up, before the roll
	// right = forward x up (the Builder's world is right-handed; looking along -Z with Y up, right is +X)
	double rx = fy * uz - fz * uy, ry = fz * ux - fx * uz, rz = fx * uy - fy * ux;
	const double rl = std::sqrt(rx * rx + ry * ry + rz * rz);
	rx /= rl; ry /= rl; rz /= rl;
	// rolled counter-clockwise seen from behind: the top of the frame leans to the left
	cam.up = {ux * std::cos(r) - rx * std::sin(r), uy * std::cos(r) - ry * std::sin(r), uz * std::cos(r) - rz * std::sin(r)};
	const double distance = std::max(1.0, current.focusDistance > 0.0 ? current.focusDistance : 8.0);
	cam.position = {current.position.x, heightMetres, current.position.z};
	cam.target = {cam.position.x + fx * distance, cam.position.y + fy * distance, cam.position.z + fz * distance};
	const double aspect = photoW > 0 && photoH > 0 ? static_cast<double>(photoH) / photoW : 0.75;   // height over width
	cam.fov = std::min(170.0, std::max(1.0, 2.0 * std::atan(std::tan(std::min(160.0, std::max(5.0, hfovDegrees)) * kPi / 360.0) * aspect) * 180.0 / kPi));
	return cam;
}

}  // namespace camera_from_photo
