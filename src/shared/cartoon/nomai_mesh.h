#pragma once
// nomai_mesh.h -- how "The Last Nomai Signal" writes a mesh (nomai_signal.h): meshText is cartoon::emitMesh less the triangles that would have no area once placed and written. The
// film is full of small parts (glyph strokes, eyelids, the spiral writing's marks, the tips of domes and lathes) whose corners meet once the scene text rounds them to
// 10 micrometres; the loader drops such a triangle with a warning, and a film's frames are meant to load without one.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "cartoon_lib.h"

namespace cartoon {
namespace nomai {

constexpr const char *kFilmKey = "nomai-signal";

inline std::string meshText(const Mesh &mesh, const Mat4 &m) {
	Mesh kept = mesh;
	kept.tris.clear();
	// (the points as the text will hold them: placed, then rounded to the 5 decimals appendPbrt writes)
	std::vector<Vec3> placed(mesh.verts.size());
	for (std::size_t i = 0; i < mesh.verts.size(); ++i) {
		const Vec3 p = transformPoint(m, mesh.verts[i]);
		// (and as the loader keeps them, in single precision: far from the origin - the observatory is 400 m down - points a few hundredths of a millimetre apart merge)
		placed[i] = {static_cast<float>(std::round(p.x * 1e5) * 1e-5), static_cast<float>(std::round(p.y * 1e5) * 1e-5), static_cast<float>(std::round(p.z * 1e5) * 1e-5)};
	}
	for (const auto &t : mesh.tris) {
		const Vec3 &a = placed[static_cast<std::size_t>(t[0])], &b = placed[static_cast<std::size_t>(t[1])], &c = placed[static_cast<std::size_t>(t[2])];
		if (!(a.x == b.x && a.y == b.y && a.z == b.z) && !(b.x == c.x && b.y == c.y && b.z == c.z) && !(a.x == c.x && a.y == c.y && a.z == c.z) && length(cross(b - a, c - a)) > 1e-11)
			kept.tris.push_back(t);
	}
	return cartoon::emitMesh(kept, m);
}
// cartoon::bar (a thin rod from a to b) through the same filter.
inline std::string rod(Vec3 a, Vec3 b, double r, int segments = 10) {
	const double len = length(b - a);
	if (len < 1e-9) return std::string();
	return meshText(capsule(r, std::max(0.0, len - 2.0 * r), segments, 3), alignY(a + normalize(b - a) * r, b - a));
}

}  // namespace nomai
}  // namespace cartoon
