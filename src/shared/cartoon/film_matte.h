#pragma once
// film_matte.h -- matte paintings: a painted picture projected from a camera onto simple shapes behind the set, as Blender's artists do it (camera projection: a
// UV Project modifier, or "Project from View"). The painting is seen exactly as painted from where it is projected from, and as the film's camera moves away from that
// place its parts slide against each other and against the set as real distance would make them - a hand-painted castle on a hill with parallax, without modelling it.
//
// A film's `matte FILE from X Y Z to X Y Z [fov F] [distance D] [floor Y] [bright B]` (pbrt's coordinates): the projector at `from` looking `to` with a vertical field of
// view F (40); the shape it is painted on a wall of the projector's rays at distance D (60), bending down onto the ground plane at height Y where a ray meets it nearer
// (`floor`: a cyclorama, the painted ground meeting the real one). The picture glows by itself at its own colours (`bright`, 1), lights nothing (light linking: only its
// own black shape; and `"bool sampled" false`: no light to aim at) and casts no shadows (the sky and the sun reach the set past it).

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace matte {

struct Matte {
	std::string file;          // the picture (absolute, as the scene reads it)
	Vec3 from, to{0.0, 0.0, -1.0};
	double fov = 40.0, distance = 60.0, bright = 1.0;
	bool hasFloor = false;
	double floor = 0.0;
	double aspect = 16.0 / 9.0;   // (the picture's width over its height: read when the film is loaded)
	int grid = 64;                // (rows of the shape; columns by the aspect)
	int line = 0;
};

// The matte's scene text: its shape (a mesh along the projector's rays, its uv the picture's place along each) glowing with the picture.
inline std::string textOf(const Matte &m, int index) {
	const Vec3 f = normalize(m.to - m.from);
	const Vec3 up0{0.0, 1.0, 0.0};
	// (pbrt's camera is left-handed: what is on its right is cross(up, forward))
	const Vec3 right = normalize(cross(std::fabs(f.y) > 0.99 ? Vec3{0.0, 0.0, 1.0} : up0, f)), up = cross(f, right);
	const double th = std::tan(0.5 * m.fov * kPi / 180.0), tw = th * m.aspect;
	const int rows = std::max(8, m.grid), cols = std::max(8, static_cast<int>(std::lround(rows * m.aspect)));
	std::string p, uv, idx;
	char buf[96];
	for (int j = 0; j <= rows; ++j)
		for (int i = 0; i <= cols; ++i) {
			const double sx = 2.0 * i / cols - 1.0, sy = 2.0 * j / rows - 1.0;   // (the picture's place, -1..1 across and up)
			const Vec3 d = normalize(f + right * (sx * tw) + up * (sy * th));
			double t = m.distance;
			if (m.hasFloor && d.y < -1e-6) t = std::min(t, (m.floor - m.from.y) / d.y);   // (where the ray meets the floor, when nearer)
			const Vec3 at = m.from + d * t;
			std::snprintf(buf, sizeof buf, "%.5g %.5g %.5g ", at.x, at.y, at.z);
			p += buf;
			std::snprintf(buf, sizeof buf, "%.6g %.6g ", 0.5 + 0.5 * sx, 0.5 + 0.5 * sy);
			uv += buf;
		}
	for (int j = 0; j < rows; ++j)
		for (int i = 0; i < cols; ++i) {
			const int a = j * (cols + 1) + i, b = a + 1, c = a + cols + 1, d = c + 1;
			std::snprintf(buf, sizeof buf, "%d %d %d %d %d %d ", a, b, d, a, d, c);
			idx += buf;
		}
	std::snprintf(buf, sizeof buf, "%.6g", m.bright);
	return "# matte painting " + std::to_string(index + 1) + " (film_matte.h)\nAttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n"
		   "  AreaLightSource \"diffuse\" \"string filename\" [ \"" + m.file + "\" ] \"float scale\" [ " + buf + " ] \"bool twosided\" true \"bool sampled\" false \"string illuminates\" [ \"matte\" ]\n"
		   "  Shape \"trianglemesh\" \"bool shadow\" false \"string linkgroup\" \"matte\" \"integer indices\" [ " + idx + "] \"point3 P\" [ " + p + "] \"point2 uv\" [ " + uv + "]\nAttributeEnd\n";
}

}  // namespace matte
}  // namespace cartoon
