// Triangle meshes for the Scene Builder's ready-made shapes that pbrt has no shape of its own for: pyramid, wedge (ramp), stairs, torus, capsule, dome
// and tube. Everything is in the object's own space, centred on its position, Y up (like the box and cylinder), and std-only so the unit tests and the
// pbrt writer share it with the GUI's previews.
//
// A mesh has outward-facing triangles (counter-clockwise seen from outside, the pbrt convention) and one normal per vertex: flat faces get their own
// vertices (sharp edges), curved ones share vertices (smooth shading), so the same mesh renders correctly on every backend.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace scene_doc {

struct ShapeMesh {
	std::vector<double> P;    // 3 per vertex
	std::vector<double> N;    // 3 per vertex, unit
	std::vector<double> UV;   // 2 per vertex
	std::vector<int> indices; // 3 per triangle
	std::size_t vertexCount() const { return P.size() / 3; }
	std::size_t triangleCount() const { return indices.size() / 3; }
};

namespace scene_shapes_detail {

struct V { double x, y, z; };
inline V sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double length(V a) { return std::sqrt(dot(a, a)); }
inline V unit(V a) { const double l = length(a); return l > 0 ? V{a.x / l, a.y / l, a.z / l} : V{0, 1, 0}; }

inline int addVertex(ShapeMesh& m, V p, V n, double u, double v) {
	m.P.insert(m.P.end(), {p.x, p.y, p.z});
	m.N.insert(m.N.end(), {n.x, n.y, n.z});
	m.UV.insert(m.UV.end(), {u, v});
	return static_cast<int>(m.vertexCount()) - 1;
}

// One triangle over existing vertices, wound so its geometric normal agrees with `outward`; a sliver with no area is dropped.
inline void addTriangle(ShapeMesh& m, int a, int b, int c, V outward) {
	const V pa{m.P[a * 3], m.P[a * 3 + 1], m.P[a * 3 + 2]}, pb{m.P[b * 3], m.P[b * 3 + 1], m.P[b * 3 + 2]}, pc{m.P[c * 3], m.P[c * 3 + 1], m.P[c * 3 + 2]};
	const V g = cross(sub(pb, pa), sub(pc, pa));
	if (length(g) < 1e-12) return;
	if (dot(g, outward) >= 0) m.indices.insert(m.indices.end(), {a, b, c});
	else m.indices.insert(m.indices.end(), {a, c, b});
}

// A flat polygon (3 or 4 corners, any order of winding) with a single normal: `outward` says which way it faces.
inline void addFlat(ShapeMesh& m, const std::vector<V>& corners, V outward) {
	const V n = unit(outward);
	std::vector<int> ids;
	static const double uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	for (std::size_t i = 0; i < corners.size(); ++i) ids.push_back(addVertex(m, corners[i], n, uvs[i][0], uvs[i][1]));
	for (std::size_t i = 1; i + 1 < ids.size(); ++i) addTriangle(m, ids[0], ids[i], ids[i + 1], n);
}

struct ProfilePoint { double r, y, nr, ny; };   // a point of a revolved outline, with its surface normal in the (r, y) plane

// Revolves `profile` (listed from the bottom up, or round a closed outline) about the Y axis. Two consecutive points at one place with different normals
// make a sharp edge.
inline void lathe(ShapeMesh& m, const std::vector<ProfilePoint>& profile, int segments) {
	const double pi = 3.14159265358979323846;
	std::vector<double> along(profile.size(), 0.0);
	for (std::size_t j = 1; j < profile.size(); ++j) along[j] = along[j - 1] + std::hypot(profile[j].r - profile[j - 1].r, profile[j].y - profile[j - 1].y);
	const double total = std::max(along.empty() ? 0.0 : along.back(), 1e-9);
	const std::size_t first = m.vertexCount();
	const std::size_t rows = profile.size();
	for (int i = 0; i <= segments; ++i) {
		const double phi = 2 * pi * i / segments, c = std::cos(phi), s = std::sin(phi);
		for (std::size_t j = 0; j < rows; ++j) {
			const ProfilePoint& p = profile[j];
			addVertex(m, {p.r * c, p.y, p.r * s}, unit({p.nr * c, p.ny, p.nr * s}), static_cast<double>(i) / segments, along[j] / total);
		}
	}
	for (int i = 0; i < segments; ++i)
		for (std::size_t j = 0; j + 1 < rows; ++j) {
			const int a = static_cast<int>(first + i * rows + j), b = static_cast<int>(first + (i + 1) * rows + j);
			const int c = static_cast<int>(first + (i + 1) * rows + j + 1), d = static_cast<int>(first + i * rows + j + 1);
			const auto outward = [&](int k) { return V{m.N[k * 3], m.N[k * 3 + 1], m.N[k * 3 + 2]}; };
			const V o1{outward(a).x + outward(b).x + outward(c).x, outward(a).y + outward(b).y + outward(c).y, outward(a).z + outward(b).z + outward(c).z};
			const V o2{outward(a).x + outward(c).x + outward(d).x, outward(a).y + outward(c).y + outward(d).y, outward(a).z + outward(c).z + outward(d).z};
			addTriangle(m, a, b, c, o1);
			addTriangle(m, a, c, d, o2);
		}
}

}  // namespace scene_shapes_detail

