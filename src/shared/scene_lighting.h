#pragma once
// scene_lighting.h -- ready-made lighting for the Scene Builder: a studio rig, an overcast day, a sunny day, golden hour, a moonlit night, a light box. Choosing one replaces
// the document's lights (an object that gives off light stays) with a rig placed round what the camera looks at, so it works for a scene of any size and from any side; the
// sky presets use Sun & sky (scene_sky.h), whose picture and Sun the Scene Builder makes from the settings. Qt-free and std-only, so the GUI and the unit tests use the same code.
//
// Units are the starter scene's: a sun gives an irradiance of 3 on a surface facing it, a point or spot light of strength I lights a surface d away facing it with I / d^2, and a
// sky of brightness L lights a level surface with pi * L. Each rig below is a few times that, split between its lights the way the name says.

#include "scene_model.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace scene_doc {
namespace lighting {

enum class Preset { Studio, Overcast, Sunny, GoldenHour, Moonlit, LightBox };

inline const std::vector<Preset>& allPresets() {
	static const std::vector<Preset> all = {Preset::Studio, Preset::Overcast, Preset::Sunny, Preset::GoldenHour, Preset::Moonlit, Preset::LightBox};
	return all;
}

namespace detail {
constexpr double kPi = 3.14159265358979323846;

struct Frame {
	Float3 subject;   // what the camera looks at
	double distance;  // how far the camera is from it (clamped), the size the rig is made for
	Float3 back;      // horizontal, from the subject towards the camera
	Float3 right;     // horizontal, to the camera's right
};

inline Frame frameOf(const Camera& c) {
	Frame f;
	f.subject = c.target;
	double fx = c.target.x - c.position.x, fz = c.target.z - c.position.z;
	double len = std::sqrt(fx * fx + fz * fz);
	if (len < 1e-9) { fx = 0.0; fz = -1.0; len = 1.0; }   // looking straight down or up: treat the camera as looking along -Z
	fx /= len;
	fz /= len;
	f.back = {-fx, 0.0, -fz};
	f.right = {-fz, 0.0, fx};
	const double dx = c.target.x - c.position.x, dy = c.target.y - c.position.y, dz = c.target.z - c.position.z;
	f.distance = std::clamp(std::sqrt(dx * dx + dy * dy + dz * dz), 2.0, 500.0);
	return f;
}

// The unit direction from the subject towards a light that is `azimuth` degrees round from the camera's side of the subject (positive to the camera's right) and `elevation` above the floor.
inline Float3 directionTo(const Frame& f, double azimuthDeg, double elevationDeg) {
	const double a = azimuthDeg * kPi / 180.0, e = elevationDeg * kPi / 180.0;
	const double h = std::cos(e), cs = std::cos(a), sn = std::sin(a);
	return {h * (cs * f.back.x + sn * f.right.x), std::sin(e), h * (cs * f.back.z + sn * f.right.z)};
}

// The sun's azimuth in SkyParams' terms (0 towards +X, 90 towards -Z) for a direction.
inline double skyAzimuthOf(const Float3& dir) {
	double a = std::atan2(-dir.z, dir.x) * 180.0 / kPi;
	if (a < 0.0) a += 360.0;
	return a;
}

inline Light spotAt(const Frame& f, const char* name, double azimuth, double elevation, double irradiance, Rgb colour, double cone, double soft) {
	Light l;
	l.name = name;
	l.kind = LightKind::Spot;
	const double d = f.distance * 0.9;
	const Float3 dir = directionTo(f, azimuth, elevation);
	l.position = {f.subject.x + dir.x * d, f.subject.y + dir.y * d, f.subject.z + dir.z * d};
	l.target = f.subject;
	l.color = colour;
	l.intensity = irradiance * d * d;   // irradiance on a surface at the subject, facing the light
	l.coneAngle = cone;
	l.coneDelta = soft;
	return l;
}

inline Light skyColour(const char* name, Rgb colour, double brightness) {
	Light l;
	l.name = name;
	l.kind = LightKind::Infinite;
	l.color = colour;
	l.intensity = brightness;
	return l;
}

inline Light physicalSky(const char* name, const Frame& f, double sunHeight, double azimuthFromCamera, double haze) {
	Light l;
	l.name = name;
	l.kind = LightKind::Infinite;
	l.intensity = 1.0;   // brightness 1 is real daylight (scene_sky.h)
	l.physicalSky = true;
	l.sky.sunElevation = sunHeight;
	l.sky.sunAzimuth = skyAzimuthOf(directionTo(f, azimuthFromCamera, 0.0));
	l.sky.turbidity = haze;
	return l;
}
}  // namespace detail

// What it is called, in English (the GUI translates it).
inline const char* presetName(Preset p) {
	switch (p) {
		case Preset::Studio: return "Studio (three lights)";
		case Preset::Overcast: return "Overcast day";
		case Preset::Sunny: return "Sunny day";
		case Preset::GoldenHour: return "Golden hour";
		case Preset::Moonlit: return "Moonlit night";
		case Preset::LightBox: return "Light box";
	}
	return "";
}

// A sentence on what it does, in English.
inline const char* presetNote(Preset p) {
	switch (p) {
		case Preset::Studio: return "A key light from the camera's left, a softer fill from its right and a rim light from behind, on a dark backdrop.";
		case Preset::Overcast: return "Even, soft light from a grey-white sky with no sun and no hard shadows.";
		case Preset::Sunny: return "A clear sky with the sun high behind the camera's right shoulder.";
		case Preset::GoldenHour: return "A clear sky with the sun low and to the side, long shadows and warm light.";
		case Preset::Moonlit: return "A dark blue sky and a dim cool moon from above and behind.";
		case Preset::LightBox: return "A bright white surround with a little light from above, as for a product shot.";
	}
	return "";
}

// The lights of the document become this lighting (an object that gives off light stays). The rig goes round what the camera looks at, at the camera's distance.
inline void apply(Document& d, Preset p) {
	const detail::Frame f = detail::frameOf(d.camera);
	std::vector<Light> rig;
	switch (p) {
		case Preset::Studio:
			rig.push_back(detail::spotAt(f, "Key light", -40.0, 40.0, 2.2, {1.0, 0.95, 0.88}, 45.0, 15.0));
			rig.push_back(detail::spotAt(f, "Fill light", 50.0, 15.0, 0.7, {0.85, 0.9, 1.0}, 55.0, 25.0));
			rig.push_back(detail::spotAt(f, "Rim light", 150.0, 35.0, 1.4, {1.0, 1.0, 1.0}, 45.0, 15.0));
			rig.push_back(detail::skyColour("Studio backdrop", {0.8, 0.85, 1.0}, 0.1));
			break;
		case Preset::Overcast:
			rig.push_back(detail::skyColour("Overcast sky", {0.9, 0.93, 1.0}, 0.8));
			break;
		case Preset::Sunny:
			rig.push_back(detail::physicalSky("Sun and sky", f, 55.0, 40.0, 3.0));
			break;
		case Preset::GoldenHour:
			rig.push_back(detail::physicalSky("Sun and sky", f, 8.0, 100.0, 3.5));
			break;
		case Preset::Moonlit: {
			rig.push_back(detail::skyColour("Night sky", {0.3, 0.4, 0.75}, 0.06));
			Light moon;
			moon.name = "Moon";
			moon.kind = LightKind::Distant;
			const Float3 dir = detail::directionTo(f, 150.0, 40.0);
			moon.position = {f.subject.x + dir.x * f.distance, f.subject.y + dir.y * f.distance, f.subject.z + dir.z * f.distance};
			moon.target = f.subject;
			moon.color = {0.75, 0.82, 1.0};
			moon.intensity = 0.35;
			rig.push_back(moon);
			break;
		}
		case Preset::LightBox:
			rig.push_back(detail::skyColour("Light box", {1.0, 1.0, 1.0}, 1.0));
			rig.push_back(detail::spotAt(f, "Top light", 0.0, 80.0, 1.2, {1.0, 1.0, 1.0}, 50.0, 20.0));
			break;
	}
	d.lights = std::move(rig);
}

}  // namespace lighting
}  // namespace scene_doc
