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
inline std::string slidePicture(const std::string &film, int k, int band, int kTiles) {
	char name[48];
	std::snprintf(name, sizeof name, "caustic_slide_%02d_b%d_t%d", k, band, kTiles);
	const double contrast = underwater::causticContrast(band);
	return film_sky::picture(film, name, kSize, kSize, [&](std::vector<float> &rgb) {
		for (int j = 0; j < kSize; ++j)
			for (int i = 0; i < kSize; ++i) {
				const double u = (i + 0.5) / kSize * kTiles, v = (j + 0.5) / kSize * kTiles;
				const double net = 0.8 + contrast * (0.35 + 1.9 * underwater::causticAt(u - std::floor(u), v - std::floor(v), k) - 0.8);
				const std::size_t q = (static_cast<std::size_t>(j) * kSize + i) * 3;
				rgb[q] = rgb[q + 1] = rgb[q + 2] = static_cast<float>(std::max(0.02, net));
			}
	});
}
inline double slideMean(int k, int band) {
	const double contrast = underwater::causticContrast(band);
	double sum = 0.0;
	for (int j = 0; j < 40; ++j)
		for (int i = 0; i < 40; ++i) sum += std::max(0.02, 0.8 + contrast * (0.35 + 1.9 * underwater::causticAt((i + 0.5) / 40.0, (j + 0.5) / 40.0, k) - 0.8));
	return sum / 1600.0;
}
// The frame with its sun's light shared out: most of it as the caustic slide over the water round the camera (pbrt's eye), the sun keeping the rest. `bedDepth`: how
// deep the bed under the camera is. The frame as it was when it has no sun.
inline std::string withCaustics(std::string scene, const std::string &film, double t, const Vec3 &eye, double level, double bedDepth) {
	Sun sun;
	if (!findSun(scene, sun)) return scene;
	const Vec3 d = refractedSun(sun.toward);
	const int k = underwater::causticFrame(t), band = underwater::causticBand(bedDepth);
	const Vec3 aim{std::round(eye.x / 4.0) * 4.0, level, std::round(eye.z / 4.0) * 4.0};   // (in 4 m steps: the net stays where it is in the water as the camera moves)
	const Vec3 at = aim + d * kBack;
	const double r = kBack + std::max(0.0, bedDepth) / std::max(0.2, d.y);   // (to the bed, along the beam)
	const double luminance = 0.2126 * sun.L.x + 0.7152 * sun.L.y + 0.0722 * sun.L.z;
	const double scale = kCausticShare * luminance * r * r / slideMean(k, band);
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
	return scene + buf + slidePicture(film, k, band, tiles) + "\" ]\nAttributeEnd\n";
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

}  // namespace sealight
}  // namespace cartoon
