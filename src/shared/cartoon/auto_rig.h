#pragma once
// auto_rig.h -- a skeleton for a model that has none (round 12: `rigged KIND FILE autorig`, and any .obj, .ply or unrigged .glb a `rigged` line names): a humanoid
// skeleton fitted inside the mesh, its skin weighted to the bones, and the clips a film needs (idle, walk, wave, nod), so a sculpt, a downloaded statue or an object
// made from a photo (tools/photo_to_mesh) walks, gestures and takes motion capture as a rigged character does.
//
//   the fit       the model stands on its lowest point facing +z (glTF's front), y up. Slices across it find its body's middle, its legs (two clusters of the slices
//                 below the hips, their gap the crotch) and its arms (sticking out at the shoulders: a T or an A pose, the hands at the furthest points; else hanging
//                 at its sides). The joints are a person's proportions of its height where nothing better is found. They are named as Mixamo names them (Hips,
//                 Spine, Spine1, Neck, Head, LeftArm, LeftForeArm, LeftHand, LeftUpLeg, LeftLeg, LeftFoot and the right ones), so `mocap` finds them.
//   the weights   each vertex goes to the bones nearest it (by its distance to each bone's segment in that bone's thicknesses - a chest is thick, an arm thin - up to
//                 four), never to a bone of the other side of the body - a leg's skin does not follow the other leg - and the legs only below the hips
//   the clips     idle (the arms down from a T pose, a slow breath), walk (two steps a second), wave (the right arm), nod
//
// It is a guess, not a rigger's work: a model in an odd pose, with a long robe or a tail, gets a skeleton that moves it roughly. Unit tested on a made-up figure.

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "../gltf_skin.h"

