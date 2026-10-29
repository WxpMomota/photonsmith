#pragma once
// mocap_bvh.h -- motion capture for a film's rigged characters (film_rigged.h): a BVH file (the motion-capture format: Carnegie Mellon's free library, Mixamo, Rokoko,
// Blender's exporter) read and retargeted onto a glTF skeleton, as a clip the character plays like any of its own (a film's `mocap NAME FILE.bvh at T ...`).
//
// Retargeting by the world: each bone of the model that a BVH joint matches (by name: "LeftUpLeg", "mixamorig:LeftUpLeg", "leg.L", "thigh_l" ... all one bone) turns in
// the world as that joint turns away from its rest - so two skeletons whose bones point different ways in their own frames still move alike, as long as both stand in the
// same rest pose (a T-pose, as BVH files and Mixamo's models do). A bone nothing matches keeps its rest place under its parent. The hips also rise and fall with the
// capture's (scaled to the model: its hips' height over the capture's), and with `travel` move over the ground too (otherwise the film moves the character).

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../gltf_skin.h"

namespace cartoon {
namespace mocap {

struct Joint {
	std::string name;
	int parent = -1;
	double offset[3] = {0, 0, 0};
	std::vector<int> channels;   // 0..2 X Y Z position, 3..5 X Y Z rotation, in the file's order
	int first = 0;               // its first value in a frame
	double end[3] = {0, 0, 0};   // its End Site's offset (a joint with no joint below it: where its bone ends)
	bool hasEnd = false;
};
struct Bvh {
	std::vector<Joint> joints;
	int frames = 0;
	double frameTime = 1.0 / 30.0;
	std::vector<double> values;   // frames x channels
	int width = 0;                // channels a frame
};

// A BVH file's text: "" or what is wrong.
inline std::string parse(const std::string &text, Bvh &out) {
	std::istringstream in(text);
	std::string w;
	std::vector<int> stack;
	int pending = -1;   // (a joint named, its { not yet read)
	bool endSite = false;
	if (!(in >> w) || w != "HIERARCHY") return "not a BVH file (it does not begin with HIERARCHY)";
	while (in >> w) {
		if (w == "ROOT" || w == "JOINT") {
			Joint j;
			in >> j.name;
			j.parent = stack.empty() ? -1 : stack.back();
			out.joints.push_back(j);
			pending = static_cast<int>(out.joints.size()) - 1;
		} else if (w == "End") {
			std::string site;
			in >> site;
			endSite = true;
		} else if (w == "{") {
			if (endSite) stack.push_back(-2);   // (an end site: its offset is not a joint)
			else if (pending >= 0) stack.push_back(pending), pending = -1;
			else return "a { with no joint before it";
			endSite = false;
		} else if (w == "}") {
			if (stack.empty()) return "a } too many";
			stack.pop_back();
		} else if (w == "OFFSET") {
			double o[3];
			if (!(in >> o[0] >> o[1] >> o[2])) return "an OFFSET without three numbers";
			if (!stack.empty() && stack.back() >= 0) {
				for (int k = 0; k < 3; ++k) out.joints[static_cast<std::size_t>(stack.back())].offset[k] = o[k];
			} else if (stack.size() >= 2 && stack[stack.size() - 2] >= 0) {   // (an End Site's: where its joint's bone ends)
				Joint &j = out.joints[static_cast<std::size_t>(stack[stack.size() - 2])];
				for (int k = 0; k < 3; ++k) j.end[k] = o[k];
				j.hasEnd = true;
			}
		} else if (w == "CHANNELS") {
			int n = 0;
			if (!(in >> n) || n < 0 || n > 6 || stack.empty() || stack.back() < 0) return "a CHANNELS line out of place";
			Joint &j = out.joints[static_cast<std::size_t>(stack.back())];
			j.first = out.width;
			for (int k = 0; k < n; ++k) {
				std::string c;
				in >> c;
				const int axis = c.empty() ? -1 : std::toupper(static_cast<unsigned char>(c[0])) - 'X';
				if (axis < 0 || axis > 2 || c.size() < 2) return "an unknown channel \"" + c + "\"";
				j.channels.push_back((c.find("osition") != std::string::npos ? 0 : 3) + axis);
			}
			out.width += n;
		} else if (w == "MOTION") {
			std::string a, b;
			if (!(in >> a >> out.frames) || a != "Frames:" || !(in >> a >> b >> out.frameTime) || a != "Frame" || b != "Time:") return "the MOTION section's Frames and Frame Time are not there";
			out.values.resize(static_cast<std::size_t>(out.frames) * out.width);
			for (double &v : out.values)
				if (!(in >> v)) return "the motion has fewer numbers than its frames and channels say";
			if (out.joints.empty() || out.frames <= 0) return "no joints or no frames";
			if (!(out.frameTime > 0.0)) out.frameTime = 1.0 / 30.0;
			return std::string();
		}
	}
	return "no MOTION section";
}

// ---- quaternions (x y z w, as glTF's) ----
struct Q {
	double x = 0, y = 0, z = 0, w = 1;
};
inline Q mul(const Q &a, const Q &b) {
	return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
			a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Q conj(const Q &q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Q axisAngle(int axis, double degrees) {
	const double h = 0.5 * degrees * 3.14159265358979 / 180.0, s = std::sin(h);
	Q q;
	q.w = std::cos(h);
	(axis == 0 ? q.x : axis == 1 ? q.y : q.z) = s;
	return q;
}
inline void rotate(const Q &q, const double v[3], double out[3]) {
	const Q r = mul(mul(q, {v[0], v[1], v[2], 0.0}), conj(q));
	out[0] = r.x, out[1] = r.y, out[2] = r.z;
}

// A joint's turn and the root's place at a frame: each joint's rotation in the world (the composition down from the root, its channels' rotations in their order) and
// the root's position (file units).
inline void frameOf(const Bvh &b, int f, std::vector<Q> &world, double root[3]) {
	world.assign(b.joints.size(), Q());
	root[0] = root[1] = root[2] = 0.0;
	const double *v = &b.values[static_cast<std::size_t>(std::clamp(f, 0, b.frames - 1)) * b.width];
	for (std::size_t i = 0; i < b.joints.size(); ++i) {
		const Joint &j = b.joints[i];
		Q local;
		for (std::size_t c = 0; c < j.channels.size(); ++c) {
			const double x = v[j.first + static_cast<int>(c)];
			if (j.channels[c] >= 3) local = mul(local, axisAngle(j.channels[c] - 3, x));
			else if (j.parent < 0) root[j.channels[c]] = x;
		}
		world[i] = j.parent < 0 ? local : mul(world[static_cast<std::size_t>(j.parent)], local);
	}
}

// ---- names: one bone, however a skeleton spells it ----
// "mixamorig:LeftUpLeg", "LeftUpLeg", "leg.L", "thigh_l", "L_Thigh" -> "l thigh". `hasUpper` says the skeleton has upper-leg (and upper-arm) joints of their own, so
// that its "Leg" is the shin (Mixamo, CMU) rather than the whole leg (a simple rig).
inline std::string boneOf(std::string name, bool hasUpperLeg) {
	const std::size_t colon = name.rfind(':');
	if (colon != std::string::npos) name = name.substr(colon + 1);
	std::string s;
	for (char c : name) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	std::string side;
	const auto take = [&](const std::string &prefix, const std::string &suffix1, const std::string &suffix2, const std::string &which) {
		if (s.rfind(prefix, 0) == 0 && s.size() > prefix.size()) side = which, s = s.substr(prefix.size());
		else if (s.size() > suffix1.size() && s.compare(s.size() - suffix1.size(), suffix1.size(), suffix1) == 0) side = which, s = s.substr(0, s.size() - suffix1.size());
		else if (s.size() > suffix2.size() && s.compare(s.size() - suffix2.size(), suffix2.size(), suffix2) == 0) side = which, s = s.substr(0, s.size() - suffix2.size());
	};
	take("left", ".l", "_l", "l");
	if (side.empty()) take("right", ".r", "_r", "r");
	if (side.empty() && (s.rfind("l_", 0) == 0 || s.rfind("r_", 0) == 0)) side = s.substr(0, 1), s = s.substr(2);
	std::string part;
	for (char c : s)
		if (std::isalnum(static_cast<unsigned char>(c))) part += c;
	static const std::map<std::string, std::string> parts = {
		{"hips", "hips"}, {"pelvis", "hips"}, {"hip", "hips"}, {"root", "hips"}, {"lowerback", "spine"}, {"spine", "spine"}, {"spine0", "spine"}, {"spine1", "chest"}, {"spine2", "chest"},
		{"chest", "chest"}, {"upperchest", "chest"}, {"neck", "neck"}, {"neck1", "neck"}, {"head", "head"}, {"shoulder", "shoulder"}, {"clavicle", "shoulder"}, {"collar", "shoulder"},
		{"arm", "upperarm"}, {"uparm", "upperarm"}, {"upperarm", "upperarm"}, {"humerus", "upperarm"}, {"forearm", "forearm"}, {"lowerarm", "forearm"}, {"elbow", "forearm"},
		{"hand", "hand"}, {"wrist", "hand"}, {"upleg", "thigh"}, {"upperleg", "thigh"}, {"thigh", "thigh"}, {"femur", "thigh"}, {"shin", "shin"}, {"lowerleg", "shin"},
		{"calf", "shin"}, {"knee", "shin"}, {"foot", "foot"}, {"ankle", "foot"}, {"toebase", "toe"}, {"toe", "toe"}};
	std::string bone;
	if (part == "leg") bone = hasUpperLeg ? "shin" : "thigh";
	else if (const auto it = parts.find(part); it != parts.end()) bone = it->second;
	else return std::string();
	if ((bone == "hips" || bone == "spine" || bone == "chest" || bone == "neck" || bone == "head") != side.empty()) return side.empty() ? std::string() : side + " " + bone;
	return side.empty() ? bone : side + " " + bone;
}
inline bool hasUpperLegOf(const std::vector<std::string> &names) {
	for (const std::string &n : names) {
		std::string s;
		for (char c : n) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (s.find("upleg") != std::string::npos || s.find("thigh") != std::string::npos || s.find("upperleg") != std::string::npos) return true;
	}
	return false;
}

struct Options {
	double speed = 1.0;   // how fast it plays
	double turn = 0.0;    // degrees about the vertical: the capture turned to face the model's way (glTF's front is +z)
	bool travel = false;  // the hips move over the ground as captured (else only up and down: the film moves the character)
};

// The capture as a clip of `rig` named `name`: "" or what is wrong (no joint matched, ...). `matched` (if given) counts the model's bones that move.
inline std::string retarget(const gltf_skin::Rig &rig, const Bvh &b, const Options &o, const std::string &name, gltf_skin::Clip &clip, int *matched = nullptr) {
	using gltf_skin::Trs;
	std::vector<std::string> modelNames, bvhNames;
	for (const gltf_skin::Node &n : rig.nodes) modelNames.push_back(n.name);
	for (const Joint &j : b.joints) bvhNames.push_back(j.name);
	const bool modelUpper = hasUpperLegOf(modelNames), bvhUpper = hasUpperLegOf(bvhNames);
	std::map<std::string, int> bvhBone;   // bone -> the capture's joint (the first, highest one of that bone)
	for (std::size_t i = 0; i < b.joints.size(); ++i) {
		const std::string bone = boneOf(b.joints[i].name, bvhUpper);
		if (!bone.empty() && !bvhBone.count(bone)) bvhBone[bone] = static_cast<int>(i);
	}
	std::vector<int> source(rig.nodes.size(), -1);   // each model node's capture joint
	int count = 0, hips = -1;
	std::map<std::string, bool> used;
	for (int n : rig.order) {
		const std::string bone = boneOf(rig.nodes[static_cast<std::size_t>(n)].name, modelUpper);
		if (bone.empty() || used[bone] || !bvhBone.count(bone)) continue;
		used[bone] = true;
		source[static_cast<std::size_t>(n)] = bvhBone[bone];
		++count;
		if (bone == "hips") hips = n;
	}
	if (matched) *matched = count;
	if (count == 0) return "none of the capture's joints matched a bone of the model (their names: " + (bvhNames.empty() ? std::string() : bvhNames[0]) + " ...)";
	// the model's rest: each node's turn in the world (scale and translation do not turn bones), and its place
	const gltf_skin::Pose rest = gltf_skin::restPose(rig);
	std::vector<Q> restWorld(rig.nodes.size());
	for (int n : rig.order) {
		const Trs &t = rest[static_cast<std::size_t>(n)];
		const Q local{t.q[0], t.q[1], t.q[2], t.q[3]};
		const int p = rig.nodes[static_cast<std::size_t>(n)].parent;
		restWorld[static_cast<std::size_t>(n)] = p < 0 ? local : mul(restWorld[static_cast<std::size_t>(p)], local);
	}
	const std::vector<gltf_skin::Mat> restMat = gltf_skin::worldOf(rig, rest);
	// the two rests made one: each matched bone first turned from the way it points at the model's rest to the way its joint's bone points at the capture's (a model
	// standing arms down, a capture in a T-pose). A bone's way: to its first child, else (a capture) to its End Site, else (a model) to the middle of the mesh it moves.
	const Q yawRest = axisAngle(1, o.turn);
	const auto unit = [](double v[3]) {
		const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		if (l < 1e-9) return false;
		for (int k = 0; k < 3; ++k) v[k] /= l;
		return true;
	};
	const auto bvhWay = [&](int j, double out[3]) {
		for (std::size_t c = 0; c < b.joints.size(); ++c)
			if (b.joints[c].parent == j) {
				const double *o3 = b.joints[c].offset;
				double v[3] = {o3[0], o3[1], o3[2]};
				rotate(yawRest, v, out);
				return unit(out);
			}
		if (!b.joints[static_cast<std::size_t>(j)].hasEnd) return false;
		const double *e = b.joints[static_cast<std::size_t>(j)].end;
		double v[3] = {e[0], e[1], e[2]};
		rotate(yawRest, v, out);
		return unit(out);
	};
	const auto modelWay = [&](int n, double out[3]) {
		const gltf_skin::Mat &m = restMat[static_cast<std::size_t>(n)];
		for (int c : rig.nodes[static_cast<std::size_t>(n)].children) {
			const gltf_skin::Mat &mc = restMat[static_cast<std::size_t>(c)];
			out[0] = mc[12] - m[12], out[1] = mc[13] - m[13], out[2] = mc[14] - m[14];
			if (unit(out)) return true;
		}
		double sum[3] = {0, 0, 0}, wsum = 0.0;   // (a leaf bone: the middle of the vertices it holds most of, at rest)
		const std::vector<gltf_skin::Posed> posed = gltf_skin::posed(rig, rest);
		for (std::size_t p = 0; p < rig.pieces.size() && p < posed.size(); ++p) {
			const gltf_skin::Piece &pc = rig.pieces[p];
			if (pc.skin < 0) continue;
			const std::vector<int> &joints = rig.skins[static_cast<std::size_t>(pc.skin)].joints;
			for (std::size_t v = 0; v * 4 + 3 < pc.joints.size() && v * 3 + 2 < posed[p].positions.size(); ++v)
				for (int k = 0; k < 4; ++k) {
					const int ji = pc.joints[v * 4 + k];
					if (ji < 0 || ji >= static_cast<int>(joints.size()) || joints[static_cast<std::size_t>(ji)] != n || pc.weights[v * 4 + k] < 0.5) continue;
					for (int a = 0; a < 3; ++a) sum[a] += posed[p].positions[v * 3 + a];
					wsum += 1.0;
				}
		}
		if (wsum <= 0.0) return false;
		out[0] = sum[0] / wsum - m[12], out[1] = sum[1] / wsum - m[13], out[2] = sum[2] / wsum - m[14];
		return unit(out);
	};
	std::vector<Q> align(rig.nodes.size());   // (the shortest turn from the model's rest way to the capture's)
	for (std::size_t n = 0; n < rig.nodes.size(); ++n) {
		if (source[n] < 0 || rig.nodes[n].parent < 0) continue;   // (the hips keep their own: the whole body would turn)
		double from[3], to[3];
		if (!modelWay(static_cast<int>(n), from) || !bvhWay(source[n], to)) continue;
		const double c = from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
		double axis[3] = {from[1] * to[2] - from[2] * to[1], from[2] * to[0] - from[0] * to[2], from[0] * to[1] - from[1] * to[0]};
		if (c < -0.9999) continue;   // (opposite ways: no single shortest turn; left as it is)
		if (c > std::cos(30.0 * 3.14159265358979 / 180.0)) continue;   // (rests that differ a lot - arms down against a T-pose - are made one; a head's mesh leaning
		                                                                // past its bone, a foot's, is not a different rest)
		const double s = std::sqrt(std::max(0.0, 2.0 * (1.0 + c)));
		if (s < 1e-9) continue;
		align[n] = {axis[0] / s, axis[1] / s, axis[2] / s, 0.5 * s};
	}
	// the hips' scale: the model's hips height over the capture's
	std::vector<Q> w0;
	double root0[3];
	frameOf(b, 0, w0, root0);
	double k = 1.0;
	if (hips >= 0 && root0[1] > 1e-6) k = restMat[static_cast<std::size_t>(hips)][13] / root0[1];
	const Q yaw = axisAngle(1, o.turn);
	clip = gltf_skin::Clip();
	clip.name = name;
	const double step = b.frameTime / std::max(1e-6, o.speed);
	clip.length = step * std::max(0, b.frames - 1);
	std::vector<gltf_skin::Channel> rot(rig.nodes.size());
	gltf_skin::Channel move;
	move.node = hips, move.path = 0;
	for (int f = 0; f < b.frames; ++f) {
		std::vector<Q> bw;
		double root[3];
		frameOf(b, f, bw, root);
		std::vector<Q> world = restWorld;   // (the model's turns in the world at this frame)
		for (int n : rig.order) {
			const int p = rig.nodes[static_cast<std::size_t>(n)].parent;
			const Trs &t = rest[static_cast<std::size_t>(n)];
			Q local{t.q[0], t.q[1], t.q[2], t.q[3]};
			if (source[static_cast<std::size_t>(n)] >= 0) {
				// the joint's turn away from its rest (the capture's rest is no turn at all), turned to the model's way, laid on the bone's own rest turn in the world
				const Q d = mul(mul(yaw, bw[static_cast<std::size_t>(source[static_cast<std::size_t>(n)])]), conj(yaw));
				world[static_cast<std::size_t>(n)] = mul(d, mul(align[static_cast<std::size_t>(n)], restWorld[static_cast<std::size_t>(n)]));
				local = p < 0 ? world[static_cast<std::size_t>(n)] : mul(conj(world[static_cast<std::size_t>(p)]), world[static_cast<std::size_t>(n)]);
				gltf_skin::Channel &c = rot[static_cast<std::size_t>(n)];
				c.node = n, c.path = 1;
				c.times.push_back(f * step);
				c.values.insert(c.values.end(), {local.x, local.y, local.z, local.w});
			} else {
				world[static_cast<std::size_t>(n)] = p < 0 ? local : mul(world[static_cast<std::size_t>(p)], local);
			}
		}
		if (hips >= 0) {   // the hips: up and down (and over the ground, with `travel`) as captured, in the units of the hips' parent
			double dw[3] = {(root[0] - root0[0]) * k, (root[1] - root0[1]) * k, (root[2] - root0[2]) * k}, d[3];
			rotate(yaw, dw, d);
			if (!o.travel) d[0] = d[2] = 0.0;
			const int p = rig.nodes[static_cast<std::size_t>(hips)].parent;
			double local[3] = {d[0], d[1], d[2]};
			if (p >= 0) {   // (into the parent's frame: its 3 x 3 inverted)
				const gltf_skin::Mat &m = restMat[static_cast<std::size_t>(p)];
				const double a = m[0], b1 = m[4], c = m[8], e = m[1], f1 = m[5], g = m[9], h = m[2], i = m[6], jj = m[10];
				const double det = a * (f1 * jj - g * i) - b1 * (e * jj - g * h) + c * (e * i - f1 * h);
				if (std::fabs(det) > 1e-12) {
					const double inv[9] = {(f1 * jj - g * i) / det, (c * i - b1 * jj) / det, (b1 * g - c * f1) / det, (g * h - e * jj) / det, (a * jj - c * h) / det,
										   (c * e - a * g) / det, (e * i - f1 * h) / det, (b1 * h - a * i) / det, (a * f1 - b1 * e) / det};
					for (int r = 0; r < 3; ++r) local[r] = inv[r * 3] * d[0] + inv[r * 3 + 1] * d[1] + inv[r * 3 + 2] * d[2];
				}
			}
			const Trs &t = rest[static_cast<std::size_t>(hips)];
			move.times.push_back(f * step);
			move.values.insert(move.values.end(), {t.t[0] + local[0], t.t[1] + local[1], t.t[2] + local[2]});
		}
	}
	for (gltf_skin::Channel &c : rot)
		if (c.node >= 0) clip.channels.push_back(std::move(c));
	if (hips >= 0) clip.channels.push_back(std::move(move));
	return std::string();
}

}  // namespace mocap
}  // namespace cartoon
