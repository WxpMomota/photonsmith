#pragma once
// tengwang_pavilion.h -- the Pavilion of Prince Teng (tengwang.h): a three-tiered tower of the Tang in its full colours on a two-tiered terrace of white stone at the river's edge.
//
//   the terrace     two tiers of pale granite with carved white balustrades (posts with lotus tops, panels between), grand stairs down to the river (west) and to the city (east)
//   the tiers       vermilion columns round each tier; between the inner columns lattice doors backed with paper (lamplight glows through them at night), open on the river side;
//                   a balcony walk round the second and third tiers behind red railings
//   the brackets    rows of bracket sets under every eave, painted in bands of lapis blue, malachite green and gold
//   the roofs       jade-green glazed tiles in rolled rows on concave slopes whose corners sweep up and out, dark ridges down the hips, a hip-and-gable crown with gold dragon-fish
//                   at the ends of its main ridge
//   the rest        the gold-on-blue name board under the second eave, red silk lanterns at the corners, floors and painted ceilings inside, a stair up through the tiers
//
// Story space: the Pavilion's middle is at x = z = 0, its front (the river side) faces -z, the ground round it is at y = kGround. Every part is written once as a PLY file per
// material (film_cache.h); pavilionText() places them. Dimensions in metres; the top of the main ridge is about 47 m up.

#include <map>
#include <string>
#include <vector>

#include "tengwang_land.h"

