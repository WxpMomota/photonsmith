#pragma once
// film_ice.h -- ice worlds for a film file (film_file.h `glacier`, `iceberg`, `seaice`, and `waterfall ... frozen`):
//
//   glacier    a tongue of ice laid down the land from where it starts along a heading: thick in its middle and thinning to its edges, its surface heaved and cut by
//              crevasses across its flow (more of them towards its front), snow lying on top; it ends in a cliff of ice - into the sea, where there is one - and at
//              `calve T` a slab of that cliff leans out, topples and falls into the water in a burst of spray, then floats off as a berg;
//   iceberg    a block of ice floating in the sea, seven eighths of it under the water, its faces carved and ridged, bobbing and turning slowly;
//   seaice     the sea frozen over in floes: plates of white ice with dark water between, rising and falling with the waves;
//   frozen     (a `waterfall` option) the fall hung in ice: a curtain of icicles and columns from its lip down to a frozen pool.
//
// Ice is pbrt's subsurface material (a tabulated BSSRDF): light goes into it and comes out blue, the reds absorbed over a metre or so; snow on top is white. The ice's
// faces that look up are snow, the steep ones (cliffs, crevasse walls, a berg's sides) ice. Coordinates are pbrt's, as a film file's; the meshes are written in them.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_water.h"
#include "nature_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace ice {

// A glacier: where its head is (pbrt's x z), the way it flows (a compass bearing, as `wind`), how long and wide, how thick, when its front calves.
struct Glacier {
	double x = 0.0, z = 0.0, toward = 0.0, length = 600.0, width = 160.0, thick = 0.0;
	std::vector<double> calving;
	int seed = 1;
	double thickness() const { return thick > 0.0 ? thick : 0.18 * width; }
};
// An iceberg: where (pbrt's x z), how big (metres across at the water), a seed for its shape.
struct Iceberg {
	double x = 0.0, z = -200.0, size = 40.0;
	int seed = 1;
};
// Sea ice: how much of the sea round the camera it covers (0 to 1), how big its floes are, a seed.
struct SeaIce {
	bool on = false;
	double cover = 0.6, floe = 12.0;
	int seed = 1;
};

// The materials: glacier ice (blue from inside) and the snow on it.
inline std::string iceMaterial() {
	return "Material \"subsurface\" \"rgb sigma_a\" [ 0.11 0.028 0.011 ] \"rgb sigma_s\" [ 3.0 3.0 3.0 ] \"float scale\" [ 1 ] \"float eta\" [ 1.31 ] \"float roughness\" [ 0.05 ]";
}
inline std::string snowMaterial() { return "Material \"diffuse\" \"rgb reflectance\" [ 0.86 0.88 0.9 ]"; }

namespace detail {
inline Vec3 heading(double degrees) { return {std::sin(degrees * kPi / 180.0), 0.0, std::cos(degrees * kPi / 180.0)}; }
inline Vec3 sideOf(Vec3 u) { return {u.z, 0.0, -u.x}; }
// A mesh written in pbrt's coordinates, split into snow (faces that look up) and ice; normals from its faces.
inline std::string iceAndSnow(Mesh m, double snowAbove = 0.75) {
	m.normals.assign(m.verts.size(), Vec3{0.0, 0.0, 0.0});
	for (const auto &t : m.tris) {
		const Vec3 n = cross(m.verts[t[1]] - m.verts[t[0]], m.verts[t[2]] - m.verts[t[0]]);
		for (int c = 0; c < 3; ++c) m.normals[t[c]] = m.normals[t[c]] + n;
	}
	for (Vec3 &n : m.normals) n = length(n) > 1e-12 ? normalize(n) : Vec3{0.0, 1.0, 0.0};
	Mesh snow, ice;
	snow.verts = ice.verts = m.verts, snow.normals = ice.normals = m.normals;
	for (const auto &t : m.tris) {
		const Vec3 n = normalize(cross(m.verts[t[1]] - m.verts[t[0]], m.verts[t[2]] - m.verts[t[0]]));
		(n.y > snowAbove ? snow : ice).tris.push_back(t);
	}
	std::string out, shapes;
	if (!snow.tris.empty()) snow.appendPbrt(shapes, identity(), false), out += attr(snowMaterial(), shapes), shapes.clear();
	if (!ice.tris.empty()) ice.appendPbrt(shapes, identity(), false), out += attr(iceMaterial(), shapes);
	return out;
}
// A grid of points (rows of `cols + 1`) as triangles.
inline void grid(Mesh &m, int base, int rows, int cols, bool flip = false) {
	for (int j = 0; j < rows; ++j)
		for (int i = 0; i < cols; ++i) {
			const int k = base + j * (cols + 1) + i;
			if (flip) m.tris.push_back({k, k + cols + 1, k + 1}), m.tris.push_back({k + 1, k + cols + 1, k + cols + 2});
			else m.tris.push_back({k, k + 1, k + cols + 1}), m.tris.push_back({k + 1, k + cols + 2, k + cols + 1});
		}
}
}  // namespace detail

