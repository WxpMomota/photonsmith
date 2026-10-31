#pragma once
// film_animals.h -- the films' rigged animals (film_wildlife.h's herds of deer, horses, wolves, lions, zebras, giraffes, elephants, bears and foxes, its eagles and geese):
// skinned glTF models with their clips
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
#include "film_coats.h"
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
		// (round 13: the savanna's and the forest's; an elephant never gallops, it ambles fast)
		{"lions", {"lion.glb", 1.089, 1.841, 4.156, 2.2, 4.5, 1.2, 14.0, 1.1, false, false}},
		{"zebras", {"zebra.glb", 1.279, 2.282, 4.412, 2.2, 4.8, 1.4, 14.0, 1.15, true, false}},
		{"giraffes", {"giraffe.glb", 2.225, 3.896, 7.800, 3.0, 6.5, 1.6, 12.0, 1.3, true, false}},
		{"elephants", {"elephant.glb", 2.061, 3.694, 6.589, 2.6, 99.0, 1.4, 6.0, 1.9, true, false}},
		{"bears", {"bear.glb", 0.947, 1.627, 3.180, 1.8, 4.0, 1.1, 11.0, 1.15, false, false}},
		{"foxes", {"fox.glb", 0.394, 0.666, 1.404, 1.3, 2.8, 1.0, 12.0, 0.45, false, false}},
		// (each its own way of going: a rabbit hops, a penguin waddles and never runs, a snake slithers - a wave running back along it carries it a wavelength a cycle)
		{"rabbits", {"rabbit.glb", 0.307, 0.503, 0.803, 0.6, 1.4, 0.4, 8.0, 0.24, true, false}},
		{"penguins", {"penguin.glb", 0.240, 0.240, 0.240, 99.0, 99.0, 0.4, 0.8, 0.3, false, false}},
		{"snakes", {"snake.glb", 0.800, 0.800, 0.800, 99.0, 99.0, 0.5, 1.5, 1.6, false, false}},
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
inline int posesOf(const std::string &clip) { return clip == "idle" ? 20 : clip == "glide" ? 8 : clip == "perch" ? 10 : 16; }

// A posed rig's pieces as meshes (story space) and their colours.
inline void piecesOf(const gltf_skin::Rig &rig, const gltf_skin::Pose &pose, std::vector<Mesh> &meshes, std::vector<gltf_import::Material> &materials) {
	for (const gltf_skin::Posed &p : gltf_skin::posed(rig, pose)) {
		Mesh mesh;
		const std::size_t vn = p.positions.size() / 3;
		for (std::size_t v = 0; v < vn; ++v) {
			mesh.verts.push_back({p.positions[v * 3], p.positions[v * 3 + 1], p.positions[v * 3 + 2]});
			mesh.normals.push_back({p.normals[v * 3], p.normals[v * 3 + 1], p.normals[v * 3 + 2]});
			if (p.uvs.size() >= (v + 1) * 2) mesh.uvs.push_back({p.uvs[v * 2], 1.0 - p.uvs[v * 2 + 1]});   // (the coat's picture: film_coats.h; a glTF picture starts at its top)
		}
		if (mesh.uvs.size() != mesh.verts.size()) mesh.uvs.clear();
		for (std::size_t t = 0; t + 2 < p.indices.size(); t += 3)
			mesh.tris.push_back({static_cast<int>(p.indices[t]), static_cast<int>(p.indices[t + 1]), static_cast<int>(p.indices[t + 2])});
		meshes.push_back(std::move(mesh));
		materials.push_back(p.material >= 0 && static_cast<std::size_t>(p.material) < rig.materials.size() ? rig.materials[static_cast<std::size_t>(p.material)] : gltf_import::Material());
	}
}