namespace cartoon {
namespace tengwang {

constexpr int kPavilionVersion = 5;
constexpr double kGround = 1.0;

// The levels (y) of the Pavilion, for the shots and the people in it.
constexpr double kLowerTerrace = 6.0, kUpperTerrace = 11.0;   // the tops of the two stone tiers
constexpr double kDeck2 = 22.8, kDeck3 = 32.6;                // the floors (and balcony walks) of the second and third tiers
constexpr double kCol1 = 7.6, kCol2 = 6.4, kCol3 = 6.0;       // the tiers' column heights
constexpr double kRail2X = 18.3, kRail2Z = 11.8, kRail3X = 16.0, kRail3Z = 10.0;   // where the balcony railings run

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Building blocks
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline void append(Mesh &dst, const Mesh &src, const Mat4 &m) {
	const int base = static_cast<int>(dst.verts.size());
	const Mat4 inv = invertAffine(m);
	for (std::size_t i = 0; i < src.verts.size(); ++i) {
		dst.verts.push_back(transformPoint(m, src.verts[i]));
		const Vec3 n = i < src.normals.size() ? src.normals[i] : Vec3{0.0, 1.0, 0.0};
		dst.normals.push_back(normalize({n.x * inv[0] + n.y * inv[4] + n.z * inv[8], n.x * inv[1] + n.y * inv[5] + n.z * inv[9], n.x * inv[2] + n.y * inv[6] + n.z * inv[10]}));
	}
	if (!dst.uvs.empty() || (dst.verts.size() == src.verts.size() && !src.uvs.empty())) {
		dst.uvs.resize(static_cast<std::size_t>(base));
		for (std::size_t i = 0; i < src.verts.size(); ++i) dst.uvs.push_back(i < src.uvs.size() ? src.uvs[i] : std::pair<double, double>{0.0, 0.0});
	}
	for (const auto &t : src.tris) dst.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
}
// A box with its centre at c, turned `yaw` degrees about the vertical.
inline void addBox(Mesh &m, Vec3 c, Vec3 size, double yaw = 0.0) {
	static const Mesh unit = box(1.0, 1.0, 1.0);
	append(m, unit, mul(translate(c), rotate({0, 1, 0}, yaw), scale(size.x, size.y, size.z)));
}
// A box between two points on the ground plan (a beam, a rail, a step), `w` wide and `h` tall, its top at y.
inline void addBeam(Mesh &m, Vec3 a, Vec3 b, double w, double h) {
	const Vec3 d = b - a;
	const double len = std::hypot(d.x, d.z);
	if (len < 1e-6) return;
	const double yaw = std::atan2(d.x, d.z) * 180.0 / kPi;
	addBox(m, (a + b) * 0.5 - Vec3{0.0, h / 2.0, 0.0}, {w, h, len}, yaw);
}
inline void addCylinder(Mesh &m, Vec3 base, double height, double r0, double r1, int segs = 20) {
	append(m, lathe({{0.0, 0.0}, {r0, 0.0}, {r1, height}, {0.0, height}}, segs), translate(base));
}
// The points round a rectangle of half-sizes (hx, hz), `step` apart (corners included).
inline std::vector<Vec3> ringPoints(double hx, double hz, double y, double step) {
	std::vector<Vec3> out;
	const Vec3 corners[4] = {{-hx, y, -hz}, {hx, y, -hz}, {hx, y, hz}, {-hx, y, hz}};
	for (int k = 0; k < 4; ++k) {
		const Vec3 a = corners[k], b = corners[(k + 1) % 4];
		const int n = (std::max)(1, static_cast<int>(std::lround(length(b - a) / step)));
		for (int i = 0; i < n; ++i) out.push_back(lerp(a, b, static_cast<double>(i) / n));
	}
	return out;
}

// The Pavilion's parts by material.
struct Parts {
	std::map<std::string, Mesh> by;
	Mesh &operator[](const std::string &k) { return by[k]; }
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Roofs
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A roof skirt: the ring of roof between an inner rectangle (where it meets the wall or the balcony above, half-sizes ix, iz at height iy) and the eave (ox, oz at oy), concave
// (steep above, flattening to the eave), its corners sweeping up by `upturn` and out by `flare`, with rows of rolled tiles running down it. The tiles' top surface, a soffit under
// it (`thick` lower) and the eave's edge are written into `parts`.
struct Skirt {
	double ix, iz, iy;
	double ox, oz, oy;
	double upturn = 1.2, flare = 1.0;
	double thick = 0.55;
	double period = 0.38;   // the tile rows' spacing
	double tileDepth = 0.07;
};
inline void addSkirt(Parts &parts, const Skirt &s, int across = 22) {
	Mesh top, under, edge;
	// one side at a time: k = 0 front (-z), 1 right (+x), 2 back, 3 left
	const Vec3 ic[4] = {{-s.ix, 0, -s.iz}, {s.ix, 0, -s.iz}, {s.ix, 0, s.iz}, {-s.ix, 0, s.iz}};
	const Vec3 oc[4] = {{-s.ox, 0, -s.oz}, {s.ox, 0, -s.oz}, {s.ox, 0, s.oz}, {-s.ox, 0, s.oz}};
	const double reach = 0.42 * (std::min)(s.ox, s.oz);
	Vec3 outwards[4];
	for (int k = 0; k < 4; ++k) outwards[k] = normalize(Vec3{(oc[k] + oc[(k + 1) % 4]).x, 0.0, (oc[k] + oc[(k + 1) % 4]).z});
	for (int k = 0; k < 4; ++k) {
		const Vec3 ia = ic[k], ib = ic[(k + 1) % 4], oa = oc[k], ob = oc[(k + 1) % 4];
		const double side = length(ob - oa);
		const int along = (std::max)(8, static_cast<int>(side / (s.period / 4.0)));
		const Vec3 outward = outwards[k];
		const auto point = [&](int i, int j, double down) {
			const double a = static_cast<double>(i) / along, r = static_cast<double>(j) / across;
			const double toCorner = (std::min)(a, 1.0 - a) * side;
			const double c = (std::max)(0.0, 1.0 - toCorner / reach);
			// the corners sweep out along the diagonal (this side's outward plus its neighbour's), so the two sides meet along the hip
			const Vec3 flareDir = outward + (a < 0.5 ? outwards[(k + 3) % 4] : outwards[(k + 1) % 4]) * c;
			Vec3 inner = lerp(ia, ib, a), outer = lerp(oa, ob, a) + flareDir * (s.flare * c * c);
			Vec3 p = lerp(inner, outer, r);
			double y = s.oy + (s.iy - s.oy) * std::pow(1.0 - r, 1.65);
			y += s.upturn * c * c * std::pow(r, 1.6);
			const double u = a * side;
			// (the tile rows fade out at the hips, where the two sides' rows would not meet: the hip ridge covers the join)
			if (down == 0.0) y += s.tileDepth * (0.5 + 0.5 * std::cos(2.0 * kPi * u / s.period)) * (0.4 + 0.6 * r) * (1.0 - smoothstep(0.92, 1.0, r) * 0.6) * smoothstep(0.1, 0.9, toCorner);
			y -= down * (0.35 + 0.65 * r);
			p.y = y;
			return p;
		};
		const auto grid = [&](Mesh &m, double down, bool flip) {
			const int base = static_cast<int>(m.verts.size());
			for (int j = 0; j <= across; ++j)
				for (int i = 0; i <= along; ++i) m.verts.push_back(point(i, j, down));
			for (int j = 0; j < across; ++j)
				for (int i = 0; i < along; ++i) {
					const int q = base + j * (along + 1) + i;
					if (!flip) {
						m.tris.push_back({q, q + 1, q + along + 1});
						m.tris.push_back({q + 1, q + along + 2, q + along + 1});
					} else {
						m.tris.push_back({q, q + along + 1, q + 1});
						m.tris.push_back({q + 1, q + along + 1, q + along + 2});
					}
				}
		};
		grid(top, 0.0, false);
		grid(under, s.thick, true);
		// the eave's edge: a band from the tiles down to the soffit, along r = 1
		const int base = static_cast<int>(edge.verts.size());
		for (int i = 0; i <= along; ++i) {
			edge.verts.push_back(point(i, across, 0.0) + Vec3{0.0, 0.05, 0.0});
			edge.verts.push_back(point(i, across, s.thick));
		}
		for (int i = 0; i < along; ++i) {
			const int q = base + 2 * i;
			edge.tris.push_back({q, q + 1, q + 2});
			edge.tris.push_back({q + 1, q + 3, q + 2});
		}
	}
	// (the windings: the tiles face up, the soffit down, the edge out)
	for (auto &t : top.tris) {
		const Vec3 n = cross(top.verts[static_cast<std::size_t>(t[1])] - top.verts[static_cast<std::size_t>(t[0])], top.verts[static_cast<std::size_t>(t[2])] - top.verts[static_cast<std::size_t>(t[0])]);
		if (n.y < 0.0) std::swap(t[1], t[2]);
	}
	for (auto &t : under.tris) {
		const Vec3 n = cross(under.verts[static_cast<std::size_t>(t[1])] - under.verts[static_cast<std::size_t>(t[0])], under.verts[static_cast<std::size_t>(t[2])] - under.verts[static_cast<std::size_t>(t[0])]);
		if (n.y > 0.0) std::swap(t[1], t[2]);
	}
	for (auto &t : edge.tris) {
		const Vec3 a = edge.verts[static_cast<std::size_t>(t[0])], n = cross(edge.verts[static_cast<std::size_t>(t[1])] - a, edge.verts[static_cast<std::size_t>(t[2])] - a);
		if (dot(n, Vec3{a.x, 0.0, a.z}) < 0.0) std::swap(t[1], t[2]);
	}
	sdf::computeNormals(top);
	sdf::computeNormals(under);
	sdf::computeNormals(edge);
	append(parts["tile"], top, identity());
	append(parts["soffit"], under, identity());
	append(parts["fascia"], edge, identity());
	// the hip ridges: a dark rolled ridge down each corner, curling up at its end, with a little gold at the tip
	for (int k = 0; k < 4; ++k) {
		const double sx = (k == 1 || k == 2) ? 1.0 : -1.0, sz = (k >= 2) ? 1.0 : -1.0;
		std::vector<Vec3> pts;
		for (int j = 0; j <= 16; ++j) {
			const double r = j / 16.0;
			Vec3 p = lerp(Vec3{sx * s.ix, 0.0, sz * s.iz}, Vec3{sx * (s.ox + s.flare), 0.0, sz * (s.oz + s.flare)}, r);
			p.y = s.oy + (s.iy - s.oy) * std::pow(1.0 - r, 1.65) + s.upturn * std::pow(r, 1.6) + 0.30 + 0.25 * smoothstep(0.85, 1.0, r);
			pts.push_back(p);
		}
		append(parts["ridge"], sdf::tubeMesh(pts, 0.24, 0.85, 10), identity());
		append(parts["gold"], uvSphere(0.22, 10, 6), translate(pts.back() + Vec3{0.0, 0.25, 0.0}));
	}
	// rafters under the eave: a row of round rafter ends along each side, just under the edge
	for (int k = 0; k < 4; ++k) {
		const Vec3 oa = oc[k], ob = oc[(k + 1) % 4];
		const double side = length(ob - oa);
		const Vec3 dir = normalize(ob - oa), outward = normalize(Vec3{(oa + ob).x, 0.0, (oa + ob).z});
		for (double u = 0.9; u < side - 0.9; u += 0.42) {
			const double c = (std::max)(0.0, 1.0 - (std::min)(u, side - u) / reach);
			const Vec3 p = oa + dir * u + outward * (s.flare * c * c - 0.25) + Vec3{0.0, s.oy + s.upturn * c * c - s.thick - 0.08, 0.0};
			append(parts["rafter"], capsule(0.07, 0.7, 6, 1), alignY(p - outward * 0.7, outward));
		}
	}
}

// Bracket sets (dougong) along a rectangle at height y: stacked blocks and arms, painted lapis and malachite with gold edges, one at every column and two between.
inline void addBrackets(Parts &parts, double hx, double hz, double y, double reachOut, double step) {
	for (const Vec3 &p : ringPoints(hx, hz, y, step)) {
		const Vec3 out = normalize(Vec3{std::abs(std::abs(p.x) - hx) < 1e-6 ? p.x : 0.0, 0.0, std::abs(std::abs(p.z) - hz) < 1e-6 ? p.z : 0.0});
		const Vec3 side = Vec3{-out.z, 0.0, out.x};
		const double yaw = std::atan2(out.x, out.z) * 180.0 / kPi;
		const bool blue = (static_cast<int>(std::lround((p.x + p.z) / step)) % 2) == 0;
		Mesh &paint = parts[blue ? "paint_blue" : "paint_green"];
		addBox(parts["paint_gold"], p + Vec3{0.0, 0.12, 0.0}, {0.62, 0.24, 0.62}, yaw);   // the big block (ludou)
		for (int tier = 0; tier < 3; ++tier) {
			const double ty = 0.28 + tier * 0.30, len = 0.9 + tier * 0.45;   // (above p, which is at y)
			addBox(paint, p + Vec3{0.0, ty + 0.11, 0.0} + out * (0.18 + tier * reachOut * 0.25), {0.22, 0.22, len}, yaw);              // the arm reaching out
			addBox(paint, p + Vec3{0.0, ty + 0.11, 0.0} + out * (0.10 + tier * reachOut * 0.22), {0.20, 0.20, len * 1.1}, yaw + 90.0);  // the cross arm
			for (int sgn = -1; sgn <= 1; sgn += 2)
				addBox(parts["paint_gold"], p + Vec3{0.0, ty + 0.26, 0.0} + out * (0.10 + tier * reachOut * 0.22) + side * (sgn * len * 0.5), {0.24, 0.10, 0.24}, yaw);
		}
	}
	// the painted beam the brackets sit on: lapis with a gold line
	const Vec3 corners[4] = {{-hx, y, -hz}, {hx, y, -hz}, {hx, y, hz}, {-hx, y, hz}};
	for (int k = 0; k < 4; ++k) {
		addBeam(parts["paint_blue"], corners[k], corners[(k + 1) % 4], 0.5, 0.55);
		addBeam(parts["paint_gold"], corners[k] - Vec3{0.0, 0.55, 0.0}, corners[(k + 1) % 4] - Vec3{0.0, 0.55, 0.0}, 0.52, 0.07);
		addBeam(parts["paint_green"], corners[k] - Vec3{0.0, 0.62, 0.0}, corners[(k + 1) % 4] - Vec3{0.0, 0.62, 0.0}, 0.46, 0.45);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Walls, railings, terraces
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A ring of columns (vermilion, on round stone bases) round a rectangle.
inline void addColumns(Parts &parts, double hx, double hz, double y0, double height, double step, double r = 0.42) {
	for (const Vec3 &p : ringPoints(hx, hz, y0, step)) {
		addCylinder(parts["red"], p, height, r, r * 0.9, 22);
		append(parts["stone"], lathe({{0.0, 0.0}, {r * 1.55, 0.0}, {r * 1.55, 0.12}, {r * 1.3, 0.32}, {0.0, 0.32}}, 22), translate(p));
		addCylinder(parts["paint_gold"], p + Vec3{0.0, height - 0.5, 0.0}, 0.12, r * 0.95, r * 0.95, 22);   // a gold band under the capital
	}
}

// Lattice doors between the columns of a wall round a rectangle (hx, hz), from y0 to y1: frames and lattice in red-brown, paper behind the lattice, a solid panel below; the
// sides listed in `open` (0 front ... 3 left) have their middle bays open.
inline void addLatticeWalls(Parts &parts, double hx, double hz, double y0, double y1, double bay, const std::vector<int> &open, int openBays) {
	const Vec3 corners[4] = {{-hx, y0, -hz}, {hx, y0, -hz}, {hx, y0, hz}, {-hx, y0, hz}};
	const double h = y1 - y0;
	for (int k = 0; k < 4; ++k) {
		const Vec3 a = corners[k], b = corners[(k + 1) % 4];
		const double side = length(b - a);
		const Vec3 dir = normalize(b - a), out = normalize(Vec3{(a + b).x, 0.0, (a + b).z});
		const double yaw = std::atan2(dir.x, dir.z) * 180.0 / kPi + 90.0;
		const int bays = (std::max)(1, static_cast<int>(std::lround(side / bay)));
		const double bw = side / bays;
		const bool sideOpen = std::find(open.begin(), open.end(), k) != open.end();
		for (int i = 0; i < bays; ++i) {
			if (sideOpen && std::abs(i - (bays - 1) / 2.0) < openBays / 2.0) continue;
			// four leaves to a bay
			for (int leaf = 0; leaf < 4; ++leaf) {
				const double w = bw / 4.0;
				const Vec3 c = a + dir * (bw * i + w * (leaf + 0.5));
				const double split = y0 + h * 0.36;
				// the frame
				addBox(parts["lattice"], c + Vec3{0.0, y0 - c.y + h / 2.0, 0.0} + dir * (w / 2.0 - 0.04), {0.08, h, 0.14}, yaw + 90.0);
				addBox(parts["lattice"], c + Vec3{0.0, split - c.y, 0.0}, {w, 0.10, 0.14}, yaw + 90.0 - 90.0);
				addBox(parts["lattice"], c + Vec3{0.0, y1 - c.y - 0.05, 0.0}, {w, 0.10, 0.14}, yaw);
				// the solid panel below, with a gold-edged inset
				addBox(parts["panel"], c + Vec3{0.0, (y0 + split) / 2.0 - c.y, 0.0}, {w - 0.08, split - y0, 0.06}, yaw);
				addBox(parts["paint_gold"], c + Vec3{0.0, (y0 + split) / 2.0 - c.y, 0.0} + out * 0.04, {w * 0.6, (split - y0) * 0.5, 0.02}, yaw);
				// the lattice above: a grid of thin bars, and the paper behind it
				for (int g = 1; g < 5; ++g) addBox(parts["lattice"], c + Vec3{0.0, split - c.y + (y1 - split) / 2.0, 0.0} + dir * (w * (g / 5.0 - 0.5)) + out * 0.02, {0.035, y1 - split, 0.05}, yaw);
				for (double gy = split + 0.16; gy < y1 - 0.08; gy += 0.16) addBox(parts["lattice"], c + Vec3{0.0, gy - c.y, 0.0} + out * 0.02, {w, 0.035, 0.05}, yaw);
				addBox(parts["paper"], c + Vec3{0.0, split - c.y + (y1 - split) / 2.0, 0.0} - out * 0.03, {w, y1 - split, 0.01}, yaw);
			}
		}
		// the beam over the doors
		addBeam(parts["paint_green"], a + Vec3{0.0, h, 0.0}, b + Vec3{0.0, h, 0.0}, 0.35, 0.4);
	}
}

// A red wooden railing round a rectangle, `height` tall, open where `gaps` says (a list of {side, from, to} along the side, in metres from its start).
struct Gap {
	int side;
	double from, to;
};
inline void addRailing(Parts &parts, double hx, double hz, double y, double height, const std::vector<Gap> &gaps = {}) {
	const Vec3 corners[4] = {{-hx, y, -hz}, {hx, y, -hz}, {hx, y, hz}, {-hx, y, hz}};
	for (int k = 0; k < 4; ++k) {
		const Vec3 a = corners[k], b = corners[(k + 1) % 4];
		const double side = length(b - a);
		const Vec3 dir = normalize(b - a);
		const auto inGap = [&](double u) {
			for (const Gap &g : gaps)
				if (g.side == k && u > g.from && u < g.to) return true;
			return false;
		};
		const auto run = [&](double u0, double u1) {
			if (u1 - u0 < 0.3) return;
			const Vec3 p0 = a + dir * u0, p1 = a + dir * u1;
			addBeam(parts["red"], p0 + Vec3{0.0, height, 0.0}, p1 + Vec3{0.0, height, 0.0}, 0.16, 0.12);
			addBeam(parts["red"], p0 + Vec3{0.0, height * 0.55, 0.0}, p1 + Vec3{0.0, height * 0.55, 0.0}, 0.10, 0.08);
			addBeam(parts["red"], p0 + Vec3{0.0, 0.12, 0.0}, p1 + Vec3{0.0, 0.12, 0.0}, 0.10, 0.10);
			for (double u = u0; u <= u1 + 1e-6; u += 1.6) addBox(parts["red"], a + dir * u + Vec3{0.0, height / 2.0 + 0.05, 0.0}, {0.17, height + 0.1, 0.17});
			for (double u = u0 + 0.2; u < u1; u += 0.2) addBox(parts["paint_gold"], a + dir * u + Vec3{0.0, height * 0.33, 0.0}, {0.035, height * 0.38, 0.035});
		};
		double start = 0.0;
		for (double u = 0.0; u <= side + 1e-6; u += 0.1) {
			if (inGap(u) || u >= side) {
				run(start, (std::min)(u, side));
				while (u <= side && inGap(u)) u += 0.1;
				start = u;
			}
		}
	}
}

// A white stone balustrade round a rectangle: posts with lotus tops and carved panels between, open where `gaps` says.
inline void addBalustrade(Parts &parts, double hx, double hz, double y, const std::vector<Gap> &gaps = {}) {
	const Vec3 corners[4] = {{-hx, y, -hz}, {hx, y, -hz}, {hx, y, hz}, {-hx, y, hz}};
	for (int k = 0; k < 4; ++k) {
		const Vec3 a = corners[k], b = corners[(k + 1) % 4];
		const double side = length(b - a);
		const Vec3 dir = normalize(b - a);
		const double yaw = std::atan2(dir.x, dir.z) * 180.0 / kPi;
		for (double u = 0.0; u < side - 0.1; u += 1.9) {
			bool skip = false;
			for (const Gap &g : gaps) skip = skip || (g.side == k && u + 0.95 > g.from && u + 0.95 < g.to);
			const Vec3 p = a + dir * u;
			addBox(parts["marble"], p + Vec3{0.0, 0.62, 0.0}, {0.24, 1.24, 0.24});
			append(parts["marble"], lathe({{0.0, 0.0}, {0.13, 0.0}, {0.16, 0.08}, {0.13, 0.2}, {0.0, 0.26}}, 12), translate(p + Vec3{0.0, 1.24, 0.0}));
			if (skip) continue;
			addBox(parts["marble"], p + dir * 0.95 + Vec3{0.0, 0.42, 0.0}, {0.12, 0.66, 1.66}, yaw);   // the panel
			addBox(parts["marble"], p + dir * 0.95 + Vec3{0.0, 0.95, 0.0}, {0.16, 0.12, 1.66}, yaw);   // its rail
		}
	}
}

// A flight of steps from (x0..x1) at z = zTop (level yTop) down to zBottom (level yBottom), solid below.
inline void addStairs(Parts &parts, double x0, double x1, double zTop, double yTop, double zBottom, double yBottom) {
	const int n = (std::max)(1, static_cast<int>(std::lround((yTop - yBottom) / 0.16)));
	for (int i = 0; i < n; ++i) {
		const double y = yBottom + (yTop - yBottom) * (i + 1.0) / n, z0 = lerp(zBottom, zTop, static_cast<double>(i) / n), z1 = lerp(zBottom, zTop, (i + 1.0) / n);
		addBox(parts["stone"], {(x0 + x1) / 2.0, (y + yBottom) / 2.0, (z0 + zTop) / 2.0}, {x1 - x0, y - yBottom, std::abs(zTop - z0)});
		(void)z1;
	}
	// the carved ramp down the middle (the imperial way) and the sloping side walls with their balustrades
	const double mid = (x0 + x1) / 2.0, run = std::abs(zTop - zBottom);
	const auto wedge = [&](Mesh &m, double xa, double xb, double lift) {
		// a solid whose top slopes from (zTop, yTop + lift) down to (zBottom, yBottom + lift), from yBottom - 0.2 up
		Mesh w;
		const double yb = yBottom - 0.2;
		const Vec3 v[8] = {{xa, yb, zTop}, {xb, yb, zTop}, {xb, yb, zBottom}, {xa, yb, zBottom}, {xa, yTop + lift, zTop}, {xb, yTop + lift, zTop}, {xb, yBottom + lift, zBottom}, {xa, yBottom + lift, zBottom}};
		const int f[6][4] = {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1}, {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}};
		for (const auto &q : f) {
			const int b = static_cast<int>(w.verts.size());
			const Vec3 n = normalize(cross(v[q[1]] - v[q[0]], v[q[2]] - v[q[0]]));
			for (int c = 0; c < 4; ++c) {
				w.verts.push_back(v[q[c]]);
				w.normals.push_back(n);
			}
			w.tris.push_back({b, b + 1, b + 2});
			w.tris.push_back({b, b + 2, b + 3});
		}
		// (outward-facing whichever way the stair runs)
		const Vec3 centre = {(xa + xb) / 2.0, (yTop + yBottom) / 2.0, (zTop + zBottom) / 2.0};
		for (std::size_t i = 0; i < w.tris.size(); ++i) {
			auto &tr = w.tris[i];
			const Vec3 a = w.verts[static_cast<std::size_t>(tr[0])];
			if (dot(cross(w.verts[static_cast<std::size_t>(tr[1])] - a, w.verts[static_cast<std::size_t>(tr[2])] - a), a - centre) < 0.0) std::swap(tr[1], tr[2]);
		}
		// (flat faces: each face's own normal, from its triangle as it is now wound)
		for (std::size_t i = 0; i < w.tris.size(); i += 2) {
			const auto &tr = w.tris[i];
			const Vec3 a = w.verts[static_cast<std::size_t>(tr[0])];
			const Vec3 n = normalize(cross(w.verts[static_cast<std::size_t>(tr[1])] - a, w.verts[static_cast<std::size_t>(tr[2])] - a));
			for (int c = 0; c < 4; ++c) w.normals[i * 2 + static_cast<std::size_t>(c)] = n;
		}
		append(m, w, identity());
	};
	wedge(parts["marble"], mid - 1.3, mid + 1.3, 0.12);
	for (const double x : {x0 - 0.4, x1 + 0.4}) {
		wedge(parts["stone"], x - 0.4, x + 0.4, 0.25);
		for (int i = 0; i <= static_cast<int>(run / 1.9); ++i) {
			const double u = i * 1.9 / run, y = lerp(yTop, yBottom, u), z = lerp(zTop, zBottom, u);
			addBox(parts["marble"], {x, y + 0.62, z}, {0.24, 1.24, 0.24});
		}
		std::vector<Vec3> rail = {{x, yTop + 0.95, zTop}, {x, yBottom + 0.95, zBottom}};
		addBeam(parts["marble"], {x, yTop + 1.0, zTop}, {x, yTop + 1.0, zTop}, 0.1, 0.1);
		append(parts["marble"], sdf::tubeMesh(rail, 0.09, 1.0, 8), identity());
	}
}

// The riverside in front of the Pavilion: a stone quay along the bank (its paving a little above the ground, its wall standing in the water), a broad flight of steps down to the
// water before the Pavilion's stairs, and a wooden jetty on posts with mooring posts for the boats.
inline void addRiverside(Parts &p) {
	const double top = kGround + 0.2, foot = kWater - 1.2;
	const double x0 = -380.0, x1 = 380.0, step = 3.0;
	for (double x = x0; x < x1; x += step) {
		const double za = nearBank(x) - 10.0, zb = nearBank(x + step) - 10.0;   // the quay's face, ten metres out in the water
		const double back = (std::max)(nearBank(x), nearBank(x + step)) + 26.0;
		Mesh slab;
		// the paving (a quad from the face back onto the land) and the wall's face
		const Vec3 v[6] = {{x, top, za}, {x + step, top, zb}, {x + step, top, back}, {x, top, back}, {x, foot, za}, {x + step, foot, zb}};
		slab.verts = {v[0], v[1], v[2], v[3]};
		slab.normals.assign(4, Vec3{0.0, 1.0, 0.0});
		slab.tris = {{0, 2, 1}, {0, 3, 2}};
		append(p["paving"], slab, identity());
		Mesh wall;
		wall.verts = {v[0], v[1], v[5], v[4]};
		const Vec3 n = normalize(cross(v[1] - v[0], v[4] - v[0]));
		wall.normals.assign(4, n.z < 0.0 ? n : n * -1.0);
		wall.tris = n.z < 0.0 ? std::vector<std::array<int, 3>>{{0, 1, 2}, {0, 2, 3}} : std::vector<std::array<int, 3>>{{0, 2, 1}, {0, 3, 2}};
		append(p["stone"], wall, identity());
		// a coping along the edge, and a low stone post every few metres
		addBeam(p["stone_dark"], {x, top + 0.25, za + 0.3}, {x + step, top + 0.25, zb + 0.3}, 0.7, 0.3);
		if (std::fmod(x - x0, 9.0) < 0.5 && std::abs(x) > 14.0) addBox(p["marble"], {x, top + 0.55, za + 0.4}, {0.35, 0.7, 0.35});
	}
	// the water steps before the Pavilion
	const double zFace = nearBank(0.0) - 10.0;
	const int n = static_cast<int>((top - kWater + 1.0) / 0.2);
	for (int i = 0; i < n; ++i) {
		const double y = top - 0.2 * (i + 1), z = zFace - 0.45 * (i + 1);
		addBox(p["stone"], {0.0, (y + foot) / 2.0, z + 0.225}, {24.0, y - foot, 0.45 + 0.01});
	}
	// the jetty: planks on posts, out into the river beside the steps, and its mooring posts
	const double jx = 46.0, jz0 = nearBank(jx) - 10.0, jz1 = jz0 - 60.0;
	addBox(p["wood"], {jx, top - 0.1, (jz0 + jz1) / 2.0}, {5.0, 0.25, jz0 - jz1});
	for (double z = jz0; z >= jz1; z -= 4.0)
		for (const double dx : {-2.3, 2.3}) addCylinder(p["wood"], {jx + dx, foot, z}, top - foot + 0.6, 0.16, 0.14, 10);
	for (double z = jz0 - 6.0; z >= jz1; z -= 12.0) addCylinder(p["red"], {jx - 2.6, top, z}, 1.1, 0.13, 0.11, 10);
}

// Where the red silk lanterns hang: under the eaves at every tier's corners and between the front columns of the first tier (the light of each at night: lanternLights).
inline const std::vector<Vec3> &lanternPlaces() {
	static const std::vector<Vec3> places = [] {
		std::vector<Vec3> v;
		const struct {
			double hx, hz, y;
		} tiers[3] = {{21.2, 14.7, kUpperTerrace + kCol1 - 0.9}, {18.2, 12.1, kDeck2 + kCol2 - 0.8}, {15.7, 10.1, kDeck3 + kCol3 - 0.8}};
		for (const auto &t : tiers)
			for (const double sx : {-1.0, 1.0})
				for (const double sz : {-1.0, 1.0}) v.push_back({sx * t.hx, t.y, sz * t.hz});
		for (const double x : {-7.3, -2.44, 2.44, 7.3}) v.push_back({x, kUpperTerrace + kCol1 - 1.0, -14.2});
		return v;
	}();
	return places;
}
// A lantern hanging from `top`: a cord, a gold cap, the round red silk, a gold foot and a red tassel.
inline void addLantern(Parts &p, Vec3 top) {
	const Vec3 c = top - Vec3{0.0, 1.05, 0.0};
	append(p["red"], capsule(0.015, 0.55, 5, 1), translate(c + Vec3{0.0, 0.5, 0.0}));
	addCylinder(p["paint_gold"], c + Vec3{0.0, 0.40, 0.0}, 0.12, 0.26, 0.24, 16);
	append(p["silk"], lathe({{0.0, -0.42}, {0.26, -0.40}, {0.44, -0.25}, {0.50, 0.0}, {0.44, 0.25}, {0.26, 0.40}, {0.0, 0.42}}, 24), translate(c));
	addCylinder(p["paint_gold"], c - Vec3{0.0, 0.52, 0.0}, 0.12, 0.24, 0.26, 16);
	append(p["red"], capsule(0.06, 0.55, 8, 2), translate(c - Vec3{0.0, 1.1, 0.0}));
}
// Planar texture coordinates on the stonework, in blocks (1.4 m square on the paving, 1.6 by 0.55 m courses on the walls), for its two-tone pattern.
inline void stoneUvs(Mesh &m, double block, double course) {
	m.uvs.resize(m.verts.size());
	for (std::size_t i = 0; i < m.verts.size(); ++i) {
		const Vec3 v = m.verts[i], n = i < m.normals.size() ? m.normals[i] : Vec3{0.0, 1.0, 0.0};
		if (std::abs(n.y) > 0.7) m.uvs[i] = {v.x / block, v.z / block};
		else if (std::abs(n.x) > std::abs(n.z)) m.uvs[i] = {v.z / (block * 1.15), v.y / course};
		else m.uvs[i] = {v.x / (block * 1.15), v.y / course};
	}
}

// The stair from the hall of the second tier up to the walk round the third: along the inside of the hall's north wall, rising westwards, and out through an opening in the
// floor above. kStairX0 is its foot (at kDeck2), kStairX1 its head (at kDeck3); it is kStairZ0..kStairZ1 wide.
constexpr double kStairX0 = 9.5, kStairX1 = -7.5, kStairZ0 = 7.3, kStairZ1 = 8.3;
constexpr double kHoleX0 = -7.9, kHoleX1 = 2.4, kHoleZ0 = 7.2, kHoleZ1 = 8.4;
inline double stairHeightAt(double x) { return lerp(kDeck2, kDeck3, clamp01((kStairX0 - x) / (kStairX0 - kStairX1))); }
// A slab (a box) with a rectangular opening in it.
inline void addSlabWithHole(Mesh &m, Vec3 c, Vec3 size, double hx0, double hx1, double hz0, double hz1) {
	const double x0 = c.x - size.x / 2.0, x1 = c.x + size.x / 2.0, z0 = c.z - size.z / 2.0, z1 = c.z + size.z / 2.0;
	const auto piece = [&](double ax, double bx, double az, double bz) {
		if (bx - ax > 1e-6 && bz - az > 1e-6) addBox(m, {(ax + bx) / 2.0, c.y, (az + bz) / 2.0}, {bx - ax, size.y, bz - az});
	};
	piece(x0, x1, z0, hz0);
	piece(x0, x1, hz1, z1);
	piece(x0, hx0, hz0, hz1);
	piece(hx1, x1, hz0, hz1);
}
inline void addInnerStair(Parts &p) {
	const int n = static_cast<int>(std::lround((kDeck3 - kDeck2) / 0.2));
	for (int i = 0; i < n; ++i) {
		const double u0 = static_cast<double>(i) / n, u1 = (i + 1.0) / n;
		const double xa = lerp(kStairX0, kStairX1, u0), xb = lerp(kStairX0, kStairX1, u1), y = lerp(kDeck2, kDeck3, u1);
		addBox(p["wood"], {(xa + xb) / 2.0, y - 0.04, (kStairZ0 + kStairZ1) / 2.0}, {std::abs(xb - xa) + 0.02, 0.08, kStairZ1 - kStairZ0});
	}
	// the stringers and the handrail (red)
	for (const double z : {kStairZ0 - 0.04, kStairZ1 + 0.04}) {
		const Vec3 a{kStairX0, kDeck2 - 0.3, z}, b{kStairX1, kDeck3 - 0.3, z};
		append(p["red"], sdf::tubeMesh({a, b}, 0.07, 1.0, 6), identity());
	}
	append(p["red"], sdf::tubeMesh({{kStairX0, kDeck2 + 0.95, kStairZ0 - 0.04}, {kStairX1, kDeck3 + 0.95, kStairZ0 - 0.04}}, 0.04, 1.0, 6), identity());
	for (int i = 0; i <= 8; ++i) {
		const double x = lerp(kStairX0, kStairX1, i / 8.0);
		addBox(p["red"], {x, stairHeightAt(x) + 0.47, kStairZ0 - 0.04}, {0.06, 0.95, 0.06});
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The whole Pavilion
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline Parts pavilionParts() {
	Parts p;
	const double g = kGround;
	addRiverside(p);
	// ---- the terrace: two tiers of granite with a moulded lip, white balustrades, stairs to the river and the city
	// (each tier: a plinth at its foot, the wall, a darker band and a cornice under its paving, like the moulded base of a hall)
	const auto tier = [&](double hx, double hz, double y0, double y1) {
		addBox(p["stone"], {0.0, (y0 + y1) / 2.0 - 0.5, 0.0}, {hx * 2.0, y1 - y0 + 1.0, hz * 2.0});
		addBox(p["stone"], {0.0, y0 + 0.35, 0.0}, {hx * 2.0 + 0.8, 0.7, hz * 2.0 + 0.8});
		addBox(p["stone_dark"], {0.0, y1 - 0.75, 0.0}, {hx * 2.0 + 0.1, 0.5, hz * 2.0 + 0.1});
		addBox(p["stone"], {0.0, y1 - 0.25, 0.0}, {hx * 2.0 + 0.7, 0.5, hz * 2.0 + 0.7});
		addBox(p["paving"], {0.0, y1 + 0.01, 0.0}, {hx * 2.0 + 0.5, 0.04, hz * 2.0 + 0.5});
	};
	tier(40.0, 28.0, g, kLowerTerrace);
	tier(28.0, 19.0, kLowerTerrace, kUpperTerrace);
	addBalustrade(p, 40.2, 28.2, kLowerTerrace, {{0, 29.0, 51.4}, {2, 29.0, 51.4}});
	addBalustrade(p, 28.2, 19.2, kUpperTerrace, {{0, 20.0, 36.4}, {2, 20.0, 36.4}});
	addStairs(p, -10.0, 10.0, -28.3, kLowerTerrace, -44.0, g);
	addStairs(p, -10.0, 10.0, 28.3, kLowerTerrace, 44.0, g);
	addStairs(p, -7.5, 7.5, -19.3, kUpperTerrace, -28.3, kLowerTerrace);
	addStairs(p, -7.5, 7.5, 19.3, kUpperTerrace, 28.3, kLowerTerrace);
	// ---- the first tier: a colonnade round lattice walls, brackets, the first eave
	const double y1 = kUpperTerrace;
	addColumns(p, 19.5, 13.0, y1, kCol1, 4.875, 0.46);
	addColumns(p, 16.0, 9.5, y1, kCol1, 4.57, 0.40);
	addLatticeWalls(p, 16.0, 9.5, y1 + 0.3, y1 + kCol1 - 0.4, 4.57, {0, 2}, 1);
	addBrackets(p, 19.5, 13.0, y1 + kCol1, 1.0, 1.625);
	addSkirt(p, {18.4, 11.9, kDeck2 - 0.5, 23.6, 17.1, y1 + kCol1 + 1.3, 1.9, 1.7});
	addBox(p["wood"], {0.0, kDeck2 - 0.25, 0.0}, {37.0, 0.5, 24.0});   // the second tier's deck
	// ---- the second tier: the balcony walk, columns, the hall's lattice walls (open to the river), brackets, the second eave, the name board
	addRailing(p, kRail2X, kRail2Z, kDeck2, 1.1);
	addColumns(p, 16.6, 10.5, kDeck2, kCol2, 4.15, 0.38);
	addLatticeWalls(p, 14.5, 8.5, kDeck2 + 0.1, kDeck2 + kCol2 - 0.3, 4.14, {0}, 5);
	addBox(p["paint_blue"], {0.0, kDeck2 + kCol2 - 0.15, 0.0}, {29.4, 0.3, 17.4});
	addBrackets(p, 16.6, 10.5, kDeck2 + kCol2, 0.9, 1.38);
	addSkirt(p, {16.2, 10.1, kDeck3 - 0.45, 20.8, 14.5, kDeck2 + kCol2 + 1.2, 1.75, 1.5});
	addSlabWithHole(p["wood"], {0.0, kDeck3 - 0.22, 0.0}, {32.6, 0.44, 20.6}, kHoleX0, kHoleX1, kHoleZ0, kHoleZ1);
	addInnerStair(p);
	// ---- the third tier: the high terrace (the balcony walk Wang Bo steps out on), columns, walls open to the river, brackets, the crown
	addRailing(p, kRail3X, kRail3Z, kDeck3, 1.1);
	addColumns(p, 14.2, 8.6, kDeck3, kCol3, 3.55, 0.36);
	addLatticeWalls(p, 12.0, 7.0, kDeck3 + 0.1, kDeck3 + kCol3 - 0.3, 4.0, {0}, 3);
	addBox(p["paint_blue"], {0.0, kDeck3 + kCol3 - 0.15, 0.0}, {24.4, 0.3, 14.4});
	addBrackets(p, 14.2, 8.6, kDeck3 + kCol3, 0.9, 1.42);
	// the crown: a hip-and-gable roof, a skirt up to the gables and two concave slopes to the main ridge
	const double crownBase = kDeck3 + kCol3 + 4.2, ridgeY = kDeck3 + kCol3 + 11.0;
	addSkirt(p, {12.6, 7.1, crownBase, 18.8, 13.0, kDeck3 + kCol3 + 1.2, 2.2, 1.9});
	{
		Mesh slope;
		const int nx = 260, nz = 24;
		for (int side = 0; side < 2; ++side) {
			const double sz = side == 0 ? -1.0 : 1.0;
			const int base = static_cast<int>(slope.verts.size());
			for (int j = 0; j <= nz; ++j)
				for (int i = 0; i <= nx; ++i) {
					const double u = static_cast<double>(i) / nx, r = static_cast<double>(j) / nz;
					const double x = lerp(-13.3, 13.3, u), z = sz * lerp(0.15, 7.6, r);
					double y = crownBase - 0.3 + (ridgeY - crownBase + 0.3) * std::pow(1.0 - r, 1.5);
					y += 0.07 * (0.5 + 0.5 * std::cos(2.0 * kPi * (x + 13.3) / 0.38)) * (0.4 + 0.6 * r);
					slope.verts.push_back({x, y, z});
				}
			for (int j = 0; j < nz; ++j)
				for (int i = 0; i < nx; ++i) {
					const int q = base + j * (nx + 1) + i;
					slope.tris.push_back({q, q + 1, q + nx + 1});
					slope.tris.push_back({q + 1, q + nx + 2, q + nx + 1});
				}
		}
		for (auto &t : slope.tris) {
			const Vec3 n = cross(slope.verts[static_cast<std::size_t>(t[1])] - slope.verts[static_cast<std::size_t>(t[0])], slope.verts[static_cast<std::size_t>(t[2])] - slope.verts[static_cast<std::size_t>(t[0])]);
			if (n.y < 0.0) std::swap(t[1], t[2]);
		}
		sdf::computeNormals(slope);
		append(p["tile"], slope, identity());
		// the gables: red walls with a gold sunburst, under the slopes' ends
		for (const double x : {-12.4, 12.4}) {
			Mesh gable;
			gable.verts = {{x, crownBase - 0.4, -7.0}, {x, crownBase - 0.4, 7.0}, {x, ridgeY - 0.3, 0.0}};
			gable.normals.assign(3, Vec3{x > 0 ? 1.0 : -1.0, 0.0, 0.0});
			gable.tris = {{0, 1, 2}};
			if (x > 0) gable.tris = {{0, 2, 1}};
			append(p["red"], gable, identity());
			append(p["paint_gold"], uvSphere(1.1, 16, 8), mul(translate(x + (x > 0 ? 0.05 : -0.05), crownBase + 1.8, 0.0), scale(0.15, 1.0, 1.0)));
			// the bargeboards along the gable's edges
			for (const double sz : {-1.0, 1.0}) {
				std::vector<Vec3> edgePts;
				for (int j = 0; j <= 12; ++j) {
					const double r = j / 12.0;
					edgePts.push_back({x * 1.06, crownBase - 0.3 + (ridgeY - crownBase + 0.3) * std::pow(1.0 - r, 1.5) - 0.1, sz * lerp(0.15, 7.6, r)});
				}
				append(p["ridge"], sdf::tubeMesh(edgePts, 0.2, 1.0, 8), identity());
			}
		}
		// the main ridge, and the dragon-fish (chiwen) curling up at its ends
		addBox(p["ridge"], {0.0, ridgeY + 0.35, 0.0}, {25.6, 0.9, 0.7});
		addBox(p["paint_gold"], {0.0, ridgeY + 0.2, 0.0}, {25.7, 0.12, 0.74});
		for (const double sx : {-1.0, 1.0}) {
			std::vector<Vec3> body;
			for (int j = 0; j <= 14; ++j) {
				const double a = j / 14.0 * 3.4;
				body.push_back({sx * (12.6 + 0.9 * std::sin(a) - 0.4 * a), ridgeY + 0.6 + 1.0 * a, 0.0});
			}
			append(p["gold"], sdf::tubeMesh(body, 0.42, 0.45, 12), identity());
			append(p["gold"], uvSphere(0.5, 12, 8), translate(sx * 12.9, ridgeY + 1.0, 0.0));
		}
		// a gilded jewel in the middle of the ridge
		append(p["gold"], lathe({{0.0, 0.0}, {0.55, 0.0}, {0.75, 0.5}, {0.4, 1.3}, {0.12, 2.0}, {0.0, 2.3}}, 16), translate(0.0, ridgeY + 0.8, 0.0));
	}
	// ---- inside: floors, painted ceilings, inner columns (the halls the banquet and the stair are in)
	addBox(p["floor"], {0.0, kDeck2 + 0.005, 0.0}, {28.8, 0.02, 16.8});
	addBox(p["floor"], {0.0, kDeck3 + 0.005, 0.0}, {23.8, 0.02, 13.8});
	for (const double y : {kDeck2 + kCol2 - 0.3, kDeck3 + kCol3 - 0.3}) {
		const double hx = y < kDeck3 ? 14.4 : 11.9, hz = y < kDeck3 ? 8.4 : 6.9;
		const bool second = y < kDeck3;
		if (second) addSlabWithHole(p["ceiling"], {0.0, y + 0.1, 0.0}, {hx * 2.0, 0.2, hz * 2.0}, kHoleX0, kHoleX1, kHoleZ0, kHoleZ1);
		else addBox(p["ceiling"], {0.0, y + 0.1, 0.0}, {hx * 2.0, 0.2, hz * 2.0});
		for (double x = -hx; x <= hx + 1e-6; x += hx / 6.0) {
			if (second && x > kHoleX0 && x < kHoleX1) {   // (the beams stop short of the stairwell)
				addBox(p["paint_gold"], {x, y - 0.08, (-hz + kHoleZ0) / 2.0}, {0.14, 0.18, kHoleZ0 + hz});
				continue;
			}
			addBox(p["paint_gold"], {x, y - 0.08, 0.0}, {0.14, 0.18, hz * 2.0});
		}
		for (double z = -hz; z <= hz + 1e-6; z += hz / 4.0) addBox(p["paint_blue"], {0.0, y - 0.08, z}, {hx * 2.0, 0.17, 0.14});
	}
	for (int i = -2; i <= 2; ++i)
		for (const double z : {-3.8, 3.8}) {
			addCylinder(p["red"], {i * 5.4, kDeck2, z}, kCol2 - 0.3, 0.32, 0.3);
			if (std::abs(i) <= 1) addCylinder(p["red"], {i * 5.0, kDeck3, z * 0.85}, kCol3 - 0.3, 0.3, 0.28);
		}
	for (const Vec3 &l : lanternPlaces()) addLantern(p, l);
	stoneUvs(p["stone"], 1.4, 0.55);
	stoneUvs(p["stone_dark"], 1.4, 0.55);
	stoneUvs(p["paving"], 1.4, 0.55);
	return p;
}

// The materials. `night` lights the paper of the lattice from within (a hall full of lamps).
inline std::string pavilionMaterial(const std::string &key, double night) {
	if (key == "stone") return "Texture \"stone_checks\" \"spectrum\" \"checkerboard\" \"rgb tex1\" [ 0.60 0.58 0.54 ] \"rgb tex2\" [ 0.52 0.50 0.46 ]\n  Material \"coateddiffuse\" \"texture reflectance\" [ \"stone_checks\" ] \"float roughness\" [ 0.45 ]";
	if (key == "silk") return "Material \"diffusetransmission\" \"rgb reflectance\" [ 0.62 0.06 0.04 ] \"rgb transmittance\" [ 0.75 0.12 0.05 ]";
	if (key == "stone_dark") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.36 0.34 0.31 ] \"float roughness\" [ 0.45 ]";
	if (key == "paving") return "Texture \"paving_checks\" \"spectrum\" \"checkerboard\" \"rgb tex1\" [ 0.52 0.50 0.47 ] \"rgb tex2\" [ 0.45 0.43 0.40 ]\n  Material \"coateddiffuse\" \"texture reflectance\" [ \"paving_checks\" ] \"float roughness\" [ 0.25 ]";
	if (key == "marble") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.84 0.83 0.79 ] \"float roughness\" [ 0.22 ]";
	if (key == "red") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.62 0.055 0.030 ] \"float roughness\" [ 0.16 ]";
	if (key == "tile") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.07 0.40 0.27 ] \"float roughness\" [ 0.06 ]";
	if (key == "ridge") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.05 0.22 0.15 ] \"float roughness\" [ 0.08 ]";
	if (key == "soffit") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.06 0.22 0.20 ] \"float roughness\" [ 0.35 ]";
	if (key == "fascia") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.55 0.07 0.04 ] \"float roughness\" [ 0.2 ]";
	if (key == "rafter") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.07 0.20 0.34 ] \"float roughness\" [ 0.3 ]";
	if (key == "paint_blue") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.035 0.13 0.42 ] \"float roughness\" [ 0.25 ]";
	if (key == "paint_green") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.04 0.34 0.20 ] \"float roughness\" [ 0.25 ]";
	if (key == "paint_gold" || key == "gold") return "Material \"conductor\" \"spectrum eta\" [ \"metal-Au-eta\" ] \"spectrum k\" [ \"metal-Au-k\" ] \"float roughness\" [ 0.18 ]";
	if (key == "lattice") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.40 0.05 0.03 ] \"float roughness\" [ 0.2 ]";
	if (key == "panel") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.30 0.05 0.03 ] \"float roughness\" [ 0.2 ]";
	if (key == "wood") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.30 0.14 0.06 ] \"float roughness\" [ 0.3 ]";
	if (key == "floor") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.26 0.11 0.05 ] \"float roughness\" [ 0.07 ]";
	if (key == "ceiling") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.06 0.26 0.24 ] \"float roughness\" [ 0.4 ]";
	if (key == "paper") {
		(void)night;
		return "Material \"diffusetransmission\" \"rgb reflectance\" [ 0.80 0.72 0.58 ] \"rgb transmittance\" [ 0.60 0.48 0.30 ]";
	}
	return diffuseMaterial({0.5, 0.5, 0.5});
}

