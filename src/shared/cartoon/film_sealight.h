#pragma once
// film_sealight.h -- the light under the sea, round 15: the net of light the waves focus (caustics) on everything in the water, and the sun seen up through the surface.
//
//   caustics on everything   most of the sun's light comes down through the water as a projector's slide of the caustic net (a sharp picture 1024 pixels across, in 4 m
//                            tiles, looping every 2.4 s), aimed down the sun's way as the surface bends it and placed above the water 150 m back along that way, so the net
//                            plays over the sand, the rocks, the fish, a diver - anything up to 24 m round the camera (less near the sand: reachFor) - and the water shows it as shafts of light
//                            fanning down. The sun itself keeps the rest (a share of 1 - kCausticShare) so shadows stay. Deeper beds get a softer net (the focus blurs
//                            on its way down: underwater::causticBand). The seabed's painted net (film_underwater.h) gives way to plain sand under it. The GPU reads the slide
//                            whole (gpu/optix ProjectionLightGPU::texIdx); before round 15 it saw it at 64 x 64.
//   the sun from below       looking up, the sun is a cluster of glints dancing on the ripples where the line to it leaves the water (each glint lit for a moment, never
//                            sampled as a light), and the lens draws them as small stars.
//   rain from below          with rain falling (`weather rain`, a rainy `storm`), each drop that lands near the camera leaves a widening silver ring under the surface and a
//                            few tiny bubbles.
//
// Coordinates are pbrt's; meshes are made in story space (x mirrored) for emitMesh.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <regex>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_sky.h"
#include "film_underwater.h"
#include "nature_lib.h"

