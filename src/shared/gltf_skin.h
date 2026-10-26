#pragma once
// gltf_skin.h -- a rigged glTF 2.0 character (.glb or .gltf): its skeleton, its skins and its animation clips, and the model posed at a moment of a clip (or of several,
// blended). gltf_import.h reads the file (its materials, pictures, buffers); this reads what that leaves out:
//
//   nodes       each node's rest translation / rotation / scale (or matrix) and its children: the skeleton is nodes like any other
//   skins       the joints (nodes) a mesh's vertices hang from and each joint's inverse bind matrix
//   meshes      each primitive's positions, normals, texture coordinates and, for a skinned mesh, the four joints (JOINTS_0) and weights (WEIGHTS_0) of each vertex; a mesh
//               without a skin follows its node (a robot of rigid parts)
//   animations  clips of channels: a node's translation, rotation or scale over time (LINEAR, STEP; CUBICSPLINE is read through its values, as LINEAR)
//
// A pose is every node's translation, rotation and scale; a clip gives one at a time (looped or held), poses blend (lerp, and a normalised lerp of the rotations), and the
// posed model is each skinned vertex moved by its joints (world * inverse bind, weighted) and each rigid part by its node. Morph targets are not read. The file is not
// trusted: every index is checked before it is used.
//
// Header-only, standard library only; unit tested.

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gltf_import.h"

