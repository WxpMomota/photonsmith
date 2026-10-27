#pragma once
// film_paths.h -- a walk that finds its own way (film_file.h `walk NAME T SPEED to X Z`): a map of the floor from the set itself - everything of the scene between ankle and head
// height, seen from above - grown by the walker's size, and a shortest way across it (A* on the map's cells, then straightened where nothing is in the way), which the walk then
// follows (its corners turned gradually, rig::PathWalk). Characters standing about are in the way too.
//
// Coordinates are pbrt's (x z on the ground), as a film file's.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "../pbrt_flatten.h"
#include "../ply_mesh.h"
#include "cartoon_lib.h"

namespace cartoon {
namespace film_paths {

// The map: a square of cells over the floor, each blocked or not.
struct Grid {
	double x0 = 0.0, z0 = 0.0, cell = 0.08;
	int nx = 0, nz = 0;
	std::vector<std::uint8_t> blocked;
	bool inside(int i, int k) const { return i >= 0 && k >= 0 && i < nx && k < nz; }
	bool at(int i, int k) const { return !inside(i, k) || blocked[static_cast<std::size_t>(k) * nx + i] != 0; }
	void set(int i, int k) {
		if (inside(i, k)) blocked[static_cast<std::size_t>(k) * nx + i] = 1;
	}
	int col(double x) const { return static_cast<int>(std::floor((x - x0) / cell)); }
	int row(double z) const { return static_cast<int>(std::floor((z - z0) / cell)); }
	double cx(int i) const { return x0 + (i + 0.5) * cell; }
	double cz(int k) const { return z0 + (k + 0.5) * cell; }
};

// A map of `half` metres either way round (cx, cz), `cell` metres a cell.
inline Grid gridAround(double cx, double cz, double half, double cell) {
	Grid g;
	g.cell = cell;
	g.nx = g.nz = std::max(4, static_cast<int>(std::ceil(2.0 * half / cell)));
	g.x0 = cx - 0.5 * g.nx * cell;
	g.z0 = cz - 0.5 * g.nz * cell;
	g.blocked.assign(static_cast<std::size_t>(g.nx) * g.nz, 0);
	return g;
}

// A round thing in the way (a disc of radius r).
inline void addDisc(Grid &g, double x, double z, double r) {
	for (int k = g.row(z - r); k <= g.row(z + r); ++k)
		for (int i = g.col(x - r); i <= g.col(x + r); ++i) {
			const double dx = std::max(0.0, std::abs(g.cx(i) - x) - 0.5 * g.cell), dz = std::max(0.0, std::abs(g.cz(k) - z) - 0.5 * g.cell);
			if (dx * dx + dz * dz <= r * r) g.set(i, k);
		}
}

namespace detail {
struct P3 {
	double x, y, z;
};
// A polygon cut to the slab low <= y <= high (Sutherland-Hodgman, two planes).
inline std::vector<P3> clipToSlab(std::vector<P3> poly, double low, double high) {
	for (int side = 0; side < 2 && !poly.empty(); ++side) {
		std::vector<P3> out;
		const auto keep = [&](const P3 &p) { return side == 0 ? p.y >= low : p.y <= high; };
		const double plane = side == 0 ? low : high;
		for (std::size_t i = 0; i < poly.size(); ++i) {
			const P3 &a = poly[i], &b = poly[(i + 1) % poly.size()];
			if (keep(a)) out.push_back(a);
			if (keep(a) != keep(b)) {
				const double u = (plane - a.y) / (b.y - a.y);
				out.push_back({a.x + (b.x - a.x) * u, plane, a.z + (b.z - a.z) * u});
			}
		}
		poly = std::move(out);
	}
	return poly;
}
// The cells a polygon on the floor covers: those its edges pass through and those whose middles are inside it (a wall seen from above is a line of cells).
inline void fillPolygon(Grid &g, const std::vector<P3> &poly) {
	if (poly.empty()) return;
	double xmin = 1e30, xmax = -1e30, zmin = 1e30, zmax = -1e30;
	for (const P3 &p : poly) {
		xmin = std::min(xmin, p.x), xmax = std::max(xmax, p.x);
		zmin = std::min(zmin, p.z), zmax = std::max(zmax, p.z);
	}
	if (xmax < g.x0 || zmax < g.z0 || xmin > g.x0 + g.nx * g.cell || zmin > g.z0 + g.nz * g.cell) return;
	for (std::size_t e = 0; e < poly.size(); ++e) {   // the edges, sampled finer than a cell
		const P3 &a = poly[e], &b = poly[(e + 1) % poly.size()];
		const double len = std::hypot(b.x - a.x, b.z - a.z);
		const int n = std::max(1, static_cast<int>(std::ceil(len / (0.4 * g.cell))));
		for (int s = 0; s <= n; ++s) {
			const double u = static_cast<double>(s) / n;
			g.set(g.col(a.x + (b.x - a.x) * u), g.row(a.z + (b.z - a.z) * u));
		}
	}
	for (int k = std::max(0, g.row(zmin)); k <= std::min(g.nz - 1, g.row(zmax)); ++k)   // the inside (even-odd)
		for (int i = std::max(0, g.col(xmin)); i <= std::min(g.nx - 1, g.col(xmax)); ++i) {
			const double x = g.cx(i), z = g.cz(k);
			bool in = false;
			for (std::size_t e = 0, f = poly.size() - 1; e < poly.size(); f = e++)
				if ((poly[e].z > z) != (poly[f].z > z) && x < (poly[f].x - poly[e].x) * (z - poly[e].z) / (poly[f].z - poly[e].z) + poly[e].x) in = !in;
			if (in) g.set(i, k);
		}
}
inline P3 applyXform(const double m[16], double x, double y, double z) {
	return {m[0] * x + m[1] * y + m[2] * z + m[3], m[4] * x + m[5] * y + m[6] * z + m[7], m[8] * x + m[9] * y + m[10] * z + m[11]};
}
}  // namespace detail

// What of a flattened scene stands between `low` and `high` (metres above the ground at height `ground`), on the map: its triangles cut to that slab, its spheres, its disks and
// cylinders (as polygons).
inline void addScene(Grid &g, const pbrt_flatten::FlatScene &s, double ground, double low, double high) {
	using detail::P3;
	const double y0 = ground + low, y1 = ground + high;
	for (const pbrt_flatten::Triangle &t : s.triangles)
		detail::fillPolygon(g, detail::clipToSlab({{t.v[0], t.v[1], t.v[2]}, {t.v[3], t.v[4], t.v[5]}, {t.v[6], t.v[7], t.v[8]}}, y0, y1));
	for (const pbrt_flatten::Sphere &sp : s.spheres) {
		const double d = sp.center[1] < y0 ? y0 - sp.center[1] : sp.center[1] > y1 ? sp.center[1] - y1 : 0.0;
		if (d < sp.radius) addDisc(g, sp.center[0], sp.center[2], std::sqrt(sp.radius * sp.radius - d * d));
	}
	for (const pbrt_flatten::Instance &in : s.instances) {   // (placed objects: a soft one - grass, a fern, a bush - is walked through)
		if (in.group < 0 || static_cast<std::size_t>(in.group) >= s.groups.size()) continue;
		const pbrt_flatten::InstanceGroup &g2 = s.groups[static_cast<std::size_t>(in.group)];
		if (g2.name.find("_soft_") != std::string::npos) continue;
		for (const pbrt_flatten::Triangle &t : g2.triangles) {
			const P3 a = detail::applyXform(in.xform, t.v[0], t.v[1], t.v[2]), b = detail::applyXform(in.xform, t.v[3], t.v[4], t.v[5]), c = detail::applyXform(in.xform, t.v[6], t.v[7], t.v[8]);
			detail::fillPolygon(g, detail::clipToSlab({a, b, c}, y0, y1));
		}
	}
	constexpr int kSides = 16;
	for (const pbrt_flatten::Disk &dk : s.disks)
		for (int k = 0; k < kSides; ++k) {
			const double a0 = 2.0 * kPi * k / kSides, a1 = 2.0 * kPi * (k + 1) / kSides;
			detail::fillPolygon(g, detail::clipToSlab({detail::applyXform(dk.xform, 0.0, 0.0, dk.height), detail::applyXform(dk.xform, dk.radius * std::cos(a0), dk.radius * std::sin(a0), dk.height),
													   detail::applyXform(dk.xform, dk.radius * std::cos(a1), dk.radius * std::sin(a1), dk.height)},
													  y0, y1));
		}
	for (const pbrt_flatten::Cylinder &cy : s.cylinders)
		for (int k = 0; k < kSides; ++k) {
			const double a0 = 2.0 * kPi * k / kSides, a1 = 2.0 * kPi * (k + 1) / kSides;
			const double c0 = cy.radius * std::cos(a0), s0 = cy.radius * std::sin(a0), c1 = cy.radius * std::cos(a1), s1 = cy.radius * std::sin(a1);
			detail::fillPolygon(g, detail::clipToSlab({detail::applyXform(cy.xform, c0, s0, cy.zMin), detail::applyXform(cy.xform, c1, s1, cy.zMin), detail::applyXform(cy.xform, c1, s1, cy.zMax),
													   detail::applyXform(cy.xform, c0, s0, cy.zMax)},
													  y0, y1));
		}
}

// The map grown by `radius` (the walker's size): a cell is blocked when anything in the way is closer than that to its middle.
inline Grid grown(const Grid &g, double radius) {
	Grid out = g;
	const int r = static_cast<int>(std::ceil(radius / g.cell));
	std::vector<std::pair<int, int>> disc;
	for (int dk = -r; dk <= r; ++dk)
		for (int di = -r; di <= r; ++di)
			if ((di * di + dk * dk) * g.cell * g.cell <= radius * radius) disc.push_back({di, dk});
	for (int k = 0; k < g.nz; ++k)
		for (int i = 0; i < g.nx; ++i)
			if (g.blocked[static_cast<std::size_t>(k) * g.nx + i])
				for (const auto &[di, dk] : disc) out.set(i + di, k + dk);
	return out;
}

// Whether the straight line between two points crosses no blocked cell.
inline bool clear(const Grid &g, double ax, double az, double bx, double bz) {
	const double len = std::hypot(bx - ax, bz - az);
	const int n = std::max(1, static_cast<int>(std::ceil(len / (0.25 * g.cell))));
	for (int s = 0; s <= n; ++s) {
		const double u = static_cast<double>(s) / n;
		if (g.at(g.col(ax + (bx - ax) * u), g.row(az + (bz - az) * u))) return false;
	}
	return true;
}

// The shortest way across the map from (ax, az) to (bx, bz): its corners (the start not included, the end included), or "" in `why` and false when there is none. The walker's
// own cell and a little round it are free (it may stand close to a wall), its goal must not be in the way.
inline bool findWay(Grid g, double ax, double az, double bx, double bz, std::vector<std::pair<double, double>> &way, std::string &why) {
	way.clear();
	const int si = g.col(ax), sk = g.row(az), gi = g.col(bx), gk = g.row(bz);
	if (!g.inside(gi, gk) || !g.inside(si, sk)) {
		why = "it is off the map (too far from where it starts)";
		return false;
	}
	if (g.at(gi, gk)) {
		why = "something is in the way there";
		return false;
	}
	for (int dk = -2; dk <= 2; ++dk)
		for (int di = -2; di <= 2; ++di)
			if (g.inside(si + di, sk + dk)) g.blocked[static_cast<std::size_t>(sk + dk) * g.nx + si + di] = 0;
	if (clear(g, ax, az, bx, bz)) {
		way.push_back({bx, bz});
		return true;
	}
	// A* over the cells, 8 neighbours
	const std::size_t n = static_cast<std::size_t>(g.nx) * g.nz;
	std::vector<double> cost(n, 1e30);
	std::vector<int> from(n, -1);
	using Item = std::pair<double, int>;
	std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
	const auto id = [&](int i, int k) { return k * g.nx + i; };
	const auto h = [&](int i, int k) { return std::hypot(static_cast<double>(i - gi), static_cast<double>(k - gk)); };
	cost[static_cast<std::size_t>(id(si, sk))] = 0.0;
	open.push({h(si, sk), id(si, sk)});
	bool found = false;
	while (!open.empty()) {
		const auto [f, c] = open.top();
		open.pop();
		const int i = c % g.nx, k = c / g.nx;
		if (i == gi && k == gk) {
			found = true;
			break;
		}
		if (f - h(i, k) > cost[static_cast<std::size_t>(c)] + 1e-9) continue;
		for (int dk = -1; dk <= 1; ++dk)
			for (int di = -1; di <= 1; ++di) {
				if (!di && !dk) continue;
				const int ni = i + di, nk = k + dk;
				if (g.at(ni, nk) || (di && dk && (g.at(i + di, k) || g.at(i, k + dk)))) continue;   // (no cutting a corner between two blocked cells)
				const double c2 = cost[static_cast<std::size_t>(c)] + ((di && dk) ? 1.41421356 : 1.0);
				const std::size_t m = static_cast<std::size_t>(id(ni, nk));
				if (c2 < cost[m]) {
					cost[m] = c2;
					from[m] = c;
					open.push({c2 + h(ni, nk), id(ni, nk)});
				}
			}
	}
	if (!found) {
		why = "there is no way round what is in the way";
		return false;
	}
	std::vector<std::pair<double, double>> cells;
	for (int c = id(gi, gk); c != id(si, sk); c = from[static_cast<std::size_t>(c)]) cells.push_back({g.cx(c % g.nx), g.cz(c / g.nx)});
	std::reverse(cells.begin(), cells.end());
	cells.back() = {bx, bz};
	// straightened: from each corner, on to the farthest cell it can see
	double px = ax, pz = az;
	std::size_t at = 0;
	while (at < cells.size()) {
		std::size_t farthest = at;
		for (std::size_t j = cells.size(); j-- > at;)
			if (clear(g, px, pz, cells[j].first, cells[j].second)) {
				farthest = j;
				break;
			}
		way.push_back(cells[farthest]);
		px = cells[farthest].first;
		pz = cells[farthest].second;
		at = farthest + 1;
	}
	return true;
}

// ---- the floor: uneven ground, steps, platforms ----
// The set as columns of what is in each cell of the map: the heights each thing spans there (a floor's top is where one can stand, a wall spans its height). A walker's floor is
// found from where it stands, a cell at a time: the highest top within a step of the floor beside it, with room above it for the walker; a drop or a rise of more than a step is
// a cliff. Only a surface no steeper than 25 degrees is a floor (a boulder's flank, a roof's pitch is not: it stays in the way). Heights under `flat` from the set's own ground
// are that ground (a path's slabs, a rug), so a level set stays exactly level.
struct Span {
	float lo = 0.0f, hi = 0.0f;
	bool floor = false;   // its top can be stood on (it is level enough)
};
constexpr double kMaxSlope = 0.47;   // tan(25 degrees)
struct Terrain {
	Grid grid;   // (the cells; its `blocked` is not used)
	double base = 0.0, flat = 0.05;
	std::vector<std::vector<Span>> spans;   // per cell: each thing in it, lowest to highest
	const std::vector<Span> &at(int i, int k) const {
		static const std::vector<Span> none;
		return grid.inside(i, k) ? spans[static_cast<std::size_t>(k) * grid.nx + i] : none;
	}
	// The floor under (x, z) for a walker near height `around`: the highest top no more than `step` above it (and no more than 3 m below), or the ground.
	double floorAt(double x, double z, double around, double step) const {
		double best = base;
		bool found = false;
		for (const Span &s : at(grid.col(x), grid.row(z)))
			if (s.floor && s.hi <= around + step && s.hi >= around - 3.0 && (!found || s.hi > best)) best = s.hi, found = true;
		if (!found) best = base;
		return std::abs(best - base) < flat ? base : best;
	}
	// Whether a walker on a floor at height `floor` in a cell has room: nothing in it between `low` and `high` above the floor (its own floor's top is not in the way).
	bool roomy(int i, int k, double floor, double low, double high) const {
		for (const Span &s : at(i, k))
			if (s.hi > floor + low && s.lo < floor + high) return false;
		return true;
	}
};
namespace detail {
inline void addSpan(Terrain &t, int i, int k, double lo, double hi, bool floor) {
	if (!t.grid.inside(i, k) || hi < t.base - 3.0 || lo > t.base + 8.0) return;
	t.spans[static_cast<std::size_t>(k) * t.grid.nx + i].push_back({static_cast<float>(lo), static_cast<float>(hi), floor});
}
// A triangle's spans: in each cell it covers, the heights it takes there (a slope's, from its plane round the cell's middle; a wall's, its whole height).
inline void addTriangle(Terrain &t, P3 a, P3 b, P3 c) {
	const Grid &g = t.grid;
	const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
	const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx, nl = std::sqrt(nx * nx + ny * ny + nz * nz);
	if (nl < 1e-12) return;
	const double ylo = std::min({a.y, b.y, c.y}), yhi = std::max({a.y, b.y, c.y});
	const bool steep = std::abs(ny) / nl < 0.2;
	const bool level = !steep && std::hypot(nx, nz) / std::abs(ny) <= kMaxSlope;   // (one can stand on it)
	const double slope = steep ? 0.0 : std::hypot(nx, nz) / std::abs(ny) * 0.5 * g.cell * 1.42;
	std::vector<std::pair<int, int>> cells;
	{
		const double xmin = std::min({a.x, b.x, c.x}), xmax = std::max({a.x, b.x, c.x}), zmin = std::min({a.z, b.z, c.z}), zmax = std::max({a.z, b.z, c.z});
		if (xmax < g.x0 || zmax < g.z0 || xmin > g.x0 + g.nx * g.cell || zmin > g.z0 + g.nz * g.cell) return;
		// (one scratch grid a thread, its marks cleared again over the triangle's box - fillPolygon marks nothing outside it: a fresh copy of the whole map for each
		// triangle made a scene of a million triangles on a fine map take hours)
		thread_local Grid mark;
		if (mark.nx != g.nx || mark.nz != g.nz || mark.x0 != g.x0 || mark.z0 != g.z0 || mark.cell != g.cell) {
			mark = g;
			mark.blocked.assign(mark.blocked.size(), 0);
		}
		fillPolygon(mark, {a, b, c});
		for (int k = std::max(0, g.row(zmin)); k <= std::min(g.nz - 1, g.row(zmax)); ++k)
			for (int i = std::max(0, g.col(xmin)); i <= std::min(g.nx - 1, g.col(xmax)); ++i) {
				std::uint8_t &m = mark.blocked[static_cast<std::size_t>(k) * g.nx + i];
				if (m) cells.push_back({i, k}), m = 0;
			}
	}
	for (const auto &[i, k] : cells) {
		if (steep) {
			addSpan(t, i, k, ylo, yhi, false);
			continue;
		}
		const double y = a.y - (nx * (g.cx(i) - a.x) + nz * (g.cz(k) - a.z)) / ny;   // (the plane at the cell's middle)
		addSpan(t, i, k, std::max(ylo, y - slope), std::min(yhi, y + slope), level);   // (either way up: a pbrt file may wind it either way)
	}
}
}  // namespace detail
// The terrain of a flattened scene over a map `half` metres either way round (cx, cz), `cell` a cell, its ground at `base` (soft placed things - grass - are not in it).
inline Terrain terrainOf(const pbrt_flatten::FlatScene &s, double cx, double cz, double half, double cell, double base) {
	using detail::P3;
	Terrain t;
	t.grid = gridAround(cx, cz, half, cell);
	t.base = base;
	t.spans.assign(t.grid.blocked.size(), {});
	for (const pbrt_flatten::Triangle &tr : s.triangles) detail::addTriangle(t, {tr.v[0], tr.v[1], tr.v[2]}, {tr.v[3], tr.v[4], tr.v[5]}, {tr.v[6], tr.v[7], tr.v[8]});
	for (const pbrt_flatten::Sphere &sp : s.spheres)
		for (int k = t.grid.row(sp.center[2] - sp.radius); k <= t.grid.row(sp.center[2] + sp.radius); ++k)
			for (int i = t.grid.col(sp.center[0] - sp.radius); i <= t.grid.col(sp.center[0] + sp.radius); ++i) {
				const double d2 = (t.grid.cx(i) - sp.center[0]) * (t.grid.cx(i) - sp.center[0]) + (t.grid.cz(k) - sp.center[2]) * (t.grid.cz(k) - sp.center[2]);
				if (d2 < sp.radius * sp.radius) {
					const double h = std::sqrt(sp.radius * sp.radius - d2);   // (its top is level enough to stand on near the crown)
					detail::addSpan(t, i, k, sp.center[1] - h, sp.center[1] + h, std::sqrt(d2) <= kMaxSlope * h);
				}
			}
	for (const pbrt_flatten::Instance &in : s.instances) {
		if (in.group < 0 || static_cast<std::size_t>(in.group) >= s.groups.size()) continue;
		const pbrt_flatten::InstanceGroup &g2 = s.groups[static_cast<std::size_t>(in.group)];
		if (g2.name.find("_soft_") != std::string::npos) continue;
		for (const pbrt_flatten::Triangle &tr : g2.triangles)
			detail::addTriangle(t, detail::applyXform(in.xform, tr.v[0], tr.v[1], tr.v[2]), detail::applyXform(in.xform, tr.v[3], tr.v[4], tr.v[5]), detail::applyXform(in.xform, tr.v[6], tr.v[7], tr.v[8]));
	}
	return t;
}
// Where a walker standing at (x, y, z) can go: the floor of every cell it can reach a cell at a time (a step up or down at most `step`, room above for `low` .. `high`), and a
// map whose blocked cells are those it cannot reach or that stand at a cliff's edge. `floors` gets each cell's floor (NaN where it cannot go).
inline Grid reachable(const Terrain &t, double x, double y, double z, double step, double low, double high, std::vector<double> &floors) {
	const Grid &g = t.grid;
	Grid out = g;
	out.blocked.assign(g.blocked.size(), 1);
	floors.assign(g.blocked.size(), std::nan(""));
	const int si = g.col(x), sk = g.row(z);
	if (!g.inside(si, sk)) return out;
	std::vector<std::pair<int, int>> queue{{si, sk}};
	floors[static_cast<std::size_t>(sk) * g.nx + si] = t.floorAt(x, z, y, step);
	for (std::size_t q = 0; q < queue.size(); ++q) {
		const auto [i, k] = queue[q];
		const double f = floors[static_cast<std::size_t>(k) * g.nx + i];
		static const int di[4] = {1, -1, 0, 0}, dk[4] = {0, 0, 1, -1};
		for (int d = 0; d < 4; ++d) {
			const int ni = i + di[d], nk = k + dk[d];
			if (!g.inside(ni, nk) || !std::isnan(floors[static_cast<std::size_t>(nk) * g.nx + ni])) continue;
			const double f2 = t.floorAt(g.cx(ni), g.cz(nk), f, step);
			if (std::abs(f2 - f) > step || !t.roomy(ni, nk, f2, low, high)) continue;
			floors[static_cast<std::size_t>(nk) * g.nx + ni] = f2;
			queue.push_back({ni, nk});
		}
	}
	for (int k = 0; k < g.nz; ++k)
		for (int i = 0; i < g.nx; ++i) {
			const double f = floors[static_cast<std::size_t>(k) * g.nx + i];
			if (std::isnan(f)) continue;
			bool cliff = false;   // (beside a cell whose floor is more than a step away: the edge of a drop)
			static const int di[4] = {1, -1, 0, 0}, dk[4] = {0, 0, 1, -1};
			for (int d = 0; d < 4 && !cliff; ++d) {
				const int ni = i + di[d], nk = k + dk[d];
				if (!g.inside(ni, nk)) continue;
				const double f2 = floors[static_cast<std::size_t>(nk) * g.nx + ni];
				cliff = !std::isnan(f2) && std::abs(f2 - f) > step;
			}
			if (!cliff) out.blocked[static_cast<std::size_t>(k) * g.nx + i] = 0;
		}
	return out;
}
// The floor under a point from a reachable map's floors: the mean of the reached cells round it (a smooth slope, not steps of a cell), or NaN.
inline double floorFrom(const Grid &g, const std::vector<double> &floors, double x, double z) {
	const int i0 = g.col(x), k0 = g.row(z);
	double sum = 0.0;
	int n = 0;
	for (int k = k0 - 1; k <= k0 + 1; ++k)
		for (int i = i0 - 1; i <= i0 + 1; ++i)
			if (g.inside(i, k) && !std::isnan(floors[static_cast<std::size_t>(k) * g.nx + i])) sum += floors[static_cast<std::size_t>(k) * g.nx + i], ++n;
	return n ? sum / n : std::nan("");
}

// A flattened scene from its text (meshes it names by an absolute path are read).
inline pbrt_flatten::FlatScene flattenText(const std::string &text, const pbrt_flatten::MeshResolver &meshes) {
	const pbrt_scene::ParseResult r = pbrt_scene::parse(text);
	if (!r.ok) return pbrt_flatten::FlatScene();
	return pbrt_flatten::flatten(r.scene, meshes);
}

}  // namespace film_paths
}  // namespace cartoon
