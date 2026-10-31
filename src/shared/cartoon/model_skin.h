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
//
// Bones of your own, on any mesh and any placed model, posed from the film (`pose ID BONE [axis X Y Z] T DEG ...`, model_anim.h): `bone ear.l from X Y Z to X Y Z parent
// head radius R` moves only the skin within R of it, fading smoothly to nothing, the rest staying where it is - an ear droops, a cheek lifts, a tail curls with no seam. A
// piece with any such bone is weighted that way: a vertex takes from each bone it is within reach of, and what they leave of its weight stays at rest. A child bone rides on
// its parent's turn.

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "model_modifiers.h"

namespace cartoon {
namespace model {

struct Bone {
	std::string name;
	Vec3 a, b;   // (its start, the joint it turns about, and its end)
	std::string parent;   // (`parent NAME`: it rides on that bone's turn)
	double radius = 0.0;  // (`radius R`: it moves only the skin within R of it; 0: the nearest bones share every vertex)
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
// Each vertex's three bones and their weights (summing to 1, or with bones of a limited reach to what is not left at rest: bone -1).
struct Weights {
	std::vector<std::array<int, 3>> bone;
	std::vector<std::array<double, 3>> w;
};
inline Weights weightsOf(const Mesh &m, const std::vector<Bone> &bones) {
	Weights out;
	out.bone.resize(m.verts.size()), out.w.resize(m.verts.size());
	if (std::any_of(bones.begin(), bones.end(), [](const Bone &b) { return b.radius > 0.0; })) {   // bones of a limited reach: the skin near them, a smooth fall to nothing
		for (std::size_t v = 0; v < m.verts.size(); ++v) {
			std::array<std::pair<double, int>, 3> best{{{0.0, -1}, {0.0, -1}, {0.0, -1}}};
			for (std::size_t b = 0; b < bones.size(); ++b) {
				if (!(bones[b].radius > 0.0)) continue;
				const double u = 1.0 - segmentDistance(m.verts[v], bones[b].a, bones[b].b) / bones[b].radius;
				if (u <= 0.0) continue;
				const double s = u * u * (3.0 - 2.0 * u);
				for (int k = 0; k < 3; ++k)
					if (s > best[k].first) {
						for (int q = 2; q > k; --q) best[q] = best[q - 1];
						best[k] = {s, static_cast<int>(b)};
						break;
					}
			}
			const double sum = best[0].first + best[1].first + best[2].first, scale = sum > 1.0 ? 1.0 / sum : 1.0;
			for (int k = 0; k < 3; ++k) out.bone[v][k] = best[k].second, out.w[v][k] = best[k].first * scale;
		}
		return out;
	}
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
	const bool keepNormals = !rest.uvs.empty() && rest.normals.size() == rest.verts.size();   // (an imported mesh's vertices are split along its uv seams: smooth normals worked out again would show them)
	for (std::size_t v = 0; v < m.verts.size(); ++v) {
		Vec3 p{0, 0, 0}, n{0, 0, 0};
		double moved = 0.0;
		for (int k = 0; k < 3; ++k)
			if (w.w[v][k] > 0.0 && w.bone[v][k] >= 0) {
				const Mat4 &pose = poses[static_cast<std::size_t>(w.bone[v][k])];
				p = p + transformPoint(pose, rest.verts[v]) * w.w[v][k];
				if (keepNormals) n = n + (transformPoint(pose, rest.verts[v] + rest.normals[v]) - transformPoint(pose, rest.verts[v])) * w.w[v][k];
				moved += w.w[v][k];
			}
		m.verts[v] = p + rest.verts[v] * (1.0 - moved);
		if (keepNormals) m.normals[v] = normalize(n + rest.normals[v] * (1.0 - moved));
	}
	if (!keepNormals) sdf::computeNormals(m);
	return m;
}
// The bones' matrices from each one's own turn (`own`, about its start), a child riding on its parent's.
inline std::vector<Mat4> chainedPoses(const std::vector<Bone> &bones, const std::vector<Mat4> &own) {
	std::vector<Mat4> out(bones.size());
	std::vector<int> done(bones.size(), 0);
	const std::function<const Mat4 &(std::size_t, int)> pose = [&](std::size_t b, int depth) -> const Mat4 & {
		if (done[b]) return out[b];
		out[b] = own[b];
		if (!bones[b].parent.empty() && depth < static_cast<int>(bones.size())) {
			for (std::size_t q = 0; q < bones.size(); ++q)
				if (bones[q].name == bones[b].parent && q != b) {
					out[b] = mul(pose(q, depth + 1), own[b]);
					break;
				}
		}
		done[b] = 1;
		return out[b];
	};
	for (std::size_t b = 0; b < bones.size(); ++b) pose(b, 0);
	return out;
}
// A leg's phase in its gait (radians): legs numbered front to back, left then right - two in turn, four and six in diagonals.
inline double gaitPhase(int index) {
	const int row = index / 2, side = index % 2;
	return ((row + side) % 2) * kPi;
}

}  // namespace model
}  // namespace cartoon