// Base `w` (X) by `d` (Z), apex over the middle of it; `h` tall.
inline ShapeMesh pyramidMesh(double w, double h, double d) {
	using namespace scene_shapes_detail;
	ShapeMesh m;
	const double x = w / 2, z = d / 2, lo = -h / 2, hi = h / 2;
	const V b0{-x, lo, -z}, b1{x, lo, -z}, b2{x, lo, z}, b3{-x, lo, z}, apex{0, hi, 0};
	addFlat(m, {b3, b2, b1, b0}, {0, -1, 0});
	// Each side leans out by half its base and up by the height: the normal of a slope that rises `h` over a run of `half`.
	addFlat(m, {b0, b1, apex}, {0, z, -h});
	addFlat(m, {b1, b2, apex}, {h, x, 0});
	addFlat(m, {b2, b3, apex}, {0, z, h});
	addFlat(m, {b3, b0, apex}, {-h, x, 0});
	return m;
}

// A ramp: the box w x h x d cut along its diagonal, low at the front (-Z) and `h` high at the back (+Z).
inline ShapeMesh wedgeMesh(double w, double h, double d) {
	using namespace scene_shapes_detail;
	ShapeMesh m;
	const double x = w / 2, y = h / 2, z = d / 2;
	const V fl{-x, -y, -z}, fr{x, -y, -z}, bl{-x, -y, z}, br{x, -y, z}, tl{-x, y, z}, tr{x, y, z};
	addFlat(m, {fl, fr, br, bl}, {0, -1, 0});
	addFlat(m, {bl, br, tr, tl}, {0, 0, 1});
	addFlat(m, {fl, fr, tr, tl}, {0, d, -h});
	addFlat(m, {fl, bl, tl}, {-1, 0, 0});
	addFlat(m, {fr, br, tr}, {1, 0, 0});
	return m;
}

// The most steps a staircase may have; a scene file asking for more is reported by validate() and drawn with this many, so a corrupt value cannot make a huge mesh.
constexpr int kMaxStairSteps = 100;

// `steps` steps climbing from the front (-Z) to the back (+Z), `w` wide, `h` high in all and `d` deep.
inline ShapeMesh stairsMesh(double w, double h, double d, int steps) {
	using namespace scene_shapes_detail;
	ShapeMesh m;
	steps = std::min(kMaxStairSteps, std::max(1, steps));
	const double x = w / 2, lo = -h / 2;
	for (int i = 0; i < steps; ++i) {
		const double z0 = -d / 2 + d * i / steps, z1 = -d / 2 + d * (i + 1) / steps;
		const double top = lo + h * (i + 1) / steps, prev = lo + h * i / steps;
		addFlat(m, {{-x, top, z0}, {x, top, z0}, {x, top, z1}, {-x, top, z1}}, {0, 1, 0});                  // the tread
		addFlat(m, {{-x, prev, z0}, {x, prev, z0}, {x, top, z0}, {-x, top, z0}}, {0, 0, -1});               // the riser
		addFlat(m, {{x, lo, z0}, {x, lo, z1}, {x, top, z1}, {x, top, z0}}, {1, 0, 0});                      // the sides
		addFlat(m, {{-x, lo, z0}, {-x, lo, z1}, {-x, top, z1}, {-x, top, z0}}, {-1, 0, 0});
	}
	addFlat(m, {{-x, lo, -d / 2}, {x, lo, -d / 2}, {x, lo, d / 2}, {-x, lo, d / 2}}, {0, -1, 0});
	addFlat(m, {{-x, lo, d / 2}, {x, lo, d / 2}, {x, lo + h, d / 2}, {-x, lo + h, d / 2}}, {0, 0, 1});
	return m;
}