// ---- glaciers ----
// How deep a crevasse cuts the glacier at (a along its length, b across: metres), as a share of its depth: narrow cracks across the flow, bowed downstream, more and
// deeper towards its front and its edges.
inline double crevasse(const Glacier &g, double a, double b) {
	const double across = 2.0 * b / g.width, bow = 0.12 * g.width * across * across;
	const double spacing = 0.09 * g.width * (0.7 + 0.6 * nature::noise2(a * 0.004, 1.0, g.seed + 90)), wander = 0.9 * spacing * (nature::noise2(a * 0.02, b * 0.05, g.seed + 91) - 0.5);
	const double c = std::pow(std::max(0.0, std::sin(2.0 * kPi * (a - bow + wander) / spacing)), 24.0);
	const double where = smoothstep(0.3, 0.9, a / g.length) + 0.6 * smoothstep(0.6, 0.95, std::abs(across));
	const double broken = smoothstep(0.45, 0.7, nature::noise2(a * 0.03 + 3.0, b * 0.08, g.seed + 93));   // (in fields, not ribs)
	return c * std::min(1.0, where) * broken;
}
// How far back the front is from the glacier's full length at t: each calving takes a slab `slab` metres deep off it once it has fallen.
inline double slabDepth(const Glacier &g) { return 0.05 * g.width; }
// The glacier's surface height at (a, b) over the land (`groundAt` pbrt's): a smoothed bed along its middle, the ice thick there and thinner to its edges, heaved by
// noise, the crevasses cut down into it; `level` (the sea's, or -1e30) is where its front stands in water.
inline double glacierTop(const Glacier &g, double a, double b, const std::function<double(double, double)> &groundAt) {
	const Vec3 u = detail::heading(g.toward), side = detail::sideOf(u);
	double bed = 0.0;
	for (int k = -2; k <= 2; ++k) {   // (the bed along its middle, smoothed over a width)
		const Vec3 p = Vec3{g.x, 0.0, g.z} + u * std::clamp(a + k * 0.1 * g.width, 0.0, g.length);
		bed += groundAt(p.x, p.z) / 5.0;
	}
	const double across = std::min(1.0, std::abs(2.0 * b / g.width)), T = g.thickness();
	const double heave = 0.08 * T * (nature::noise2(a * 0.03, b * 0.03, g.seed + 95) - 0.5) + 0.03 * T * (nature::noise2(a * 0.15, b * 0.15, g.seed + 97) - 0.5);
	const Vec3 p = Vec3{g.x, 0.0, g.z} + u * a + side * b;
	const double top = bed + T * std::pow(1.0 - across * across, 0.55) + heave * (1.0 - across) - 0.6 * T * crevasse(g, a, b);   // (thinning to nothing at its edges)
	return std::max(top, groundAt(p.x, p.z) + 0.3 * (1.0 - across));
}
// The glacier as scene text at `nt` (its front `cut` metres back), its cliff standing down into the water (`level`) or to the ground.
inline std::string glacierText(const Glacier &g, double cut, const std::function<double(double, double)> &groundAt, double level) {
	const Vec3 u = detail::heading(g.toward), side = detail::sideOf(u);
	const double L = std::max(10.0, g.length - cut);
	const int na = std::clamp(static_cast<int>(L / (0.012 * g.width)), 40, 400), nb = 60;
	Mesh m;
	const auto at = [&](double a, double b, double y) { const Vec3 p = Vec3{g.x, 0.0, g.z} + u * a + side * b; return Vec3{p.x, y, p.z}; };
	for (int j = 0; j <= na; ++j)   // its top
		for (int i = 0; i <= nb; ++i) {
			const double a = L * j / na, b = g.width * (i / double(nb) - 0.5);
			m.verts.push_back(at(a, b, glacierTop(g, a, b, groundAt)));
		}
	detail::grid(m, 0, na, nb, true);
	// its front: a cliff from the top down below the water (or into the ground), ragged in columns (seracs)
	const int fb = static_cast<int>(m.verts.size()), nf = 12;
	for (int j = 0; j <= nf; ++j)
		for (int i = 0; i <= nb; ++i) {
			const double b = g.width * (i / double(nb) - 0.5), top = glacierTop(g, L, b, groundAt);
			const Vec3 g0 = at(L, b, 0.0);
			const double bottom = std::min(groundAt(g0.x, g0.z), level > -1e29 ? level - 0.3 * g.thickness() : 1e30) - 2.0;
			const double v = j / double(nf), jag = 0.035 * g.width * (nature::noise2(b * 0.18, v * 1.5, g.seed + 99) - 0.5) + 0.012 * g.width * (nature::noise2(b * 0.7, v * 4.0, g.seed + 98) - 0.5);   // (fluted in columns)
			m.verts.push_back(at(L + jag + 0.02 * g.width * v * v, b, top + (bottom - top) * v));
		}
	detail::grid(m, fb, nf, nb, true);
	// its sides: down into the ground
	for (int s = 0; s < 2; ++s) {
		const int base = static_cast<int>(m.verts.size());
		const double b = (s == 0 ? -0.5 : 0.5) * g.width;
		for (int j = 0; j <= 1; ++j)
			for (int k = 0; k <= na; ++k) {
				const double a = L * k / na, top = glacierTop(g, a, b, groundAt);
				const Vec3 p = at(a, b, 0.0);
				m.verts.push_back(at(a, b, j == 0 ? top : groundAt(p.x, p.z) - 3.0));
			}
		detail::grid(m, base, 1, na, s == 0);
	}
	return "# a glacier (film_ice.h)\n" + detail::iceAndSnow(std::move(m));
}
// A calving: the slab off the front of glacier `g` at `since` seconds after it began to go - leaning out, toppling into the water (2.5 s), then floating off. Its splash
// time is `kCalveFall`.
constexpr double kCalveFall = 3.0;
inline std::string slabText(const Glacier &g, double cutBefore, double since, const std::function<double(double, double)> &groundAt, double level, double nt) {
	const Vec3 u = detail::heading(g.toward), side = detail::sideOf(u);
	const double L = g.length - cutBefore, d = slabDepth(g), w = 0.35 * g.width;
	const double b0 = (hash01(g.seed * 7 + static_cast<int>(cutBefore), 3) - 0.5) * 0.4 * g.width;
	const double top = glacierTop(g, L - 0.5 * d, b0, groundAt), bottom = level > -1e29 ? level - 0.3 * g.thickness() : groundAt(g.x, g.z);
	const double H = std::max(5.0, top - bottom);
	// toppling about its foot's front edge: 0 to 100 degrees in kCalveFall seconds, then lying in the water, drifting out and bobbing
	const double s = clamp01(since / kCalveFall), angle = 100.0 * s * s * kPi / 180.0;
	const double drift = since > kCalveFall ? 2.0 * (since - kCalveFall) : 0.0;
	const double rest = level > -1e29 ? level - 0.8 * d : bottom;   // (lying in the water it floats, a fifth of it above)
	const double y0 = bottom + (rest - bottom) * smoothstep(kCalveFall, kCalveFall + 4.0, since);
	const Vec3 pivot = Vec3{g.x, 0.0, g.z} + u * (L + drift) + side * b0 + Vec3{0.0, y0 + (since > kCalveFall ? 0.25 * std::sin(0.8 * nt) : 0.0), 0.0};
	Mesh m;
	// a box d deep, w wide, H tall, its front lower edge on the pivot, rotated about the side axis (outward)
	const auto corner = [&](double x, double y, double zz) {   // x back from the front (0..d), y up (0..H), zz across
		const double ca = std::cos(angle), sa = std::sin(angle);
		const double back = -x * ca + y * sa, up = x * sa + y * ca;
		return pivot + u * back + side * zz + Vec3{0.0, up, 0.0};
	};
	const int n = 6;
	const auto face = [&](const std::function<Vec3(double, double)> &f, bool flip) {
		const int base = static_cast<int>(m.verts.size());
		for (int j = 0; j <= n; ++j)
			for (int i = 0; i <= n; ++i) m.verts.push_back(f(i / double(n), j / double(n)));
		detail::grid(m, base, n, n, flip);
	};
	const auto jag = [&](double p, double q) { return 0.06 * d * (nature::noise2(p * 4.0, q * 4.0, g.seed + 101) - 0.5); };
	face([&](double i, double j) { return corner(jag(i, j), j * H, (i - 0.5) * w); }, false);   // the front
	face([&](double i, double j) { return corner(d + jag(i + 3.0, j), j * H, (i - 0.5) * w); }, true);   // the back
	face([&](double i, double j) { return corner(i * d, H, (j - 0.5) * w); }, true);   // the top
	face([&](double i, double j) { return corner(i * d, 0.0, (j - 0.5) * w); }, false);   // the bottom
	face([&](double i, double j) { return corner(i * d, j * H, -0.5 * w); }, true);
	face([&](double i, double j) { return corner(i * d, j * H, 0.5 * w); }, false);
	return "# a calving slab (film_ice.h)\n" + detail::iceAndSnow(std::move(m));
}
// Where a calving's slab hits the water (pbrt's): its top's landing place, about its height out from the front.
inline Vec3 calveSplash(const Glacier &g, double cutBefore, const std::function<double(double, double)> &groundAt, double level) {
	const Vec3 u = detail::heading(g.toward), side = detail::sideOf(u);
	const double L = g.length - cutBefore, b0 = (hash01(g.seed * 7 + static_cast<int>(cutBefore), 3) - 0.5) * 0.4 * g.width;
	const double top = glacierTop(g, L, b0, groundAt), H = std::max(5.0, top - (level > -1e29 ? level : 0.0));
	const Vec3 p = Vec3{g.x, 0.0, g.z} + u * (L + 0.6 * H) + side * b0;
	return {p.x, level > -1e29 ? level : groundAt(p.x, p.z), p.z};
}

