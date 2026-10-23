#pragma once
// nature_lib.h -- what the films that are not made of toys are built from: a rolling ground, grass that sways in the wind, trees with tapering trunks, branches and translucent
// leaves, ferns, mossy rocks, and the mist that turns a low sun into shafts of light. Header-only, on top of cartoon_lib.h; story space as everywhere (+x to the right of the
// picture, +y up, +z towards the camera; every emitted x is negated because pbrt is left-handed).
//
// Plants are made of many small pieces, so they are written once as pbrt objects (ObjectBegin ... ObjectEnd) and placed many times (ObjectInstance): a frame of a forest is
// then a few hundred kilobytes of text instead of tens of megabytes, and the renderers keep one copy of each piece. A placement can lean (a shear), so a tuft or a cluster of leaves
// sways in the wind without being written again.

#include "cartoon_lib.h"

namespace cartoon {
namespace nature {

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

// Smooth value noise in [0, 1): a lattice of hash01 values blended with a smoothstep.
inline double noise2(double x, double z, int salt) {
	const double fx = std::floor(x), fz = std::floor(z);
	const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
	const auto at = [salt](int a, int b) { return hash01(a * 73856093 ^ b * 19349663, salt); };
	const double u = x - fx, v = z - fz;
	const double su = u * u * (3.0 - 2.0 * u), sv = v * v * (3.0 - 2.0 * v);
	return lerp(lerp(at(ix, iz), at(ix + 1, iz), su), lerp(at(ix, iz + 1), at(ix + 1, iz + 1), su), sv);
}
// Four octaves of it, in [0, 1).
inline double fbm2(double x, double z, int salt) {
	double sum = 0.0, amp = 0.5, total = 0.0;
	for (int o = 0; o < 4; ++o) {
		sum += amp * noise2(x, z, salt + o * 101);
		total += amp;
		x *= 2.03;
		z *= 2.03;
		amp *= 0.5;
	}
	return sum / total;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Ground
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

struct Ground {
	double amplitude = 0.6;   // the height of the gentlest rises
	double scale = 9.0;       // how far apart they are
	int salt = 1;
	double height(double x, double z) const { return amplitude * (fbm2(x / scale, z / scale, salt) - 0.5) * 2.0; }
	Vec3 at(double x, double z) const { return {x, height(x, z), z}; }
	Vec3 normal(double x, double z) const {
		const double e = 0.05;
		return normalize({height(x - e, z) - height(x + e, z), 2.0 * e, height(x, z - e) - height(x, z + e)});
	}
};

// The ground as a grid of `n` x `n` squares over a square of side `size` centred on (cx, cz).
inline Mesh groundMesh(const Ground &g, double cx, double cz, double size, int n) {
	Mesh m;
	for (int j = 0; j <= n; ++j)
		for (int i = 0; i <= n; ++i) {
			const double x = cx - size / 2 + size * i / n, z = cz - size / 2 + size * j / n;
			m.verts.push_back(g.at(x, z));
			m.normals.push_back(g.normal(x, z));
			m.uvs.push_back({x / 4.0, z / 4.0});
		}
	for (int j = 0; j < n; ++j)
		for (int i = 0; i < n; ++i) {
			const int a = j * (n + 1) + i;
			m.tris.push_back({a, a + n + 1, a + 1});
			m.tris.push_back({a + 1, a + n + 1, a + n + 2});
		}
	return m;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Pieces of plants
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

// A curved strip from `base` along `path(s)` (s from 0 to 1), `width(s)` wide across `side`: a blade of grass, a frond's stem, a ribbon. Normals face across the strip.
template <typename Path, typename Width>
inline void addStrip(Mesh &m, Path path, Width width, Vec3 side, int segments) {
	const int base = static_cast<int>(m.verts.size());
	for (int k = 0; k <= segments; ++k) {
		const double s = static_cast<double>(k) / segments;
		const Vec3 p = path(s);
		const Vec3 tangent = normalize(path((std::min)(1.0, s + 0.02)) - path((std::max)(0.0, s - 0.02)));
		const Vec3 n = normalize(cross(tangent, side));
		const double w = width(s) * 0.5;
		m.verts.push_back(p - side * w);
		m.verts.push_back(p + side * w);
		m.normals.push_back(n);
		m.normals.push_back(n);
		m.uvs.push_back({0.0, s});
		m.uvs.push_back({1.0, s});
	}
	for (int k = 0; k < segments; ++k) {
		const int a = base + 2 * k;
		m.tris.push_back({a, a + 1, a + 3});
		// (a strip that ends in a point has its last two vertices in one place: the second triangle of the last piece would have no area)
		if (k + 1 < segments || width(1.0) > 1e-9) m.tris.push_back({a, a + 3, a + 2});
	}
}

// A leaf: a pointed oval from `base` along `dir`, `length` long and `width` wide, folded a little along its midrib, facing `facing` (roughly its normal).
inline void addLeaf(Mesh &m, Vec3 base, Vec3 dir, Vec3 facing, double len, double width) {
	dir = normalize(dir);
	Vec3 side = cross(facing, dir);
	side = length(side) > 1e-9 ? normalize(side) : normalize(cross(Vec3{0.0, 1.0, 0.0}, dir));
	const Vec3 up = normalize(cross(dir, side));
	const int b = static_cast<int>(m.verts.size());
	const double fold = 0.18 * width;
	const Vec3 pts[6] = {base, base + dir * (0.3 * len) + side * (0.5 * width) - up * fold, base + dir * (0.65 * len) + side * (0.42 * width) - up * fold, base + dir * len,
						 base + dir * (0.65 * len) - side * (0.42 * width) - up * fold, base + dir * (0.3 * len) - side * (0.5 * width) - up * fold};
	for (const Vec3 &p : pts) {
		m.verts.push_back(p);
		m.normals.push_back(up);
	}
	m.uvs.insert(m.uvs.end(), {{0.5, 0.0}, {1.0, 0.3}, {0.9, 0.65}, {0.5, 1.0}, {0.1, 0.65}, {0.0, 0.3}});
	m.tris.push_back({b, b + 1, b + 2});
	m.tris.push_back({b, b + 2, b + 3});
	m.tris.push_back({b, b + 3, b + 4});
	m.tris.push_back({b, b + 4, b + 5});
}

// A tuft of grass around the origin: `blades` blades up to `height` tall within `radius`, each with its own lean. Variant `salt` gives a different tuft.
inline Mesh grassTuft(int blades, double height, double radius, double width, int salt) {
	Mesh m;
	for (int i = 0; i < blades; ++i) {
		const double a = 2.0 * kPi * hash01(i, salt), r = radius * std::sqrt(hash01(i, salt + 1));
		const Vec3 root = {r * std::cos(a), 0.0, r * std::sin(a)};
		const double h = height * (0.55 + 0.45 * hash01(i, salt + 2));
		const double yaw = 2.0 * kPi * hash01(i, salt + 3);
		const Vec3 side = {std::cos(yaw), 0.0, std::sin(yaw)};
		const Vec3 bend = Vec3{-side.z, 0.0, side.x} * (h * (0.15 + 0.45 * hash01(i, salt + 4)));
		const Vec3 out = Vec3{root.x, 0.0, root.z} * (0.6 / (std::max)(radius, 1e-6));   // blades lean away from the tuft's middle
		addStrip(m, [&](double s) { return root + Vec3{0.0, h * s, 0.0} + (bend + out * h * 0.2) * (s * s); },
				 [&](double s) { return width * (1.0 - std::pow(s, 2.2)); }, side, 4);
	}
	return m;
}

// A cluster of leaves around the origin, about `size` across: `count` leaves on short invisible stalks pointing outwards and a little down. Variant `salt`.
inline Mesh leafCluster(int count, double size, double leafLength, int salt) {
	Mesh m;
	for (int i = 0; i < count; ++i) {
		const double y = 1.0 - 2.0 * hash01(i, salt), ring = std::sqrt((std::max)(0.0, 1.0 - y * y)), a = 2.0 * kPi * hash01(i, salt + 1);
		const Vec3 d = normalize(Vec3{ring * std::cos(a), y * 0.6, ring * std::sin(a)});
		const Vec3 base = d * (size * 0.5 * (0.35 + 0.65 * std::cbrt(hash01(i, salt + 2))));
		const Vec3 dir = normalize(d + Vec3{0.0, -0.35, 0.0} + Vec3{hash01(i, salt + 3) - 0.5, 0.0, hash01(i, salt + 4) - 0.5} * 0.6);
		const Vec3 facing = normalize(Vec3{0.0, 1.0, 0.0} + d * 0.5);
		const double len = leafLength * (0.75 + 0.5 * hash01(i, salt + 5));
		addLeaf(m, base, dir, facing, len, len * 0.55);
	}
	return m;
}

// A fern around the origin: `fronds` arching fronds `size` long, each a stem with pairs of leaflets that shrink towards the tip.
inline Mesh fern(int fronds, double size, int salt) {
	Mesh m;
	for (int f = 0; f < fronds; ++f) {
		const double a = 2.0 * kPi * (f + 0.5 * hash01(f, salt)) / fronds;
		const Vec3 out = {std::cos(a), 0.0, std::sin(a)};
		const double len = size * (0.75 + 0.35 * hash01(f, salt + 1)), rise = 0.55 + 0.25 * hash01(f, salt + 2);
		const auto path = [&](double s) { return out * (len * s) + Vec3{0.0, len * (rise * s - 0.75 * s * s), 0.0}; };
		const Vec3 side = normalize(cross(Vec3{0.0, 1.0, 0.0}, out));
		addStrip(m, path, [&](double s) { return 0.025 * size * (1.0 - s); }, side, 6);
		const int pairs = 11;
		for (int k = 1; k <= pairs; ++k) {
			const double s = static_cast<double>(k) / (pairs + 1);
			const Vec3 p = path(s);
			const Vec3 tangent = normalize(path(s + 0.02) - path(s - 0.02));
			const double leaflet = size * 0.2 * (1.0 - s) + size * 0.03;
			for (int sgn = -1; sgn <= 1; sgn += 2) {
				const Vec3 dir = normalize(side * static_cast<double>(sgn) + tangent * 0.45);
				addLeaf(m, p, dir, normalize(cross(dir, tangent) * static_cast<double>(sgn) + Vec3{0.0, 0.6, 0.0}), leaflet, leaflet * 0.35);
			}
		}
	}
	return m;
}

// A rock about `size` across: a squashed sphere pushed in and out by noise.
inline Mesh rock(double size, int salt) {
	Mesh m = uvSphere(1.0, 20, 10);
	for (std::size_t i = 0; i < m.verts.size(); ++i) {
		Vec3 &v = m.verts[i];
		const double k = 0.75 + 0.5 * fbm2(v.x * 1.7 + v.y * 0.9 + 5.0, v.z * 1.7 - v.y * 0.6, salt);
		v = Vec3{v.x * size * 0.5 * k, (std::max)(-0.1, v.y) * size * 0.32 * k, v.z * size * 0.45 * k};
	}
	// normals again from the faces (the noise moved the points)
	std::vector<Vec3> acc(m.verts.size(), Vec3{0.0, 0.0, 0.0});
	for (const auto &t : m.tris) {
		const Vec3 n = cross(m.verts[static_cast<std::size_t>(t[1])] - m.verts[static_cast<std::size_t>(t[0])], m.verts[static_cast<std::size_t>(t[2])] - m.verts[static_cast<std::size_t>(t[0])]);
		for (int c = 0; c < 3; ++c) acc[static_cast<std::size_t>(t[c])] = acc[static_cast<std::size_t>(t[c])] + n;
	}
	for (std::size_t i = 0; i < acc.size(); ++i) m.normals[i] = length(acc[i]) > 1e-12 ? normalize(acc[i]) : Vec3{0.0, 1.0, 0.0};
	return m;
}

// A tree trunk `height` tall from the origin: a tapering column that flares at the root, with a gentle bend (`lean` metres at the top towards +x) and bark-like bumps.
inline Mesh trunk(double height, double radius, double lean, int salt) {
	std::vector<std::pair<double, double>> profile;
	const int n = 24;
	for (int i = 0; i <= n; ++i) {
		const double s = static_cast<double>(i) / n;
		const double flare = 1.0 + 0.9 * std::exp(-s * 18.0);
		const double r = radius * flare * (1.0 - 0.55 * s) * (0.94 + 0.12 * noise2(s * 9.0, 0.5, salt));
		profile.push_back({r, height * s});
	}
	Mesh m = lathe(profile, 18);
	for (Vec3 &v : m.verts) {
		const double s = v.y / height;
		v.x += lean * s * s;
	}
	return m;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Objects and placements
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

// An object definition: the meshes (each with its material) written once under `name`.
inline std::string objectBegin(const std::string &name) { return "ObjectBegin \"" + name + "\"\n"; }
inline std::string objectEnd() { return "ObjectEnd\n"; }

// A placement of object `name` by the story-space transform `m` (the object's meshes were written by emitMesh, so they are already mirrored: the placement is mirror * m * mirror).
inline std::string placeObject(const std::string &name, const Mat4 &m) {
	static const Mat4 mirror = scale(-1.0, 1.0, 1.0);
	const Mat4 p = mul(mirror, m, mirror);
	char buf[420];
	// pbrt's ConcatTransform takes the matrix by columns
	std::snprintf(buf, sizeof(buf), "AttributeBegin\n  ConcatTransform [ %.5f %.5f %.5f %.5f  %.5f %.5f %.5f %.5f  %.5f %.5f %.5f %.5f  %.5f %.5f %.5f %.5f ]\n  ObjectInstance \"%s\"\nAttributeEnd\n",
				  p[0], p[4], p[8], p[12], p[1], p[5], p[9], p[13], p[2], p[6], p[10], p[14], p[3], p[7], p[11], p[15], name.c_str());
	return buf;
}

// A lean that grows with height: x' = x + kx * y, z' = z + kz * y. What sways a placed tuft or cluster in the wind.
inline Mat4 shearXZ(double kx, double kz) { return {1, kx, 0, 0, 0, 1, 0, 0, 0, kz, 1, 0, 0, 0, 0, 1}; }

// The wind's lean at a place and time: a slow swell plus gusts that travel across the ground (`strength` 0 = still air).
inline Vec3 windLean(double x, double z, double t, double strength) {
	const double swell = std::sin(0.9 * t + 0.35 * x + 0.2 * z), gust = std::sin(2.3 * t + 0.8 * x - 0.3 * z) * std::sin(0.37 * t + 0.11 * x);
	const double k = strength * (0.6 * swell + 0.4 * gust);
	return {k, 0.0, 0.35 * k};
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Mist
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

// Morning mist that ends at a large sphere around the set (src/shared/pbrt_flatten_shapes.h: the pbrt way of bounding the camera's own fog). The sun and the sky are outside it and
// light it, so a low sun shining between trunks draws shafts of light. The camera must be inside the sphere. `density` is the scattering per metre (0.02 a light haze,
// 0.08 a thick mist); `g` how strongly it scatters forwards (0.6: the shafts glow brightest looking towards the sun).
struct Mist {
	Vec3 centre = {0.0, 0.0, 0.0};
	double radius = 60.0;
	double density = 0.03;
	Vec3 tint = {1.0, 1.0, 1.05};   // a cool mist scatters blue a little more
	double absorption = 0.002;
	double g = 0.6;
};
// What goes before the Camera line: the medium and the camera's place in it.
inline std::string mistBeforeCamera(const Mist &m) {
	char buf[320];
	std::snprintf(buf, sizeof(buf), "MakeNamedMedium \"mist\" \"string type\" [ \"homogeneous\" ] \"rgb sigma_a\" [ %.5f %.5f %.5f ] \"rgb sigma_s\" [ %.5f %.5f %.5f ] \"float g\" [ %.3f ]\nMediumInterface \"\" \"mist\"\n",
				  m.absorption, m.absorption, m.absorption, m.density * m.tint.x, m.density * m.tint.y, m.density * m.tint.z, m.g);
	return buf;
}
// What goes after WorldBegin: the invisible sphere the mist ends at.
inline std::string mistBoundary(const Mist &m) {
	char buf[240];
	std::snprintf(buf, sizeof(buf), "AttributeBegin\n  MediumInterface \"mist\" \"\"\n  Material \"interface\"\n  Translate %.4f %.4f %.4f\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n",
				  -m.centre.x, m.centre.y, m.centre.z, m.radius);
	return buf;
}

}  // namespace nature
}  // namespace cartoon
