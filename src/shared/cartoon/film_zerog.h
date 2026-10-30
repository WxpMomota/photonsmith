#pragma once
// film_zerog.h -- what floats in a ship with no weight (film_file.h `blob`, `drift`; round 11):
//
//   blob    a ball of water, wobbling as surface tension pulls it round - its shape ringing in the modes a free drop rings in (Rayleigh's: an egg and a pear, each at
//           its own pace, faster for a small drop); drifting and turning slowly; `split T` stretches it into a peanut and pinches it into two that drift apart; `on NAME`
//           holds it on a character's face, growing from nothing (a tear that cannot fall);
//   drift   a scatter of small things floating in a space - crumbs, pens, tools or all of them - each tumbling at its own pace and drifting until it meets a wall of the
//           space and bounces off it.
//
// Coordinates are pbrt's. Times are nature's.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace zerog {

struct Blob {
	std::string name;
	Vec3 at, move;   // where at nature's time 0, and its drift (m/s)
	double size = 0.05;   // its radius
	double split = -1.0;  // when it splits in two (below 0: never)
	std::string on;       // (a tear) the character whose face holds it ("" none)
	double grow0 = 0.0, grow1 = 2.0;   // (a tear) growing from nothing to its size
	int seed = 1;
};
struct Drift {
	std::string name, kind = "mixed";   // crumbs, pens, tools or mixed
	Vec3 centre, half{1.0, 0.8, 1.0};    // the space they float in (a box)
	int count = 12;
	double size = 0.1;                   // how big they are (a pen's length)
	double speed = 0.08;                 // how fast they drift (m/s)
	int seed = 1;
};
inline const std::vector<std::string> &driftKinds() {
	static const std::vector<std::string> k = {"crumbs", "pens", "tools", "mixed"};
	return k;
}

