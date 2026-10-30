#pragma once
// film_rigged.h -- a rigged glTF character as a kind a film file can bring in (film_file.h `rigged KIND FILE ...`; included by film_file.h after its CharacterKind): a model
// with a skeleton and animation clips - one made in Blender, or from Mixamo (download it as glTF / .glb, or convert its FBX in Blender) - drawn as its clips say:
//
//   standing    its idle clip, looped (a clip named "idle"; none: its rest pose)
//   walking     its walk clip in step with its walk: one cycle of the clip is two steps (a clip with "walk" in its name), its run clip when it runs ("run")
//   gesturing   a clip named as the gesture's pose (`gesture bob "wave" 3 4 6 7`: the clip "wave"), played from the gesture's start and eased in and out by its weight
//
// The model is sized to `height` (its rest pose's height; 1.7 by default), stood on the ground at its feet and centred, and turned by `turn` degrees when it does not face
// +z (glTF's front); what its walk clip moves it forward is taken out (the film walks it), its bob kept. Its materials are the file's base colours and pictures (written
// once to the film cache for pbrt to read), metal where the file says so. It has no face, so no `feel` lines; its hands are its own (no props).

#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>

#include "../gltf_skin.h"
#include "../ply_mesh.h"
#include "auto_rig.h"      // (a model with no skeleton: one fitted to it)
#include "face_morphs.h"   // (its face: blinks, feelings and speech on its morph targets)
#include "mocap_bvh.h"   // motion capture onto a rigged character (addMocapClip)