// A ring (doughnut) lying flat: `major` from its centre to the middle of the tube, `minor` the tube's radius.
inline ShapeMesh torusMesh(double major, double minor) {
	using namespace scene_shapes_detail;
	const double pi = 3.14159265358979323846;
	ShapeMesh m;
	std::vector<ProfilePoint> profile;
	const int around = 20;
	for (int k = 0; k <= around; ++k) {
		const double a = 2 * pi * k / around;
		profile.push_back({major + minor * std::cos(a), minor * std::sin(a), std::cos(a), std::sin(a)});
	}
	lathe(m, profile, 40);
	return m;
}

// A cylinder with rounded ends: `radius` across, `height` in all (never less than the two ends need).
inline ShapeMesh capsuleMesh(double radius, double height) {
	using namespace scene_shapes_detail;
	const double pi = 3.14159265358979323846;
	ShapeMesh m;
	const double straight = std::max(0.0, height - 2 * radius) / 2;
	std::vector<ProfilePoint> profile;
	const int arc = 10;
	for (int k = 0; k <= arc; ++k) {   // the bottom cap, from the pole round to the side
		const double a = -pi / 2 + (pi / 2) * k / arc;
		profile.push_back({radius * std::cos(a), -straight + radius * std::sin(a), std::cos(a), std::sin(a)});
	}
	for (int k = 0; k <= arc; ++k) {   // the top cap, from the side up to the pole
		const double a = (pi / 2) * k / arc;
		profile.push_back({radius * std::cos(a), straight + radius * std::sin(a), std::cos(a), std::sin(a)});
	}
	lathe(m, profile, 32);
	return m;
}

// Half a sphere with a flat bottom, `radius` across; the whole thing is `radius` high, centred on its position.
inline ShapeMesh domeMesh(double radius) {
	using namespace scene_shapes_detail;
	const double pi = 3.14159265358979323846;
	ShapeMesh m;
	const double lo = -radius / 2;
	std::vector<ProfilePoint> profile;
	profile.push_back({0, lo, 0, -1});
	profile.push_back({radius, lo, 0, -1});
	const int arc = 12;
	for (int k = 0; k <= arc; ++k) {
		const double a = (pi / 2) * k / arc;
		profile.push_back({radius * std::cos(a), lo + radius * std::sin(a), std::cos(a), std::sin(a)});
	}
	lathe(m, profile, 32);
	return m;
}

// A pipe: a cylinder `height` tall with a round hole through it; `radius` outside, `inner` inside.
inline ShapeMesh tubeMesh(double radius, double inner, double height) {
	using namespace scene_shapes_detail;
	ShapeMesh m;
	const double hy = height / 2;
	std::vector<ProfilePoint> profile = {
		{radius, -hy, 1, 0}, {radius, hy, 1, 0},     // the outside
		{radius, hy, 0, 1},  {inner, hy, 0, 1},      // the top rim
		{inner, hy, -1, 0},  {inner, -hy, -1, 0},    // the inside
		{inner, -hy, 0, -1}, {radius, -hy, 0, -1},   // the bottom rim
	};
	lathe(m, profile, 36);
	return m;
}

