#pragma once
// model_surface.h -- modeling round 3: what is done to a piece's surface.
//
//   fur LENGTH [density N] [width W] [colour R G B] [curl C] [droop D]      (part 6) strands grown from the piece: N a square metre (4000), each a curve LENGTH long
//                                                                             that droops and curls - fur, grass on a hill, moss, a fuzzy toy
//   cloth W D [at X Y Z] [res N] [wave A]                                   (part 6) a sheet W x D dropped from above (at X Y Z, its middle) onto the model's other
//                                                                             pieces and the ground, and left as it settles - a tablecloth, a cape on a chair, a
//                                                                             tarpaulin; `wave A` keeps it rippling (a flag held at one edge)
//   stroke inflate|crease|grab|smooth|pinch|flatten RADIUS STRENGTH path X Y Z ... [toward X Y Z]   (part 8) a sculpting brush dragged along a path through
//                                                                             the piece: puffed out, creased in, pulled toward a direction, smoothed, pinched to the
//                                                                             line, flattened
//   remesh [CELL]                                                           (part 8) the piece made again as one clean closed surface (its distance field, meshed)
//   close                                                                   (part 8) its holes filled (each open border fanned to its middle)
//   region NAME above Y|below Y|facing up|down|front|back|left|right|near X Y Z R [colour R G B] [rough R] [plastic|matte|metal|glass|skin|glow G]
//                                                                           (part 9) the triangles that match drawn in a material of their own
//   unwrap                                                                  (part 10) proper uvs: the surface cut into charts along its creases, each laid flat
//                                                                             and packed - for its paint and its export, in place of box projection

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "model_boolean.h"
#include "model_curves.h"
#include "model_detail.h"
#include "model_doctor.h"
#include "model_modifiers.h"

