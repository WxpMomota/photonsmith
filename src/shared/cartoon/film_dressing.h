#pragma once
// film_dressing.h -- a set dressed from its film file (film_file.h `place`, `scatter`, `sky`, `ambience`): trees, rocks, ferns, bushes and grass from nature_lib.h placed or
// scattered on the ground (pbrt objects placed many times, swaying in the wind), a sun and a sky that move with the time of day (scene_sky.h's clear sky and sun, the Scene
// Builder's "Sun & sky"), and the set's sound - wind, birds, crickets, water, the night.
//
// Coordinates are pbrt's, as a film file's (story space is x mirrored). Grass, ferns and bushes are soft: a walk that finds its way (film_paths.h) goes through them; trees and
// rocks are in its way.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../scene_sky.h"
#include "cartoon_audio.h"
#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_sky.h"
#include "nature_lib.h"

namespace cartoon {
namespace dressing {

// A thing placed on the ground: its kind (tree, rock, fern, bush, grass), where (pbrt's x y z: its foot), its size, its turn (degrees) and a seed for its shape.
struct Placement {
	std::string kind;
	Vec3 at;
	double scale = 1.0, turn = 0.0;
	int seed = 0;
};
// Many of a kind over a rectangle of the ground (pbrt's x z), at height y, sizes between two scales.
struct Scatter {
	std::string kind;
	int count = 0;
	double x0 = 0.0, z0 = 0.0, x1 = 0.0, z1 = 0.0, y = 0.0;
	int seed = 1;
	double scale0 = 0.8, scale1 = 1.2;
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"tree", "rock", "fern", "bush", "grass"};
	return k;
}
inline bool soft(const std::string &kind) { return kind == "grass" || kind == "fern" || kind == "bush"; }

// The pieces, written once as pbrt objects (a soft piece's name says so: film_paths.h walks through it).
inline std::string grassMaterial(int v) {
	const Vec3 r[4] = {{0.20, 0.36, 0.08}, {0.24, 0.38, 0.09}, {0.28, 0.36, 0.10}, {0.18, 0.32, 0.08}};
	return translucentMaterial(r[v % 4], r[v % 4] * 1.25 + Vec3{0.06, 0.08, 0.0});
}
inline std::string leafMaterial(int v) {
	const Vec3 r[4] = {{0.16, 0.30, 0.07}, {0.20, 0.32, 0.07}, {0.24, 0.30, 0.08}, {0.14, 0.27, 0.07}};
	return translucentMaterial(r[v % 4], Vec3{0.30, 0.42, 0.10} + r[v % 4] * 0.4);
}
inline const std::string &objects() {
	static const std::string s = [] {
		std::string out;
		for (int v = 0; v < 4; ++v)
			out += nature::objectBegin("dress_soft_tuft" + std::to_string(v)) + attr(grassMaterial(v), emitMesh(nature::grassTuft(14, 0.32, 0.11, 0.012, 100 + 17 * v), identity())) + nature::objectEnd();
		for (int v = 0; v < 4; ++v)
			out += nature::objectBegin("dress_leaves" + std::to_string(v)) + attr(leafMaterial(v), emitMesh(nature::leafCluster(150, 2.2, 0.22, 300 + 23 * v), identity())) + nature::objectEnd();
		for (int v = 0; v < 2; ++v)
			out += nature::objectBegin("dress_soft_bush" + std::to_string(v)) + attr(leafMaterial(v + 1), emitMesh(nature::leafCluster(110, 1.2, 0.16, 800 + 29 * v), identity())) + nature::objectEnd();
		for (int v = 0; v < 2; ++v)
			out += nature::objectBegin("dress_soft_fern" + std::to_string(v)) + attr(translucentMaterial({0.12, 0.25, 0.07}, {0.22, 0.36, 0.08}), emitMesh(nature::fern(9, 0.9, 500 + 31 * v), identity())) +
				   nature::objectEnd();
		for (int v = 0; v < 3; ++v)
			out += nature::objectBegin("dress_trunk" + std::to_string(v)) + attr(coated({0.11, 0.085, 0.065}, 0.85), emitMesh(nature::trunk(1.0, 1.0, 0.0, 600 + 7 * v), identity())) + nature::objectEnd();
		out += nature::objectBegin("dress_branch") + attr(coated({0.10, 0.08, 0.06}, 0.85), emitMesh(lathe({{1.0, 0.0}, {0.75, 0.5}, {0.5, 1.0}, {0.0, 1.0}}, 7), identity())) + nature::objectEnd();
		for (int v = 0; v < 3; ++v)
			out += nature::objectBegin("dress_rock" + std::to_string(v)) + attr(coated({0.30, 0.29, 0.26}, 0.7), emitMesh(nature::rock(1.0, 700 + 13 * v), identity())) + nature::objectEnd();
		return out;
	}();
	return s;
}

// Gusts of wind (film_file.h `gusts`): fronts that sweep across the land at `speed` along `wind` (story space), bending what grows as they pass.
struct Gusts {
	double strength = 0.0, dx = 0.0, dz = 1.0, speed = 7.0;
};
// How much a gust bends what grows at story-space (x, z) at t (0 between fronts): a sharp front, a slow easing behind it, the fronts' lines broken by noise.
inline Vec3 gustAt(const Gusts &g, double x, double z, double t) {
	if (g.strength <= 0.0) return {0.0, 0.0, 0.0};
	const double along = x * g.dx + z * g.dz, across = -x * g.dz + z * g.dx;
	const double phase = (along - g.speed * t) / 28.0 + 0.6 * nature::noise2(across / 40.0, t * 0.05, 61);
	const double f = phase - std::floor(phase);
	const double pulse = f < 0.15 ? smoothstep(0.0, 0.15, f) : 1.0 - smoothstep(0.15, 0.7, f);
	return Vec3{g.dx, 0.0, g.dz} * (g.strength * pulse * pulse);
}
// A placed thing at t (its leaves and grass sway in the wind, and bend in its gusts), as scene text.
inline std::string placementText(const Placement &p, double t, const Gusts &gusts = {}) {
	const Vec3 foot{-p.at.x, p.at.y, p.at.z};   // (story space)
	const double s = p.scale;
	const int salt = p.seed * 7919 + static_cast<int>(std::lround(p.at.x * 13.0 + p.at.z * 71.0));
	const Mat4 place = mul(translate(foot), rotate({0, 1, 0}, p.turn));
	const auto variant = [&](int n, int k) { return static_cast<int>(hash01(salt, k) * n) % n; };
	if (p.kind == "rock") return nature::placeObject("dress_rock" + std::to_string(variant(3, 1)), mul(translate(foot - Vec3{0.0, 0.05 * s, 0.0}), rotate({0, 1, 0}, p.turn), scale(s)));   // (1 m across, a third of that high)
	if (p.kind == "fern") return nature::placeObject("dress_soft_fern" + std::to_string(variant(2, 1)), mul(place, scale(s)));
	if (p.kind == "grass") {
		const Vec3 w = nature::windLean(foot.x, foot.z, t, 0.06) + gustAt(gusts, foot.x, foot.z, t) * 0.5;
		return nature::placeObject("dress_soft_tuft" + std::to_string(variant(4, 1)), mul(translate(foot), nature::shearXZ(w.x, w.z), rotate({0, 1, 0}, p.turn), scale(s)));
	}
	if (p.kind == "bush") {
		const Vec3 w = nature::windLean(foot.x, foot.z, t, 0.02) + gustAt(gusts, foot.x, foot.z, t) * 0.2;
		return nature::placeObject("dress_soft_bush" + std::to_string(variant(2, 1)), mul(translate(foot + Vec3{0.0, 0.35 * s, 0.0}), nature::shearXZ(w.x, w.z), rotate({0, 1, 0}, p.turn), scale(0.7 * s, 0.55 * s, 0.7 * s)));
	}
	// a tree: its trunk (the unit one scaled), branches out to its leaf clusters, and the clusters, which sway a little
	const double height = 3.6 * s, radius = 0.18 * s;
	std::string out = nature::placeObject("dress_trunk" + std::to_string(variant(3, 1)), mul(place, scale(radius, height, radius)));
	const int clusters = 13 + static_cast<int>(6 * hash01(salt, 51));
	for (int c = 0; c < clusters; ++c) {
		const double h = 0.58 + 0.40 * hash01(salt * 31 + c, 52);
		const double a = 2.0 * kPi * hash01(salt * 31 + c, 53), reach = (0.5 + 2.0 * hash01(salt * 31 + c, 54)) * (1.15 - h) * s;
		const Vec3 onTrunk{0.0, h * height, 0.0};
		const Vec3 centre = onTrunk + Vec3{reach * std::cos(a), (0.5 + 0.6 * hash01(salt * 31 + c, 55)) * s, reach * std::sin(a)};
		const Vec3 w = nature::windLean(foot.x + centre.x, foot.z + centre.z, t, 0.02);
		const Vec3 from = transformPoint(place, onTrunk), to = transformPoint(place, centre * 0.92 + onTrunk * 0.08);
		const double r = (0.04 + 0.03 * (1.0 - h)) * s;
		out += nature::placeObject("dress_branch", mul(alignY(from, to - from), scale(r, length(to - from), r)));
		out += nature::placeObject("dress_leaves" + std::to_string(((salt + c) % 4 + 4) % 4),   // (a tree left of or behind the middle has a negative salt)
								   mul(place, translate(centre), nature::shearXZ(w.x, w.z), rotate({0, 1, 0}, 360.0 * hash01(salt * 31 + c, 57)), scale((0.8 + 0.5 * hash01(salt * 31 + c, 56)) * s)));
	}
	return out;
}

// A scatter's things: placed by a fixed sequence (the same every frame), sizes and turns from it too.
inline std::vector<Placement> placementsOf(const Scatter &sc) {
	std::vector<Placement> out;
	for (int i = 0; i < sc.count; ++i) {
		Placement p;
		p.kind = sc.kind;
		p.at = {sc.x0 + (sc.x1 - sc.x0) * hash01(sc.seed * 1009 + i, 1), sc.y, sc.z0 + (sc.z1 - sc.z0) * hash01(sc.seed * 1009 + i, 2)};
		p.scale = sc.scale0 + (sc.scale1 - sc.scale0) * hash01(sc.seed * 1009 + i, 3);
		p.turn = 360.0 * hash01(sc.seed * 1009 + i, 4);
		p.seed = sc.seed * 1009 + i;
		out.push_back(p);
	}
	return out;
}

// ---- the time of day ----
// A sun and sky that follow the time of day from `from` to `to` (hours, 18.5 = 6:30 pm) over the film: where the sun is comes from the latitude and the day of the year, and
// `south` is which way south is on the set (degrees as a character's `facing`: 0 +z, 90 +x; west is a quarter turn on from it, 270 when south is 180).
struct Sky {
	double from = 12.0, to = 12.0;
	double south = 180.0, latitude = 40.0, haze = 3.0;
	int day = 172;   // (the longest day)
	bool physical = false;   // the sky and the sun's light from the atmosphere itself (atmosphere.h), seen from `altitude` metres above the sea
	double altitude = 0.0;
};
// The sun's height above the horizon and its direction on the ground (a `facing`, degrees) at an hour.
inline void sunAt(const Sky &s, double hour, double &elevation, double &facing) {
	const double d2r = kPi / 180.0;
	const double dec = 23.44 * std::sin(2.0 * kPi * (284.0 + s.day) / 365.0) * d2r, lat = s.latitude * d2r, H = 15.0 * (hour - 12.0) * d2r;
	const double sinEl = std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(H);
	elevation = std::asin(std::max(-1.0, std::min(1.0, sinEl))) / d2r;
	const double fromSouth = std::atan2(std::sin(H), std::cos(H) * std::sin(lat) - std::tan(dec) * std::cos(lat)) / d2r;   // (towards the west, positive)
	facing = s.south + fromSouth;
}
inline double hourAt(const Sky &s, double t, double length) { return s.from + (s.to - s.from) * (length > 0.0 ? std::max(0.0, std::min(1.0, t / length)) : 0.0); }
// The sun (a distant light) and the sky (an infinite light: film_sky.h's picture, made once for each quarter degree of the sun, in the film's cache folder) at t, as scene
// text. Round sunset a glow round the sun and a warm afterglow along the horizon under it, the pink belt opposite; below the horizon the sun goes out and the sky dims into
// twilight, then night: the Milky Way and the stars, turning about the celestial pole as the hours go by. `rainbow` (0 to 1) paints a rainbow opposite the sun.
// `storm` (0 .. 1, film_file.h `storm`; `stormKind` rain, snow or sand): the sky darkens under a deck of cloud (a dust storm's turns ochre), the sun dims behind it, rain shafts
// hang under it round the horizon, and the deck drifts (the sky's picture turned a little with the time).
struct SkyStorm {
	double level = 0.0;
	std::string kind = "rain";
};
inline std::string skyText(const std::string &film, const Sky &s, double t, double length, double rainbow = 0.0, const SkyStorm &storm = SkyStorm()) {
	double el = 0.0, facing = 0.0;
	const double hour = hourAt(s, t, length);
	sunAt(s, hour, el, facing);
	const double d2r = kPi / 180.0;
	const double ce = std::cos(std::max(0.5, el) * d2r);
	const Vec3 toward{ce * std::sin(facing * d2r), std::sin(std::max(0.5, el) * d2r), ce * std::cos(facing * d2r)};   // (pbrt's)
	scene_doc::SkyParams p;
	p.sunElevation = std::round(std::max(0.5, el) * 10.0) / 10.0;
	p.sunAzimuth = std::round(std::fmod(std::atan2(-toward.z, toward.x) / d2r + 720.0, 360.0));   // (scene_sky.h: 0 is +x, 90 is -z)
	p.turbidity = s.haze;
	scene_doc::sky::SunLight sun = scene_doc::sky::sunLight(p);
	if (s.physical) {   // (the sunlight through the air from the atmosphere itself: red and faint at the horizon, gone when the Earth hides the sun)
		const std::shared_ptr<const atmosphere::Model> air = atmosphere::modelFor(film_sky::physicalHaze(s.haze));
		const double up[3] = {0.0, std::sin(el * d2r), std::cos(el * d2r)};
		const atmosphere::RGB L = air->sunAt(up, s.altitude) * film_sky::physicalScale(*air);
		sun.irradiance = std::max(1e-9, L.luminance());
		sun.colour.r = L.r / sun.irradiance, sun.colour.g = L.g / sun.irradiance, sun.colour.b = L.b / sun.irradiance;
		if (!(L.luminance() > 1e-6)) sun.irradiance = 0.0;
	}
	const double stormy = std::round(std::clamp(storm.level, 0.0, 1.0) * 20.0) / 20.0;   // (in twentieths: a sky picture for each)
	sun.irradiance *= 1.0 - (storm.kind == "sand" ? 0.85 : 0.97) * std::min(1.0, stormy / 0.6);   // (behind the deck: hidden once it has closed)
	const double twilight = std::max(0.02, smoothstep(-7.0, 0.5, el)), sunUp = s.physical ? (sun.irradiance > 0.0 ? 1.0 : 0.0) : smoothstep(-0.4, 1.2, el);
	std::string path, error;
	char buf[400];
	std::string out;
	if (sunUp > 0.0) {
		std::snprintf(buf, sizeof buf, "LightSource \"distant\" \"point3 from\" [ %.5f %.5f %.5f ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ %.5f %.5f %.5f ]\n", toward.x, toward.y, toward.z,
					  sun.colour.r * sun.irradiance * sunUp, sun.colour.g * sun.irradiance * sunUp, sun.colour.b * sun.irradiance * sunUp);
		out += buf;
	}
	(void)twilight, (void)path, (void)error;
	film_sky::Spec fs;
	fs.width = 2048;
	fs.sunElevation = std::round(el * 4.0) / 4.0;
	const double hx = -std::sin(facing * d2r), hz = std::cos(facing * d2r);   // (the sun's way in story space: film_sky's azimuth runs from -z towards +x)
	fs.sunAzimuth = std::round(std::atan2(hx, -hz) / d2r);
	fs.turbidity = s.haze;
	const double night = smoothstep(-5.0, -15.0, el) * (el < -5.0 ? 1.0 : 0.0);
	fs.night = std::round(night * 20.0) / 20.0;
	fs.milkyWay = fs.stars = fs.night;
	const double low = smoothstep(-9.0, -1.0, el) * (1.0 - smoothstep(3.0, 14.0, el));   // (the sun near the horizon)
	fs.glow = std::round(0.6 * low * 20.0) / 20.0;
	fs.afterglow = std::round(0.45 * low * 20.0) / 20.0;
	fs.belt = std::round(0.3 * smoothstep(-6.0, -1.0, el) * (1.0 - smoothstep(1.0, 6.0, el)) * 20.0) / 20.0;
	fs.rainbow = std::round(std::max(0.0, std::min(1.0, rainbow)) * 20.0) / 20.0;
	if (stormy > 0.0) {   // the storm's deck of cloud, dark beneath, over all but a little of the sky; the sky under it dim and grey (ochre in a dust storm)
		film_sky::Clouds deck;
		deck.altitude = 1400.0, deck.scaleAlong = 7000.0, deck.scaleAcross = 3500.0, deck.coverage = std::min(0.97, 0.4 + 0.6 * stormy), deck.softness = 0.22;
		deck.lit = storm.kind == "sand" ? Vec3{0.55, 0.42, 0.28} : Vec3{0.42, 0.44, 0.47};
		deck.shade = storm.kind == "sand" ? Vec3{0.3, 0.2, 0.11} : Vec3{0.17, 0.18, 0.2};
		deck.silver = 0.3, deck.opacity = 0.97, deck.seed = 29;
		fs.clouds.push_back(deck);
		fs.brightness *= 1.0 - 0.5 * stormy;
		fs.tint = lerp(Vec3{1.0, 1.0, 1.0}, storm.kind == "sand" ? Vec3{1.0, 0.72, 0.42} : Vec3{0.85, 0.9, 1.0}, stormy);
		fs.glow *= 1.0 - stormy, fs.afterglow *= 1.0 - stormy, fs.belt *= 1.0 - stormy;
		if (storm.kind == "rain") fs.shafts = stormy;
		else fs.shafts = 0.0;
	}
	if (s.physical) {   // (the glow round the sun, the afterglow and the belt of Venus are the atmosphere's own)
		fs.physical = true;
		fs.altitude = s.altitude;
		fs.glow = fs.afterglow = fs.belt = 0.0;
	}
	// the stars turn about the celestial pole, 15 degrees an hour (the sky's picture with them, as far as it is night)
	const double nf = (s.south + 180.0) * d2r, lat = s.latitude * d2r;
	const Vec3 pole{std::sin(nf) * std::cos(lat), std::sin(lat), std::cos(nf) * std::cos(lat)};
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Rotate %.4f %.5f %.5f %.5f\n", -15.0 * (hour - s.from) * fs.night, pole.x, pole.y, pole.z);
	out += buf;
	if (stormy > 0.0) {   // (the deck drifting: the sky turned a fifth of a degree a second)
		std::snprintf(buf, sizeof buf, "  Rotate %.4f 0 1 0\n", 0.2 * t);
		out += buf;
	}
	out += "  LightSource \"infinite\" \"string filename\" [ \"" + film_sky::file(film, fs) + "\" ]\nAttributeEnd\n";
	return out;
}

// ---- the set's sound ----
// A sound that goes on under the whole film: wind, birds, crickets, water (a stream), night (crickets and an owl), sea (the wash of waves, each breaking now and then) or
// falls (the roar of a waterfall), at a gain.
struct Ambience {
	std::string kind;
	double gain = 1.0;
};
inline const std::vector<std::string> &ambienceKinds() {
	static const std::vector<std::string> k = {"wind", "birds", "crickets", "water", "night", "sea", "falls", "geese"};
	return k;
}
namespace sound {
inline void wind(audio::Mix &m, double length, double gain) {
	for (double s = 0.0; s < length; s += 1.6) {
		const double g = gain * (0.7 + 0.3 * std::sin(s * 0.7 + 1.3));
		m.put(s, audio::noiseBurst(3.0, 1.1, 0.6, 260.0, 520.0, 21u + static_cast<unsigned>(s * 10.0)), 0.12 * g, 0.7 * std::sin(s * 0.3), 0.35);
	}
}
inline void birds(audio::Mix &m, double length, double gain) {
	for (double t = 0.6; t < length; t += 1.2 + 2.6 * audio::unit(static_cast<unsigned>(t * 100.0) + 7u)) {
		const unsigned seed = static_cast<unsigned>(t * 1000.0) + 3u;
		const int notes = 3 + static_cast<int>(4.0 * audio::unit(seed));
		const double pan = -0.8 + 1.6 * audio::unit(seed + 5u);
		double at = t;
		for (int k = 0; k < notes; ++k) {
			const double f0 = 2300.0 + 2600.0 * audio::unit(seed * 7u + static_cast<unsigned>(k)), f1 = f0 * (0.75 + 0.6 * audio::unit(seed * 13u + static_cast<unsigned>(k)));
			const double len = 0.05 + 0.10 * audio::unit(seed * 17u + static_cast<unsigned>(k));
			m.put(at, audio::chirp(f0, f1, len, 18.0, 0.03, 1.0), 0.05 * gain, pan, 0.35);
			at += len + 0.03 + 0.06 * audio::unit(seed * 19u + static_cast<unsigned>(k));
		}
	}
}
inline void crickets(audio::Mix &m, double length, double gain) {
	for (int v = 0; v < 4; ++v) {
		const double f = 4300.0 + 260.0 * v, period = 0.55 + 0.13 * v, pan = -0.7 + 0.45 * v;
		for (double t = 0.15 * v; t < length; t += period) {
			if (audio::unit(static_cast<unsigned>(t * 1000.0) + 31u * static_cast<unsigned>(v)) > 0.7) continue;
			for (int k = 0; k < 3; ++k) m.put(t + 0.045 * k, audio::chirp(f, f * 0.995, 0.028, 0.0, 0.0), 0.04 * gain, pan, 0.25);
		}
	}
}
inline void water(audio::Mix &m, double length, double gain) {
	for (double s = 0.0; s < length; s += 1.0)   // the rush: low noise, overlapping
		m.put(s, audio::noiseBurst(2.2, 0.6, 0.6, 500.0, 1400.0, 51u + static_cast<unsigned>(s * 10.0)), 0.07 * gain, -0.3, 0.3);
	for (double t = 0.1; t < length; t += 0.05 + 0.18 * audio::unit(static_cast<unsigned>(t * 1000.0) + 11u)) {   // the bubbling: quick rising blips
		const double f = 280.0 + 700.0 * audio::unit(static_cast<unsigned>(t * 997.0) + 3u);
		m.put(t, audio::chirp(f, f * 1.6, 0.035, 0.0, 0.0), 0.025 * gain, -0.6 + 1.2 * audio::unit(static_cast<unsigned>(t * 991.0)), 0.3);
	}
}
inline void sea(audio::Mix &m, double length, double gain) {
	for (double s = 0.0; s < length; s += 1.4)   // the wash: low noise, overlapping
		m.put(s, audio::noiseBurst(2.8, 1.0, 0.8, 300.0, 700.0, 61u + static_cast<unsigned>(s * 10.0)), 0.06 * gain, 0.4 * std::sin(s * 0.2), 0.4);
	for (double t = 1.5; t < length; t += 5.5 + 3.5 * audio::unit(static_cast<unsigned>(t * 100.0) + 3u)) {   // a wave breaking: a swell of noise that opens up, then a long hiss
		const unsigned seed = static_cast<unsigned>(t * 1000.0) + 5u;
		m.put(t, audio::noiseBurst(1.2, 0.9, 0.2, 400.0, 2400.0, seed), 0.12 * gain, -0.5 + audio::unit(seed), 0.5);
		m.put(t + 1.0, audio::noiseBurst(3.5, 0.05, 3.2, 2600.0, 900.0, seed + 1u), 0.07 * gain, -0.5 + audio::unit(seed + 2u), 0.5);
	}
}
inline void falls(audio::Mix &m, double length, double gain) {
	for (double s = 0.0; s < length; s += 0.8) {   // a roar: broad noise, low and high, overlapping so it never breathes
		m.put(s, audio::noiseBurst(1.8, 0.6, 0.6, 180.0, 400.0, 71u + static_cast<unsigned>(s * 10.0)), 0.10 * gain, -0.2, 0.3);
		m.put(s + 0.4, audio::noiseBurst(1.8, 0.6, 0.6, 1500.0, 3000.0, 73u + static_cast<unsigned>(s * 10.0)), 0.035 * gain, 0.2, 0.3);
	}
}
inline void geese(audio::Mix &m, double length, double gain) {   // honking: two or three calls together, now and then
	for (double t = 0.8; t < length; t += 1.4 + 2.6 * audio::unit(static_cast<unsigned>(t * 100.0) + 41u)) {
		const unsigned seed = static_cast<unsigned>(t * 1000.0) + 43u;
		const int calls = 2 + static_cast<int>(2.0 * audio::unit(seed));
		for (int k = 0; k < calls; ++k) {
			const double f = 330.0 + 90.0 * audio::unit(seed + static_cast<unsigned>(k) * 7u), at = t + 0.22 * k;
			for (int hm = 1; hm <= 3; ++hm) m.put(at, audio::chirp(f * hm, f * hm * 0.88, 0.2, 6.0, 0.01, 1.0), 0.03 * gain / hm, -0.4 + 0.8 * audio::unit(seed + 3u), 0.5);
		}
	}
}
inline void night(audio::Mix &m, double length, double gain) {
	crickets(m, length, gain);
	for (double t = 3.0; t < length; t += 9.0 + 6.0 * audio::unit(static_cast<unsigned>(t * 10.0) + 17u)) {   // an owl far off
		m.put(t, audio::chirp(415.0, 380.0, 0.38, 4.0, 0.01, 1.0), 0.08 * gain, -0.6, 0.8);
		m.put(t + 0.55, audio::chirp(410.0, 360.0, 0.55, 4.0, 0.01, 1.0), 0.07 * gain, -0.6, 0.8);
	}
}
}  // namespace sound
inline void play(audio::Mix &m, const std::vector<Ambience> &beds) {
	for (const Ambience &a : beds) {
		if (a.kind == "wind") sound::wind(m, m.seconds(), a.gain);
		else if (a.kind == "birds") sound::birds(m, m.seconds(), a.gain);
		else if (a.kind == "crickets") sound::crickets(m, m.seconds(), a.gain);
		else if (a.kind == "water") sound::water(m, m.seconds(), a.gain);
		else if (a.kind == "night") sound::night(m, m.seconds(), a.gain);
		else if (a.kind == "sea") sound::sea(m, m.seconds(), a.gain);
		else if (a.kind == "falls") sound::falls(m, m.seconds(), a.gain);
		else if (a.kind == "geese") sound::geese(m, m.seconds(), a.gain);
	}
}

}  // namespace dressing
}  // namespace cartoon
