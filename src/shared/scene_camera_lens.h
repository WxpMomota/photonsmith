#pragma once
// scene_camera_lens.h -- the camera in a photographer's terms: a focal length in millimetres on a sensor of a real size, and a lens opening as an f-number, instead of a field of
// view in degrees and a lens radius in scene units. The document keeps the field of view and the lens radius (that is what the renderer reads, and every older scene has
// them); this header converts, so the Scene Builder can show and edit either.
//
// The film. A real sensor has a width (36 mm for "full frame") and the picture uses all of it: the horizontal field of view is 2 atan(width / (2 f)), and the vertical one follows
// from the picture's shape (width / height of the rendered image), so a 16:9 picture on a 36 mm sensor is 36 mm wide and 20 mm tall. (A real camera crops to the shape the same
// way.) With this the numbers are the ones a photograph's lens would give: 50 mm on full frame is about 39.6 degrees across, 24 mm about 73.7.
//
// The lens opening. A lens of focal length f at f-number N has an opening of diameter f / N, so the thin lens' radius is f / (2 N). Scene units are free, so this takes how
// many of them make a metre (1 by default: the starter scene is a few metres across): at 1 unit = 1 m, 50 mm at f/1.8 has a radius of 0.0139 units, which blurs what is off the
// focus distance the way that lens would on a person-sized scene.
//
// std-only, so the unit tests and the GUI share it.

#include "scene_model.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace scene_doc {

struct SensorPreset {
	const char* id;    // stable, never shown
	const char* name;  // what a user sees (translated by the GUI)
	double widthMm, heightMm;
};

inline const std::vector<SensorPreset>& sensorPresets() {
	static const std::vector<SensorPreset> all = {
		{"full-frame", "Full frame (36 x 24 mm)", 36.0, 24.0},
		{"aps-c", "APS-C (23.5 x 15.6 mm)", 23.5, 15.6},
		{"aps-c-canon", "APS-C Canon (22.3 x 14.9 mm)", 22.3, 14.9},
		{"micro-four-thirds", "Micro Four Thirds (17.3 x 13 mm)", 17.3, 13.0},
		{"super-35", "Super 35 film (24.9 x 18.7 mm)", 24.89, 18.66},
		{"medium-format", "Medium format (43.8 x 32.9 mm)", 43.8, 32.9},
		{"imax-70", "IMAX 70 mm film (70.4 x 52.6 mm)", 70.41, 52.63},
		{"one-inch", "1 inch (13.2 x 8.8 mm)", 13.2, 8.8},
		{"phone", "Phone (7.6 x 5.7 mm)", 7.6, 5.7},
	};
	return all;
}

// The preset whose width is `widthMm` (to a tenth of a millimetre), or null.
inline const SensorPreset* findSensorByWidth(double widthMm) {
	for (const SensorPreset& s : sensorPresets())
		if (std::fabs(s.widthMm - widthMm) < 0.05) return &s;
	return nullptr;
}

struct LensPreset {
	const char* id;
	const char* name;
	double focalMm;
};

inline const std::vector<LensPreset>& lensPresets() {
	static const std::vector<LensPreset> all = {
		{"ultra-wide", "14 mm ultra wide", 14.0}, {"wide", "24 mm wide", 24.0}, {"street", "35 mm street", 35.0}, {"normal", "50 mm normal", 50.0},
		{"portrait", "85 mm portrait", 85.0},     {"tele", "135 mm telephoto", 135.0}, {"long-tele", "200 mm long telephoto", 200.0}, {"super-tele", "400 mm super telephoto", 400.0},
	};
	return all;
}

// The f-numbers on a lens ring, widest opening first.
inline const std::vector<double>& standardFNumbers() {
	static const std::vector<double> all = {1.2, 1.4, 1.8, 2.0, 2.8, 4.0, 5.6, 8.0, 11.0, 16.0, 22.0};
	return all;
}

constexpr double kMinFovDeg = 1.0, kMaxFovDeg = 170.0;

// The picture's width over its height (1 for a degenerate one).
inline double aspectOf(const RenderSettings& r) { return r.height > 0 && r.width > 0 ? static_cast<double>(r.width) / r.height : 1.0; }

// The focal length (mm) that gives this vertical field of view on a sensor `sensorWidthMm` wide, with the picture `aspect` (width / height) wide.
inline double focalLengthMm(double fovDeg, double aspect, double sensorWidthMm) {
	const double t = std::tan(fovDeg * 3.14159265358979323846 / 360.0);
	return sensorWidthMm / (2.0 * std::max(1e-9, aspect) * std::max(1e-9, t));
}

// The vertical field of view (degrees) of a lens of `focalMm` on that sensor, kept to what the camera allows (1 to 170 degrees).
inline double fovFromFocalLength(double focalMm, double aspect, double sensorWidthMm) {
	const double t = sensorWidthMm / (2.0 * std::max(1e-9, focalMm) * std::max(1e-9, aspect));
	const double fov = 2.0 * std::atan(t) * 180.0 / 3.14159265358979323846;
	return std::min(kMaxFovDeg, std::max(kMinFovDeg, fov));
}

// The thin lens' radius (scene units) of a lens of `focalMm` opened to f/`fNumber`, with `unitsPerMetre` scene units to the metre. 0 (a pinhole) for a number that is not above 0.
inline double lensRadiusFromFNumber(double focalMm, double fNumber, double unitsPerMetre = 1.0) {
	if (!(fNumber > 0.0)) return 0.0;
	return focalMm / (2.0 * fNumber) / 1000.0 * unitsPerMetre;
}

// The f-number a lens radius means for that focal length; 0 for a pinhole (no radius).
inline double fNumberFromLensRadius(double focalMm, double lensRadius, double unitsPerMetre = 1.0) {
	if (!(lensRadius > 0.0) || !(unitsPerMetre > 0.0)) return 0.0;
	return focalMm * unitsPerMetre / (2.0 * lensRadius * 1000.0);
}

// The focus distance that puts `point` in focus: how far it is along the way the camera looks (the focal plane is square to that, not a sphere round the camera). At least 0.01.
inline double focusDistanceTo(const Camera& c, const Float3& point) {
	const double fx = c.target.x - c.position.x, fy = c.target.y - c.position.y, fz = c.target.z - c.position.z;
	const double fl = std::sqrt(fx * fx + fy * fy + fz * fz);
	if (fl < 1e-12) return 0.01;
	const double d = ((point.x - c.position.x) * fx + (point.y - c.position.y) * fy + (point.z - c.position.z) * fz) / fl;
	return std::max(0.01, d);
}

// The camera with this focal length: its field of view follows (the sensor and the lens radius are left alone).
inline Camera withFocalLength(const Camera& c, double focalMm, const RenderSettings& r) {
	Camera out = c;
	out.fov = fovFromFocalLength(focalMm, aspectOf(r), c.sensorWidthMm);
	return out;
}

}  // namespace scene_doc