namespace cartoon {
namespace auto_rig {

using gltf_import::detail::Mat;

struct V3 {
	double x = 0.0, y = 0.0, z = 0.0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double len(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 unit(V3 a) {
	const double l = len(a);
	return l > 1e-12 ? a * (1.0 / l) : V3{0.0, 1.0, 0.0};
}
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// The joints, parents first; each one's bone runs to `tip` (its child's place, or an end of its own).
struct Joint {
	std::string name;
	int parent = -1;
	V3 at, tip;
	int side = 0;          // +1 the model's left (+x), -1 its right, 0 the middle
	double radius = 0.0;   // how thick the body is round it (a person's share of the height): distances to it are measured in these
};
struct Skeleton {
	std::vector<Joint> joints;
	bool tPose = false;   // (its arms out at the shoulders)
	double height = 0.0;
	double armsFrom = 0.0;   // how far out from the middle the arms' skin starts (inside it is the chest's)
	int find(const std::string &n) const {
		for (std::size_t i = 0; i < joints.size(); ++i)
			if (joints[i].name == n) return static_cast<int>(i);
		return -1;
	}
};

namespace detail {
inline double percentile(std::vector<double> v, double q) {
	if (v.empty()) return 0.0;
	const std::size_t k = std::min(v.size() - 1, static_cast<std::size_t>(q * static_cast<double>(v.size() - 1)));
	std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
	return v[k];
}
inline double segmentDistance(V3 p, V3 a, V3 b) {
	const V3 ab = b - a;
	const double t = std::clamp(dot(p - a, ab) / std::max(1e-12, dot(ab, ab)), 0.0, 1.0);
	return len(p - (a + ab * t));
}
// A quaternion (x, y, z, w) turning unit direction a onto b.
inline std::array<double, 4> swing(V3 a, V3 b) {
	a = unit(a), b = unit(b);
	const double c = dot(a, b);
	if (c < -0.999999) {
		const V3 axis = unit(cross(a, std::abs(a.x) < 0.9 ? V3{1.0, 0.0, 0.0} : V3{0.0, 1.0, 0.0}));
		return {axis.x, axis.y, axis.z, 0.0};
	}
	const V3 v = cross(a, b);
	const double s = std::sqrt((1.0 + c) * 2.0);
	return {v.x / s, v.y / s, v.z / s, 0.5 * s};
}
inline std::array<double, 4> axisAngle(V3 axis, double degrees) {
	axis = unit(axis);
	const double h = 0.5 * degrees * 3.14159265358979323846 / 180.0;
	return {axis.x * std::sin(h), axis.y * std::sin(h), axis.z * std::sin(h), std::cos(h)};
}
inline std::array<double, 4> quatMul(const std::array<double, 4> &a, const std::array<double, 4> &b) {
	return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0], a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
			a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}
}  // namespace detail

// The skeleton fitted to the points of a model standing on y = lo.y, facing +z.
inline Skeleton fit(const std::vector<V3> &points) {
	Skeleton sk;
	if (points.empty()) return sk;
	V3 lo = points[0], hi = points[0];
	for (const V3 &p : points) lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)}, hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
	const double H = std::max(1e-6, hi.y - lo.y);
	sk.height = H;
	const auto band = [&](double f0, double f1) {
		std::vector<V3> out;
		for (const V3 &p : points)
			if (p.y >= lo.y + f0 * H && p.y <= lo.y + f1 * H) out.push_back(p);
		return out;
	};
	// the body's middle (its chest's), and how wide it is at the waist
	const std::vector<V3> chest = band(0.62, 0.74);
	std::vector<double> xs, zs;
	for (const V3 &p : chest) xs.push_back(p.x), zs.push_back(p.z);
	const double cx = xs.empty() ? 0.5 * (lo.x + hi.x) : detail::percentile(xs, 0.5), cz = zs.empty() ? 0.5 * (lo.z + hi.z) : detail::percentile(zs, 0.5);
	const auto at = [&](double x, double f, double z = 0.0) { return V3{cx + x, lo.y + f * H, cz + z}; };
	// the legs: in the slices between the shins and the thighs, the points' distance from the middle on each side (a gap between them: two legs)
	std::vector<double> left, right;
	for (const V3 &p : band(0.12, 0.38)) (p.x >= cx ? left : right).push_back(std::abs(p.x - cx));
	double legX = 0.09 * H;
	if (!left.empty() && !right.empty()) {
		const double inner = std::min(detail::percentile(left, 0.05), detail::percentile(right, 0.05)), mid = 0.5 * (detail::percentile(left, 0.5) + detail::percentile(right, 0.5));
		if (inner > 0.005 * H) legX = std::clamp(mid, 0.04 * H, 0.16 * H);   // (a gap: the legs' own middles)
	}
	// the arms: out at the shoulders (a T or an A pose) when the shoulder slices reach far beyond the chest
	std::vector<double> waist;
	for (const V3 &p : band(0.55, 0.60)) waist.push_back(std::abs(p.x - cx));
	const double waistX = std::max(0.06 * H, detail::percentile(waist, 0.6));
	std::vector<double> reach;
	for (const V3 &p : band(0.60, 0.86)) reach.push_back(std::abs(p.x - cx));
	const double armReach = detail::percentile(reach, 0.995);
	sk.tPose = armReach > waistX + 0.22 * H;
	std::vector<double> forearms;   // (arms hanging: the outer edge of the forearms beside the hips, an arm's thickness in from it)
	for (const V3 &p : band(0.50, 0.64)) forearms.push_back(std::abs(p.x - cx));
	const double armX = std::clamp(detail::percentile(forearms, 0.98) - 0.02 * H, 0.08 * H, 0.18 * H);
	const double shoulderX = sk.tPose ? std::clamp(waistX * 1.05, 0.08 * H, 0.14 * H) : armX - 0.01 * H, shoulderY = 0.80;
	V3 elbow[2], wrist[2], hand[2];
	for (int s = 0; s < 2; ++s) {
		const double sign = s == 0 ? 1.0 : -1.0;
		if (sk.tPose) {   // (out to the furthest point: its height the height of the arm's points out there)
			double sumY = 0.0;
			int n = 0;
			for (const V3 &p : band(0.45, 0.95))
				if ((p.x - cx) * sign > 0.8 * armReach) sumY += p.y, ++n;
			const double handY = n > 0 ? sumY / n : lo.y + shoulderY * H;
			const V3 sh = at(sign * shoulderX, shoulderY), end = V3{cx + sign * armReach, handY, cz};
			elbow[s] = sh + (end - sh) * 0.45, wrist[s] = sh + (end - sh) * 0.86, hand[s] = end;
		} else {
			double tip = 1e300;   // (the hand's tip: the lowest point of the arm's outer skin; none found, a person's)
			for (const V3 &p : band(0.25, 0.70))
				if ((p.x - cx) * sign > armX + 0.004 * H) tip = std::min(tip, p.y);
			tip = tip > 1e299 ? lo.y + 0.40 * H : std::max(tip, lo.y + 0.25 * H);
			const double f = (tip - lo.y) / H, w = std::max(f + 0.07, 0.40), e = 0.5 * (shoulderY + w) + 0.01;
			elbow[s] = at(sign * armX, e), wrist[s] = at(sign * (armX + 0.005 * H), w), hand[s] = at(sign * (armX + 0.008 * H), f);
		}
	}
	sk.armsFrom = shoulderX - 1.5 * 0.028 * H;
	const auto add = [&](const char *name, int parent, V3 p, V3 tip, int side) {
		static const std::map<std::string, double> thick = {{"Hips", 0.09}, {"Spine", 0.09}, {"Spine1", 0.09}, {"Neck", 0.035}, {"Head", 0.065}, {"Arm", 0.028}, {"ForeArm", 0.024},
															 {"Hand", 0.022}, {"UpLeg", 0.045}, {"Leg", 0.035}, {"Foot", 0.03}};
		std::string part = name;
		if (part.rfind("Left", 0) == 0) part = part.substr(4);
		if (part.rfind("Right", 0) == 0) part = part.substr(5);
		const auto it = thick.find(part);
		sk.joints.push_back({name, parent, p, tip, side, (it == thick.end() ? 0.05 : it->second) * H});
		return static_cast<int>(sk.joints.size() - 1);
	};
	const int hips = add("Hips", -1, at(0, 0.52), at(0, 0.60), 0);
	const int spine = add("Spine", hips, at(0, 0.60), at(0, 0.72), 0);
	const int spine1 = add("Spine1", spine, at(0, 0.72), at(0, 0.84), 0);
	const int neck = add("Neck", spine1, at(0, 0.84), at(0, 0.88), 0);
	add("Head", neck, at(0, 0.88), at(0, 1.0), 0);
	for (int s = 0; s < 2; ++s) {
		const double sign = s == 0 ? 1.0 : -1.0;
		const std::string side = s == 0 ? "Left" : "Right";
		const int sideId = s == 0 ? 1 : -1;
		const int arm = add((side + "Arm").c_str(), spine1, at(sign * shoulderX, shoulderY), elbow[s], sideId);
		const int fore = add((side + "ForeArm").c_str(), arm, elbow[s], wrist[s], sideId);
		add((side + "Hand").c_str(), fore, wrist[s], hand[s], sideId);
		const int up = add((side + "UpLeg").c_str(), hips, at(sign * legX, 0.50), at(sign * legX, 0.28), sideId);
		const int leg = add((side + "Leg").c_str(), up, at(sign * legX, 0.28), at(sign * legX, 0.05), sideId);
		add((side + "Foot").c_str(), leg, at(sign * legX, 0.05), at(sign * legX, 0.0, 0.10 * H), sideId);
	}
	return sk;
}

// Each vertex's four joints and weights (see the top): its distance to each bone in that bone's thicknesses, so the side of a chest goes with the chest and not
// with the arm hanging beside it.
inline void weigh(const Skeleton &sk, const std::vector<V3> &points, std::vector<int> &joints, std::vector<double> &weights) {
	const double H = sk.height, sigma = 0.10;
	const double hipsY = sk.joints[static_cast<std::size_t>(sk.find("Hips"))].at.y, cx = sk.joints[0].at.x;
	joints.assign(points.size() * 4, 0), weights.assign(points.size() * 4, 0.0);
	std::vector<std::pair<double, int>> d;
	for (std::size_t v = 0; v < points.size(); ++v) {
		const V3 &p = points[v];
		const int side = p.x - cx > 0.02 * H ? 1 : p.x - cx < -0.02 * H ? -1 : 0;
		d.clear();
		for (std::size_t j = 0; j < sk.joints.size(); ++j) {
			const Joint &b = sk.joints[j];
			if (side != 0 && b.side != 0 && b.side != side) continue;
			const bool leg = b.name.find("Leg") != std::string::npos || b.name.find("Foot") != std::string::npos;
			if (leg && p.y > hipsY + 0.02 * H) continue;
			const bool arm = b.name.find("Arm") != std::string::npos || b.name.find("Hand") != std::string::npos;
			if (arm && std::abs(p.x - cx) < sk.armsFrom) continue;   // (the chest's side is the chest's, though an arm hangs beside it)
			d.push_back({detail::segmentDistance(p, b.at, b.tip) / std::max(1e-9, b.radius), static_cast<int>(j)});
		}
		std::sort(d.begin(), d.end());
		double sum = 0.0;
		for (std::size_t k = 0; k < std::min<std::size_t>(4, d.size()); ++k) {
			const double w = d[k].first - d[0].first > 0.6 ? 0.0 : std::exp(-(d[k].first - d[0].first) / sigma);   // (a bone well beyond the nearest has none: a hand hanging by the hip does not take the hip with it)
			joints[v * 4 + k] = d[k].second, weights[v * 4 + k] = w, sum += w;
		}
		for (std::size_t k = 0; k < 4; ++k) weights[v * 4 + k] /= std::max(1e-12, sum);
	}
}

// The rig: `rig`'s pieces (each placed in the model's world by its node; skinned now to the skeleton), its materials and pictures kept, its old nodes, skins and clips
// replaced. "" or why it could not.
inline std::string rig(gltf_skin::Rig &rig) {
	using namespace gltf_import::detail;
	const std::vector<Mat> world = gltf_skin::worldOf(rig, gltf_skin::restPose(rig));
	std::vector<V3> points;
	for (gltf_skin::Piece &piece : rig.pieces) {   // (into the model's world: an unrigged model's parts hang from their nodes)
		if (piece.skin < 0 && piece.node >= 0 && static_cast<std::size_t>(piece.node) < world.size()) {
			const Mat &m = world[static_cast<std::size_t>(piece.node)];
			for (std::size_t v = 0; v + 2 < piece.positions.size(); v += 3) {
				double a[3], b[3];
				gltf_skin::detail::transformPoint(m, &piece.positions[v], a);
				gltf_skin::detail::transformDir(m, &piece.normals[v], b);
				for (int e = 0; e < 3; ++e) piece.positions[v + static_cast<std::size_t>(e)] = a[e], piece.normals[v + static_cast<std::size_t>(e)] = b[e];
			}
		}
		for (std::size_t v = 0; v + 2 < piece.positions.size(); v += 3) points.push_back({piece.positions[v], piece.positions[v + 1], piece.positions[v + 2]});
	}
	if (points.size() < 8) return "the model has too few points to rig";
	const Skeleton sk = fit(points);
	if (!(sk.height > 1e-6)) return "the model has no height to rig";
	// the nodes: the joints (rest translations from their parents), then one for the mesh
	rig.nodes.clear(), rig.order.clear(), rig.skins.clear(), rig.clips.clear();
	gltf_skin::Skin skin;
	for (std::size_t j = 0; j < sk.joints.size(); ++j) {
		const Joint &jt = sk.joints[j];
		gltf_skin::Node n;
		n.name = jt.name;
		n.parent = jt.parent;
		const V3 rel = jt.parent < 0 ? jt.at : jt.at - sk.joints[static_cast<std::size_t>(jt.parent)].at;
		n.rest.t[0] = rel.x, n.rest.t[1] = rel.y, n.rest.t[2] = rel.z;
		rig.nodes.push_back(n);
		if (jt.parent >= 0) rig.nodes[static_cast<std::size_t>(jt.parent)].children.push_back(static_cast<int>(j));
		rig.order.push_back(static_cast<int>(j));
		skin.joints.push_back(static_cast<int>(j));
		Mat ib = identity();
		ib[12] = -jt.at.x, ib[13] = -jt.at.y, ib[14] = -jt.at.z;
		skin.inverseBind.push_back(ib);
	}
	rig.skins.push_back(skin);
	for (gltf_skin::Piece &piece : rig.pieces) {
		std::vector<V3> own;
		for (std::size_t v = 0; v + 2 < piece.positions.size(); v += 3) own.push_back({piece.positions[v], piece.positions[v + 1], piece.positions[v + 2]});
		weigh(sk, own, piece.joints, piece.weights);
		piece.skin = 0, piece.node = 0;
	}
	// the clips (rotations of the joints, quaternions x y z w)
	const auto J = [&](const char *n) { return sk.find(n); };
	const auto clip = [&](const char *name, double length, const std::vector<std::pair<int, std::vector<std::pair<double, std::array<double, 4>>>>> &tracks) {
		gltf_skin::Clip c;
		c.name = name, c.length = length;
		for (const auto &[joint, keys] : tracks) {
			if (joint < 0) continue;
			gltf_skin::Channel ch;
			ch.node = joint, ch.path = 1;
			for (const auto &[t, q] : keys) ch.times.push_back(t), ch.values.insert(ch.values.end(), q.begin(), q.end());
			c.channels.push_back(std::move(ch));
		}
		rig.clips.push_back(std::move(c));
	};
	const V3 X{1.0, 0.0, 0.0}, Y{0.0, 1.0, 0.0}, Z{0.0, 0.0, 1.0};
	// the arms from where they rest to hanging down a little out from the body (a T pose's lowered; arms already down stay)
	std::array<double, 4> down[2];
	for (int s = 0; s < 2; ++s) {
		const Joint &arm = sk.joints[static_cast<std::size_t>(J(s == 0 ? "LeftArm" : "RightArm"))];
		down[s] = detail::swing(arm.tip - arm.at, V3{s == 0 ? 0.18 : -0.18, -1.0, 0.0});
	}
	const auto keys = [](std::initializer_list<std::pair<double, std::array<double, 4>>> k) { return std::vector<std::pair<double, std::array<double, 4>>>(k); };
	const auto arms = [&](int s, double swingDeg) { return detail::quatMul(detail::axisAngle(X, swingDeg), down[s]); };
	{   // idle: the arms down, a slow breath of the chest
		std::vector<std::pair<int, std::vector<std::pair<double, std::array<double, 4>>>>> tr;
		tr.push_back({J("LeftArm"), keys({{0.0, down[0]}, {3.0, down[0]}})});
		tr.push_back({J("RightArm"), keys({{0.0, down[1]}, {3.0, down[1]}})});
		tr.push_back({J("Spine1"), keys({{0.0, detail::axisAngle(X, 0.0)}, {1.5, detail::axisAngle(X, -2.0)}, {3.0, detail::axisAngle(X, 0.0)}})});
		clip("idle", 3.0, tr);
	}
	{   // walk: two steps a second
		std::vector<std::pair<int, std::vector<std::pair<double, std::array<double, 4>>>>> tr;
		std::vector<std::pair<double, std::array<double, 4>>> l, r, kl, kr, al, ar;
		for (int i = 0; i <= 8; ++i) {
			const double t = i / 8.0, ph = 2.0 * 3.14159265358979323846 * t;
			l.push_back({t, detail::axisAngle(X, -26.0 * std::sin(ph))});
			r.push_back({t, detail::axisAngle(X, 26.0 * std::sin(ph))});
			kl.push_back({t, detail::axisAngle(X, 35.0 * std::max(0.0, std::sin(ph - 1.2)))});
			kr.push_back({t, detail::axisAngle(X, 35.0 * std::max(0.0, -std::sin(ph - 1.2)))});
			al.push_back({t, arms(0, 20.0 * std::sin(ph))});
			ar.push_back({t, arms(1, -20.0 * std::sin(ph))});
		}
		tr = {{J("LeftUpLeg"), l}, {J("RightUpLeg"), r}, {J("LeftLeg"), kl}, {J("RightLeg"), kr}, {J("LeftArm"), al}, {J("RightArm"), ar}};
		clip("walk", 1.0, tr);
	}
	{   // wave: the right arm up and waving
		const Joint &arm = sk.joints[static_cast<std::size_t>(J("RightArm"))];
		const std::array<double, 4> up = detail::swing(arm.tip - arm.at, V3{-0.55, 0.85, 0.15});
		std::vector<std::pair<double, std::array<double, 4>>> a, f;
		for (double t : {0.0, 0.4, 0.7, 1.0, 1.3, 1.6, 2.0}) {
			const bool raised = t > 0.2 && t < 1.8;
			a.push_back({t, raised ? up : down[1]});
			f.push_back({t, detail::axisAngle(Z, raised ? (std::fmod(t, 0.6) < 0.3 ? 35.0 : 5.0) : 0.0)});
		}
		clip("wave", 2.0, {{J("RightArm"), a}, {J("RightForeArm"), f}, {J("LeftArm"), keys({{0.0, down[0]}, {2.0, down[0]}})}});
	}
	clip("nod", 1.0, {{J("Head"), keys({{0.0, detail::axisAngle(X, 0.0)}, {0.25, detail::axisAngle(X, 16.0)}, {0.5, detail::axisAngle(X, 0.0)}, {0.75, detail::axisAngle(X, 16.0)}, {1.0, detail::axisAngle(X, 0.0)}})},
					  {J("LeftArm"), keys({{0.0, down[0]}, {1.0, down[0]}})},
					  {J("RightArm"), keys({{0.0, down[1]}, {1.0, down[1]}})}});
	(void)Y;
	rig.notes.push_back(std::string("auto-rigged: ") + (sk.tPose ? "arms out (T or A pose)" : "arms down") + ", " + std::to_string(sk.joints.size()) + " joints");
	return std::string();
}

}  // namespace auto_rig
}  // namespace cartoon
