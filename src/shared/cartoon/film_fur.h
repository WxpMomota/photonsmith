#pragma once
// film_fur.h -- fur and feathers on the films' rigged animals (film_animals.h; a `herd ... fur A` or `flock ... feathers A` line): strands grown over each posed
// body, so a deer's coat, a wolf's shaggy pelt and an eagle's plumage break up its smooth skin and soften its outline.
//
//   strands   small tapered ribbons (two segments each, lying flat to the skin), rooted on the posed mesh's triangles - each triangle's share by its area in the
//             rest pose, and each strand's place on it fixed by its number, so a strand stays on the same spot of the skin from pose to pose and the coat does
//             not crawl; coloured a little lighter than the skin under them, with a soft sheen. (Ribbons and not pbrt curves: the poses are object instances,
//             and an instance holds triangles and spheres only - pbrt_flatten_shapes.h.)
//   lie       a strand leaves the skin a little and lies back along the body (the models face +z) and droops; fur is narrow and pointed, a bird's contour
//             feather broad and blunt, lying flatter;
//   cost      `amount` 1 is some 6000 strands on a deer, 7000 on a wolf, 9000 on a horse, 4000 on a bird (4 triangles each); they are part of each pose's object,
//             so a herd of hundreds costs a few poses' strands.
//
// Coordinates: the models' story space (written with emitMesh, x mirrored).

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace fur {

// A kind's coat: how long its strands are (metres), how many on the whole animal at amount 1, how broad (a share of the length at the root), how flat they lie,
// whether it is a bird's.
struct Coat {
	double length = 0.03, strands = 6000.0, width = 0.18, lie = 0.85;
	bool feathers = false;
};
inline Coat coatOf(const std::string &kind) {
	Coat c;
	if (kind == "horses") c.length = 0.018, c.strands = 9000.0, c.width = 0.2, c.lie = 0.95;   // (a short sleek coat)
	else if (kind == "wolves") c.length = 0.06, c.strands = 7000.0, c.width = 0.12, c.lie = 0.7;   // (a thick shaggy one)
	else if (kind == "eagles" || kind == "geese") c.length = 0.035, c.strands = 4000.0, c.width = 0.45, c.lie = 1.0, c.feathers = true;
	return c;
}

// The strands on a posed body - its pieces as posed (`posed`) and in the rest pose (`rest`, the same triangles: the strands are shared out by the rest pose's areas),
// each piece's colour - as scene text (`amount` 1 the coat's number of strands).
inline std::string strandsText(const std::vector<Mesh> &posed, const std::vector<Mesh> &rest, const std::vector<Vec3> &colours, const Coat &coat, double amount) {
	double total = 0.0;
	for (const Mesh &m : rest)
		for (const auto &t : m.tris) total += 0.5 * length(cross(m.verts[t[1]] - m.verts[t[0]], m.verts[t[2]] - m.verts[t[0]]));
	if (total <= 0.0 || amount <= 0.0) return std::string();
	const double perArea = coat.strands * amount / total;
	std::string out;
	for (std::size_t piece = 0; piece < posed.size() && piece < rest.size(); ++piece) {
		const Mesh &m = posed[piece], &r = rest[piece];
		if (m.tris.size() != r.tris.size() || m.verts.size() != m.normals.size()) continue;
		Mesh mesh;
		for (std::size_t ti = 0; ti < r.tris.size(); ++ti) {
			const auto &t = r.tris[ti];
			const double area = 0.5 * length(cross(r.verts[t[1]] - r.verts[t[0]], r.verts[t[2]] - r.verts[t[0]]));
			const int salt = static_cast<int>(piece * 7919 + ti);
			const double want = area * perArea;
			const int n = static_cast<int>(want) + (hash01(salt, 1) < want - std::floor(want) ? 1 : 0);
			for (int k = 0; k < n; ++k) {
				double u = hash01(salt * 31 + k, 2), v = hash01(salt * 31 + k, 3);
				if (u + v > 1.0) u = 1.0 - u, v = 1.0 - v;
				const auto &pt = m.tris[ti];
				const Vec3 root = m.verts[pt[0]] * (1.0 - u - v) + m.verts[pt[1]] * u + m.verts[pt[2]] * v;
				Vec3 nrm = m.normals[pt[0]] * (1.0 - u - v) + m.normals[pt[1]] * u + m.normals[pt[2]] * v;
				if (length(nrm) < 1e-9) continue;
				nrm = normalize(nrm);
				// lying back along the body (the model faces +z) and drooping a little, each a little its own way
				Vec3 back = Vec3{0.0, 0.0, -1.0} - nrm * (-nrm.z);
				if (length(back) < 1e-6) back = Vec3{0.0, -1.0, 0.0} - nrm * (-nrm.y);
				back = normalize(back + Vec3{(hash01(salt * 31 + k, 4) - 0.5) * 0.6, -0.2, (hash01(salt * 31 + k, 5) - 0.5) * 0.3});
				const double L = coat.length * (0.6 + 0.8 * hash01(salt * 31 + k, 6));
				const Vec3 d = normalize(nrm * (1.0 - coat.lie) + back * coat.lie);
				// a tapered ribbon in two segments along a curve that leaves the skin and lies back, flat to the skin
				const Vec3 side = normalize(cross(nrm, d) + Vec3{1e-9, 0.0, 0.0}) * (0.5 * coat.width * L);
				const Vec3 c0 = root - nrm * (0.05 * L), c1 = root + nrm * (0.22 * L) + d * (0.45 * L), c2 = root + d * L + nrm * (0.08 * L) + Vec3{0.0, -0.1 * L, 0.0};
				const double tip = coat.feathers ? 0.6 : 0.1;
				const int base = static_cast<int>(mesh.verts.size());
				for (const auto &[c, w] : {std::pair<Vec3, double>{c0, 1.0}, {c1, 0.5 * (1.0 + tip)}, {c2, tip}}) {
					mesh.verts.push_back(c - side * w), mesh.verts.push_back(c + side * w);
					mesh.normals.push_back(nrm), mesh.normals.push_back(nrm);
				}
				for (int q = 0; q < 2; ++q) {
					const int b = base + 2 * q;
					mesh.tris.push_back({b, b + 1, b + 3});
					mesh.tris.push_back({b, b + 3, b + 2});
				}
			}
		}
		if (mesh.tris.empty()) continue;
		const Vec3 c = piece < colours.size() ? colours[piece] : Vec3{0.3, 0.2, 0.1};
		out += attr(coated(c * 1.08, 0.45), emitMesh(mesh, identity()));   // (a little lighter than the skin under it, with a sheen)
	}
	return out.empty() ? out : "# fur (film_fur.h)\n" + out;
}

}  // namespace fur
}  // namespace cartoon