// ---- icebergs ----
// An iceberg's shape (size 1 across at the water, its waterline at y = 0): a lumpy block, flat-topped and steep-sided above the water where the sea has carved it, a
// broad keel below - seven eighths of it under.
inline const Mesh &bergShape(int seed) {
	static std::vector<std::pair<int, Mesh>> made;
	for (const auto &[s, m] : made)
		if (s == seed) return m;
	const auto f = [seed](Vec3 p) {
		const double lump = 0.12 * (nature::noise2(p.x * 2.5 + seed, p.z * 2.5 + p.y * 1.7, seed + 111) - 0.5) + 0.05 * (nature::noise2(p.x * 7.0, p.y * 7.0 + p.z * 3.0, seed + 113) - 0.5);
		const double above = sdf::roundBox(p, translate(Vec3{0.0, -0.18, 0.0} * -1.0), {0.42, 0.18 + 0.12 * hash01(seed, 115), 0.34}, 0.06);   // (the berg above the water)
		const double keel = sdf::ellipsoid(p, Vec3{0.05, -0.55, 0.0}, {0.75, 0.6, 0.6});
		return sdf::smin(above, keel, 0.15) + lump;
	};
	Mesh m = sdf::surfaceNets(f, {-1.0, -1.3, -0.9}, {1.0, 0.6, 0.9}, 1.0 / 48.0);
	made.push_back({seed, std::move(m)});
	return made.back().second;
}
// An iceberg at `nt`, as scene text: bobbing and turning slowly on the sea.
inline std::string bergText(const Iceberg &b, double nt, double level) {
	const Mesh &shape = bergShape(b.seed);
	const double turn = 0.4 * nt * (hash01(b.seed, 117) - 0.5), bob = 0.012 * b.size * std::sin(0.7 * nt + 6.28 * hash01(b.seed, 119));
	const double tilt = 1.5 * std::sin(0.45 * nt + 3.0 * hash01(b.seed, 121));
	const Mat4 place = mul(translate(b.x, level + bob, b.z), rotate({0.0, 1.0, 0.0}, turn * 180.0 / kPi + 360.0 * hash01(b.seed, 123)), rotate({1.0, 0.0, 0.0}, tilt), scale(b.size));
	Mesh m;
	m.tris = shape.tris;
	for (const Vec3 &v : shape.verts) m.verts.push_back(transformPoint(place, v));
	return "# an iceberg (film_ice.h)\n" + detail::iceAndSnow(std::move(m), 0.8);
}