namespace cartoon {
namespace film_file {

struct RigOptions {
	double height = 1.7, stride = 0.0, turn = 0.0;   // stride 0: 0.37 of its height
	std::string walk, idle, run;                     // its clips by name ("": the first whose name has walk, idle, run)
	bool linearSkin = false;                         // `skinning linear`: blend its joints' matrices (the default is dual quaternions: elbows and wrists keep their volume)
	bool skinningGiven = false;                      // (a `skinning` word: else an auto-rigged model blends linearly - its blends are wide, and dual quaternions balloon them)
	bool autoRig = false;                            // `autorig`: a skeleton fitted to it (auto_rig.h) even if it has one; a model with none (an .obj, a .ply) gets one anyway
};

namespace rigged {

// What a kind keeps of its file: the rig, how it is sized and stood, and its clips' places.
struct Kept {
	gltf_skin::Rig rig;
	double scale = 1.0, offset[3] = {0, 0, 0};
	int walk = -1, idle = -1, run = -1;
	std::vector<int> roots;              // the nodes whose forward travel is taken out (the skeleton's top joints, or the top nodes)
	std::vector<std::string> pictures;   // the file's pictures, as files the renderer reads ("" when one could not be written)
	face_morphs::Map face;               // its face's morphs by what they do (face_morphs.h)
	bool autoRigged = false;             // (its skeleton fitted by auto_rig.h)
};

inline std::shared_ptr<const Kept> keep(const std::string &path, const RigOptions &o, std::string &why) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		why = "the model " + path + " cannot be read";
		return nullptr;
	}
	const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	const std::filesystem::path dir = std::filesystem::path(path).parent_path();
	std::string ext = std::filesystem::path(path).extension().string();
	for (char &ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	gltf_skin::Result r;
	bool autoRigged = false;
	if (ext == ".obj" || ext == ".ply") {   // (a mesh with no skeleton: one piece, a plain material, rigged below)
		const ply_mesh::LoadResult m = ply_mesh::loadFile(path);
		if (!m.ok) {
			why = path + ": " + m.error;
			return nullptr;
		}
		gltf_skin::Piece piece;
		piece.positions.assign(m.mesh.positions.begin(), m.mesh.positions.end());
		piece.uvs.assign(m.mesh.vertexCount() * 2, 0.0);
		if (m.mesh.uvs.size() == m.mesh.vertexCount() * 2) piece.uvs.assign(m.mesh.uvs.begin(), m.mesh.uvs.end());
		for (int i : m.mesh.indices) piece.indices.push_back(static_cast<uint32_t>(i));
		piece.normals.assign(m.mesh.vertexCount() * 3, 0.0);
		if (m.mesh.normals.size() == m.mesh.vertexCount() * 3) {
			piece.normals.assign(m.mesh.normals.begin(), m.mesh.normals.end());
		} else {   // (smooth normals from the triangles)
			for (std::size_t t = 0; t + 2 < piece.indices.size(); t += 3) {
				const double *a = &piece.positions[piece.indices[t] * 3], *b = &piece.positions[piece.indices[t + 1] * 3], *c = &piece.positions[piece.indices[t + 2] * 3];
				const double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
				const double n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
				for (int k = 0; k < 3; ++k)
					for (int e = 0; e < 3; ++e) piece.normals[piece.indices[t + static_cast<std::size_t>(k)] * 3 + static_cast<std::size_t>(e)] += n[e];
			}
		}
		piece.node = 0;
		r.rig.nodes.push_back({});
		r.rig.order.push_back(0);
		r.rig.pieces.push_back(std::move(piece));
		gltf_import::Material plain;
		plain.name = "body", plain.baseColor[0] = 0.75, plain.baseColor[1] = 0.72, plain.baseColor[2] = 0.68, plain.metallic = 0.0, plain.roughness = 0.8;
		r.rig.materials.push_back(plain);
		r.rig.pieces.back().material = 0;
		r.ok = true;
	} else {
		r = gltf_skin::load(bytes, [&dir](const std::string &uri, std::vector<unsigned char> &out) {
			if (uri.find("..") != std::string::npos) return false;   // (beside the model only)
			std::ifstream f(dir / uri, std::ios::binary);
			if (!f) return false;
			out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
			return true;
		});
	}
	if (!r.ok) {
		why = path + ": " + r.error;
		return nullptr;
	}
	if (o.autoRig || (r.rig.skins.empty() && r.rig.clips.empty())) {   // (no skeleton and nothing that moves it: one fitted to it, auto_rig.h)
		if (const std::string e = auto_rig::rig(r.rig); !e.empty()) {
			why = path + ": " + e;
			return nullptr;
		}
		autoRigged = true;
	}
	auto k = std::make_shared<Kept>();
	k->autoRigged = autoRigged;
	k->rig = std::move(r.rig);
	k->face = face_morphs::mapOf(k->rig);
	const auto find = [&](const std::string &asked, const char *word) {
		if (!asked.empty()) return k->rig.clipNamed(asked);
		for (std::size_t i = 0; i < k->rig.clips.size(); ++i)
			if (k->rig.clips[i].name.find(word) != std::string::npos) return static_cast<int>(i);
		return -1;
	};
	k->walk = find(o.walk, "walk"), k->idle = find(o.idle, "idle"), k->run = find(o.run, "run");
	for (const auto &[asked, at] : {std::make_pair(o.walk, k->walk), std::make_pair(o.idle, k->idle), std::make_pair(o.run, k->run)})
		if (!asked.empty() && at < 0) {
			std::string clips;
			for (const gltf_skin::Clip &c : k->rig.clips) clips += (clips.empty() ? "" : ", ") + c.name;
			why = path + " has no clip called \"" + asked + "\" (it has: " + (clips.empty() ? std::string("none") : clips) + ")";
			return nullptr;
		}
	// the top of the skeleton (or of the nodes): what carries the model forward
	std::set<int> joints;
	for (const gltf_skin::Skin &s : k->rig.skins) joints.insert(s.joints.begin(), s.joints.end());
	for (std::size_t n = 0; n < k->rig.nodes.size(); ++n) {
		const int parent = k->rig.nodes[n].parent;
		if (joints.empty() ? parent < 0 : (joints.count(static_cast<int>(n)) && !joints.count(parent))) k->roots.push_back(static_cast<int>(n));
	}
	// sized and stood by its rest pose
	double lo[3], hi[3];
	gltf_skin::boundsOf(gltf_skin::posed(k->rig, gltf_skin::restPose(k->rig)), lo, hi);
	const double tall = hi[1] - lo[1];
	if (!(tall > 1e-6)) {
		why = path + ": the model has no height";
		return nullptr;
	}
	k->scale = o.height / tall;
	k->offset[0] = 0.5 * (lo[0] + hi[0]), k->offset[1] = lo[1], k->offset[2] = 0.5 * (lo[2] + hi[2]);
	// its pictures, as files (named by their bytes: written once)
	for (const gltf_import::Image &img : k->rig.images) {
		std::uint64_t h = 1469598103934665603ull;
		for (unsigned char c : img.bytes) h = (h ^ c) * 1099511628211ull;
		char name[40];
		std::snprintf(name, sizeof name, "%016llx%s", static_cast<unsigned long long>(h), img.mime == "image/jpeg" ? ".jpg" : ".png");
		const std::filesystem::path file = film_cache::filmDir("_rigs") / name;
		std::error_code ec;
		if (!std::filesystem::exists(file, ec)) film_cache::writeOnce(file, std::string(img.bytes.begin(), img.bytes.end()));
		k->pictures.push_back(std::filesystem::exists(file, ec) ? film_cache::scenePath(file) : std::string());
	}
	return k;
}

// A rig read once a program (by its file and how it is sized).
inline std::shared_ptr<const Kept> kept(const std::string &path, const RigOptions &o, std::string &why) {
	static std::mutex lock;
	static std::map<std::string, std::shared_ptr<const Kept>> all;
	char key[160];
	std::snprintf(key, sizeof key, "|%.4f|%s|%s|%s|%d", o.height, o.walk.c_str(), o.idle.c_str(), o.run.c_str(), o.autoRig ? 1 : 0);
	const std::lock_guard<std::mutex> hold(lock);
	const auto it = all.find(path + key);
	if (it != all.end()) return it->second;
	std::shared_ptr<const Kept> k = keep(path, o, why);
	if (k) all[path + key] = k;
	return k;
}

// Whether a material of the model is its skin, by its name (skin, body, face, head, hand: as Mixamo, VRoid and Ready Player Me name them).
inline bool isSkin(std::string name) {
	for (char &ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	for (const char *w : {"skin", "body", "face", "head", "hand"})
		if (name.find(w) != std::string::npos) return true;
	return false;
}
// The model at a moment, as scene text (story space through the toolkit's emitMesh).
inline std::string textOf(const Kept &k, const std::string &kind, const RigOptions &o, const CharacterMoment &m) {
	const gltf_skin::Rig &rig = k.rig;
	gltf_skin::Pose pose = gltf_skin::restPose(rig);
	// standing (its idle clip, or the rest pose) faded into its walk by how much it is walking: it snapped from one to the other the frame a walk began or ended
	const int moving = m.running > 0.5 && k.run >= 0 ? k.run : k.walk;
	if (k.idle >= 0 && m.going < 1.0) gltf_skin::apply(rig, rig.clips[static_cast<std::size_t>(k.idle)], m.t, pose);
	if (moving >= 0 && m.going > 0.0)
		gltf_skin::apply(rig, rig.clips[static_cast<std::size_t>(moving)], m.steps * 0.5 * rig.clips[static_cast<std::size_t>(moving)].length, pose, m.going);
	for (int n : k.roots) {   // (the film carries it forward: what the clip moves it along the ground is taken out)
		pose[static_cast<std::size_t>(n)].t[0] = rig.nodes[static_cast<std::size_t>(n)].rest.t[0];
		pose[static_cast<std::size_t>(n)].t[2] = rig.nodes[static_cast<std::size_t>(n)].rest.t[2];
	}
	// its face: the clips' own morph weights, then a blink, the feeling and the mouth on top (face_morphs.h)
	std::vector<double> morphs = rig.morphRest;
	if (k.idle >= 0 && m.going < 1.0) gltf_skin::applyMorphs(rig, rig.clips[static_cast<std::size_t>(k.idle)], m.t, morphs);
	if (m.gestures)
		for (const rig::Gesture &g : *m.gestures) {
			const double w = rig::gestureWeight(g, m.t);
			const int clip = rig.clipNamed(g.pose);
			if (w > 0.0 && clip >= 0) {
				gltf_skin::apply(rig, rig.clips[static_cast<std::size_t>(clip)], m.t - g.in0, pose, w, false);
				gltf_skin::applyMorphs(rig, rig.clips[static_cast<std::size_t>(clip)], m.t - g.in0, morphs, w, false);
			}
		}
	if (!rig.morphs.empty()) face_morphs::weightsOf(k.face, face_morphs::controlsOf(m.face, m.faceShapes.empty() ? rig::blink(m.t, m.seed) : 0.0, m.mouth), morphs);
	for (const auto &[name, w] : m.faceShapes)   // (`act`: a video's ARKit shapes onto the morphs of those names, as they are)
		if (const int i = rig.morphNamed(name); i >= 0) morphs[static_cast<std::size_t>(i)] = std::max(morphs[static_cast<std::size_t>(i)], std::clamp(w, 0.0, 1.0));
	const std::vector<gltf_skin::Posed> model = gltf_skin::posed(rig, pose, &morphs, o.skinningGiven ? !o.linearSkin : !k.autoRigged);
	const Mat4 place = mul(m.frame, scale(m.scale, m.scale, m.scale), rotate({0, 1, 0}, o.turn));
	std::string out;
	std::map<int, std::string> textures;   // (the file's pictures named for this character: two of a kind in a frame do not share a name)
	for (std::size_t i = 0; i < k.pictures.size(); ++i)
		if (!k.pictures[i].empty()) {
			const std::string name = "rig-" + kind + "-" + std::to_string(m.seed) + "-" + std::to_string(i);
			out += "Texture \"" + name + "\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + k.pictures[i] + "\" ]\n";
			textures[static_cast<int>(i)] = name;
		}
	for (const gltf_skin::Posed &p : model) {
		Mesh mesh;
		const std::size_t vn = p.positions.size() / 3;
		for (std::size_t v = 0; v < vn; ++v) {
			mesh.verts.push_back({(p.positions[v * 3] - k.offset[0]) * k.scale, (p.positions[v * 3 + 1] - k.offset[1]) * k.scale, (p.positions[v * 3 + 2] - k.offset[2]) * k.scale});
			mesh.normals.push_back({p.normals[v * 3], p.normals[v * 3 + 1], p.normals[v * 3 + 2]});
			mesh.uvs.push_back({p.uvs[v * 2], 1.0 - p.uvs[v * 2 + 1]});   // (a glTF picture starts at its top)
		}
		for (std::size_t t = 0; t + 2 < p.indices.size(); t += 3)
			mesh.tris.push_back({static_cast<int>(p.indices[t]), static_cast<int>(p.indices[t + 1]), static_cast<int>(p.indices[t + 2])});
		const gltf_import::Material mat = p.material >= 0 && static_cast<std::size_t>(p.material) < rig.materials.size() ? rig.materials[static_cast<std::size_t>(p.material)]
																														   : gltf_import::Material();
		const Vec3 colour{mat.baseColor[0], mat.baseColor[1], mat.baseColor[2]};
		std::string material;
		if (const auto tex = textures.find(mat.baseColorImage); tex != textures.end()) material = "Material \"diffuse\" \"texture reflectance\" \"" + tex->second + "\"";
		else if (mat.metallic > 0.5) material = metalMaterial(colour, std::max(0.02, mat.roughness * 0.5));
		else if (mat.roughness < 0.5) material = coated(colour, mat.roughness);
		else material = diffuseMaterial(colour);
		if (m.skinLight > 0.0 && isSkin(mat.name)) {   // (`skin-light`: its skin's materials, by their names, let light in)
			const Vec3 mfp = skinMfp(m.skinLight, m.scale * o.height / 1.8);
			char words[160];
			std::snprintf(words, sizeof words, "\" \"rgb mfp\" [ %.5f %.5f %.5f ] \"float eta\" [ 1.33 ] \"float roughness\" [ 0.35 ]", mfp.x, mfp.y, mfp.z);
			const auto tex = textures.find(mat.baseColorImage);
			material = tex != textures.end() ? "Material \"subsurface\" \"texture reflectance\" \"" + tex->second + words : skinMaterial(colour, m.skinLight, m.scale * o.height / 1.8, material);
		}
		out += attr(material, emitMesh(mesh, place));
	}
	return out;
}

}  // namespace rigged

namespace rigged {
// The rig each rigged kind draws with now (a motion capture adds a clip to it: addMocapClip), and its sizing.
struct Registered {
	std::shared_ptr<const Kept> kept;
	RigOptions options;
};
inline std::map<std::string, Registered> &registered() {
	static std::map<std::string, Registered> r;
	return r;
}
}  // namespace rigged

// A kind made from a rigged glTF character (see above): "" when it is added to the kinds under `kind`, else what is wrong.
inline std::string addRiggedKind(const std::string &kind, const std::string &path, const RigOptions &o) {
	std::string why;
	const std::shared_ptr<const rigged::Kept> k = rigged::kept(path, o, why);
	if (!k) return why;
	rigged::registered()[kind] = {k, o};
	CharacterKind c;
	const double h = o.height;
	c.chest = {0.0, 0.72 * h, 0.0};
	c.head = {0.0, 0.93 * h, 0.05 * h};
	c.carry = {0.15 * h, 0.55 * h, 0.2 * h};
	c.height = h;
	c.radius = 0.16 * h;
	c.stride = o.stride > 0.0 ? o.stride : 0.37 * h;
	c.firstStep = 0.5;
	c.scalable = true;
	c.face = k->face.any();   // (a face of morph targets takes `feel` lines)
	for (std::size_t i = 0; i < k->rig.clips.size(); ++i)
		if (static_cast<int>(i) != k->walk && static_cast<int>(i) != k->idle && static_cast<int>(i) != k->run) c.poses.push_back(k->rig.clips[i].name);
	c.text = [k, kind, o](const CharacterMoment &m) { return rigged::textOf(*k, kind, o, m); };
	characterKinds()[kind] = c;
	return std::string();
}

// A motion capture (mocap_bvh.h) as a clip `clip` of the rigged kind `kind`, which its gestures can then play: "" or what is wrong. `length` gets the clip's seconds.
inline std::string addMocapClip(const std::string &kind, const std::string &clip, const std::string &bvhPath, const mocap::Options &o, double &length) {
	const auto reg = rigged::registered().find(kind);
	if (reg == rigged::registered().end() || !characterKinds().count(kind)) return "motion capture is for a rigged character (a `rigged` model's kind)";
	const int have = reg->second.kept->rig.clipNamed(clip);
	if (have >= 0) {   // (the same capture again: the clip it made)
		length = reg->second.kept->rig.clips[static_cast<std::size_t>(have)].length;
		return std::string();
	}
	std::ifstream in(bvhPath, std::ios::binary);
	if (!in) return "the motion capture " + bvhPath + " cannot be read";
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	mocap::Bvh b;
	if (const std::string e = mocap::parse(text, b); !e.empty()) return bvhPath + ": " + e;
	auto k = std::make_shared<rigged::Kept>(*reg->second.kept);
	gltf_skin::Clip made;
	if (const std::string e = mocap::retarget(k->rig, b, o, clip, made); !e.empty()) return bvhPath + ": " + e;
	length = made.length;
	k->rig.clips.push_back(std::move(made));
	std::shared_ptr<const rigged::Kept> kc = k;
	reg->second.kept = kc;
	const RigOptions ro = reg->second.options;
	characterKinds()[kind].text = [kc, kind, ro](const CharacterMoment &m) { return rigged::textOf(*kc, kind, ro, m); };
	characterKinds()[kind].poses.push_back(clip);
	return std::string();
}

}  // namespace film_file
}  // namespace cartoon