namespace gltf_skin {

using gltf_import::detail::Mat;

struct Trs {
	double t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
};
struct Node {
	std::string name;
	Trs rest;
	bool hasMatrix = false;   // (a node given by a matrix: not animated)
	Mat matrix{};
	std::vector<int> children;
	int parent = -1;
};
struct Skin {
	std::vector<int> joints;          // nodes
	std::vector<Mat> inverseBind;     // one each (identity where the file gives none)
};
struct Channel {
	int node = -1;
	int path = 0;                     // 0 translation, 1 rotation, 2 scale
	std::vector<double> times, values;   // values: 3 or 4 per time
	bool step = false;
};
struct Clip {
	std::string name;
	double length = 0.0;
	std::vector<Channel> channels;
};
// The triangles of one primitive in its mesh's space, with what moves them.
struct Piece {
	int material = -1;                // gltf_import::Model::materials index (-1: plain)
	std::vector<double> positions, normals, uvs;   // 3, 3 and 2 a vertex
	std::vector<uint32_t> indices;
	int node = -1;                    // the node it hangs from (a rigid piece)
	int skin = -1;                    // its skin (a skinned piece)
	std::vector<int> joints;          // 4 a vertex: indices into its skin's joints
	std::vector<double> weights;      // 4 a vertex, summing to 1
};
struct Rig {
	std::vector<Node> nodes;
	std::vector<int> order;           // the nodes parents first
	std::vector<Skin> skins;
	std::vector<Clip> clips;
	std::vector<Piece> pieces;
	std::vector<gltf_import::Material> materials;
	std::vector<gltf_import::Image> images;
	std::vector<std::string> notes;
	int clipNamed(const std::string &name) const {
		for (std::size_t i = 0; i < clips.size(); ++i)
			if (clips[i].name == name) return static_cast<int>(i);
		return -1;
	}
};
struct Result {
	bool ok = false;
	std::string error;
	Rig rig;
};
using Pose = std::vector<Trs>;

namespace detail {
using namespace gltf_import::detail;

inline void normalise(double q[4]) {
	const double l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	if (l < 1e-12) {
		q[0] = q[1] = q[2] = 0.0, q[3] = 1.0;
		return;
	}
	for (int k = 0; k < 4; ++k) q[k] /= l;
}
// a toward b by w: positions and scales lerped, rotations nlerped the short way round
inline void blendInto(Trs &a, const Trs &b, double w) {
	for (int k = 0; k < 3; ++k) a.t[k] += (b.t[k] - a.t[k]) * w, a.s[k] += (b.s[k] - a.s[k]) * w;
	const double dot = a.q[0] * b.q[0] + a.q[1] * b.q[1] + a.q[2] * b.q[2] + a.q[3] * b.q[3];
	const double sign = dot < 0.0 ? -1.0 : 1.0;
	for (int k = 0; k < 4; ++k) a.q[k] += (sign * b.q[k] - a.q[k]) * w;
	normalise(a.q);
}
inline void transformPoint(const Mat &m, const double *p, double *out) {
	for (int r = 0; r < 3; ++r) out[r] = m[static_cast<size_t>(r)] * p[0] + m[static_cast<size_t>(4 + r)] * p[1] + m[static_cast<size_t>(8 + r)] * p[2] + m[static_cast<size_t>(12 + r)];
}
inline void transformDir(const Mat &m, const double *d, double *out) {
	for (int r = 0; r < 3; ++r) out[r] = m[static_cast<size_t>(r)] * d[0] + m[static_cast<size_t>(4 + r)] * d[1] + m[static_cast<size_t>(8 + r)] * d[2];
}
}  // namespace detail

// The rig in `bytes` (a .glb, or a .gltf's JSON whose buffers and pictures `fetch` supplies).
inline Result load(const std::string &bytes, const gltf_import::FileFetcher &fetch = gltf_import::FileFetcher()) {
	using namespace detail;
	Result r;
	gltf_import::Result base = gltf_import::parse(bytes, fetch);
	if (!base.ok || !base.doc) {
		r.error = base.error.empty() ? "the file could not be read" : base.error;
		return r;
	}
	Rig &rig = r.rig;
	rig.materials = base.model.materials;
	rig.images = std::move(base.model.images);
	Doc &d = *base.doc;
	const Json *nodes = get(d.root, "nodes");
	const std::size_t count = nodes && nodes->type == Json::Type::Arr ? std::min<std::size_t>(nodes->a.size(), 100000) : 0;
	rig.nodes.resize(count);
	for (std::size_t i = 0; i < count; ++i) {
		const Json &n = nodes->a[i];
		Node &node = rig.nodes[i];
		node.name = strOr(get(n, "name"));
		if (const Json *m = get(n, "matrix"); m && m->type == Json::Type::Arr && m->a.size() == 16) {
			node.hasMatrix = true;
			node.matrix = nodeMatrix(n);
		}
		const auto fill = [](const Json *j, double *out, std::size_t k) {
			if (!j || j->type != Json::Type::Arr || j->a.size() != k) return;
			for (std::size_t i = 0; i < k; ++i) out[i] = numOr(&j->a[i], out[i]);
		};
		fill(get(n, "translation"), node.rest.t, 3);
		fill(get(n, "rotation"), node.rest.q, 4);
		fill(get(n, "scale"), node.rest.s, 3);
		normalise(node.rest.q);
		if (const Json *c = get(n, "children"))
			for (const Json &k : c->a) {
				const long child = intOr(&k, -1);
				if (child >= 0 && static_cast<std::size_t>(child) < count && child != static_cast<long>(i) && rig.nodes[static_cast<std::size_t>(child)].parent < 0) {
					node.children.push_back(static_cast<int>(child));
					rig.nodes[static_cast<std::size_t>(child)].parent = static_cast<int>(i);
				}
			}
	}
	// parents first (a loop in the file is cut: what is not reached from a root is left out)
	{
		std::vector<int> stack;
		for (std::size_t i = 0; i < count; ++i)
			if (rig.nodes[i].parent < 0) stack.push_back(static_cast<int>(i));
		std::vector<bool> seen(count, false);
		while (!stack.empty()) {
			const int n = stack.back();
			stack.pop_back();
			if (seen[static_cast<std::size_t>(n)]) continue;
			seen[static_cast<std::size_t>(n)] = true;
			rig.order.push_back(n);
			for (int c : rig.nodes[static_cast<std::size_t>(n)].children) stack.push_back(c);
		}
	}
	// skins
	if (const Json *skins = get(d.root, "skins"))
		for (const Json &s : skins->a) {
			Skin skin;
			if (const Json *joints = get(s, "joints"))
				for (const Json &j : joints->a) {
					const long n = intOr(&j, -1);
					skin.joints.push_back(n >= 0 && static_cast<std::size_t>(n) < count ? static_cast<int>(n) : -1);
				}
			skin.inverseBind.assign(skin.joints.size(), identity());
			if (get(s, "inverseBindMatrices")) {
				std::vector<double> m;
				int components = 0;
				size_t n = 0;
				std::string err;
				if (readAccessor(d, intOr(get(s, "inverseBindMatrices"), -1), m, components, n, err) && components == 16)
					for (std::size_t k = 0; k < std::min(n, skin.joints.size()); ++k)
						for (std::size_t e = 0; e < 16; ++e) skin.inverseBind[k][e] = m[k * 16 + e];
				else
					rig.notes.push_back("a skin's inverse bind matrices could not be read: its joints are taken at rest");
			}
			rig.skins.push_back(std::move(skin));
		}
	// meshes: each primitive of each node that has one
	const Json *meshes = get(d.root, "meshes");
	for (std::size_t i = 0; i < count; ++i) {
		const Json &n = nodes->a[i];
		const long meshIndex = intOr(get(n, "mesh"), -1);
		const Json *mesh = at(meshes, meshIndex);
		const Json *primitives = mesh ? get(*mesh, "primitives") : nullptr;
		if (!primitives) continue;
		const long skinIndex = intOr(get(n, "skin"), -1);
		for (const Json &p : primitives->a) {
			const Json *attributes = get(p, "attributes");
			if (!attributes || !get(*attributes, "POSITION") || intOr(get(p, "mode"), 4) != 4) continue;
			Piece piece;
			piece.node = static_cast<int>(i);
			piece.material = static_cast<int>(intOr(get(p, "material"), -1));
			int pc = 0, nc = 0, uc = 0, ic = 0, jc = 0, wc = 0;
			size_t vn = 0, nn = 0, un = 0, in = 0, jn = 0, wn = 0;
			std::string err;
			if (!readAccessor(d, intOr(get(*attributes, "POSITION"), -1), piece.positions, pc, vn, err) || pc != 3) {
				r.error = "a mesh's positions could not be read: " + err;
				return r;
			}
			if (!(get(*attributes, "NORMAL") && readAccessor(d, intOr(get(*attributes, "NORMAL"), -1), piece.normals, nc, nn, err) && nc == 3 && nn == vn)) piece.normals.assign(vn * 3, 0.0);
			if (!(get(*attributes, "TEXCOORD_0") && readAccessor(d, intOr(get(*attributes, "TEXCOORD_0"), -1), piece.uvs, uc, un, err) && uc == 2 && un == vn)) piece.uvs.assign(vn * 2, 0.0);
			std::vector<double> idx;
			if (get(p, "indices") && readAccessor(d, intOr(get(p, "indices"), -1), idx, ic, in, err) && ic == 1) {
				for (double v : idx) {
					if (!(v >= 0.0) || v >= static_cast<double>(vn)) {
						r.error = "a mesh's indices point outside its vertices";
						return r;
					}
					piece.indices.push_back(static_cast<uint32_t>(v));
				}
			} else {
				for (size_t k = 0; k < vn; ++k) piece.indices.push_back(static_cast<uint32_t>(k));
			}
			piece.indices.resize(piece.indices.size() / 3 * 3);
			std::vector<double> joints, weights;
			if (skinIndex >= 0 && static_cast<std::size_t>(skinIndex) < rig.skins.size() && get(*attributes, "JOINTS_0") && get(*attributes, "WEIGHTS_0") &&
				readAccessor(d, intOr(get(*attributes, "JOINTS_0"), -1), joints, jc, jn, err) && jc == 4 && jn == vn &&
				readAccessor(d, intOr(get(*attributes, "WEIGHTS_0"), -1), weights, wc, wn, err) && wc == 4 && wn == vn) {
				const std::size_t jointCount = rig.skins[static_cast<std::size_t>(skinIndex)].joints.size();
				piece.skin = static_cast<int>(skinIndex);
				piece.joints.resize(vn * 4);
				piece.weights.resize(vn * 4);
				for (std::size_t v = 0; v < vn; ++v) {
					double sum = 0.0;
					for (std::size_t k = 0; k < 4; ++k) {
						const double j = joints[v * 4 + k], w = std::max(0.0, weights[v * 4 + k]);
						const bool good = j >= 0.0 && j < static_cast<double>(jointCount);
						piece.joints[v * 4 + k] = good ? static_cast<int>(j) : 0;
						piece.weights[v * 4 + k] = good ? w : 0.0;
						sum += piece.weights[v * 4 + k];
					}
					for (std::size_t k = 0; k < 4; ++k) piece.weights[v * 4 + k] = sum > 1e-9 ? piece.weights[v * 4 + k] / sum : (k == 0 ? 1.0 : 0.0);
				}
			}
			rig.pieces.push_back(std::move(piece));
		}
	}
	if (rig.pieces.empty()) {
		r.error = "the model has no triangles";
		return r;
	}
	// clips
	if (const Json *animations = get(d.root, "animations"))
		for (std::size_t a = 0; a < animations->a.size(); ++a) {
			const Json &anim = animations->a[a];
			Clip clip;
			clip.name = strOr(get(anim, "name"), "clip " + std::to_string(a + 1));
			for (char &c : clip.name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			const Json *samplers = get(anim, "samplers");
			if (const Json *channels = get(anim, "channels"))
				for (const Json &ch : channels->a) {
					const Json *target = get(ch, "target");
					const Json *sampler = at(samplers, intOr(get(ch, "sampler"), -1));
					if (!target || !sampler) continue;
					const std::string path = strOr(get(*target, "path"));
					Channel c;
					c.node = static_cast<int>(intOr(get(*target, "node"), -1));
					c.path = path == "translation" ? 0 : path == "rotation" ? 1 : path == "scale" ? 2 : -1;
					if (c.path < 0 || c.node < 0 || static_cast<std::size_t>(c.node) >= count) continue;   // (morph weights are not read)
					const std::string interpolation = strOr(get(*sampler, "interpolation"), "LINEAR");
					c.step = interpolation == "STEP";
					int tc = 0, vc = 0;
					size_t tn = 0, vn = 0;
					std::string err;
					std::vector<double> values;
					if (!readAccessor(d, intOr(get(*sampler, "input"), -1), c.times, tc, tn, err) || tc != 1 || tn == 0) continue;
					if (!readAccessor(d, intOr(get(*sampler, "output"), -1), values, vc, vn, err) || vc != (c.path == 1 ? 4 : 3)) continue;
					if (interpolation == "CUBICSPLINE") {   // (in-tangent, value, out-tangent for each time: its values)
						if (vn != tn * 3) continue;
						for (size_t k = 0; k < tn; ++k) c.values.insert(c.values.end(), values.begin() + static_cast<std::ptrdiff_t>((k * 3 + 1) * static_cast<size_t>(vc)),
																		values.begin() + static_cast<std::ptrdiff_t>((k * 3 + 2) * static_cast<size_t>(vc)));
						rig.notes.push_back("cubic spline animation is read through its values (as linear)");
					} else {
						if (vn != tn) continue;
						c.values = std::move(values);
					}
					clip.length = std::max(clip.length, c.times.back());
					clip.channels.push_back(std::move(c));
				}
			if (!clip.channels.empty()) rig.clips.push_back(std::move(clip));
		}
	r.ok = true;
	return r;
}

// The rest pose: every node as the file sets it.
inline Pose restPose(const Rig &rig) {
	Pose p(rig.nodes.size());
	for (std::size_t i = 0; i < rig.nodes.size(); ++i) p[i] = rig.nodes[i].rest;
	return p;
}

// A clip at `t` (seconds; looped when `loop`, else held at its ends) blended into `pose` by `weight` (1: the clip's pose where it moves a node).
inline void apply(const Rig &rig, const Clip &clip, double t, Pose &pose, double weight = 1.0, bool loop = true) {
	if (weight <= 0.0 || clip.length <= 0.0) return;
	t = loop ? std::fmod(std::fmod(t, clip.length) + clip.length, clip.length) : std::clamp(t, 0.0, clip.length);
	for (const Channel &c : clip.channels) {
		if (static_cast<std::size_t>(c.node) >= pose.size() || rig.nodes[static_cast<std::size_t>(c.node)].hasMatrix) continue;
		const std::size_t n = c.times.size(), k = c.path == 1 ? 4 : 3;
		std::size_t i = 0;
		while (i + 1 < n && c.times[i + 1] <= t) ++i;
		const double t0 = c.times[i], t1 = i + 1 < n ? c.times[i + 1] : t0;
		const double u = c.step || t1 <= t0 || t <= t0 ? 0.0 : std::min(1.0, (t - t0) / (t1 - t0));
		Trs target = pose[static_cast<std::size_t>(c.node)];
		double *dst = c.path == 0 ? target.t : c.path == 1 ? target.q : target.s;
		const double *a = &c.values[i * k], *b = &c.values[(i + 1 < n ? i + 1 : i) * k];
		if (c.path == 1) {
			const double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
			const double sign = dot < 0.0 ? -1.0 : 1.0;
			for (std::size_t e = 0; e < 4; ++e) dst[e] = a[e] + (sign * b[e] - a[e]) * u;
			detail::normalise(dst);
		} else {
			for (std::size_t e = 0; e < 3; ++e) dst[e] = a[e] + (b[e] - a[e]) * u;
		}
		detail::blendInto(pose[static_cast<std::size_t>(c.node)], target, weight);
	}
}

// Every node's world matrix for a pose.
inline std::vector<Mat> worldOf(const Rig &rig, const Pose &pose) {
	using namespace gltf_import::detail;
	std::vector<Mat> world(rig.nodes.size(), identity());
	for (int n : rig.order) {
		const Node &node = rig.nodes[static_cast<std::size_t>(n)];
		const Trs &p = pose[static_cast<std::size_t>(n)];
		const Mat local = node.hasMatrix ? node.matrix : fromTrs(p.t, p.q, p.s);
		world[static_cast<std::size_t>(n)] = node.parent >= 0 ? multiply(world[static_cast<std::size_t>(node.parent)], local) : local;
	}
	return world;
}

// The model posed: each piece's vertices and normals where the pose puts them (a skinned piece by its joints, a rigid one by its node), in the model's world.
struct Posed {
	int material = -1;
	std::vector<double> positions, normals, uvs;
	std::vector<uint32_t> indices;
};
inline std::vector<Posed> posed(const Rig &rig, const Pose &pose) {
	using namespace gltf_import::detail;
	const std::vector<Mat> world = worldOf(rig, pose);
	std::vector<std::vector<Mat>> jointMatrices(rig.skins.size());
	for (std::size_t s = 0; s < rig.skins.size(); ++s)
		for (std::size_t j = 0; j < rig.skins[s].joints.size(); ++j) {
			const int node = rig.skins[s].joints[j];
			jointMatrices[s].push_back(node >= 0 ? multiply(world[static_cast<std::size_t>(node)], rig.skins[s].inverseBind[j]) : identity());
		}
	std::vector<Posed> out;
	for (const Piece &piece : rig.pieces) {
		Posed p;
		p.material = piece.material;
		p.uvs = piece.uvs;
		p.indices = piece.indices;
		const std::size_t vn = piece.positions.size() / 3;
		p.positions.resize(vn * 3);
		p.normals.resize(vn * 3);
		for (std::size_t v = 0; v < vn; ++v) {
			double pos[3] = {0, 0, 0}, nor[3] = {0, 0, 0};
			if (piece.skin >= 0) {
				for (std::size_t k = 0; k < 4; ++k) {
					const double w = piece.weights[v * 4 + k];
					if (w <= 0.0) continue;
					const Mat &m = jointMatrices[static_cast<std::size_t>(piece.skin)][static_cast<std::size_t>(piece.joints[v * 4 + k])];
					double a[3], b[3];
					detail::transformPoint(m, &piece.positions[v * 3], a);
					detail::transformDir(m, &piece.normals[v * 3], b);
					for (int e = 0; e < 3; ++e) pos[e] += w * a[e], nor[e] += w * b[e];
				}
			} else {
				detail::transformPoint(world[static_cast<std::size_t>(piece.node)], &piece.positions[v * 3], pos);
				detail::transformDir(world[static_cast<std::size_t>(piece.node)], &piece.normals[v * 3], nor);
			}
			const double l = std::sqrt(nor[0] * nor[0] + nor[1] * nor[1] + nor[2] * nor[2]);
			for (int e = 0; e < 3; ++e) {
				p.positions[v * 3 + static_cast<std::size_t>(e)] = pos[e];
				p.normals[v * 3 + static_cast<std::size_t>(e)] = l > 1e-12 ? nor[e] / l : (e == 1 ? 1.0 : 0.0);
			}
		}
		out.push_back(std::move(p));
	}
	return out;
}

// The bounding box of the model posed.
inline void boundsOf(const std::vector<Posed> &model, double lo[3], double hi[3]) {
	bool first = true;
	for (const Posed &p : model)
		for (std::size_t i = 0; i + 2 < p.positions.size(); i += 3)
			for (std::size_t a = 0; a < 3; ++a) {
				if (first || p.positions[i + a] < lo[a]) lo[a] = p.positions[i + a];
				if (first || p.positions[i + a] > hi[a]) hi[a] = p.positions[i + a];
				if (a == 2) first = false;
			}
}

}  // namespace gltf_skin