// ---- sea ice ----
// The floes round the camera at `nt` (pbrt's `eye`), as scene text: plates of ice on a grid of cells `floe` across (the same in the world: they stand still as the camera
// moves), each a rough polygon with a little thickness, riding the sea's surface; `cover` of the cells have one. Out to 40 floes away.
inline std::string seaIceText(const SeaIce &si, const water::Sea &sea, double nt, Vec3 eye) {
	const double F = si.floe, reach = 40.0 * F;
	const int i0 = static_cast<int>(std::floor((eye.x - reach) / F)), i1 = static_cast<int>(std::floor((eye.x + reach) / F));
	const int k0 = static_cast<int>(std::floor((eye.z - reach) / F)), k1 = static_cast<int>(std::floor((eye.z + reach) / F));
	Mesh m;
	for (int i = i0; i <= i1; ++i)
		for (int k = k0; k <= k1; ++k) {
			const int cell = i * 7919 + k * 104729 + si.seed;
			if (hash01(cell, 131) > si.cover) continue;
			const double cx = (i + 0.5 + 0.2 * (hash01(cell, 133) - 0.5)) * F, cz = (k + 0.5 + 0.2 * (hash01(cell, 135) - 0.5)) * F;
			if (std::hypot(cx - eye.x, cz - eye.z) > reach) continue;
			const double r = 0.5 * F * (0.6 + 0.35 * hash01(cell, 137)), thick = 0.06 * F * (0.5 + hash01(cell, 139));
			const int sides = 9;
			const int base = static_cast<int>(m.verts.size());
			Vec3 p, n;
			double fold = 0.0;
			water::seaAt(sea, cx, cz, nt, 0.0, p, n, fold, F);
			for (int s = 0; s < sides; ++s) {   // its rim, top and bottom, round its middle (it rides the sea's surface there)
				const double a = 2.0 * kPi * (s + 0.45 * hash01(cell * 13 + s, 141) + hash01(cell, 145)) / sides, rr = r * (0.5 + 0.5 * hash01(cell * 13 + s, 143));
				const Vec3 q{cx + rr * std::cos(a), 0.0, cz + rr * std::sin(a)};
				const double y = p.y + (q.x - cx) * (-n.x / n.y) + (q.z - cz) * (-n.z / n.y);
				m.verts.push_back({q.x, y + 0.3 * thick, q.z});
				m.verts.push_back({q.x, y - 0.7 * thick, q.z});
			}
			m.verts.push_back({p.x, p.y + 0.3 * thick, p.z});   // (its middle, on top)
			const int mid = base + 2 * sides;
			for (int s = 0; s < sides; ++s) {
				const int a = base + 2 * s, b = base + 2 * ((s + 1) % sides);
				m.tris.push_back({mid, b, a});
				m.tris.push_back({a, b, b + 1});
				m.tris.push_back({a, b + 1, a + 1});
			}
		}
	if (m.tris.empty()) return std::string();
	return "# sea ice (film_ice.h)\n" + detail::iceAndSnow(std::move(m), 0.7);
}

