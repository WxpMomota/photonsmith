#pragma once
// film_cache.h -- what a large film keeps on disk instead of in every frame's text: its heavy, unchanging meshes (a landscape, a building, a city's houses) written once as
// binary PLY files, and the pictures it makes for itself (skies), in a cache folder of the film's own; and the files that come with it (pbrt_scenes/film_assets/<film>/...).
//
// A frame's scene is text the renderer parses, and a landscape of a few hundred thousand triangles written as text is tens of megabytes a frame. Written once as a PLY and named
// by every frame ("Shape plymesh"), it is a few bytes of each frame's text and a fast binary read. The files are made by whichever process asks first, written to a temporary
// name and renamed, so a worker never reads one that is half written; the name carries a version the film raises when it changes a mesh, so an old file is never used.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "../pbrt_asset_check.h"

namespace cartoon {
namespace film_cache {

// <temp>/photonsmith_film_cache/<film>, created on first use.
inline std::filesystem::path dir(const std::string &film) {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) base = ".";
	const std::filesystem::path d = base / "photonsmith_film_cache" / film;
	std::filesystem::create_directories(d, ec);
	return d;
}

// A path as a scene file wants it (forward slashes).
inline std::string scenePath(const std::filesystem::path &p) {
	std::string s = p.string();
	for (char &c : s)
		if (c == '\\') c = '/';
	return s;
}

// Writes `bytes` to `target` unless it is already there: through a temporary name of this process's own, then a rename.
inline bool writeOnce(const std::filesystem::path &target, const std::string &bytes) {
	std::error_code ec;
	if (std::filesystem::exists(target, ec) && std::filesystem::file_size(target, ec) == bytes.size()) return true;
	static unsigned counter = 0;
	const std::filesystem::path tmp = target.string() + ".tmp" + std::to_string(static_cast<unsigned long long>(std::hash<std::string>()(target.string()) ^ reinterpret_cast<std::uintptr_t>(&ec))) + "_" +
									  std::to_string(++counter);
	{
		std::ofstream f(tmp, std::ios::binary);
		if (!f) return false;
		f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		if (!f) return false;
	}
	std::filesystem::rename(tmp, target, ec);
	if (ec) {   // (another process got there first: its file is the same)
		std::filesystem::remove(tmp, ec);
		return std::filesystem::exists(target);
	}
	return true;
}

// A mesh in story space as a binary PLY, mirrored into pbrt's space the way emitMesh mirrors it (x negated, the winding reversed).
inline std::string plyBytes(const Mesh &m) {
	const bool uv = !m.uvs.empty() && m.uvs.size() == m.verts.size();
	const bool normals = m.normals.size() == m.verts.size();
	std::string out = "ply\nformat binary_little_endian 1.0\nelement vertex " + std::to_string(m.verts.size()) + "\nproperty float x\nproperty float y\nproperty float z\n";
	if (normals) out += "property float nx\nproperty float ny\nproperty float nz\n";
	if (uv) out += "property float u\nproperty float v\n";
	out += "element face " + std::to_string(m.tris.size()) + "\nproperty list uchar int vertex_indices\nend_header\n";
	const auto putF = [&out](double v) {
		const float f = static_cast<float>(v);
		char b[4];
		std::memcpy(b, &f, 4);
		out.append(b, 4);
	};
	const auto putI = [&out](int v) {
		char b[4];
		std::memcpy(b, &v, 4);
		out.append(b, 4);
	};
	out.reserve(out.size() + m.verts.size() * 32 + m.tris.size() * 13);
	for (std::size_t i = 0; i < m.verts.size(); ++i) {
		putF(-m.verts[i].x);
		putF(m.verts[i].y);
		putF(m.verts[i].z);
		if (normals) {
			const Vec3 n = normalize(m.normals[i]);
			putF(-n.x);
			putF(n.y);
			putF(n.z);
		}
		if (uv) {
			putF(m.uvs[i].first);
			putF(m.uvs[i].second);
		}
	}
	for (const auto &t : m.tris) {
		out.push_back(3);
		putI(t[0]);
		putI(t[2]);   // (reversed: the mirror turns the winding round)
		putI(t[1]);
	}
	return out;
}

// The PLY file of `name` for `film`, made from `make()` the first time any process asks for it; "" if it could not be written. `make` is only called when the file is missing.
template <typename Make>
inline std::string ply(const std::string &film, const std::string &name, Make make) {
	static std::mutex lock;
	static std::set<std::string> known;
	const std::filesystem::path target = dir(film) / (name + ".ply");
	const std::string key = target.string();
	std::lock_guard<std::mutex> hold(lock);
	if (known.count(key)) return scenePath(target);
	std::error_code ec;
	if (!(std::filesystem::exists(target, ec) && std::filesystem::file_size(target, ec) > 0)) {
		const Mesh m = make();
		if (m.tris.empty() || !writeOnce(target, plyBytes(m))) return std::string();
	}
	known.insert(key);
	return scenePath(target);
}

// The Shape line that names a PLY file (empty text for an empty path).
inline std::string plyShape(const std::string &path) { return path.empty() ? std::string() : "  Shape \"plymesh\" \"string filename\" [ \"" + path + "\" ]\n"; }

// A file that comes with a film: <scenes folder>/film_assets/<rel>, looked for where the scenes are (RAY_TRACER_PBRT_DIR, pbrt_scenes beside the program or up from it). "" when
// it is not found.
inline std::string asset(const std::string &rel) {
	std::vector<std::filesystem::path> dirs;
	for (const std::string &d : pbrt_asset_check::sceneSearchPaths()) dirs.emplace_back(d);
	std::error_code ec;
	for (const std::filesystem::path &d : dirs) {
		const std::filesystem::path p = d / "film_assets" / rel;
		if (std::filesystem::exists(p, ec)) return scenePath(std::filesystem::absolute(p, ec));
	}
	return std::string();
}

}  // namespace film_cache
}  // namespace cartoon
