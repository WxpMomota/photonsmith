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

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>

#include "../gltf_skin.h"

namespace cartoon {
namespace film_file {

struct RigOptions {
	double height = 1.7, stride = 0.0, turn = 0.0;   // stride 0: 0.37 of its height
	std::string walk, idle, run;                     // its clips by name ("": the first whose name has walk, idle, run)
};

namespace rigged {

// What a kind keeps of its file: the rig, how it is sized and stood, and its clips' places.
struct Kept {
	gltf_skin::Rig rig;
	double scale = 1.0, offset[3] = {0, 0, 0};
	int walk = -1, idle = -1, run = -1;
	std::vector<int> roots;              // the nodes whose forward travel is taken out (the skeleton's top joints, or the top nodes)
	std::vector<std::string> pictures;   // the file's pictures, as files the renderer reads ("" when one could not be written)
};

inline std::shared_ptr<const Kept> keep(const std::string &path, const RigOptions &o, std::string &why) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		why = "the model " + path + " cannot be read";
		return nullptr;
	}
	const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	const std::filesystem::path dir = std::filesystem::path(path).parent_path();
	gltf_skin::Result r = gltf_skin::load(bytes, [&dir](const std::string &uri, std::vector<unsigned char> &out) {
		if (uri.find("..") != std::string::npos) return false;   // (beside the model only)
		std::ifstream f(dir / uri, std::ios::binary);
		if (!f) return false;
		out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		return true;
	});
	if (!r.ok) {
		why = path + ": " + r.error;
		return nullptr;
	}
	auto k = std::make_shared<Kept>();
	k->rig = std::move(r.rig);
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
	std::snprintf(key, sizeof key, "|%.4f|%s|%s|%s", o.height, o.walk.c_str(), o.idle.c_str(), o.run.c_str());
	const std::lock_guard<std::mutex> hold(lock);
	const auto it = all.find(path + key);
	if (it != all.end()) return it->second;
	std::shared_ptr<const Kept> k = keep(path, o, why);
	if (k) all[path + key] = k;
	return k;
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
	if (m.gestures)
		for (const rig::Gesture &g : *m.gestures) {
			const double w = rig::gestureWeight(g, m.t);
			const int clip = rig.clipNamed(g.pose);
			if (w > 0.0 && clip >= 0) gltf_skin::apply(rig, rig.clips[static_cast<std::size_t>(clip)], m.t - g.in0, pose, w, false);
		}
	const std::vector<gltf_skin::Posed> model = gltf_skin::posed(rig, pose);
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
		out += attr(material, emitMesh(mesh, place));
	}
	return out;
}

}  // namespace rigged

// A kind made from a rigged glTF character (see above): "" when it is added to the kinds under `kind`, else what is wrong.
inline std::string addRiggedKind(const std::string &kind, const std::string &path, const RigOptions &o) {
	std::string why;
	const std::shared_ptr<const rigged::Kept> k = rigged::kept(path, o, why);
	if (!k) return why;
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
	c.face = false;
	for (std::size_t i = 0; i < k->rig.clips.size(); ++i)
		if (static_cast<int>(i) != k->walk && static_cast<int>(i) != k->idle && static_cast<int>(i) != k->run) c.poses.push_back(k->rig.clips[i].name);
	c.text = [k, kind, o](const CharacterMoment &m) { return rigged::textOf(*k, kind, o, m); };
	characterKinds()[kind] = c;
	return std::string();
}

}  // namespace film_file
}  // namespace cartoon