// A drop's ringing: its radius at the direction n (unit, its own frame) at time `since` (seconds after it was set ringing), for a drop of radius R. The l = 2 and l = 3 modes
// (egg and pear), each at Rayleigh's frequency for water - omega^2 = l (l - 1) (l + 2) sigma / (rho R^3) - and slowly dying away to a gentler ring that never stops.
inline double ringingRadius(Vec3 n, double R, double since, int seed) {
	const double sigmaOverRho = 0.072 / 1000.0;
	const double w2 = std::sqrt(8.0 * sigmaOverRho / (R * R * R)), w3 = std::sqrt(30.0 * sigmaOverRho / (R * R * R));
	const double fade = 0.05 + 0.1 * std::exp(-0.4 * since);
	const Vec3 a = normalize(Vec3{hash01(seed, 1) - 0.5, hash01(seed, 2) - 0.5, hash01(seed, 3) - 0.5} + Vec3{0.0, 0.3, 0.0}), b = normalize(Vec3{hash01(seed, 4) - 0.5, 0.2, hash01(seed, 5) - 0.5});
	const double c2 = dot(n, a), c3 = dot(n, b);
	const double p2 = 0.5 * (3.0 * c2 * c2 - 1.0), p3 = 0.5 * (5.0 * c3 * c3 * c3 - 3.0 * c3);   // (Legendre's)
	return R * (1.0 + fade * (p2 * std::cos(w2 * since + seed) + 0.5 * p3 * std::cos(w3 * since + 2.0 * seed)));
}
// A blob's shape as a mesh about its centre (story space, as a mesh is): the drop ringing; or, splitting, a peanut pinching in two (`pinch` 0 .. 1) and its halves `apart`.
inline Mesh blobMesh(const Blob &b, double R, double since, double pinch) {
	Mesh m = uvSphere(1.0, 40, 20);
	for (std::size_t i = 0; i < m.verts.size(); ++i) {
		const Vec3 n = normalize(m.verts[i]);
		double r = ringingRadius(n, R, since, b.seed);
		if (pinch > 0.0) {   // (stretched along x, its waist drawn in)
			const double along = n.x;
			r *= 1.0 + 0.6 * pinch * along * along - 0.55 * pinch * (1.0 - along * along) * std::exp(-along * along / 0.08);
		}
		m.verts[i] = n * r;
	}
	// normals from the faces (the shape is no longer a sphere's)
	std::vector<Vec3> nn(m.verts.size(), Vec3{0.0, 0.0, 0.0});
	for (const auto &t : m.tris) {
		const Vec3 f = cross(m.verts[static_cast<std::size_t>(t[1])] - m.verts[static_cast<std::size_t>(t[0])], m.verts[static_cast<std::size_t>(t[2])] - m.verts[static_cast<std::size_t>(t[0])]);
		for (int k = 0; k < 3; ++k) nn[static_cast<std::size_t>(t[k])] = nn[static_cast<std::size_t>(t[k])] + f;
	}
	for (std::size_t i = 0; i < nn.size(); ++i) m.normals[i] = length(nn[i]) > 1e-20 ? normalize(nn[i]) : normalize(m.verts[i]);
	return m;
}
inline std::string waterMaterial() { return "Material \"dielectric\" \"float eta\" [ 1.33 ]"; }
// A blob at nature's time `nt` as scene text (`face`: where the character's face holds a tear, pbrt's, when it is on one).
inline std::string blobText(const Blob &b, double nt, const Vec3 *face = nullptr) {
	std::string out = "# a blob of water (film_zerog.h): " + b.name + "\n";
	const std::string water = waterMaterial();
	if (face) {   // a tear held on a face, growing
		const double R = b.size * std::cbrt(std::clamp((nt - b.grow0) / std::max(1e-6, b.grow1 - b.grow0), 0.0, 1.0));
		if (R < 1e-4) return std::string();
		const Vec3 c{-face->x, face->y, face->z};
		return out + attr(water, emitMesh(blobMesh(b, R, nt - b.grow0, 0.0), translate(c.x, c.y, c.z)));
	}
	const Vec3 at = b.at + b.move * nt;
	const double turn = 12.0 * nt + 40.0 * b.seed;   // (degrees: it turns slowly as it drifts)
	const Mat4 spin = rotate(normalize(Vec3{0.3, 1.0, 0.2}), turn);
	if (b.split < 0.0 || nt < b.split - 0.6) return out + attr(water, emitMesh(blobMesh(b, b.size, nt, 0.0), mul(translate(-at.x, at.y, at.z), spin)));
	if (nt < b.split) {   // stretching into a peanut, its waist pinching in
		const double u = smoothstep(b.split - 0.6, b.split, nt);
		return out + attr(water, emitMesh(blobMesh(b, b.size, nt, u), mul(translate(-at.x, at.y, at.z), spin)));
	}
	// two drops, each half its water (the radius by the cube root), flying apart along the peanut's length and ringing hard from the pinch
	const double r = b.size / std::cbrt(2.0), since = nt - b.split;
	const Vec3 axis = transformPoint(spin, {1.0, 0.0, 0.0}) - transformPoint(spin, {0.0, 0.0, 0.0});
	for (int k = 0; k < 2; ++k) {
		Blob half = b;
		half.seed = b.seed * 3 + k;
		const Vec3 off = axis * ((k == 0 ? 1.0 : -1.0) * (0.9 * b.size + 0.6 * b.size * since));
		out += attr(water, emitMesh(blobMesh(half, r, since, 0.0), mul(translate(-at.x + off.x, at.y + off.y, at.z + off.z), spin)));
	}
	return out;
}

