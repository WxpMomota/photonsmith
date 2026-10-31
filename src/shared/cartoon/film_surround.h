#pragma once
// film_surround.h -- `surround FILE [radius R] [height H] [repeat N] [bright B] [ground R G B] [at X Z]`: what a scene built for one camera lacks everywhere else. A ring of a panorama
// round the set, glowing a little and left out of the light sampling, its picture's top carried on upwards so a low camera cannot see out of it, and a plain ground disc just under
// the set's ground out to beyond the ring. The picture should wrap: its left edge meeting its right, `repeat` times round. Coordinates are pbrt's.

#include <cmath>
#include <cstdio>
#include <string>

namespace cartoon {
namespace surround {

struct Surround {
	std::string file;
	double radius = 115.0, height = 62.0, repeat = 3.0, bright = 0.35, ground[3] = {0.025, 0.04, 0.025}, x = 0.0, z = 0.0;
	bool on = false;
};

inline std::string text(const Surround &s) {
	if (!s.on) return std::string();
	constexpr int kSegments = 96;
	std::string points, uvs, corners, disc, floor;
	char buf[120];
	for (int i = 0; i <= kSegments; ++i) {
		const double a = 6.283185307179586 * i / kSegments, x = s.x + s.radius * std::cos(a), z = s.z + s.radius * std::sin(a), u = s.repeat * i / kSegments;
		std::snprintf(buf, sizeof buf, "%.3f -3 %.3f  %.3f %.3f %.3f  %.3f %.3f %.3f  ", x, z, x, s.height, z, x, s.height * 2.5, z);
		points += buf;
		std::snprintf(buf, sizeof buf, "%.4f 0  %.4f 0.999  %.4f 0.999  ", u, u, u);
		uvs += buf;
	}
	for (int i = 0; i < kSegments; ++i) {
		const int b = 3 * i;
		std::snprintf(buf, sizeof buf, "%d %d %d %d %d %d %d %d %d %d %d %d ", b, b + 1, b + 3, b + 1, b + 4, b + 3, b + 1, b + 2, b + 4, b + 2, b + 5, b + 4);
		corners += buf;
	}
	std::snprintf(buf, sizeof buf, "%.3f -0.03 %.3f  ", s.x, s.z);
	floor = buf;
	for (int i = 0; i < kSegments; ++i) {
		const double a = 6.283185307179586 * i / kSegments;
		std::snprintf(buf, sizeof buf, "%.3f -0.03 %.3f  ", s.x + (s.radius + 20.0) * std::cos(a), s.z + (s.radius + 20.0) * std::sin(a));
		floor += buf;
		std::snprintf(buf, sizeof buf, "0 %d %d ", 1 + i, 1 + (i + 1) % kSegments);
		disc += buf;
	}
	std::snprintf(buf, sizeof buf, "%.4f", s.bright);
	std::string out = "# surround (film_surround.h)\nAttributeBegin\n  AreaLightSource \"diffuse\" \"string filename\" [ \"" + s.file + "\" ] \"float scale\" [ " + buf +
					  " ] \"bool twosided\" true \"bool sampled\" false\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n  Shape \"trianglemesh\" \"integer indices\" [ " + corners +
					  "] \"point3 P\" [ " + points + "] \"point2 uv\" [ " + uvs + "]\nAttributeEnd\n";
	std::snprintf(buf, sizeof buf, "[ %.4f %.4f %.4f ]", s.ground[0], s.ground[1], s.ground[2]);
	return out + "AttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" " + buf + "\n  Shape \"trianglemesh\" \"integer indices\" [ " + disc + "] \"point3 P\" [ " + floor + "]\nAttributeEnd\n";
}

}  // namespace surround
}  // namespace cartoon