// ---- a frozen waterfall ----
// A waterfall hung in ice: icicles from the lip, long and short, thickest at the top, some reaching all the way down as columns, and a frozen pool where it would land.
inline std::string frozenFallText(const water::Fall &f) {
	const double a = f.facing * kPi / 180.0;
	const Vec3 out{std::sin(a), 0.0, std::cos(a)}, side{std::cos(a), 0.0, -std::sin(a)};
	Mesh m;
	const int count = std::clamp(static_cast<int>(f.width * 6.0), 12, 400);
	for (int k = 0; k < count; ++k) {
		const double across = (k + hash01(k, 151)) / count - 0.5, lenShare = hash01(k, 153) < 0.25 ? 1.0 : 0.15 + 0.7 * hash01(k, 155) * hash01(k, 157);
		const double len = f.drop * lenShare, r0 = (0.04 + 0.08 * hash01(k, 159)) * std::sqrt(f.drop) * (lenShare > 0.99 ? 1.4 : 1.0);
		const Vec3 top = water::fallPoint(f, across, 0.08) - out * (0.1 + 0.3 * hash01(k, 161)) + Vec3{0.0, 0.05, 0.0};
		const int rings = 10, sides = 7, base = static_cast<int>(m.verts.size());
		for (int j = 0; j <= rings; ++j) {
			const double v = j / double(rings), r = (lenShare > 0.99 ? r0 * (1.0 - 0.4 * v) : r0 * (1.0 - v)) * (1.0 + 0.25 * std::sin(17.0 * v + k));
			const Vec3 c = top + Vec3{0.0, -len * v, 0.0} + out * (0.4 * v * v * (lenShare > 0.99 ? 1.0 : 0.3));
			for (int s = 0; s < sides; ++s) {
				const double t = 2.0 * kPi * s / sides;
				m.verts.push_back(c + side * (r * std::cos(t)) + out * (r * std::sin(t)));
			}
		}
		for (int j = 0; j < rings; ++j)
			for (int s = 0; s < sides; ++s) {
				const int p = base + j * sides + s, q = base + j * sides + (s + 1) % sides;
				m.tris.push_back({p, q, p + sides});
				m.tris.push_back({q, q + sides, p + sides});
			}
	}
	// the frozen pool: a disc of ice where the water would have landed
	const Vec3 foot = water::fallPoint(f, 0.0, water::fallTime(f)) + out * 0.5;
	const int base = static_cast<int>(m.verts.size()), around = 24;
	const double R = 0.8 * f.width + 0.15 * f.drop;
	m.verts.push_back(foot + Vec3{0.0, 0.25, 0.0});
	for (int s = 0; s < around; ++s) {
		const double t = 2.0 * kPi * s / around, rr = R * (0.8 + 0.2 * hash01(s, 163));
		m.verts.push_back(foot + side * (rr * std::cos(t)) + out * (rr * std::sin(t)) + Vec3{0.0, 0.05, 0.0});
	}
	for (int s = 0; s < around; ++s) m.tris.push_back({base, base + 1 + (s + 1) % around, base + 1 + s});
	return "# a frozen waterfall (film_ice.h)\n" + detail::iceAndSnow(std::move(m), 0.85);
}

}  // namespace ice
}  // namespace cartoon