// The Pavilion as scene text (its PLY files, made the first time).
inline std::string pavilionText(double night = 0.0) {
	static const std::vector<std::pair<std::string, std::string>> files = [] {
		std::vector<std::pair<std::string, std::string>> f;
		static Parts parts;
		static bool made = false;
		static const char *keys[] = {"stone", "stone_dark", "paving", "marble", "red", "tile", "ridge", "soffit", "fascia", "rafter", "paint_blue", "paint_green", "paint_gold", "gold", "lattice", "panel", "wood",
									 "floor", "ceiling", "paper", "silk"};
		for (const char *k : keys)
			f.push_back({k, film_cache::ply(kFilmKey, versioned(std::string("pavilion_") + k, kPavilionVersion), [&]() {
							 if (!made) {
								 parts = pavilionParts();
								 made = true;
							 }
							 return parts[k];
						 })});
		return f;
	}();
	std::string out;
	for (const auto &[key, file] : files) out += attr(pavilionMaterial(key, night), film_cache::plyShape(file));
	// the name board under the second eave, facing the river
	if (const std::string plaque = film_cache::asset("tengwang/plaque.jpg"); !plaque.empty()) {
		Mesh board;
		const double y = kDeck2 + kCol2 + 0.75, z = -14.8;
		board.verts = {{-3.6, y - 1.2, z}, {3.6, y - 1.2, z}, {3.6, y + 1.2, z}, {-3.6, y + 1.2, z}};
		board.normals.assign(4, Vec3{0.0, 0.0, -1.0});
		board.uvs = {{1.0, 0.0}, {0.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}};   // (seen from the river, looking along +z, +x is on the left)
		board.tris = {{0, 2, 1}, {0, 3, 2}};
		out += "AttributeBegin\n  Texture \"plaque\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + plaque + "\" ]\n  Material \"coateddiffuse\" \"texture reflectance\" [ \"plaque\" ] \"float roughness\" [ 0.2 ]\n" +
			   emitMesh(board, identity()) + "AttributeEnd\n";
		Mesh frame = box(7.6, 2.8, 0.25);
		out += attr(pavilionMaterial("gold", 0.0), emitMesh(frame, translate(0.0, y, z + 0.14)));
	}
	return out;
}

}  // namespace tengwang
}  // namespace cartoon
