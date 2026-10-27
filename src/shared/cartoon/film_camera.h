#pragma once
// film_camera.h -- cameras for nature films (film_file.h `fly`, `exposure`): a drone's flight over the land, and an exposure that follows the light.
//
//   flight       waypoints (pbrt's x z) smoothed into a path; the drone keeps `height` metres above the land, but above the highest ground a little behind it and well ahead of
//                it (so it rises before a ridge instead of skimming it), smoothed again so it does not bob over every hump; it looks 400 m ahead along its way and down a little, or
//                at a point. Made into camera keys spaced along the path, so the shot's camera goes at an even pace, easing in at its start and out at its end.
//   exposure     1 by day; through sunset and twilight it opens up, to about 30 times at night (a night sky is about a thousandth as bright as day: the films' own
//                tone mapping does the rest), written into the frame's Film line as pbrt's "iso" (pbrt multiplies every light by exposure time x iso / 100; the films'
//                cameras have no shutter, so the time is 1).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <regex>
#include <string>
#include <vector>

#include "../camera_keyframes.h"
#include "cartoon_lib.h"
#include "film_terrain.h"

namespace cartoon {
namespace film_camera {

// The keys of a drone's flight. `ground` may be null (flat land at y 0).
inline camera_keyframes::Path flight(const std::vector<std::pair<double, double>> &points, double height, bool look, Vec3 lookAt, double fov, const terrain::Field *ground) {
	camera_keyframes::Path path;
	if (points.size() < 2) return path;
	// the waypoints smoothed (Catmull-Rom), a point every 2 m
	std::vector<Vec3> p;
	const auto at = [&](int i) {
		const int k = std::clamp(i, 0, static_cast<int>(points.size()) - 1);
		return Vec3{points[static_cast<std::size_t>(k)].first, 0.0, points[static_cast<std::size_t>(k)].second};
	};
	for (int i = 0; i + 1 < static_cast<int>(points.size()); ++i) {
		const Vec3 p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
		const int steps = std::clamp(static_cast<int>(length(p2 - p1) / 2.0), 2, 20000);   // (at most 40 km a leg at 2 m: a typo of 1e9 made 500 million points)
		for (int k = 0; k < steps; ++k) {
			const double u = k / double(steps);
			const auto cr = [u](double a, double b, double c, double d) { return b + 0.5 * u * (c - a + u * (2.0 * a - 5.0 * b + 4.0 * c - d + u * (3.0 * (b - c) + d - a))); };
			p.push_back({cr(p0.x, p1.x, p2.x, p3.x), 0.0, cr(p0.z, p1.z, p2.z, p3.z)});
		}
	}
	p.push_back(at(static_cast<int>(points.size()) - 1));
	const auto groundAt = [&](double x, double z) { return ground && ground->inside(x, z) ? ground->heightAt(x, z) : (ground ? ground->spec.y : 0.0); };
	// the height: above the highest ground from 40 m behind to 80 m ahead, then smoothed over 50 m each way
	const int n = static_cast<int>(p.size());
	std::vector<double> raw(static_cast<std::size_t>(n)), smooth(static_cast<std::size_t>(n));
	for (int i = 0; i < n; ++i) {
		double top = -1e30;
		for (int j = std::max(0, i - 20); j <= std::min(n - 1, i + 40); j += 2) top = std::max(top, groundAt(p[static_cast<std::size_t>(j)].x, p[static_cast<std::size_t>(j)].z));
		raw[static_cast<std::size_t>(i)] = top + height;
	}
	for (int i = 0; i < n; ++i) {
		double sum = 0.0;
		int count = 0;
		for (int j = std::max(0, i - 25); j <= std::min(n - 1, i + 25); ++j) sum += raw[static_cast<std::size_t>(j)], ++count;
		smooth[static_cast<std::size_t>(i)] = std::max(sum / count, groundAt(p[static_cast<std::size_t>(i)].x, p[static_cast<std::size_t>(i)].z) + 0.5 * height);
	}
	// keys spaced along it, each looking well ahead (200 points, 400 m) and gently down - the horizon in the picture - or at the point
	const int keys = std::min(48, std::max(2, n / 4));
	for (int k = 0; k < keys; ++k) {
		const int i = static_cast<int>(std::lround(k * (n - 1.0) / (keys - 1.0)));
		camera_keyframes::Key key;
		key.pos[0] = p[static_cast<std::size_t>(i)].x, key.pos[1] = smooth[static_cast<std::size_t>(i)], key.pos[2] = p[static_cast<std::size_t>(i)].z;
		Vec3 target = lookAt;
		if (!look) {
			const int a = std::min(n - 1, i + 200);
			Vec3 ahead = p[static_cast<std::size_t>(a)];
			if (a - i < 200) {   // (near the end: on along the last direction)
				const Vec3 dir = normalize(p[static_cast<std::size_t>(n - 1)] - p[static_cast<std::size_t>(std::max(0, n - 5))] + Vec3{1e-9, 0, 0});
				ahead = ahead + dir * (2.0 * (200 - (a - i)));
			}
			target = {ahead.x, std::max(groundAt(ahead.x, ahead.z) + 0.3 * height, smooth[static_cast<std::size_t>(i)] - 0.2 * height), ahead.z};
		}
		key.target[0] = target.x, key.target[1] = target.y, key.target[2] = target.z;
		key.fov = fov;
		path.keys.push_back(key);
	}
	return path;
}

// The exposure that follows the light, for a sun `elevation` degrees above the horizon: 1 by day (8 degrees and up), opening up smoothly to about 30 at night (12 below).
inline double autoExposure(double elevation) { return std::pow(2.0, 4.9 * smoothstep(8.0, -12.0, elevation)); }

// A frame's header with its exposure: the Film line's "iso" set to 100 times it (or its own iso multiplied by it).
inline std::string exposed(const std::string &header, double exposure) {
	if (std::abs(exposure - 1.0) < 1e-9) return header;
	static const std::regex iso(R"("float iso"\s*\[\s*([0-9.eE+-]+)\s*\])");
	std::smatch m;
	char buf[64];
	if (std::regex_search(header, m, iso)) {
		std::snprintf(buf, sizeof buf, "\"float iso\" [ %.4f ]", std::stod(m[1].str()) * exposure);
		return std::string(m.prefix()) + buf + std::string(m.suffix());
	}
	const std::size_t film = header.find("Film \"");
	if (film == std::string::npos) return header;
	const std::size_t typeEnd = header.find('"', film + 6);
	if (typeEnd == std::string::npos) return header;
	std::snprintf(buf, sizeof buf, " \"float iso\" [ %.4f ]", 100.0 * exposure);
	return header.substr(0, typeEnd + 1) + buf + header.substr(typeEnd + 1);
}

}  // namespace film_camera
}  // namespace cartoon