namespace cartoon {
namespace sealight {

constexpr double kCausticShare = 0.8;   // (how much of the sun the caustic slide carries)
constexpr double kReach = 24.0;         // (the slide covers this far each way round the camera, at most)
constexpr double kBack = 150.0;         // (how far back along the sun's way the projector stands)
constexpr int kSize = 1024;
// How far the slide reaches round a camera `above` metres over the seabed: the whole 24 m from afar, closer in for a camera near the sand (a macro lens: the slide is
// looked up pixel by pixel, as pbrt's is, so its pixels must be small there), in whole 4 m tiles.
inline double reachFor(double above) { return std::clamp(4.0 * std::ceil((6.0 * std::max(0.0, above) + 4.0) / 4.0), 4.0, kReach); }

// The sun's line in a frame's text: where it shines from (pbrt's, a direction) and its light, and the line's place, or false when there is none (night).
struct Sun {
	Vec3 toward, L;
	std::size_t at = 0, length = 0;
};
inline bool findSun(const std::string &scene, Sun &sun) {
	static const std::regex re(R"(LightSource "distant" "point3 from" \[ (\S+) (\S+) (\S+) \] "point3 to" \[ 0 0 0 \] "rgb L" \[ (\S+) (\S+) (\S+) \])");
	std::smatch m;
	if (!std::regex_search(scene, m, re)) return false;
	double v[6];
	for (int k = 0; k < 6; ++k) v[k] = std::atof(m[k + 1].str().c_str());
	sun.toward = normalize(Vec3{v[0], v[1], v[2]});
	sun.L = {v[3], v[4], v[5]};
	sun.at = static_cast<std::size_t>(m.position(0));
	sun.length = static_cast<std::size_t>(m.length(0));
	return sun.toward.y > 0.0;
}
// The way the sun's light goes on under the surface (pbrt's, pointing back up towards it): bent towards straight down by Snell's law.
inline Vec3 refractedSun(const Vec3 &toward) {
	const double h = std::hypot(toward.x, toward.z), hw = h / 1.333;
	if (h < 1e-9) return {0.0, 1.0, 0.0};
	return {toward.x / h * hw, std::sqrt(std::max(0.0, 1.0 - hw * hw)), toward.z / h * hw};
}
// The slide: twelve tiles of the caustic net each way at frame k of the loop, its contrast for the bed's depth band (underwater::causticBand); and its mean brightness.
// (round 17) the net in a cartoon's three soft bands - dim, lit, bright - as a toon film paints its light: the shafts it throws through the water are banded too
inline double toonNet(double net) {
	const double a = smoothstep(0.62, 0.74, net), b = smoothstep(1.32, 1.46, net);
	return 0.45 + 0.55 * a + 1.2 * b;
}
inline std::string slidePicture(const std::string &film, int k, int band, int kTiles, bool toon = false) {
	char name[48];
	std::snprintf(name, sizeof name, "caustic_slide_%02d_b%d_t%d%s", k, band, kTiles, toon ? "_toon" : "");
	const double contrast = underwater::causticContrast(band);
	return film_sky::picture(film, name, kSize, kSize, [&](std::vector<float> &rgb) {
		for (int j = 0; j < kSize; ++j)
			for (int i = 0; i < kSize; ++i) {
				const double u = (i + 0.5) / kSize * kTiles, v = (j + 0.5) / kSize * kTiles;
				double net = 0.8 + contrast * (0.35 + 1.9 * underwater::causticAt(u - std::floor(u), v - std::floor(v), k) - 0.8);
				if (toon && contrast > 0.0) net = toonNet(net);
				const std::size_t q = (static_cast<std::size_t>(j) * kSize + i) * 3;
				rgb[q] = rgb[q + 1] = rgb[q + 2] = static_cast<float>(std::max(0.02, net));
			}
	});
}
inline double slideMean(int k, int band, bool toon = false) {
	const double contrast = underwater::causticContrast(band);
	double sum = 0.0;
	for (int j = 0; j < 40; ++j)
		for (int i = 0; i < 40; ++i) {
			const double net = 0.8 + contrast * (0.35 + 1.9 * underwater::causticAt((i + 0.5) / 40.0, (j + 0.5) / 40.0, k) - 0.8);
			sum += std::max(0.02, toon && contrast > 0.0 ? toonNet(net) : net);
		}
	return sum / 1600.0;
}
// The frame with its sun's light shared out: most of it as the caustic slide over the water round the camera (pbrt's eye), the sun keeping the rest. `bedDepth`: how
// deep the bed under the camera is. The frame as it was when it has no sun.
inline std::string withCaustics(std::string scene, const std::string &film, double t, const Vec3 &eye, double level, double bedDepth, bool toon = false) {
	Sun sun;
	if (!findSun(scene, sun)) return scene;
	const Vec3 d = refractedSun(sun.toward);
	const int k = underwater::causticFrame(t), band = underwater::causticBand(bedDepth);
	// where the beam that reaches the camera comes in through the surface (round 16: not straight above it - deep down, a slanting sun's light arrives from metres off,
	// and a camera near the sand was outside a small slide), in 4 m steps: the net stays where it is in the water as the camera moves
	const Vec3 in = eye + d * (std::max(0.0, level - eye.y) / std::max(0.2, d.y));
	const Vec3 aim{std::round(in.x / 4.0) * 4.0, level, std::round(in.z / 4.0) * 4.0};
	const Vec3 at = aim + d * kBack;
	const double r = kBack + std::max(0.0, bedDepth) / std::max(0.2, d.y);   // (to the bed, along the beam)
	const double luminance = 0.2126 * sun.L.x + 0.7152 * sun.L.y + 0.0722 * sun.L.z;
	const double scale = kCausticShare * luminance * r * r / slideMean(k, band, toon);
	const double reach = reachFor(eye.y - (level - bedDepth));
	const int tiles = static_cast<int>(std::lround(2.0 * reach / 4.0));
	const double fov = 2.0 * std::atan(reach / kBack) * 180.0 / kPi;
	// the slide looks down its +z: turned from +z onto -d
	const Vec3 z{0.0, 0.0, 1.0}, to = d * -1.0;
	Vec3 axis = cross(z, to);
	const double angle = std::acos(std::clamp(dot(z, to), -1.0, 1.0)) * 180.0 / kPi;
	if (length(axis) < 1e-9) axis = {1.0, 0.0, 0.0};
	axis = normalize(axis);
	char buf[480];
	std::snprintf(buf, sizeof buf, "# the caustic net over everything in the water (film_sealight.h)\nAttributeBegin\n  Translate %.4f %.4f %.4f\n  Rotate %.5f %.6f %.6f %.6f\n"
				  "  LightSource \"projection\" \"float fov\" [ %.5f ] \"float scale\" [ %.5f ] \"string filename\" [ \"",
				  at.x, at.y, at.z, angle, axis.x, axis.y, axis.z, fov, scale);
	char sunL[160];
	std::snprintf(sunL, sizeof sunL, "\"rgb L\" [ %.5f %.5f %.5f ]", sun.L.x * (1.0 - kCausticShare), sun.L.y * (1.0 - kCausticShare), sun.L.z * (1.0 - kCausticShare));
	std::string line = scene.substr(sun.at, sun.length);
	line = line.substr(0, line.find("\"rgb L\"")) + sunL;
	scene = scene.substr(0, sun.at) + line + scene.substr(sun.at + sun.length);
	return scene + buf + slidePicture(film, k, band, tiles, toon) + "\" ]\nAttributeEnd\n";
}

// ---- the sun seen from below, and rain ----
// Glints of the sun where the line to it leaves the water (seen from pbrt's eye under the surface): a cluster on the ripples round that point, each lit for a tenth of a
// second now and then, glowing without lighting anything. None when the sun is not up.
inline std::string glintsText(const std::string &scene, double t, const Vec3 &eye, double level) {
	Sun sun;
	if (!findSun(scene, sun) || eye.y > level - 0.2) return std::string();
	const Vec3 d = refractedSun(sun.toward);
	const double depth = level - eye.y, reach = depth / std::max(0.2, d.y);
	const Vec3 q = eye + d * reach;   // (on the surface)
	const Vec3 side = normalize(cross(d, Vec3{0.0, 1.0, 0.0}) + Vec3{1e-9, 0, 0}), up = normalize(cross(side, d));
	const int frame = static_cast<int>(std::floor(t * 10.0));
	Mesh m;
	const Mesh dot1 = uvSphere(1.0, 5, 3);
	const double spread = 0.07 * reach, size = 0.004 * reach;
	for (int i = 0; i < 70; ++i) {
		if (hash01(i * 131 + frame, 7) < 0.55) continue;   // (each lit now and then)
		const double a = 2.0 * kPi * hash01(i, 11), rr = spread * std::sqrt(hash01(i * 17 + frame / 3, 13));
		const Vec3 p = q + (side * std::cos(a) + up * std::sin(a)) * rr + Vec3{0.0, 0.02, 0.0};
		const double s = size * (0.5 + hash01(i * 7 + frame, 19));
		const int base = static_cast<int>(m.verts.size());
		for (std::size_t k = 0; k < dot1.verts.size(); ++k) m.verts.push_back(Vec3{-p.x, p.y, p.z} + dot1.verts[k] * s), m.normals.push_back(dot1.normals[k]);
		for (const auto &tr : dot1.tris) m.tris.push_back({tr[0] + base, tr[1] + base, tr[2] + base});
	}
	if (m.tris.empty()) return std::string();
	const double L = 4.0 * (0.2126 * sun.L.x + 0.7152 * sun.L.y + 0.0722 * sun.L.z) + 20.0;
	char mat[220];
	std::snprintf(mat, sizeof mat, "AreaLightSource \"diffuse\" \"rgb L\" [ %.3f %.3f %.3f ] \"bool sampled\" false\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", L, L * 0.98, L * 0.92);
	std::string shapes = emitMesh(m, identity());
	for (std::size_t at = 0; (at = shapes.find("Shape \"trianglemesh\"", at)) != std::string::npos; at += 20) shapes.insert(at + 20, " \"bool shadow\" false");
	return "# the sun's glints through the surface (film_sealight.h)\n" + attr(mat, shapes);
}
// The look's words that turn the glints into small stars (only lights far brighter than white get one).
inline std::string glintLook() { return " star 0.35 star-points 6 star-threshold 6 star-length 0.05"; }
// Rain on the surface seen from below (pbrt's eye; `rain` 0 to 1): the rings drops leave as they land within the camera's window, widening and fading over a third of a
// second, a few tiny bubbles under each.
inline std::string rainText(double rain, double t, const Vec3 &eye, double level) {
	if (rain <= 0.0 || eye.y > level - 0.2) return std::string();
	const double depth = level - eye.y, R = std::min(30.0, 1.3 * depth + 2.0);
	const int perTick = static_cast<int>(std::min(600.0, 40.0 * rain * R * R));   // (drops landing in each 1/30 s, inside the window and a little round it)
	Mesh rings, beads;
	const Mesh ring = torus(1.0, 0.08, 14, 4), bead = uvSphere(1.0, 4, 3);
	for (int tick = 0; tick < 10; ++tick) {   // (the last third of a second's drops)
		const int when = static_cast<int>(std::floor(t * 30.0)) - tick;
		const double age = t - when / 30.0;
		for (int i = 0; i < perTick / 10 + 1; ++i) {
			const int key = when * 977 + i;
			const double a = 2.0 * kPi * hash01(key, 3), rr = R * std::sqrt(hash01(key, 5));
			const Vec3 p{eye.x + rr * std::cos(a), level - 0.015, eye.z + rr * std::sin(a)};
			const double size = (0.01 + 0.05 * age / 0.33) * (0.6 + 0.8 * hash01(key, 7));
			const int base = static_cast<int>(rings.verts.size());
			for (std::size_t k = 0; k < ring.verts.size(); ++k) rings.verts.push_back(Vec3{-p.x, p.y, p.z} + Vec3{ring.verts[k].x * size, ring.verts[k].y * 0.01, ring.verts[k].z * size}), rings.normals.push_back(ring.normals[k]);
			for (const auto &tr : ring.tris) rings.tris.push_back({tr[0] + base, tr[1] + base, tr[2] + base});
			for (int b = 0; b < 3 && age < 0.25; ++b) {   // (the air the drop carried down)
				const Vec3 c = Vec3{-p.x, p.y, p.z} + Vec3{0.01 * (hash01(key + b, 9) - 0.5), -0.02 - 0.05 * age * hash01(key + b, 11), 0.01 * (hash01(key + b, 13) - 0.5)};
				const int b0 = static_cast<int>(beads.verts.size());
				for (std::size_t k = 0; k < bead.verts.size(); ++k) beads.verts.push_back(c + bead.verts[k] * 0.0015), beads.normals.push_back(bead.normals[k]);
				for (const auto &tr : bead.tris) beads.tris.push_back({tr[0] + b0, tr[1] + b0, tr[2] + b0});
			}
		}
	}
	std::string shapes = emitMesh(rings, identity()) + emitMesh(beads, identity());
	for (std::size_t at = 0; (at = shapes.find("Shape \"trianglemesh\"", at)) != std::string::npos; at += 20) shapes.insert(at + 20, " \"bool shadow\" false");
	return "# rain on the surface, from below (film_sealight.h)\n" + attr(metalMaterial({0.9, 0.92, 0.95}, 0.05), shapes);
}

// ---- a storm seen from below, lightning, the moon (round 17) ----
// The sun's light in the frame times `factor` (clouds racing over a storm: the light under the water dims and brightens); the scene as it was with no sun.
inline std::string dimSun(std::string scene, double factor) {
	Sun sun;
	if (!findSun(scene, sun) || factor >= 0.999) return scene;
	char sunL[160];
	std::snprintf(sunL, sizeof sunL, "\"rgb L\" [ %.5f %.5f %.5f ]", sun.L.x * factor, sun.L.y * factor, sun.L.z * factor);
	std::string line = scene.substr(sun.at, sun.length);
	line = line.substr(0, line.find("\"rgb L\"")) + sunL;
	return scene.substr(0, sun.at) + line + scene.substr(sun.at + sun.length);
}
// How much a storm's clouds let through now (`storm` 0 to 1): between 1 and 1 - 0.55 storm, in slow gusts of a few seconds.
inline double stormLight(double storm, double t) { return 1.0 - 0.55 * std::clamp(storm, 0.0, 1.0) * smoothstep(0.3, 0.7, nature::noise2(t * 0.45, 0.5, 991)); }
// Foam on a storm-tossed surface, seen from below (pbrt's eye; `storm` 0 to 1, blown towards `toward` degrees): streaks of white lying along the wind, drifting with it
// at 1.5 m/s, dark against the bright water above - each streak fixed in the drifting water, so it is the same streak from frame to frame.
inline std::string foamText(double storm, double t, const Vec3 &eye, double level, double toward) {
	if (storm <= 0.05 || eye.y > level - 0.2) return std::string();
	const double a = toward * kPi / 180.0;
	const Vec3 along{std::sin(a), 0.0, std::cos(a)}, across{along.z, 0.0, -along.x};
	const Vec3 drift = along * (1.5 * t);
	const double R = std::min(40.0, 2.0 * (level - eye.y) + 8.0), cell = 1.6;
	Mesh foam;
	// (the cells of the drifting water round the camera's window: in each, a streak with a chance of the storm's strength)
	const double u0 = dot(eye - drift, along), v0 = dot(eye - drift, across);
	for (int j = static_cast<int>(std::floor((v0 - R) / cell)); j <= static_cast<int>(std::floor((v0 + R) / cell)); ++j)
		for (int i = static_cast<int>(std::floor((u0 - R) / cell)); i <= static_cast<int>(std::floor((u0 + R) / cell)); ++i) {
			const int key = i * 73856093 ^ j * 19349663;
			if (hash01(key, 1) > 0.65 * storm) continue;
			const double u = (i + hash01(key, 2)) * cell, v = (j + hash01(key, 3)) * cell, len = 0.4 + 1.8 * hash01(key, 4), wide = 0.05 + 0.25 * hash01(key, 5);
			const Vec3 c = drift + along * u + across * v;
			const int base = static_cast<int>(foam.verts.size()), n = 10;
			foam.verts.push_back({-c.x, level - 0.03, c.z}), foam.normals.push_back({0.0, -1.0, 0.0});
			for (int k = 0; k < n; ++k) {
				const double w = 2.0 * kPi * k / n, ragged = 0.7 + 0.6 * hash01(key + k, 6);
				const Vec3 p = c + along * (0.5 * len * std::cos(w)) + across * (0.5 * wide * ragged * std::sin(w));
				foam.verts.push_back({-p.x, level - 0.03, p.z}), foam.normals.push_back({0.0, -1.0, 0.0});
			}
			for (int k = 0; k < n; ++k) foam.tris.push_back({base, base + 1 + k, base + 1 + (k + 1) % n});
		}
	if (foam.tris.empty()) return std::string();
	return "# foam on the storm's surface, from below (film_sealight.h)\n" + attr(diffuseMaterial({0.85, 0.88, 0.9}), emitMesh(foam, identity()));
}
// A lightning stroke's flash as it reaches under the water: a brief cold light from the bolt's way (pbrt's, from the eye), `flash` as the stroke is now (0 to 1).
inline std::string underFlashText(double flash, const Vec3 &toward) {
	if (flash <= 0.01) return std::string();
	const Vec3 d = refractedSun(normalize(toward));
	char buf[240];
	std::snprintf(buf, sizeof buf, "# a lightning flash, under the water (film_sealight.h)\nLightSource \"distant\" \"point3 from\" [ %.5f %.5f %.5f ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ %.4f %.4f %.4f ]\n",
				  d.x, d.y, d.z, 2.2 * flash, 2.4 * flash, 3.0 * flash);
	return buf;
}
// The moon (`moonlight [elevation E] [toward A] [bright B]`): a full moon E degrees up towards A (pbrt's way, degrees from +z towards +x), its cold silver light a distant
// light the sea's caustics and glints take as they take the sun's (found as the sun is when the sun is down), and its disc in the sky.
struct Moon {
	bool on = false;
	double elevation = 35.0, toward = 180.0, bright = 1.0;
	Vec3 way() const {
		const double e = elevation * kPi / 180.0, a = toward * kPi / 180.0;
		return {std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)};
	}
};
inline std::string moonText(const Moon &m, const Vec3 &eye) {
	if (!m.on) return std::string();
	const Vec3 w = m.way(), at = eye + w * 4000.0;
	const double L = 0.06 * m.bright;
	char buf[400];
	std::snprintf(buf, sizeof buf, "# the moon (film_sealight.h)\nLightSource \"distant\" \"point3 from\" [ %.5f %.5f %.5f ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ %.5f %.5f %.5f ]\n", w.x, w.y, w.z,
				  0.8 * L, 0.86 * L, 1.0 * L);
	char mat[220];
	std::snprintf(mat, sizeof mat, "AreaLightSource \"diffuse\" \"rgb L\" [ %.3f %.3f %.3f ] \"bool sampled\" false\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", 900.0 * L, 930.0 * L, 1000.0 * L);
	std::string disc = emitMesh(uvSphere(18.0, 24, 12), translate({-at.x, at.y, at.z}));   // (its disc, half a degree across: seen, never sampled, casting no shadow)
	for (std::size_t k = 0; (k = disc.find("Shape \"trianglemesh\"", k)) != std::string::npos; k += 20) disc.insert(k + 20, " \"bool shadow\" false");
	return buf + attr(mat, disc);
}

}  // namespace sealight
}  // namespace cartoon
