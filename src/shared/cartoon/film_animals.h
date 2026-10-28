#pragma once
// film_animals.h -- the films' rigged animals (film_wildlife.h's herds of deer, horses and wolves, its eagles and geese): skinned glTF models with their clips
// (pbrt_scenes/film_assets/library/animals/*.glb, made by scripts/make_rigged_animals.py), posed by gltf_skin.h.
//
//   gaits      a quadruped walks, trots or gallops by its speed, each gait's clip played by the ground it covers - one cycle of the clip for each of its strides
//              (the generator's: 2 L sin(A) / duty), so the hooves do not slide; it grazes (deer, horses) or stands looking round (wolves) when it stops
//   poses      a clip's cycle is cut into a few poses (16 a gait cycle), each written once a frame as a pbrt object when a frame uses it, and placed for each
//              animal - a herd of hundreds costs a few objects
//   on a slope the body is pitched to the ground under its fore and hind feet
//
// Coordinates: the models face +z with y up (story space, as the toolkit's meshes); film_wildlife.h places them.

#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "../gltf_skin.h"
#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_fur.h"
#include "nature_lib.h"

namespace cartoon {
namespace animals {

// A kind's file and numbers: how far one cycle of each gait carries it (metres: the generator prints them), the speeds where it changes gait, how fast it walks and
// flees, its body's length (for pitching it to the slope), whether it grazes when it stops.
struct Species {
	const char *file;
	double walkStride, trotStride, runStride;
	double trotFrom, runFrom;
	double walkSpeed, fleeSpeed, length;
	bool grazes, bird;
};
inline const Species *species(const std::string &kind) {
	static const std::map<std::string, Species> all = {
		{"deer", {"deer.glb", 1.037, 1.753, 3.355, 1.9, 4.0, 1.1, 10.0, 0.95, true, false}},
		{"horses", {"horse.glb", 1.468, 2.620, 5.065, 2.4, 5.0, 1.3, 13.0, 1.35, true, false}},
		{"wolves", {"wolf.glb", 0.830, 1.403, 2.821, 1.6, 3.5, 1.6, 11.0, 0.8, false, false}},
		{"eagles", {"eagle.glb", 0, 0, 0, 0, 0, 12.0, 0, 0, false, true}},
		{"geese", {"goose.glb", 0, 0, 0, 0, 0, 15.0, 0, 0, false, true}},
	};
	const auto it = all.find(kind);
	return it == all.end() ? nullptr : &it->second;
}

struct Model {
	gltf_skin::Rig rig;
	std::map<std::string, int> clips;
	int clip(const std::string &name) const {
		const auto it = clips.find(name);
		return it == clips.end() ? -1 : it->second;
	}
};
// A kind's model, read once (null, with `why`, when its file is missing or broken).
inline std::shared_ptr<const Model> modelOf(const std::string &kind, std::string *why = nullptr) {
	static std::mutex lock;
	static std::map<std::string, std::shared_ptr<const Model>> made;
	const std::lock_guard<std::mutex> hold(lock);
	if (const auto it = made.find(kind); it != made.end()) return it->second;
	const Species *s = species(kind);
	std::shared_ptr<Model> m;
	const std::string path = s ? film_cache::asset(std::string("library/animals/") + s->file) : std::string();
	std::ifstream in(path, std::ios::binary);
	if (!s || path.empty() || !in) {
		if (why) *why = "the model of the " + kind + " (film_assets/library/animals/" + (s ? s->file : "?") + ") cannot be read";
	} else {
		const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		gltf_skin::Result r = gltf_skin::load(bytes);
		if (!r.ok) {
			if (why) *why = path + ": " + r.error;
		} else {
			m = std::make_shared<Model>();
			m->rig = std::move(r.rig);
			for (std::size_t i = 0; i < m->rig.clips.size(); ++i) m->clips[m->rig.clips[i].name] = static_cast<int>(i);
		}
	}
	made[kind] = m;
	return m;
}

// How many poses a clip is cut into, and over how long it runs (its loop).
inline int posesOf(const std::string &clip) { return clip == "idle" ? 20 : clip == "glide" ? 8 : 16; }

// A posed rig's pieces as meshes (story space) and their colours.
inline void piecesOf(const gltf_skin::Rig &rig, const gltf_skin::Pose &pose, std::vector<Mesh> &meshes, std::vector<gltf_import::Material> &materials) {
	for (const gltf_skin::Posed &p : gltf_skin::posed(rig, pose)) {
		Mesh mesh;
		const std::size_t vn = p.positions.size() / 3;
		for (std::size_t v = 0; v < vn; ++v) {
			mesh.verts.push_back({p.positions[v * 3], p.positions[v * 3 + 1], p.positions[v * 3 + 2]});
			mesh.normals.push_back({p.normals[v * 3], p.normals[v * 3 + 1], p.normals[v * 3 + 2]});
		}
		for (std::size_t t = 0; t + 2 < p.indices.size(); t += 3)
			mesh.tris.push_back({static_cast<int>(p.indices[t]), static_cast<int>(p.indices[t + 1]), static_cast<int>(p.indices[t + 2])});
		meshes.push_back(std::move(mesh));
		materials.push_back(p.material >= 0 && static_cast<std::size_t>(p.material) < rig.materials.size() ? rig.materials[static_cast<std::size_t>(p.material)] : gltf_import::Material());
	}
}

// The pbrt object of a kind in a clip's pose k (of posesOf), named `name`: its pieces with the file's colours, and (`coat` above 0: film_fur.h) its fur or feathers.
inline std::string poseObject(const Model &m, const std::string &name, const std::string &clip, int k, const std::string &kind = std::string(), double coat = 0.0) {
	const gltf_skin::Rig &rig = m.rig;
	gltf_skin::Pose pose = gltf_skin::restPose(rig);
	const int c = m.clip(clip);
	if (c >= 0) {
		const double len = rig.clips[static_cast<std::size_t>(c)].length;
		gltf_skin::apply(rig, rig.clips[static_cast<std::size_t>(c)], len * k / posesOf(clip), pose);
	}
	std::vector<Mesh> meshes;
	std::vector<gltf_import::Material> materials;
	piecesOf(rig, pose, meshes, materials);
	std::string body;
	std::vector<Vec3> colours;
	for (std::size_t i = 0; i < meshes.size(); ++i) {
		const gltf_import::Material &mat = materials[i];
		const Vec3 colour{mat.baseColor[0], mat.baseColor[1], mat.baseColor[2]};
		const bool furred = mat.name == "coat" || mat.name == "legs" || mat.name == "plumage" || mat.name == "head" || mat.name == "tail";   // (not on hooves, antlers, eyes, a beak)
		colours.push_back(furred ? colour : Vec3{0.0, 0.0, 0.0});
		body += attr(mat.roughness < 0.5 ? coated(colour, std::max(0.1, mat.roughness)) : diffuseMaterial(colour), emitMesh(meshes[i], identity()));
	}
	if (coat > 0.0) {   // its fur or feathers, rooted by the rest pose's areas so they keep to their places on the skin
		std::vector<Mesh> rest, furred, keep;
		std::vector<gltf_import::Material> restMaterials;
		piecesOf(rig, gltf_skin::restPose(rig), rest, restMaterials);
		std::vector<Vec3> keepColours;
		for (std::size_t i = 0; i < meshes.size() && i < rest.size(); ++i) {
			if (colours[i].x + colours[i].y + colours[i].z <= 0.0) continue;
			furred.push_back(meshes[i]), keep.push_back(rest[i]), keepColours.push_back(colours[i]);
		}
		body += fur::strandsText(furred, keep, keepColours, fur::coatOf(kind), coat);
	}
	return nature::objectBegin(name) + body + nature::objectEnd();
}

// A frame's poses: each object written once, the first time a frame asks for it (`text` gets it), its name returned for the placements.
struct Poses {
	std::set<std::string> written;
	std::string poseName(const std::string &kind, const std::string &clip, int k, std::string &text, double coat = 0.0) {
		k = ((k % posesOf(clip)) + posesOf(clip)) % posesOf(clip);
		const std::string name = "anim_" + kind + "_" + clip + "_" + std::to_string(k) + (coat > 0.0 ? "_coat" + std::to_string(static_cast<int>(std::lround(coat * 100.0))) : std::string());
		if (written.insert(name).second) {
			const std::shared_ptr<const Model> m = modelOf(kind);
			if (m) text += poseObject(*m, name, clip, k, kind, coat);
		}
		return name;
	}
};

// A quadruped's gait and pose at a speed (m/s) and the ground it has covered: the clip and its pose.
inline void gaitOf(const Species &s, double speed, double covered, std::string &clip, double &cycles) {
	const double stride = speed < s.trotFrom ? s.walkStride : speed < s.runFrom ? s.trotStride : s.runStride;
	clip = speed < s.trotFrom ? "walk" : speed < s.runFrom ? "trot" : "run";
	cycles = covered / stride;
}

}  // namespace animals
}  // namespace cartoon