// A box with rounded edges and corners: `size` is the whole box, `radius` the roundness of every edge (clamped to what fits: at most half the smallest side; 0 is a plain box with
// its edges still sharp). Made from a grid on each face of the box with the inner (flat) box pushed out by `radius` along the direction from the inner box to each grid point, which
// makes the faces flat, the edges exact quarter cylinders and the corners eighth spheres, with the true normal at every vertex.
inline ShapeMesh roundedBoxMesh(double sx, double sy, double sz, double radius) {
	using namespace scene_shapes_detail;
	ShapeMesh m;
	const double h[3] = {std::max(1e-6, sx) / 2, std::max(1e-6, sy) / 2, std::max(1e-6, sz) / 2};
	const double r = std::clamp(radius, 0.0, std::min({h[0], h[1], h[2]}));
	const int k = r > 1e-9 ? 5 : 0;   // grid lines across each rounded edge
	for (int a = 0; a < 3; ++a)
		for (int sign = -1; sign <= 1; sign += 2) {
			const int t1 = (a + 1) % 3, t2 = (a + 2) % 3;
			// The grid lines along a tangent axis: the flat part's two ends, and k lines across each rounded edge at the angles that sample a quarter circle evenly.
			const auto lines = [&](int axis) {
				const double inner = h[axis] - r;
				std::vector<double> c;
				for (int j = k; j >= 1; --j) c.push_back(-(inner + r * std::tan(j * 0.78539816339744831 / k)));
				c.push_back(-inner);
				c.push_back(inner);
				for (int j = 1; j <= k; ++j) c.push_back(inner + r * std::tan(j * 0.78539816339744831 / k));
				return c;
			};
			const std::vector<double> c1 = lines(t1), c2 = lines(t2);
			std::vector<int> ids;
			for (double v2 : c2)
				for (double v1 : c1) {
					double p[3];
					p[a] = sign * h[a];
					p[t1] = v1;
					p[t2] = v2;
					double q[3], d[3];
					for (int i = 0; i < 3; ++i) {
						const double inner = h[i] - r;
						q[i] = std::clamp(p[i], -inner, inner);
						d[i] = p[i] - q[i];
					}
					V n{d[0], d[1], d[2]};
					if (length(n) < 1e-12) { n = {0, 0, 0}; (a == 0 ? n.x : a == 1 ? n.y : n.z) = sign; }
					n = unit(n);
					const V pos{q[0] + r * n.x, q[1] + r * n.y, q[2] + r * n.z};
					const double tp1 = t1 == 0 ? pos.x : t1 == 1 ? pos.y : pos.z, tp2 = t2 == 0 ? pos.x : t2 == 1 ? pos.y : pos.z;
					ids.push_back(addVertex(m, pos, n, (tp1 + h[t1]) / (2 * h[t1]), (tp2 + h[t2]) / (2 * h[t2])));
				}
			const int w = static_cast<int>(c1.size());
			for (std::size_t j = 0; j + 1 < c2.size(); ++j)
				for (int i = 0; i + 1 < w; ++i) {
					const int a0 = ids[j * w + i], b0 = ids[j * w + i + 1], c0 = ids[(j + 1) * w + i + 1], d0 = ids[(j + 1) * w + i];
					for (const std::array<int, 3>& tri : {std::array<int, 3>{a0, b0, c0}, std::array<int, 3>{a0, c0, d0}}) {
						V centre{0, 0, 0};
						for (int id : tri) { centre.x += m.P[id * 3]; centre.y += m.P[id * 3 + 1]; centre.z += m.P[id * 3 + 2]; }
						addTriangle(m, tri[0], tri[1], tri[2], centre);   // (the shape is convex and holds the origin, so the direction from it to a triangle is outwards)
					}
				}
		}
	return m;
}

// A photographer's cyclorama: a seamless studio backdrop. A floor (its front edge at +Z) that curves up, with `radius` of curve, into a wall at the back (-Z); `width` along X,
// `height` the whole height of the wall and `depth` from the front edge of the floor to the wall. A sheet, facing the camera side (up and towards +Z); in its own space it fills
// the box width x height x depth about the origin, with the floor along the bottom.
inline ShapeMesh cycloramaMesh(double width, double height, double depth, double radius) {
	using namespace scene_shapes_detail;
	ShapeMesh m;
	const double hx = std::max(1e-6, width) / 2, hy = std::max(1e-6, height) / 2, hz = std::max(1e-6, depth) / 2;
	const double r = std::clamp(radius, 0.0, std::min(2 * hy, 2 * hz));
	struct Point { double z, y, nz, ny, along; };
	std::vector<Point> profile;
	profile.push_back({hz, -hy, 0, 1, 0});                                      // the front edge of the floor
	profile.push_back({-hz + r, -hy, 0, 1, 0});                                 // where the curve starts
	const int segments = r > 1e-9 ? 14 : 0;
	const double zc = -hz + r, yc = -hy + r;
	for (int s = 1; s <= segments; ++s) {
		const double phi = 1.57079632679489662 * s / segments;
		profile.push_back({zc - r * std::sin(phi), yc - r * std::cos(phi), std::sin(phi), std::cos(phi), 0});
	}
	if (segments == 0) profile.push_back({-hz, -hy, 1, 0, 0});                  // (no curve: the corner is two vertices, so the floor and the wall keep their own normals; the sliver between is dropped)
	profile.push_back({-hz, hy, 1, 0, 0});                                      // the top of the wall
	double total = 0;
	for (std::size_t i = 1; i < profile.size(); ++i) {
		total += std::hypot(profile[i].z - profile[i - 1].z, profile[i].y - profile[i - 1].y);
		profile[i].along = total;
	}
	std::vector<int> left, right;
	for (const Point& p : profile) {
		const double v = total > 0 ? p.along / total : 0.0;
		left.push_back(addVertex(m, {-hx, p.y, p.z}, {0, p.ny, p.nz}, 0.0, v));
		right.push_back(addVertex(m, {hx, p.y, p.z}, {0, p.ny, p.nz}, 1.0, v));
	}
	for (std::size_t i = 0; i + 1 < profile.size(); ++i) {
		const V toward{0, (profile[i].ny + profile[i + 1].ny) / 2, (profile[i].nz + profile[i + 1].nz) / 2};
		addTriangle(m, left[i], right[i], right[i + 1], toward);
		addTriangle(m, left[i], right[i + 1], left[i + 1], toward);
	}
	return m;
}