// One floating thing of a drift: where it is at nature's time `nt` (pbrt's) - drifting and bouncing off the box's walls (a triangle wave in each axis) - and how it has turned.
inline Vec3 driftPlace(const Drift &d, int k, double nt) {
	const auto axis = [&](double centre, double half, double start, double v) {
		const double span = 2.0 * std::max(1e-6, half), s = std::fmod(start + v * nt, 2.0 * span);
		const double p = std::fmod(s + 2.0 * span, 2.0 * span);
		return centre - half + (p < span ? p : 2.0 * span - p);
	};
	const int h = d.seed * 7919 + k;
	const Vec3 v = normalize(Vec3{hash01(h, 1) - 0.5, hash01(h, 2) - 0.5, hash01(h, 3) - 0.5} + Vec3{1e-6, 0.0, 0.0}) * (d.speed * (0.5 + hash01(h, 4)));
	const double sx = 2.0 * d.half.x * hash01(h, 5), sy = 2.0 * d.half.y * hash01(h, 6), sz = 2.0 * d.half.z * hash01(h, 7);
	return {axis(d.centre.x, d.half.x, sx, v.x), axis(d.centre.y, d.half.y, sy, v.y), axis(d.centre.z, d.half.z, sz, v.z)};
}
inline std::string driftText(const Drift &d, double nt) {
	std::string out = "# things floating (film_zerog.h): " + d.name + "\n";
	for (int k = 0; k < d.count; ++k) {
		const int h = d.seed * 7919 + k;
		const Vec3 p = driftPlace(d, k, nt);
		const Vec3 axis = normalize(Vec3{hash01(h, 8) - 0.5, hash01(h, 9) - 0.5, hash01(h, 10) - 0.5} + Vec3{0.0, 1e-6, 0.0});
		const Mat4 place = mul(translate(-p.x, p.y, p.z), rotate(axis, (30.0 + 120.0 * hash01(h, 11)) * nt + 360.0 * hash01(h, 12)));
		std::string kind = d.kind;
		if (kind == "mixed") kind = driftKinds()[static_cast<std::size_t>(hash01(h, 13) * 3.0) % 3];
		const double s = d.size * (0.7 + 0.6 * hash01(h, 14));
		if (kind == "crumbs") {
			out += attr(diffuseMaterial(lerp(Vec3{0.75, 0.55, 0.3}, Vec3{0.9, 0.78, 0.55}, hash01(h, 15))), emitMesh(uvSphere(0.12 * s, 8, 4), mul(place, scale(1.0, 0.7, 0.85))));
		} else if (kind == "pens") {
			static const Vec3 inks[] = {{0.1, 0.2, 0.7}, {0.75, 0.1, 0.1}, {0.1, 0.1, 0.12}, {0.1, 0.55, 0.2}};
			out += attr(diffuseMaterial(inks[static_cast<std::size_t>(hash01(h, 16) * 4.0) % 4]), emitMesh(capsule(0.045 * s, 0.8 * s, 10, 3), mul(place, rotate({0.0, 0.0, 1.0}, 90.0))));
			out += attr(metalMaterial({0.8, 0.8, 0.82}, 0.2), emitMesh(capsule(0.03 * s, 0.12 * s, 8, 2), mul(place, translate(0.48 * s, 0.0, 0.0), rotate({0.0, 0.0, 1.0}, 90.0))));
		} else {   // a spanner: a bar with a jaw at one end and a ring at the other
			const std::string steel = metalMaterial({0.62, 0.62, 0.64}, 0.25);
			out += attr(steel, emitMesh(box(0.8 * s, 0.06 * s, 0.025 * s), place));
			out += attr(steel, emitMesh(torus(0.08 * s, 0.025 * s, 16, 6), mul(place, translate(-0.45 * s, 0.0, 0.0), rotate({1.0, 0.0, 0.0}, 90.0))));
			out += attr(steel, emitMesh(box(0.12 * s, 0.16 * s, 0.025 * s), mul(place, translate(0.44 * s, 0.0, 0.0))));
		}
	}
	return out;
}

inline std::string blobUsage() { return "`blob NAME at X Y Z [size R] [move DX DY DZ] [split T] [on CHARACTER [grow T0 T1]] [seed N]` (size 0.002 to 2 m)"; }
inline std::string driftUsage() { return "`drift NAME COUNT at X Y Z [box W H D] [kind crumbs|pens|tools|mixed] [size S] [speed V] [seed N]` (count 1 to 200)"; }

}  // namespace zerog
}  // namespace cartoon
