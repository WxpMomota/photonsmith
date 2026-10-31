#pragma once
// model_skin.h -- modeling round 3, part 1: a model character in one continuous skin. A piece marked `skin` is not moved stiffly as a whole: its vertices follow a
// skeleton's bones, each blended between the bones near it, so a body bends at its hips, knees, shoulders, elbows and neck - one smooth surface, no seams.
//
//   the skeleton   `bone NAME from X Y Z to X Y Z` lines in the piece (NAME: spine, head, leg.1 .. leg.N or leg.l/leg.r, arm.l, arm.r, tail - the roles the rig moves);
//                  none given, one is found from the piece's shape: a spine up its middle, a head bone above it, two legs below and two arms out to its sides
//   the weights    each vertex to its nearest bones by distance (inverse fourth power, the three nearest kept): a joint's skin is shared smoothly between its bones
//   the pose       each bone turned about its start by the rig (film_model_kind.h): legs swing, arms swing and gesture, the head turns and nods, the spine leans and
//                  breathes; arms and head ride on the spine. Linear blend skinning of the vertices by their bones' matrices.
//
// Gaits by the number of legs: two in turn; four and six in diagonals (a trot; an insect's tripod - legs numbered front to back, left then right, as `make creature`
// numbers them).

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "model_modifiers.h"

namespace cartoon {
namespace model {

struct Bone {
	std::string name;
	Vec3 a, b;   // (its start, the joint it turns about, and its end)
};
inline std::string boneRole(const std::string &name) {
	if (name.rfind("leg", 0) == 0) return "leg";
	if (name == "arm.l" || name == "arm.r" || name == "head" || name == "spine" || name.rfind("tail", 0) == 0) return name.rfind("tail", 0) == 0 ? "tail" : name;
	return "spine";
}
// A biped's skeleton found from a mesh's box: legs below, a spine and a head up the middle, arms out to the sides at the shoulders.
inline std::vector<Bone> autoSkeleton(const Mesh &m) {
	Vec3 lo, hi;
	boxOf(m, lo, hi);
	const double H = hi.y - lo.y, W = hi.x - lo.x, cx = 0.5 * (lo.x + hi.x), cz = 0.5 * (lo.z + hi.z);
	const double hip = lo.y + 0.45 * H, shoulder = lo.y + 0.72 * H;
	return {{"spine", {cx, hip, cz}, {cx, shoulder, cz}},
			{"head", {cx, lo.y + 0.78 * H, cz}, {cx, hi.y, cz}},
			{"leg.1", {cx + 0.18 * W, hip, cz}, {cx + 0.18 * W, lo.y, cz}},
			{"leg.2", {cx - 0.18 * W, hip, cz}, {cx - 0.18 * W, lo.y, cz}},
			{"arm.l", {cx + 0.3 * W, shoulder, cz}, {cx + 0.5 * W, lo.y + 0.45 * H, cz}},
			{"arm.r", {cx - 0.3 * W, shoulder, cz}, {cx - 0.5 * W, lo.y + 0.45 * H, cz}}};
}
inline double segmentDistance(Vec3 p, Vec3 a, Vec3 b) {
	const Vec3 ab = b - a;
	const double u = std::clamp(dot(p - a, ab) / std::max(1e-12, dot(ab, ab)), 0.0, 1.0);
	return length(p - (a + ab * u));
}
// Each vertex's three bones and their weights (summing to 1).
struct Weights {
	std::vector<std::array<int, 3>> bone;
	std::vector<std::array<double, 3>> w;
};
inline Weights weightsOf(const Mesh &m, const std::vector<Bone> &bones) {
	Weights out;
	out.bone.resize(m.verts.size()), out.w.resize(m.verts.size());
	for (std::size_t v = 0; v < m.verts.size(); ++v) {
		std::array<std::pair<double, int>, 3> best{{{0.0, 0}, {0.0, 0}, {0.0, 0}}};
		for (std::size_t b = 0; b < bones.size(); ++b) {
			const double d = segmentDistance(m.verts[v], bones[b].a, bones[b].b), s = 1.0 / (std::pow(d, 4.0) + 1e-10);
			for (int k = 0; k < 3; ++k)
				if (s > best[k].first) {
					for (int q = 2; q > k; --q) best[q] = best[q - 1];
					best[k] = {s, static_cast<int>(b)};
					break;
				}
		}
		const double sum = best[0].first + best[1].first + best[2].first;
		for (int k = 0; k < 3; ++k) out.bone[v][k] = best[k].second, out.w[v][k] = sum > 0.0 ? best[k].first / sum : (k == 0 ? 1.0 : 0.0);
	}
	return out;
}
// The mesh posed: each vertex by its bones' matrices (`poses`, one a bone, the model's own space), blended by its weights.
inline Mesh skinned(const Mesh &rest, const Weights &w, const std::vector<Mat4> &poses) {
	Mesh m = rest;
	for (std::size_t v = 0; v < m.verts.size(); ++v) {
		Vec3 p{0, 0, 0};
		for (int k = 0; k < 3; ++k)
			if (w.w[v][k] > 0.0) p = p + transformPoint(poses[static_cast<std::size_t>(w.bone[v][k])], rest.verts[v]) * w.w[v][k];
		m.verts[v] = p;
	}
	sdf::computeNormals(m);
	return m;
}
// A leg's phase in its gait (radians): legs numbered front to back, left then right - two in turn, four and six in diagonals.
inline double gaitPhase(int index) {
	const int row = index / 2, side = index % 2;
	return ((row + side) % 2) * kPi;
}

}  // namespace model
}  // namespace cartoon