// Terrain: rolling ground made from noise, a grid of `resolution` cells across each side over `width` x `depth`, with hills up to `height` high (the ground is centred on its box, so
// the highest hills reach +height/2 and the lowest dips -height/2). `hills` is how many hills the biggest features have across the ground, `roughness` (0 to 1) how much smaller
// lumps are added on top of them, and `seed` picks which ground (the same numbers always make the same ground, on every machine: the noise is made from integer hashing).
constexpr int kMaxTerrainResolution = 128;

namespace scene_shapes_detail {
inline double latticeValue(int x, int y, unsigned seed) {
	unsigned h = seed * 374761393u + static_cast<unsigned>(x) * 668265263u + static_cast<unsigned>(y) * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return static_cast<double>(h) / 4294967295.0;
}
inline double valueNoise(double x, double y, unsigned seed) {
	const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
	const double fx = x - ix, fy = y - iy;
	const auto fade = [](double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
	const double u = fade(fx), v = fade(fy);
	const double a = latticeValue(ix, iy, seed), b = latticeValue(ix + 1, iy, seed), c = latticeValue(ix, iy + 1, seed), d = latticeValue(ix + 1, iy + 1, seed);
	return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}
// Height of the ground at (u, v) in 0..1, 0 to 1.
inline double terrainHeight(double u, double v, double hills, double roughness, unsigned seed) {
	double sum = 0, norm = 0, amplitude = 1, frequency = std::max(0.25, hills);
	for (int octave = 0; octave < 5; ++octave) {
		sum += amplitude * valueNoise(u * frequency + 17.0 * octave, v * frequency + 31.0 * octave, seed + 101u * octave);
		norm += amplitude;
		amplitude *= std::clamp(roughness, 0.0, 1.0);
		frequency *= 2.0;
	}
	const double f = norm > 0 ? sum / norm : 0.5;
	return std::clamp((f - 0.5) * 1.8 + 0.5, 0.0, 1.0);   // (noise bunches round the middle: stretch it so the hills use the whole height)
}
}  // namespace scene_shapes_detail

inline ShapeMesh terrainMesh(double width, double height, double depth, int resolution, double roughness, double hills, int seed) {
	using namespace scene_shapes_detail;
	ShapeMesh m;
	const int n = std::min(kMaxTerrainResolution, std::max(2, resolution));
	const double hx = std::max(1e-6, width) / 2, hz = std::max(1e-6, depth) / 2, hy = std::max(0.0, height);
	const unsigned s = static_cast<unsigned>(seed);
	std::vector<double> y(static_cast<std::size_t>(n + 1) * (n + 1));
	for (int j = 0; j <= n; ++j)
		for (int i = 0; i <= n; ++i) y[static_cast<std::size_t>(j) * (n + 1) + i] = (terrainHeight(static_cast<double>(i) / n, static_cast<double>(j) / n, hills, roughness, s) - 0.5) * hy;
	const auto at = [&](int i, int j) { return y[static_cast<std::size_t>(std::clamp(j, 0, n)) * (n + 1) + std::clamp(i, 0, n)]; };
	const double dx = 2 * hx / n, dz = 2 * hz / n;
	std::vector<int> ids;
	for (int j = 0; j <= n; ++j)
		for (int i = 0; i <= n; ++i) {
			const double slopeX = (at(i + 1, j) - at(i - 1, j)) / ((i == 0 || i == n ? 1 : 2) * dx), slopeZ = (at(i, j + 1) - at(i, j - 1)) / ((j == 0 || j == n ? 1 : 2) * dz);
			ids.push_back(addVertex(m, {-hx + i * dx, at(i, j), -hz + j * dz}, unit({-slopeX, 1.0, -slopeZ}), static_cast<double>(i) / n, static_cast<double>(j) / n));
		}
	for (int j = 0; j < n; ++j)
		for (int i = 0; i < n; ++i) {
			const int a = ids[static_cast<std::size_t>(j) * (n + 1) + i], b = ids[static_cast<std::size_t>(j) * (n + 1) + i + 1];
			const int c = ids[static_cast<std::size_t>(j + 1) * (n + 1) + i + 1], d = ids[static_cast<std::size_t>(j + 1) * (n + 1) + i];
			addTriangle(m, a, d, c, {0, 1, 0});
			addTriangle(m, a, c, b, {0, 1, 0});
		}
	return m;
}

}  // namespace scene_doc