// Eye-shine (round 13: a film's `night eyeshine E`): the light a camera's flash or a torch next to the lens gets back from the mirror behind a night animal's retina
// (the tapetum lucidum) - green-gold in a cat's or a dog's, yellow in a deer's, blue-green in a horse's, orange in a bear's; a rabbit has none and its eyes shine red
// from the blood behind them. Drawn as the pupil glowing that colour (a glow that lights nothing: pbrt's "bool sampled" false), `amount` times a strong shine.
inline Vec3 eyeShineOf(const std::string &kind) {
	if (kind == "lions" || kind == "foxes" || kind == "wolves") return {0.55, 0.95, 0.35};
	if (kind == "deer" || kind == "giraffes" || kind == "elephants") return {0.95, 0.9, 0.45};
	if (kind == "horses" || kind == "zebras") return {0.35, 0.85, 0.85};
	if (kind == "bears") return {1.0, 0.55, 0.18};
	if (kind == "rabbits") return {1.0, 0.18, 0.1};
	return {0.8, 0.8, 0.6};
}
inline std::string eyeShineMaterial(const std::string &kind, double amount) {
	const Vec3 L = eyeShineOf(kind) * (6.0 * amount);
	char buf[200];
	std::snprintf(buf, sizeof buf, "AreaLightSource \"diffuse\" \"rgb L\" [ %.4f %.4f %.4f ] \"bool sampled\" false\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", L.x, L.y, L.z);
	return buf;
}

