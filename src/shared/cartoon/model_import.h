#pragma once
// model_import.h -- modeling round 2, part 5: meshes in and out.
//
//   in    `mesh FILE [fit H] [scale S] [at X Y Z] [turn AX AY AZ]` in a piece: a PLY, OBJ, glTF or GLB file (under film_assets, or a path) joins the piece - and can then be
//         cut, modified, displaced, painted or simplified like anything else. `fit H` sizes it to H metres tall and stands it on the ground at the origin. A glTF's own
//         picture comes with it (the first base-colour picture), as does an OBJ's or a PLY's uvs, for `texture FILE` to put a picture on.
//   out   `ray_tracer --model-export FILM MODEL OUT.obj`: the model's pieces as an OBJ (one group and material each, their colours in a .mtl beside it), to carry on in Blender.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../gltf_import.h"
#include "../ply_mesh.h"
#include "cartoon_lib.h"
#include "model_modifiers.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace model {

// A mesh file as one mesh (its parts merged; uvs kept when every part has them, v running up the picture). `image` gets a glTF's first base-colour picture's bytes and
// `imageExt` its kind ("png", "jpg"), when it has one. `error` says what went wrong.
inline Mesh importMesh(const std::string &path, std::string &error, std::vector<unsigned char> *image = nullptr, std::string *imageExt = nullptr) {
	Mesh out;
	std::string ext = std::filesystem::path(path).extension().string();
	for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	if (ext == ".gltf" || ext == ".glb") {
		std::ifstream in(path, std::ios::binary);
		if (!in) {
			error = "cannot read " + path;
			return out;
		}
		const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		const std::filesystem::path dir = std::filesystem::path(path).parent_path();
		const gltf_import::Result r = gltf_import::parse(bytes, [&](const std::string &uri, std::vector<unsigned char> &data) {
			std::ifstream f(dir / uri, std::ios::binary);
			if (!f) return false;
			data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
			return true;
		});
		if (!r.ok) {
			error = path + ": " + r.error;
			return out;
		}
		bool allUv = true;
		for (const gltf_import::Part &p : r.model.parts) allUv = allUv && p.hasUvs;
		for (const gltf_import::Part &p : r.model.parts) {
			const int base = static_cast<int>(out.verts.size());
			for (std::size_t i = 0; i + 2 < p.positions.size(); i += 3) out.verts.push_back({p.positions[i], p.positions[i + 1], p.positions[i + 2]});
			for (std::size_t i = 0; i + 2 < p.normals.size(); i += 3) out.normals.push_back({p.normals[i], p.normals[i + 1], p.normals[i + 2]});
			if (allUv)
				for (std::size_t i = 0; i + 1 < p.uvs.size(); i += 2) out.uvs.push_back({p.uvs[i], 1.0 - p.uvs[i + 1]});
			for (std::size_t i = 0; i + 2 < p.indices.size(); i += 3) out.tris.push_back({base + static_cast<int>(p.indices[i]), base + static_cast<int>(p.indices[i + 1]), base + static_cast<int>(p.indices[i + 2])});
		}
		if (image && imageExt)
			for (const gltf_import::Material &m : r.model.materials)
				if (m.baseColorImage >= 0 && m.baseColorImage < static_cast<int>(r.model.images.size())) {
					const gltf_import::Image &img = r.model.images[static_cast<std::size_t>(m.baseColorImage)];
					*image = img.bytes, *imageExt = img.mime.find("jp") != std::string::npos ? "jpg" : "png";
					break;
				}
	} else {
		const ply_mesh::LoadResult r = ply_mesh::loadFile(path);
		if (!r.ok) {
			error = path + ": " + r.error;
			return out;
		}
		const ply_mesh::Mesh &p = r.mesh;
		for (std::size_t i = 0; i + 2 < p.positions.size(); i += 3) out.verts.push_back({p.positions[i], p.positions[i + 1], p.positions[i + 2]});
		for (std::size_t i = 0; i + 2 < p.normals.size(); i += 3) out.normals.push_back({p.normals[i], p.normals[i + 1], p.normals[i + 2]});
		for (std::size_t i = 0; i + 1 < p.uvs.size(); i += 2) out.uvs.push_back({p.uvs[i], p.uvs[i + 1]});
		for (std::size_t i = 0; i + 2 < p.indices.size(); i += 3) out.tris.push_back({p.indices[i], p.indices[i + 1], p.indices[i + 2]});
	}
	if (out.normals.size() != out.verts.size()) sdf::computeNormals(out);
	if (out.uvs.size() != out.verts.size()) out.uvs.clear();
	if (out.tris.empty()) error = path + ": no triangles in it";
	return out;
}
// The mesh sized to `height` tall and stood on the ground (y 0), centred over the origin.
inline Mesh fitted(const Mesh &m, double height) {
	Vec3 lo, hi;
	boxOf(m, lo, hi);
	const double s = height / std::max(1e-9, hi.y - lo.y);
	return transformed(m, mul(scale(s), translate({-0.5 * (lo.x + hi.x), -lo.y, -0.5 * (lo.z + hi.z)})));
}

// The pieces (name, colour, mesh) as an OBJ file at `path` with its .mtl beside it. False when it cannot be written.
struct Exported {
	std::string name;
	Vec3 colour;
	Mesh mesh;
};
inline bool exportObj(const std::string &path, const std::vector<Exported> &pieces) {
	const std::filesystem::path p(path), mtl = std::filesystem::path(path).replace_extension(".mtl");
	std::ofstream obj(p, std::ios::binary), mat(mtl, std::ios::binary);
	if (!obj || !mat) return false;
	obj << "# a model of a film (film_model.h)\nmtllib " << mtl.filename().string() << "\n";
	int base = 1;
	char buf[160];
	for (const Exported &e : pieces) {
		std::snprintf(buf, sizeof buf, "newmtl %s\nKd %.4f %.4f %.4f\n\n", e.name.c_str(), e.colour.x, e.colour.y, e.colour.z);
		mat << buf;
		obj << "o " << e.name << "\nusemtl " << e.name << "\n";
		for (const Vec3 &v : e.mesh.verts) std::snprintf(buf, sizeof buf, "v %.6f %.6f %.6f\n", v.x, v.y, v.z), obj << buf;
		for (const Vec3 &n : e.mesh.normals) std::snprintf(buf, sizeof buf, "vn %.5f %.5f %.5f\n", n.x, n.y, n.z), obj << buf;
		const bool normals = e.mesh.normals.size() == e.mesh.verts.size();
		for (const auto &t : e.mesh.tris) {
			if (normals) std::snprintf(buf, sizeof buf, "f %d//%d %d//%d %d//%d\n", t[0] + base, t[0] + base, t[1] + base, t[1] + base, t[2] + base, t[2] + base);
			else std::snprintf(buf, sizeof buf, "f %d %d %d\n", t[0] + base, t[1] + base, t[2] + base);
			obj << buf;
		}
		base += static_cast<int>(e.mesh.verts.size());
	}
	return static_cast<bool>(obj) && static_cast<bool>(mat);
}

}  // namespace model
}  // namespace cartoon
