#pragma once
// shot_lighting.h -- lighting a shot's subject the way a film crew does: a key light, a fill and a rim (three-point lighting), placed round the subject from where the camera
// is, so they move with every camera and every shot. They are pbrt spot lights - they light, but are not seen - added to the frame's world:
//
//   key   the main light: 40 degrees to one side of the camera and 35 degrees up, warm (a face is lit and modelled, its shadow falls away from the camera)
//   fill  the soft one: 50 degrees to the other side, a little up, cool and weaker (the key's shadows are opened, not removed)
//   rim   behind the subject and high, almost white (an edge of light that lifts it off the background)
//
// Each one's strength is the light it gives the subject (its irradiance, the units a sun's `L` is in: the stage's sun is 2.2), so a shot from close or far is lit the same:
// a light stands as far from the subject as the camera does (1.5 to 30 units) and is that much stronger. The words (a film file's shot `lighting` line, a cameras file's
// section) are `key K`, `fill F`, `rim R` (0: none) and `side left|right` (where the key is, as the picture shows it); `lighting` alone is key 1.2, fill 0.4, rim 0.8.
// `only GROUP` makes the three light only the shapes of that link group (light linking, src/shared/light_link.h): `only cast` lights the characters of a film file and
// not its set (a film file puts each character's shapes in the group "cast/<name>", and "cast" names them all: film_world.cpp) - the crew's way of lifting a character off a background lit its own way.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include "cartoon_lib.h"

namespace cartoon {
namespace shot_lighting {

struct Lighting {
	bool on = false;
	double key = 1.2, fill = 0.4, rim = 0.8;
	double side = -1.0;   // -1: the key on the picture's left; 1: on its right
	std::string only;     // a link group the lights light alone (light linking); empty: everything
};

// The words after `lighting`: "" or what is wrong. It turns the lighting on.
inline std::string parse(const std::string &words, Lighting &l) {
	std::istringstream in(words);
	l.on = true;
	for (std::string w; in >> w;) {
		std::string v;
		if (!(in >> v)) return "\"" + w + "\" needs a value (key K, fill F, rim R, side left|right, only GROUP)";
		if (w == "only") {
			l.only = v;
			continue;
		}
		if (w == "side") {
			if (v != "left" && v != "right") return "side is left or right (where the key light is, as the picture shows it)";
			l.side = v == "left" ? -1.0 : 1.0;
			continue;
		}
		char *end = nullptr;
		const double x = std::strtod(v.c_str(), &end);
		if (!end || *end != '\0' || x < 0.0 || x > 50.0) return "\"" + w + "\" is a light's strength, from 0 (none) to 50";
		if (w == "key") l.key = x;
		else if (w == "fill") l.fill = x;
		else if (w == "rim") l.rim = x;
		else return "\"" + w + "\": the lighting takes key, fill, rim, side and only";
	}
	return std::string();
}

namespace detail {
inline Vec3 turned(Vec3 v, Vec3 axis, double degrees) {   // v turned about the unit axis (Rodrigues)
	const double a = degrees * kPi / 180.0, c = std::cos(a), s = std::sin(a);
	return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0 - c));
}
inline std::string spot(Vec3 from, Vec3 to, double strength, Vec3 colour, double distance, const std::string &only = std::string()) {
	char buf[600];
	const double I = strength * distance * distance;   // (I / d^2 at the subject: its irradiance there)
	// (story space to pbrt's: x negated)
	std::snprintf(buf, sizeof buf,
				  "AttributeBegin\n  LightSource \"spot\" \"point3 from\" [ %.4f %.4f %.4f ] \"point3 to\" [ %.4f %.4f %.4f ] \"rgb I\" [ %.4f %.4f %.4f ] \"float coneangle\" [ 42 ] "
				  "\"float conedeltaangle\" [ 30 ]%s%s%s\nAttributeEnd\n",
				  -from.x, from.y, from.z, -to.x, to.y, to.z, I * colour.x, I * colour.y, I * colour.z, only.empty() ? "" : " \"string illuminates\" [ \"", only.c_str(),
				  only.empty() ? "" : "\" ]");
	return buf;
}
}  // namespace detail

// The lights for a subject at `target` seen from `eye` (story space), as pbrt scene text for the world: empty when the lighting is off.
inline std::string lightsText(Vec3 eye, Vec3 target, const Lighting &l) {
	if (!l.on) return std::string();
	const Vec3 up{0.0, 1.0, 0.0};
	Vec3 back = eye - target;   // from the subject towards the camera, level
	back.y = 0.0;
	if (length(back) < 1e-6) back = Vec3{0.0, 0.0, 1.0};
	back = normalize(back);
	const double distance = std::clamp(length(eye - target), 1.5, 30.0);
	const auto from = [&](double yaw, double pitch) {   // yaw: degrees round from the camera's side (positive: towards story space's +x, the picture's right); pitch: up
		const Vec3 level = detail::turned(back, up, yaw);
		return target + (level * std::cos(pitch * kPi / 180.0) + up * std::sin(pitch * kPi / 180.0)) * distance;
	};
	std::string out = "# the shot's lighting (shot_lighting.h): key, fill and rim\n";
	const double keySide = l.side;
	if (l.key > 0.0) out += detail::spot(from(40.0 * keySide, 35.0), target, l.key, {1.0, 0.92, 0.80}, distance, l.only);
	if (l.fill > 0.0) out += detail::spot(from(-50.0 * keySide, 10.0), target, l.fill, {0.78, 0.86, 1.0}, distance, l.only);
	if (l.rim > 0.0) out += detail::spot(from(160.0 * keySide, 45.0), target, l.rim, {1.0, 0.97, 0.92}, distance, l.only);
	return out;
}

// `text` (pbrt scene text) with every Shape in link group `group` (light linking, src/shared/light_link.h): `"string linkgroup" "GROUP"` after each Shape's type.
inline std::string inLinkGroup(const std::string &text, const std::string &group) {
	std::string out;
	out.reserve(text.size() + 64);
	std::size_t from = 0;
	for (std::size_t at = text.find("Shape \"", from); at != std::string::npos; at = text.find("Shape \"", from)) {
		const std::size_t typeEnd = text.find('"', at + 7);
		if (typeEnd == std::string::npos) break;
		out.append(text, from, typeEnd + 1 - from);
		if (text.compare(typeEnd + 1, 19, " \"string linkgroup\"") != 0) out += " \"string linkgroup\" \"" + group + "\"";   // (one in a group already keeps it: a film's bubbles)
		from = typeEnd + 1;
	}
	out.append(text, from, std::string::npos);
	return out;
}

// `world` (a frame's text from its WorldBegin, or the whole scene) with the lights put in right after the WorldBegin line.
inline std::string withLights(const std::string &scene, const std::string &lights) {
	if (lights.empty()) return scene;
	const std::size_t world = scene.find("WorldBegin");
	if (world == std::string::npos) return scene;
	std::size_t end = scene.find('\n', world);
	end = end == std::string::npos ? scene.size() : end + 1;
	if (scene.find_first_not_of(" \t\r\n", end) == std::string::npos) return scene;   // (a header on its own, up to its WorldBegin: no world to light)
	return scene.substr(0, end) + lights + scene.substr(end);
}

}  // namespace shot_lighting
}  // namespace cartoon