// The pbrt object of a kind in a clip's pose k (of posesOf), named `name`: its pieces with the file's colours - its coat and legs painted in `pattern` (film_coats.h,
// variant `variant`) when it has one - and (`coat` above 0: film_fur.h) its fur or feathers.
inline std::string poseObject(const Model &m, const std::string &name, const std::string &clip, int k, const std::string &kind = std::string(), double coat = 0.0,
							  const std::string &pattern = std::string(), int variant = 0, bool wet = false, double eyeshine = 0.0) {
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
	std::string body, textures;
	std::vector<Vec3> colours;
	std::string painted;   // (the coat's picture as a texture, when it has a pattern)
	if (!pattern.empty()) {
		Vec3 coatColour{0.5, 0.4, 0.3}, legColour = coatColour;
		for (const gltf_import::Material &mat : rig.materials) {
			if (mat.name == "coat") coatColour = {mat.baseColor[0], mat.baseColor[1], mat.baseColor[2]};
			if (mat.name == "legs") legColour = {mat.baseColor[0], mat.baseColor[1], mat.baseColor[2]};
		}
		painted = "coat_" + kind + "_" + pattern + "_" + std::to_string(variant) + (wet ? "_wet" : "");
		textures = "Texture \"" + painted + "\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + coats::picture(pattern, kind, variant, coatColour, legColour) + "\" ]" +
				   (wet ? " \"float scale\" [ 0.55 ]" : "") + "\n";
	}
	for (std::size_t i = 0; i < meshes.size(); ++i) {
		const gltf_import::Material &mat = materials[i];
		const Vec3 colour{mat.baseColor[0], mat.baseColor[1], mat.baseColor[2]};
		if (!painted.empty() && (mat.name == "coat" || mat.name == "legs") && !meshes[i].uvs.empty()) {   // (painted: film_coats.h)
			colours.push_back(colour);
			body += attr(wet ? "Material \"coateddiffuse\" \"texture reflectance\" \"" + painted + "\" \"float roughness\" [ 0.2 ]" : "Material \"diffuse\" \"texture reflectance\" \"" + painted + "\"",
						 emitMesh(meshes[i], identity()));
			continue;
		}
		if (wet && (mat.name == "coat" || mat.name == "legs")) {   // (wet: darker and glossy)
			colours.push_back(colour);
			body += attr(coated(colour * 0.55, 0.2), emitMesh(meshes[i], identity()));
			continue;
		}
		if (mat.name == "pupil" && eyeshine > 0.0) {   // (eye-shine: see eyeShineOf)
			colours.push_back(Vec3{0.0, 0.0, 0.0});
			body += attr(eyeShineMaterial(kind, eyeshine), emitMesh(meshes[i], identity()));
			continue;
		}
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
	return textures + nature::objectBegin(name) + body + nature::objectEnd();
}

// A hero's fur (round 13: `herd ... hero N`): real strands - pbrt curves with pbrt's hair material - over the coat and legs of an animal close to the camera, in place of
// the herd's ribbons. Each strand is rooted on a triangle of the posed body (shared out by the rest pose's areas, its place fixed by its number, so the fur keeps to its
// spot on the skin), leaves the skin and lies back along the body and a little down, as long as the kind's coat (film_fur.h) - half that on the face, more on the tail and
// under the belly, shorter on the legs - and coloured from the coat's pattern there (film_coats.h). Written in the world through `place` (the herd's placement, mirrored
// on the outside: what the posed object's mirrored meshes end up under). `amount` 1 is some 9000 strands a square metre of skin.
inline std::string heroFur(const std::string &kind, const std::string &clip, int k, const std::string &pattern, int variant, double amount, const Mat4 &place, int seed) {
	const std::shared_ptr<const Model> m = modelOf(kind);
	if (!m || amount <= 0.0) return std::string();
	const gltf_skin::Rig &rig = m->rig;
	gltf_skin::Pose pose = gltf_skin::restPose(rig);
	if (const int c = m->clip(clip); c >= 0) gltf_skin::apply(rig, rig.clips[static_cast<std::size_t>(c)], rig.clips[static_cast<std::size_t>(c)].length * k / posesOf(clip), pose);
	std::vector<Mesh> posed, rest;
	std::vector<gltf_import::Material> materials, restMaterials;
	piecesOf(rig, pose, posed, materials);
	piecesOf(rig, gltf_skin::restPose(rig), rest, restMaterials);
	Vec3 coatColour{0.5, 0.4, 0.3}, legColour = coatColour;
	for (const gltf_import::Material &mat : rig.materials) {
		if (mat.name == "coat") coatColour = {mat.baseColor[0], mat.baseColor[1], mat.baseColor[2]};
		if (mat.name == "legs") legColour = {mat.baseColor[0], mat.baseColor[1], mat.baseColor[2]};
	}
	const fur::Coat coat = fur::coatOf(kind);
	const double density = 9000.0 * amount;
	const Vec3 back = normalize(transformPoint(place, {0.0, 0.0, -1.0}) - transformPoint(place, {0.0, 0.0, 0.0})), down{0.0, -1.0, 0.0};
	const double scaleOf = length(transformPoint(place, {1.0, 0.0, 0.0}) - transformPoint(place, {0.0, 0.0, 0.0}));
	std::map<int, std::string> byColour;   // (strands in a few dozen colours: one material each)
	std::map<int, Vec3> colourOf;
	char buf[220];
	for (std::size_t piece = 0; piece < posed.size() && piece < rest.size(); ++piece) {
		const std::string &mn = materials[piece].name;
		if (mn != "coat" && mn != "legs") continue;
		const Mesh &P = posed[piece], &R = rest[piece];
		if (P.tris.size() != R.tris.size()) continue;
		const Vec3 flat{materials[piece].baseColor[0], materials[piece].baseColor[1], materials[piece].baseColor[2]};
		for (std::size_t ti = 0; ti < R.tris.size(); ++ti) {
			const auto &t = R.tris[ti];
			const double area = 0.5 * length(cross(R.verts[t[1]] - R.verts[t[0]], R.verts[t[2]] - R.verts[t[0]]));
			const int salt = static_cast<int>(piece * 104729 + ti) + seed * 7;
			const double want = area * density;
			const int n = static_cast<int>(want) + (hash01(salt, 1) < want - std::floor(want) ? 1 : 0);
			for (int j = 0; j < n; ++j) {
				double u = hash01(salt * 31 + j, 2), v = hash01(salt * 31 + j, 3);
				if (u + v > 1.0) u = 1.0 - u, v = 1.0 - v;
				const auto &pt = P.tris[ti];
				const Vec3 rootM = P.verts[pt[0]] * (1.0 - u - v) + P.verts[pt[1]] * u + P.verts[pt[2]] * v;
				Vec3 nM = P.normals[pt[0]] * (1.0 - u - v) + P.normals[pt[1]] * u + P.normals[pt[2]] * v;
				if (length(nM) < 1e-9) continue;
				// where on the coat's picture it is: its colour and its part of the body (the face's fur short, the tail's and the belly's long, the legs' short)
				double ax = 0.995, ay = 0.995;
				if (!P.uvs.empty()) {
					ax = P.uvs[pt[0]].first * (1.0 - u - v) + P.uvs[pt[1]].first * u + P.uvs[pt[2]].first * v;
					ay = 1.0 - (P.uvs[pt[0]].second * (1.0 - u - v) + P.uvs[pt[1]].second * u + P.uvs[pt[2]].second * v);
				}
				double part = 1.0;
				if (ay >= 0.5) part = 0.6;
				else if (ax >= 0.75) part = ay < 0.25 ? 0.5 : 1.5;
				if (ay < 0.5 && ax < 0.75 && std::cos(2.0 * kPi * coats::detail::roundness(ax, ay)) > 0.6) part *= 1.3;
				const Vec3 colour = pattern.empty() ? flat : coats::colourAt(pattern, variant, coatColour, legColour, ax, ay);
				const Vec3 root = transformPoint(place, rootM);
				const Vec3 nrm = normalize(transformPoint(place, rootM + nM) - root);
				Vec3 lie = back - nrm * dot(back, nrm);
				lie = length(lie) > 1e-6 ? normalize(lie) : down;
				const double L = coat.length * part * scaleOf * (0.7 + 0.6 * hash01(salt * 31 + j, 4));
				const Vec3 d1 = normalize(nrm * 0.55 + lie * 0.45), d2 = normalize(nrm * 0.15 + lie * 0.75 + down * 0.25);
				const Vec3 p0 = root - nrm * (0.05 * L), p1 = root + d1 * (0.35 * L), p2 = p1 + d2 * (0.35 * L), p3 = p2 + normalize(d2 + down * 0.3) * (0.35 * L);
				const int key = static_cast<int>(std::lround(colour.x * 12)) * 400 + static_cast<int>(std::lround(colour.y * 12)) * 20 + static_cast<int>(std::lround(colour.z * 12));
				colourOf[key] = colour;
				std::snprintf(buf, sizeof buf, "Shape \"curve\" \"string type\" \"cylinder\" \"float width0\" [ %.5f ] \"float width1\" [ %.5f ] \"point3 P\" [", 0.0012 * scaleOf, 0.0003 * scaleOf);
				std::string &out = byColour[key];
				out += buf;
				for (const Vec3 &q : {p0, p1, p2, p3}) {
					std::snprintf(buf, sizeof buf, " %.5f %.5f %.5f", q.x, q.y, q.z);
					out += buf;
				}
				out += " ]\n";
			}
		}
	}
	std::string text = "# a hero's fur of strands (film_animals.h heroFur)\n";
	for (const auto &[key, strands] : byColour) {
		const Vec3 c = colourOf[key];
		const auto tone = [](double x) { return std::pow(std::clamp(x, 0.02, 0.95), 1.6); };   // (pbrt's hair reflectance is a thick coat's: one layer looks lighter)
		std::snprintf(buf, sizeof buf, "AttributeBegin\nMaterial \"hair\" \"rgb reflectance\" [ %.4f %.4f %.4f ] \"float beta_m\" [ 0.35 ] \"float beta_n\" [ 0.35 ]\n", tone(c.x), tone(c.y), tone(c.z));
		text += buf + strands + "AttributeEnd\n";
	}
	return text;
}

// A frame's poses: each object written once, the first time a frame asks for it (`text` gets it), its name returned for the placements.
struct Poses {
	std::set<std::string> written;
	double eyeshine = 0.0;   // (the film's `night eyeshine E`: the pupils glow - eyeShineOf)
	std::string poseName(const std::string &kind, const std::string &clip, int k, std::string &text, double coat = 0.0, const std::string &pattern = std::string(), int variant = 0,
						 bool wet = false) {
		k = ((k % posesOf(clip)) + posesOf(clip)) % posesOf(clip);
		const std::string name = "anim_" + kind + "_" + clip + "_" + std::to_string(k) + (coat > 0.0 ? "_coat" + std::to_string(static_cast<int>(std::lround(coat * 100.0))) : std::string()) +
								 (pattern.empty() ? std::string() : "_" + pattern + std::to_string(variant)) + (wet ? "_wet" : "") +
								 (eyeshine > 0.0 ? "_shine" + std::to_string(static_cast<int>(std::lround(eyeshine * 100.0))) : std::string());
		if (written.insert(name).second) {
			const std::shared_ptr<const Model> m = modelOf(kind);
			if (m) text += poseObject(*m, name, clip, k, kind, coat, pattern, variant, wet, eyeshine);
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