namespace cartoon {
namespace model {

// ---- fur ----
struct Fur {
	bool on = false;
	double length = 0.03, density = 4000.0, width = 0.0015, curl = 0.3, droop = 0.4;
	Vec3 colour{0.55, 0.4, 0.25};
	int seed = 1;
};
// The strands as pbrt curves, in the mirrored space a placed PLY is in (x negated: film_cache::placement). At most 60000 strands.
inline std::string furText(const Mesh &m, const Fur &f) {
	double area = 0.0;
	std::vector<double> run;
	for (const auto &t : m.tris) {
		area += 0.5 * length(cross(m.verts[t[1]] - m.verts[t[0]], m.verts[t[2]] - m.verts[t[0]]));
		run.push_back(area);
	}
	if (area <= 0.0) return std::string();
	const int n = static_cast<int>(std::min(60000.0, f.density * area));
	std::string out;
	out.reserve(static_cast<std::size_t>(n) * 200);
	char buf[400];
	std::snprintf(buf, sizeof buf, "  Material \"coateddiffuse\" \"rgb reflectance\" [ %.4f %.4f %.4f ] \"float roughness\" [ 0.5 ]\n", f.colour.x, f.colour.y, f.colour.z);
	out += buf;
	for (int k = 0; k < n; ++k) {
		const double pick = hash01(k * 7919 + f.seed, 1) * area;
		const std::size_t t = static_cast<std::size_t>(std::lower_bound(run.begin(), run.end(), pick) - run.begin());
		const auto &tri = m.tris[std::min(t, m.tris.size() - 1)];
		double a = hash01(k * 7919 + f.seed, 2), b = hash01(k * 7919 + f.seed, 3);
		if (a + b > 1.0) a = 1.0 - a, b = 1.0 - b;
		const Vec3 p = m.verts[tri[0]] + (m.verts[tri[1]] - m.verts[tri[0]]) * a + (m.verts[tri[2]] - m.verts[tri[0]]) * b;
		Vec3 n0 = normalize(cross(m.verts[tri[1]] - m.verts[tri[0]], m.verts[tri[2]] - m.verts[tri[0]]));
		if (m.normals.size() == m.verts.size()) n0 = normalize(m.normals[tri[0]] * (1 - a - b) + m.normals[tri[1]] * a + m.normals[tri[2]] * b);
		const double L = f.length * (0.7 + 0.6 * hash01(k * 7919 + f.seed, 4));
		const Vec3 curl{f.curl * (hash01(k, f.seed + 5) - 0.5), 0.0, f.curl * (hash01(k, f.seed + 6) - 0.5)};
		const Vec3 p1 = p + n0 * (0.35 * L), p2 = p + n0 * (0.7 * L) + (curl + Vec3{0, -f.droop, 0}) * (0.3 * L), p3 = p + n0 * (0.9 * L) + (curl * 1.6 + Vec3{0, -f.droop, 0}) * (0.7 * L);
		std::snprintf(buf, sizeof buf, "  Shape \"curve\" \"string type\" \"cylinder\" \"float width0\" [ %.5f ] \"float width1\" [ %.5f ] \"point3 P\" [ %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f ]\n",
					  f.width, 0.25 * f.width, -p.x, p.y, p.z, -p1.x, p1.y, p1.z, -p2.x, p2.y, p2.z, -p3.x, p3.y, p3.z);
		out += buf;
	}
	return "AttributeBegin\n" + out + "AttributeEnd\n";
}

// ---- cloth ----
struct Cloth {
	double w = 1.0, d = 1.0, wave = 0.0;
	Vec3 at{0, 1.5, 0};
	int res = 40;
};
// The sheet dropped onto the shapes of `colliders` (distance fields) and the ground (y 0), and let settle: a grid of points joined by springs (Verlet), pushed out of
// whatever they fall into. Each side of the sheet a surface of its own (a thin sheet seen from both sides).
inline Mesh draped(const Cloth &c, const std::vector<const MeshField *> &colliders) {
	const int n = std::clamp(c.res, 6, 120);
	std::vector<Vec3> p, prev;
	for (int j = 0; j <= n; ++j)
		for (int i = 0; i <= n; ++i) p.push_back(c.at + Vec3{c.w * (i / double(n) - 0.5), 0.0, c.d * (j / double(n) - 0.5)});
	prev = p;
	const auto at = [n](int i, int j) { return static_cast<std::size_t>(j) * (n + 1) + i; };
	const double rest = c.w / n, restD = c.d / n, gap = 0.006;
	struct Link {
		std::size_t a, b;
		double len;
	};
	std::vector<Link> links;
	for (int j = 0; j <= n; ++j)
		for (int i = 0; i <= n; ++i) {
			if (i < n) links.push_back({at(i, j), at(i + 1, j), rest});
			if (j < n) links.push_back({at(i, j), at(i, j + 1), restD});
			if (i < n && j < n) links.push_back({at(i, j), at(i + 1, j + 1), std::hypot(rest, restD)}), links.push_back({at(i + 1, j), at(i, j + 1), std::hypot(rest, restD)});
			if (i + 2 <= n) links.push_back({at(i, j), at(i + 2, j), 2 * rest});   // (bending)
			if (j + 2 <= n) links.push_back({at(i, j), at(i, j + 2), 2 * restD});
		}
	const double dt = 1.0 / 120.0;
	for (int step = 0; step < 480; ++step) {
		for (std::size_t q = 0; q < p.size(); ++q) {
			const Vec3 v = (p[q] - prev[q]) * 0.985;   // (a little air drag)
			prev[q] = p[q];
			p[q] = p[q] + v + Vec3{0, -9.81 * dt * dt, 0};
		}
		std::vector<char> touching(p.size(), 0);
		for (int it = 0; it < 6; ++it) {
			for (const Link &l : links) {
				const Vec3 d = p[l.b] - p[l.a];
				const double len = length(d);
				if (len < 1e-9) continue;
				const Vec3 fix = d * (0.5 * (len - l.len) / len);
				p[l.a] = p[l.a] + fix, p[l.b] = p[l.b] - fix;
			}
			for (std::size_t k = 0; k < p.size(); ++k) {
				Vec3 &q = p[k];
				for (const MeshField *f : colliders) {
					const double dist = (*f)(q);
					if (dist < gap) q = q + sdf::normalAt(*f, q, f->cell * 0.5) * (gap - dist), touching[k] = 1;
				}
				if (q.y < gap) q.y = gap, touching[k] = 1;
			}
		}
		for (std::size_t k = 0; k < p.size(); ++k)   // (friction: what touches something keeps a tenth of its sliding speed - a cloth stays on a table, not sliding off)
			if (touching[k]) prev[k] = p[k] - (p[k] - prev[k]) * 0.1;
	}
	Mesh m;
	m.verts = p;
	for (int j = 0; j < n; ++j)
		for (int i = 0; i < n; ++i) m.tris.push_back({static_cast<int>(at(i, j)), static_cast<int>(at(i, j + 1)), static_cast<int>(at(i + 1, j))}), m.tris.push_back({static_cast<int>(at(i + 1, j)), static_cast<int>(at(i, j + 1)), static_cast<int>(at(i + 1, j + 1))});
	sdf::computeNormals(m);
	return applied(m, {"solidify", {0.004}, "y"});
}
// A sheet rippling at t (`wave` its amplitude): waves running along it from its first edge, growing with the distance from it.
inline Mesh rippled(const Mesh &m, double wave, double t) {
	if (wave <= 0.0) return m;
	Mesh out = m;
	Vec3 lo, hi;
	boxOf(m, lo, hi);
	const double W = std::max(1e-6, hi.x - lo.x);
	for (Vec3 &p : out.verts) {
		const double u = (p.x - lo.x) / W;
		p.z += wave * u * std::sin(2.0 * kPi * (1.6 * u - 1.3 * t)), p.y += 0.3 * wave * u * std::sin(2.0 * kPi * (1.1 * u - 0.9 * t) + 1.0);
	}
	sdf::computeNormals(out);
	return out;
}

// ---- sculpting ----
struct Stroke {
	std::string kind = "inflate";
	double radius = 0.1, strength = 0.02;
	std::vector<Vec3> path;
	Vec3 toward{0, 1, 0};
};
inline Mesh sculpted(const Mesh &in, const Stroke &s) {
	Mesh m = refined(in, 0.3 * s.radius, 300000);
	sdf::computeNormals(m);
	const std::vector<Vec3> line = s.path.size() >= 2 ? spline(s.path, 8, false) : s.path;
	if (line.empty()) return m;
	const auto nearestOnLine = [&](Vec3 p, double &d) {
		Vec3 best = line[0];
		d = length(p - best);
		for (std::size_t i = 0; i + 1 < line.size(); ++i) {
			const Vec3 ab = line[i + 1] - line[i];
			const double u = std::clamp(dot(p - line[i], ab) / std::max(1e-12, dot(ab, ab)), 0.0, 1.0);
			const Vec3 q = line[i] + ab * u;
			if (length(p - q) < d) d = length(p - q), best = q;
		}
		return best;
	};
	const std::vector<std::vector<int>> nb = s.kind == "smooth" || s.kind == "flatten" ? neighboursOf(m) : std::vector<std::vector<int>>();
	Vec3 planeN{0, 0, 0}, planeP{0, 0, 0};
	double planeW = 0.0;
	std::vector<Vec3> next = m.verts;
	for (std::size_t v = 0; v < m.verts.size(); ++v) {
		double d = 0.0;
		const Vec3 q = nearestOnLine(m.verts[v], d);
		if (d >= s.radius) continue;
		const double fall = s.kind == "crease" ? std::pow(1.0 - d / s.radius, 3.0) : 0.5 + 0.5 * std::cos(kPi * d / s.radius);   // (a crease's falloff is sharp)
		if (s.kind == "inflate") next[v] = m.verts[v] + m.normals[v] * (s.strength * fall);
		else if (s.kind == "crease") next[v] = m.verts[v] - m.normals[v] * (s.strength * fall) + (q - m.verts[v]) * (0.3 * fall);
		else if (s.kind == "grab") next[v] = m.verts[v] + normalize(s.toward) * (s.strength * fall);
		else if (s.kind == "pinch") next[v] = m.verts[v] + (q - m.verts[v]) * std::min(1.0, s.strength * 10.0 * fall);
		else if (s.kind == "smooth" && !nb[v].empty()) {
			Vec3 c{0, 0, 0};
			for (int j : nb[v]) c = c + m.verts[j];
			next[v] = lerp(m.verts[v], c * (1.0 / nb[v].size()), std::min(1.0, s.strength * 20.0 * fall));
		} else if (s.kind == "flatten") planeN = planeN + m.normals[v] * fall, planeP = planeP + m.verts[v] * fall, planeW += fall;
	}
	if (s.kind == "flatten" && planeW > 0.0) {   // (onto the plane through the brushed part's middle, facing its mean way)
		planeN = normalize(planeN), planeP = planeP * (1.0 / planeW);
		for (std::size_t v = 0; v < m.verts.size(); ++v) {
			double d = 0.0;
			nearestOnLine(m.verts[v], d);
			if (d >= s.radius) continue;
			const double fall = 0.5 + 0.5 * std::cos(kPi * d / s.radius);
			next[v] = m.verts[v] - planeN * (dot(m.verts[v] - planeP, planeN) * std::min(1.0, s.strength * 20.0 * fall));
		}
	}
	m.verts = next;
	sdf::computeNormals(m);
	return m;
}
// The mesh made again as one closed surface: its distance field on `cell` cubes, meshed.
inline Mesh remeshed(const Mesh &m, double cell) {
	if (m.tris.empty()) return m;
	Vec3 lo, hi;
	boxOf(m, lo, hi);
	if (cell <= 0.0) cell = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) / 70.0;
	const MeshField f = meshField(m, cell);
	const Vec3 pad{2 * cell, 2 * cell, 2 * cell};
	return dualContour(f, lo - pad, hi + pad, cell);
}
// The mesh with its holes filled: each loop of open border edges fanned from its middle (the loops found on the mesh welded by position).
inline Mesh closedHoles(const Mesh &in) {
	Mesh m = welded(in, 1e-7);
	std::map<std::pair<int, int>, int> count;
	for (const auto &t : m.tris)
		for (int q = 0; q < 3; ++q) ++count[{std::min(t[q], t[(q + 1) % 3]), std::max(t[q], t[(q + 1) % 3])}];
	std::map<int, int> nextOf;   // (each border edge as its triangle runs it, reversed: the fill runs the other way)
	for (const auto &t : m.tris)
		for (int q = 0; q < 3; ++q) {
			const int a = t[q], b = t[(q + 1) % 3];
			if (count[{std::min(a, b), std::max(a, b)}] == 1) nextOf[b] = a;
		}
	std::set<int> seen;
	for (const auto &[start, unused] : nextOf) {
		(void)unused;
		if (seen.count(start)) continue;
		std::vector<int> loop;
		for (int v = start; !seen.count(v) && nextOf.count(v); v = nextOf[v]) seen.insert(v), loop.push_back(v);
		if (loop.size() < 3) continue;
		Vec3 c{0, 0, 0};
		for (int v : loop) c = c + m.verts[v];
		const int centre = static_cast<int>(m.verts.size());
		m.verts.push_back(c * (1.0 / loop.size()));
		for (std::size_t k = 0; k < loop.size(); ++k) m.tris.push_back({centre, loop[k], loop[(k + 1) % loop.size()]});
	}
	sdf::computeNormals(m);
	return m;
}

// ---- regions ----
struct Region {
	std::string name, rule;   // above, below, facing, near
	std::string facing;       // up down front back left right
	double value = 0.0, radius = 0.1;
	Vec3 at{0, 0, 0};
	std::string finish = "plastic";
	Vec3 colour{0.8, 0.8, 0.8};
	double rough = 0.35, glow = 0.0;
};
// Whether a triangle (its middle `c`, its normal `n`) is in the region.
inline bool inRegion(const Region &r, Vec3 c, Vec3 n) {
	if (r.rule == "above") return c.y > r.value;
	if (r.rule == "below") return c.y < r.value;
	if (r.rule == "near") return length(c - r.at) < r.radius;
	const Vec3 dir = r.facing == "up" ? Vec3{0, 1, 0} : r.facing == "down" ? Vec3{0, -1, 0} : r.facing == "front" ? Vec3{0, 0, 1} : r.facing == "back" ? Vec3{0, 0, -1}
				   : r.facing == "left"																							? Vec3{1, 0, 0}
																																: Vec3{-1, 0, 0};
	return dot(n, dir) > 0.6;
}
// The mesh split by its regions: [0] what no region takes, then one mesh a region (the first region that matches a triangle has it).
inline std::vector<Mesh> splitByRegions(const Mesh &m, const std::vector<Region> &regions) {
	std::vector<Mesh> out(regions.size() + 1);
	std::vector<std::vector<int>> index(regions.size() + 1, std::vector<int>(m.verts.size(), -1));
	for (const auto &t : m.tris) {
		const Vec3 c = (m.verts[t[0]] + m.verts[t[1]] + m.verts[t[2]]) * (1.0 / 3.0), n = normalize(cross(m.verts[t[1]] - m.verts[t[0]], m.verts[t[2]] - m.verts[t[0]]));
		std::size_t r = 0;
		for (std::size_t k = 0; k < regions.size() && r == 0; ++k)
			if (inRegion(regions[k], c, n)) r = k + 1;
		std::array<int, 3> tri{};
		for (int q = 0; q < 3; ++q) {
			int &ix = index[r][t[q]];
			if (ix < 0) {
				ix = static_cast<int>(out[r].verts.size());
				out[r].verts.push_back(m.verts[t[q]]);
				out[r].normals.push_back(t[q] < static_cast<int>(m.normals.size()) ? m.normals[t[q]] : n);
				if (m.uvs.size() == m.verts.size()) out[r].uvs.push_back(m.uvs[t[q]]);
			}
			tri[q] = ix;
		}
		out[r].tris.push_back(tri);
	}
	return out;
}

// ---- charts ----
// The mesh with chart uvs: the surface grown into charts from its largest faces (a face joins a neighbour's chart while it turns less than 45 degrees from the chart's
// first face), each chart laid flat on its plane and packed in rows into the unit square (all at one scale), a gap between them.
// `from` gets each copy's vertex in the mesh as welded by position (welded(in, 1e-7)); the normals are the welded mesh's, smooth across the charts' seams.
inline Mesh chartUnwrapped(const Mesh &in, std::vector<int> *from = nullptr) {
	Mesh m = welded(in, 1e-7);
	sdf::computeNormals(m);
	const std::size_t nt = m.tris.size();
	std::vector<Vec3> fn(nt);
	std::vector<double> fa(nt);
	std::map<std::pair<int, int>, std::vector<int>> edgeTris;
	for (std::size_t t = 0; t < nt; ++t) {
		const Vec3 c = cross(m.verts[m.tris[t][1]] - m.verts[m.tris[t][0]], m.verts[m.tris[t][2]] - m.verts[m.tris[t][0]]);
		fa[t] = 0.5 * length(c), fn[t] = fa[t] > 0 ? normalize(c) : Vec3{0, 1, 0};
		for (int q = 0; q < 3; ++q) edgeTris[{std::min(m.tris[t][q], m.tris[t][(q + 1) % 3]), std::max(m.tris[t][q], m.tris[t][(q + 1) % 3])}].push_back(static_cast<int>(t));
	}
	std::vector<int> chart(nt, -1), order(nt);
	for (std::size_t t = 0; t < nt; ++t) order[t] = static_cast<int>(t);
	std::sort(order.begin(), order.end(), [&](int a, int b) { return fa[a] > fa[b]; });
	std::vector<Vec3> chartN;
	for (int seed : order) {
		if (chart[seed] >= 0) continue;
		const int id = static_cast<int>(chartN.size());
		chartN.push_back(fn[seed]);
		std::vector<int> todo{seed};
		chart[seed] = id;
		while (!todo.empty()) {
			const int t = todo.back();
			todo.pop_back();
			for (int q = 0; q < 3; ++q)
				for (int o : edgeTris[{std::min(m.tris[t][q], m.tris[t][(q + 1) % 3]), std::max(m.tris[t][q], m.tris[t][(q + 1) % 3])}])
					if (chart[o] < 0 && dot(fn[o], chartN[id]) > 0.7) chart[o] = id, todo.push_back(o);
		}
	}
	// each chart flat on its plane: its own uvs and box
	struct Flat {
		Vec3 u, v;
		double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30, ox = 0, oy = 0;
	};
	std::vector<Flat> flats(chartN.size());
	for (std::size_t c = 0; c < chartN.size(); ++c) {
		const Vec3 n = chartN[c];
		flats[c].u = normalize(std::abs(n.y) < 0.9 ? cross(Vec3{0, 1, 0}, n) : cross(Vec3{1, 0, 0}, n)), flats[c].v = cross(n, flats[c].u);
	}
	for (std::size_t t = 0; t < nt; ++t)
		for (int q = 0; q < 3; ++q) {
			Flat &f = flats[static_cast<std::size_t>(chart[t])];
			const Vec3 p = m.verts[m.tris[t][q]];
			const double x = dot(p, f.u), y = dot(p, f.v);
			f.x0 = std::min(f.x0, x), f.x1 = std::max(f.x1, x), f.y0 = std::min(f.y0, y), f.y1 = std::max(f.y1, y);
		}
	// packed in rows, tallest first, into a square whose side is found by trying widths
	std::vector<int> byHeight(chartN.size());
	for (std::size_t c = 0; c < byHeight.size(); ++c) byHeight[c] = static_cast<int>(c);
	std::sort(byHeight.begin(), byHeight.end(), [&](int a, int b) { return flats[a].y1 - flats[a].y0 > flats[b].y1 - flats[b].y0; });
	double total = 0.0;
	for (const Flat &f : flats) total += (f.x1 - f.x0 + 1e-6) * (f.y1 - f.y0 + 1e-6);
	double side = std::sqrt(total) * 1.15, used = 0.0;
	for (int attempt = 0; attempt < 30; ++attempt, side *= 1.08) {
		const double gap = 0.01 * side;
		double x = gap, y = gap, rowH = 0.0;
		bool fits = true;
		for (int c : byHeight) {
			Flat &f = flats[static_cast<std::size_t>(c)];
			const double w = f.x1 - f.x0, h = f.y1 - f.y0;
			if (x + w + gap > side) x = gap, y += rowH + gap, rowH = 0.0;
			if (w + 2 * gap > side) fits = false;
			f.ox = x - f.x0, f.oy = y - f.y0;
			x += w + gap, rowH = std::max(rowH, h);
		}
		used = y + rowH + gap;
		if (fits && used <= side) break;
	}
	const double scaleUv = 1.0 / std::max(side, used);
	Mesh out;
	std::map<std::pair<int, int>, int> made;   // (a vertex in a chart: one copy each)
	for (std::size_t t = 0; t < nt; ++t) {
		std::array<int, 3> tri{};
		const int c = chart[t];
		const Flat &f = flats[static_cast<std::size_t>(c)];
		for (int q = 0; q < 3; ++q) {
			const int v = m.tris[t][q];
			auto it = made.find({c, v});
			if (it == made.end()) {
				it = made.emplace(std::make_pair(c, v), static_cast<int>(out.verts.size())).first;
				out.verts.push_back(m.verts[v]);
				out.normals.push_back(m.normals[v]);
				out.uvs.push_back({(dot(m.verts[v], f.u) + f.ox) * scaleUv, (dot(m.verts[v], f.v) + f.oy) * scaleUv});
				if (from) from->push_back(v);
			}
			tri[q] = it->second;
		}
		out.tris.push_back(tri);
	}
	return out;
}

}  // namespace model
}  // namespace cartoon
