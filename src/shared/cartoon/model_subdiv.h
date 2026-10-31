#pragma once
// model_subdiv.h -- modeling, part 1: subdivision surfaces. A shape is made as a coarse cage of polygons (a few dozen to a few hundred, quads mostly) and smoothed into a
// clean, rounded surface by Catmull-Clark subdivision - how film characters and props are modelled: the cage is easy to shape and to rig, the surface it makes is smooth.
//
// Creases (DeRose, Kass and Truong, "Subdivision Surfaces in Character Animation", SIGGRAPH 1998 - Pixar's semi-sharp creases): an edge with sharpness s stays sharp for
// s levels of subdivision and then smooths (a fractional s blends the sharp and the smooth rules), so one cage gives a cup's crisp rim, a nose bridge, a car's panel line.
// A vertex with two sharp edges follows them (the crease rule), with three or more it stays put (a corner), as does one marked `corner`. A boundary edge (one face) is
// infinitely sharp, so an open cage keeps its border.
//
// A cage comes from a film file's model block (film_model.h: `cage`, `v`, `f`, `crease`, `corner`), from a box (cageBox: a rounded block, a head's start) or from an OBJ
// file's polygons (readObjCage: a cage modelled in Blender). subdivide() returns triangles with normals; levels 2 or 3 are smooth on screen.

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace model {

struct Cage {
	std::vector<Vec3> verts;
	std::vector<std::vector<int>> faces;            // polygons, counter-clockwise seen from outside
	std::map<std::pair<int, int>, double> creases;  // (lower index, higher index): sharpness, 0 smooth .. 10 (as good as infinite)
	std::vector<int> corners;                       // vertices kept where they are
	double sharpness(int a, int b) const {
		const auto it = creases.find({std::min(a, b), std::max(a, b)});
		return it == creases.end() ? 0.0 : it->second;
	}
	void crease(int a, int b, double s) { creases[{std::min(a, b), std::max(a, b)}] = s; }
};

// What is wrong with a cage, or "": indices in range, faces of three or more distinct corners, an edge in at most two faces.
inline std::string problemsOf(const Cage &c) {
	std::map<std::pair<int, int>, int> uses;
	for (std::size_t f = 0; f < c.faces.size(); ++f) {
		const std::vector<int> &face = c.faces[f];
		if (face.size() < 3) return "face " + std::to_string(f + 1) + " has fewer than three corners";
		for (std::size_t k = 0; k < face.size(); ++k) {
			const int a = face[k], b = face[(k + 1) % face.size()];
			if (a < 0 || a >= static_cast<int>(c.verts.size())) return "face " + std::to_string(f + 1) + " names vertex " + std::to_string(a) + ", which there is not (0 to " + std::to_string(c.verts.size() - 1) + ")";
			if (a == b) return "face " + std::to_string(f + 1) + " repeats a corner";
			if (++uses[{std::min(a, b), std::max(a, b)}] > 2) return "the edge " + std::to_string(a) + "-" + std::to_string(b) + " is in more than two faces";
		}
	}
	for (const auto &[e, s] : c.creases)
		if (!uses.count(e)) return "crease " + std::to_string(e.first) + " " + std::to_string(e.second) + " is not an edge of the cage";
	return std::string();
}

// One level of Catmull-Clark subdivision with semi-sharp creases: a cage of quads, the creases' sharpness one less.
inline Cage subdivideOnce(const Cage &c) {
	const int nv = static_cast<int>(c.verts.size());
	// the edges, their faces, and each vertex's edges and faces
	std::map<std::pair<int, int>, int> edgeIndex;
	std::vector<std::pair<int, int>> edges;
	std::vector<std::vector<int>> edgeFaces;
	std::vector<std::vector<int>> vertEdges(nv), vertFaces(nv);
	for (std::size_t f = 0; f < c.faces.size(); ++f) {
		const std::vector<int> &face = c.faces[f];
		for (std::size_t k = 0; k < face.size(); ++k) {
			const int a = face[k], b = face[(k + 1) % face.size()];
			const std::pair<int, int> key{std::min(a, b), std::max(a, b)};
			auto it = edgeIndex.find(key);
			if (it == edgeIndex.end()) {
				it = edgeIndex.emplace(key, static_cast<int>(edges.size())).first;
				edges.push_back(key);
				edgeFaces.emplace_back();
				vertEdges[a].push_back(it->second), vertEdges[b].push_back(it->second);
			}
			edgeFaces[it->second].push_back(static_cast<int>(f));
			vertFaces[a].push_back(static_cast<int>(f));
		}
	}
	const auto sharp = [&](int e) { return edgeFaces[e].size() < 2 ? 1e9 : c.sharpness(edges[e].first, edges[e].second); };
	// face points
	std::vector<Vec3> facePt(c.faces.size());
	for (std::size_t f = 0; f < c.faces.size(); ++f) {
		Vec3 s{0, 0, 0};
		for (int v : c.faces[f]) s = s + c.verts[v];
		facePt[f] = s * (1.0 / static_cast<double>(c.faces[f].size()));
	}
	// edge points: the smooth rule, the midpoint for a sharp edge, between for a fractional one
	std::vector<Vec3> edgePt(edges.size());
	for (std::size_t e = 0; e < edges.size(); ++e) {
		const Vec3 mid = (c.verts[edges[e].first] + c.verts[edges[e].second]) * 0.5;
		const double s = sharp(static_cast<int>(e));
		if (s >= 1.0) {
			edgePt[e] = mid;
			continue;
		}
		const Vec3 smooth = (c.verts[edges[e].first] + c.verts[edges[e].second] + facePt[edgeFaces[e][0]] + facePt[edgeFaces[e][1]]) * 0.25;
		edgePt[e] = lerp(smooth, mid, s);
	}
	// vertex points
	std::vector<Vec3> vertPt(nv);
	for (int v = 0; v < nv; ++v) {
		const Vec3 p = c.verts[v];
		const int n = static_cast<int>(vertEdges[v].size());
		if (n == 0) {
			vertPt[v] = p;
			continue;
		}
		Vec3 q{0, 0, 0}, r{0, 0, 0};
		for (int f : vertFaces[v]) q = q + facePt[f];
		q = q * (1.0 / std::max<std::size_t>(1, vertFaces[v].size()));
		for (int e : vertEdges[v]) r = r + (c.verts[edges[e].first] + c.verts[edges[e].second]) * 0.5;
		r = r * (1.0 / n);
		const Vec3 smooth = vertFaces[v].size() == static_cast<std::size_t>(n) ? (q + r * 2.0 + p * (n - 3.0)) * (1.0 / n) : p;   // (a boundary vertex: the crease rule below)
		std::vector<int> creased;
		double sum = 0.0;
		for (int e : vertEdges[v]) {
			const double s = sharp(e);
			if (s > 0.0) creased.push_back(e), sum += std::min(s, 1.0);
		}
		const bool corner = std::find(c.corners.begin(), c.corners.end(), v) != c.corners.end();
		Vec3 hard = p;   // (three or more creases, or a marked corner)
		if (creased.size() == 2 && !corner) {
			const auto other = [&](int e) { return c.verts[edges[e].first == v ? edges[e].second : edges[e].first]; };
			hard = (other(creased[0]) + p * 6.0 + other(creased[1])) * 0.125;
		}
		if (creased.size() < 2 && !corner) vertPt[v] = smooth;
		else {
			const double s = corner ? 1.0 : std::min(1.0, sum / creased.size());   // (the creases' mean sharpness: below 1, part way to smooth)
			vertPt[v] = lerp(smooth, hard, s);
		}
	}
	// the new cage: vertices, then face points, then edge points; a quad for each corner of each face
	Cage out;
	out.verts = vertPt;
	const int faceBase = nv, edgeBase = nv + static_cast<int>(c.faces.size());
	out.verts.insert(out.verts.end(), facePt.begin(), facePt.end());
	out.verts.insert(out.verts.end(), edgePt.begin(), edgePt.end());
	for (std::size_t f = 0; f < c.faces.size(); ++f) {
		const std::vector<int> &face = c.faces[f];
		const int k = static_cast<int>(face.size());
		for (int i = 0; i < k; ++i) {
			const int prev = face[(i + k - 1) % k], v = face[i], next = face[(i + 1) % k];
			const int eIn = edgeIndex.at({std::min(prev, v), std::max(prev, v)}), eOut = edgeIndex.at({std::min(v, next), std::max(v, next)});
			out.faces.push_back({v, edgeBase + eOut, faceBase + static_cast<int>(f), edgeBase + eIn});
		}
	}
	// each crease carries on along its two halves, one level less sharp
	for (const auto &[key, s] : c.creases) {
		if (s <= 1.0) continue;
		const auto it = edgeIndex.find(key);
		if (it == edgeIndex.end()) continue;
		const int mid = edgeBase + it->second;
		out.crease(key.first, mid, s - 1.0), out.crease(key.second, mid, s - 1.0);
	}
	out.corners = c.corners;
	return out;
}

// The cage subdivided `levels` times (0 to 5) as triangles with smooth normals.
inline Mesh subdivide(const Cage &cage, int levels) {
	Cage c = cage;
	for (int l = 0; l < std::clamp(levels, 0, 5); ++l) c = subdivideOnce(c);
	Mesh m;
	m.verts = c.verts;
	for (const std::vector<int> &face : c.faces)
		for (std::size_t k = 1; k + 1 < face.size(); ++k) m.tris.push_back({face[0], face[k], face[k + 1]});
	sdf::computeNormals(m);
	return m;
}

// ---- cages to start from ----
// A box cage (w x h x d, centred), its edges creased `crease` (0: a soft pebble; 1 or 2: a rounded block; 5: as good as a sharp box).
inline Cage cageBox(double w, double h, double d, double crease = 0.0) {
	Cage c;
	for (int i = 0; i < 8; ++i) c.verts.push_back({(i & 1 ? 0.5 : -0.5) * w, (i & 2 ? 0.5 : -0.5) * h, (i & 4 ? 0.5 : -0.5) * d});
	c.faces = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
	if (crease > 0.0)
		for (const std::vector<int> &f : c.faces)
			for (std::size_t k = 0; k < 4; ++k) c.crease(f[k], f[(k + 1) % 4], crease);
	return c;
}
// An OBJ file's polygons as a cage (`v` and `f` lines; texture and normal indices dropped; negative indices from the end). `error` says what went wrong.
inline Cage readObjCage(const std::string &path, std::string &error) {
	Cage c;
	std::ifstream in(path);
	if (!in) {
		error = "cannot read " + path;
		return c;
	}
	for (std::string line; std::getline(in, line);) {
		std::istringstream s(line);
		std::string w;
		s >> w;
		if (w == "v") {
			Vec3 p{};
			s >> p.x >> p.y >> p.z;
			c.verts.push_back(p);
		} else if (w == "f") {
			std::vector<int> face;
			for (std::string t; s >> t;) {
				const int i = std::atoi(t.substr(0, t.find('/')).c_str());
				face.push_back(i < 0 ? static_cast<int>(c.verts.size()) + i : i - 1);
			}
			c.faces.push_back(face);
		}
	}
	error = problemsOf(c);
	return c;
}

}  // namespace model
}  // namespace cartoon
