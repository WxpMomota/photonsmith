#pragma once
// film_cache.h -- what a large film keeps on disk instead of in every frame's text: its heavy, unchanging meshes (a landscape, a building, a city's houses) written once as
// binary PLY files, and the pictures it makes for itself (skies), in a cache folder of the film's own; and the files that come with it (pbrt_scenes/film_assets/<film>/...).
//
// A frame's scene is text the renderer parses, and a landscape of a few hundred thousand triangles written as text is tens of megabytes a frame. Written once as a PLY and named
// by every frame ("Shape plymesh"), it is a few bytes of each frame's text and a fast binary read. The files are made by whichever process asks first, written to a temporary
// name and renamed, so a worker never reads one that is half written.
//
// Which meshes are current: they are kept in a folder named after the program that made them (its path, size and time: buildStamp), so a rebuilt program - whose code may make
// different meshes - makes them again, and the test program and the installed one keep caches of their own. (The cache used to trust a version number in each file's name,
// raised by hand when a mesh changed; a change to a shared generator raised none, and an old mesh was used.) The skies (film_sky.h) are named by every number they are made from,
// so they are kept across builds. Stamp folders, skies and temporary files that have not been used for some days are deleted (prune), or the cache only grew.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <process.h>   // _getpid
#else
#include <unistd.h>
#endif

#include "cartoon_lib.h"
#include "../pbrt_asset_check.h"

namespace cartoon {
namespace film_cache {

// One lock for everything here that a process's threads share (a static inside the `ply` template was one per call site, and locked nothing against the others).
inline std::mutex &lock() {
	static std::mutex m;
	return m;
}

// The program this code is in - the module holding it: ray_tracer.exe, or the GUI's scene_metadata.dll and realtime_renderer.dll (stamping the GUI's own exe, an updated DLL
// reused the meshes the older one made) - its path, size and time, hashed (FNV-1a) to a short name. "" when the path is not known.
inline std::string buildStamp() {
	static const std::string stamp = [] {
		const std::filesystem::path exe = pbrt_asset_check::moduleFile();
		std::error_code ec;
		if (exe.empty() || !std::filesystem::exists(exe, ec)) return std::string();
		const std::string id = exe.string() + "|" + std::to_string(static_cast<unsigned long long>(std::filesystem::file_size(exe, ec))) + "|" +
							   std::to_string(static_cast<long long>(std::filesystem::last_write_time(exe, ec).time_since_epoch().count()));
		std::uint64_t h = 1469598103934665603ull;
		for (const unsigned char c : id) h = (h ^ c) * 1099511628211ull;
		char out[17];
		std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
		return std::string("build_") + out;
	}();
	return stamp;
}

// <temp>/photonsmith_film_cache: every film's folder is in it (not created here).
inline std::filesystem::path cacheRoot() {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) base = ".";
	return base / "photonsmith_film_cache";
}

// <temp>/photonsmith_film_cache/<film>, created on first use: the film's skies, and the stamp folders of its meshes.
inline std::filesystem::path filmDir(const std::string &film) {
	std::error_code ec;
	const std::filesystem::path d = cacheRoot() / film;
	std::filesystem::create_directories(d, ec);
	return d;
}

// Deletes, in a film's folder, what has not been used for `days`: other programs' stamp folders, skies, temporary files left by a process that was stopped mid-write, and meshes
// from before the stamp folders. Of the other programs' folders used within the week, only the two used last are kept beyond a day (every rebuild of the program, the tests or
// the GUI makes a folder: one film had 146 of them, 22 GB, in four days); a folder in use is renewed each time it is asked for, so it is never one of those. Once per film and
// process.
inline void prune(const std::filesystem::path &dir, int days = 7) {
	static std::mutex pruning;   // (not lock(): ply() holds that while it asks for its folder)
	static std::set<std::string> done;
	std::lock_guard<std::mutex> hold(pruning);
	if (!done.insert(dir.string()).second) return;
	std::error_code ec;
	const auto now = std::filesystem::file_time_type::clock::now(), cutoff = now - std::chrono::hours(24 * days), dayAgo = now - std::chrono::hours(24);
	const std::string mine = buildStamp();
	std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> builds;   // (other programs' folders, newest first)
	for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
		const std::string name = entry.path().filename().string();
		if (name.rfind("build_", 0) == 0 && entry.is_directory(ec) && name.rfind(mine, 0) != 0) builds.push_back({entry.last_write_time(ec), entry.path()});
	}
	std::sort(builds.begin(), builds.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
	for (std::size_t k = 0; k < builds.size(); ++k)
		if (builds[k].first < cutoff || (k >= 2 && builds[k].first < dayAgo)) std::filesystem::remove_all(builds[k].second, ec);
	for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
		const std::string name = entry.path().filename().string();
		if (name == mine) continue;
		const bool old = entry.last_write_time(ec) < cutoff;
		if (entry.is_directory(ec)) {
			continue;   // (the stamp folders: above)
		} else if (entry.path().extension() == ".ply" || (old && (name.find(".tmp") != std::string::npos || entry.path().extension() == ".hdr"))) {
			std::filesystem::remove(entry.path(), ec);   // (a loose .ply is from before the stamp folders: never read again)
		}
	}
}

// Where `film`'s meshes go: <film folder>/<buildStamp>, created on first use (the film folder itself when the program's path is not known). The folder's time is renewed, so a
// program that is still used keeps its meshes however old they are.
inline std::filesystem::path dir(const std::string &film) {
	// (worked out once a film and process, its time renewed every ten minutes at most: it is asked for by every mesh and picture of every frame, and each time made the
	// folders and set their time)
	static std::mutex knownLock;
	static std::map<std::string, std::pair<std::filesystem::path, std::chrono::steady_clock::time_point>> known;
	const std::string id = film + (previewCoarsening() > 1.0 ? "|preview" : "");
	{
		const std::lock_guard<std::mutex> hold(knownLock);
		const auto it = known.find(id);
		if (it != known.end() && std::chrono::steady_clock::now() - it->second.second < std::chrono::minutes(10)) return it->second.first;
	}
	const std::filesystem::path base = filmDir(film);
	static std::once_flag everyFilm;   // (every film's folder once a process, not only those used: a film not made again kept its folders for ever)
	std::call_once(everyFilm, [&base] {
		std::error_code ec;
		for (const auto &entry : std::filesystem::directory_iterator(base.parent_path(), ec))
			if (entry.is_directory(ec)) prune(entry.path());
	});
	prune(base);
	const std::string stamp = buildStamp();
	if (stamp.empty()) {
		const std::lock_guard<std::mutex> hold(knownLock);
		known[id] = {base, std::chrono::steady_clock::now()};
		return base;
	}
	const std::filesystem::path d = base / (previewCoarsening() > 1.0 ? stamp + "_preview" : stamp);   // (a preview's coarse meshes never stand in for the film's)
	std::error_code ec;
	std::filesystem::create_directories(d, ec);
	std::filesystem::last_write_time(d, std::filesystem::file_time_type::clock::now(), ec);
	const std::lock_guard<std::mutex> hold(knownLock);
	known[id] = {d, std::chrono::steady_clock::now()};
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
	static std::atomic<unsigned> counter{0};
	// (the process's id and a count: two processes, or two threads, never write the same temporary file; an address on the stack was no such guarantee)
#if defined(_WIN32)
	const long long pid = _getpid();
#else
	const long long pid = getpid();
#endif
	const std::filesystem::path tmp = target.string() + ".tmp" + std::to_string(pid) + "_" + std::to_string(++counter);
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

// The PLY file of `name` for `film`, made from `make()` the first time any process asks for it. `make` is only called when the file is missing. "" for a mesh with nothing in it
// (a figure without shoes: there is nothing to draw, and plyShape("") draws nothing). Throws std::runtime_error when the file cannot be written: it used to return "" then too,
// and the frame went on without its landscape and without a word.
template <typename Make>
inline std::string ply(const std::string &film, const std::string &name, Make make) {
	static std::set<std::string> known, empty;
	std::lock_guard<std::mutex> hold(lock());
	const std::filesystem::path target = dir(film) / (name + ".ply");
	const std::string key = target.string();
	if (known.count(key)) return scenePath(target);
	if (empty.count(key)) return std::string();
	std::error_code ec;
	if (!(std::filesystem::exists(target, ec) && std::filesystem::file_size(target, ec) > 0)) {
		const Mesh m = make();
		if (m.tris.empty()) {
			empty.insert(key);   // (made once: not again on every frame)
			return std::string();
		}
		if (!writeOnce(target, plyBytes(m))) throw std::runtime_error("film " + film + ": could not write " + target.string());
	}
	known.insert(key);
	return scenePath(target);
}

// The Shape line that names a PLY file (empty text for an empty path).
inline std::string plyShape(const std::string &path) { return path.empty() ? std::string() : "  Shape \"plymesh\" \"string filename\" [ \"" + path + "\" ]\n"; }

// The ConcatTransform line that places a PLY written by plyBytes (or a mesh written by emitMesh) by the story-space transform `m`: the file is already mirrored, so the
// placement is mirror * m * mirror, and pbrt takes the matrix by columns.
inline std::string placement(const Mat4 &m) {
	static const Mat4 mirror = scale(-1.0, 1.0, 1.0);
	const Mat4 p = mul(mirror, m, mirror);
	char buf[400];
	std::snprintf(buf, sizeof buf, "  ConcatTransform [ %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f ]\n", p[0], p[4], p[8], p[12], p[1], p[5], p[9],
				  p[13], p[2], p[6], p[10], p[14], p[3], p[7], p[11], p[15]);
	return buf;
}

// A part that never changes shape - a mask, a tank, a pair of horns - made once (`make()`, in the part's own space) as the PLY `name` of `film`, and placed in a frame by
// `frame` (part space -> story space) with its material. A character sculpted every frame spends most of its time on such parts, and writes them into every frame's text.
template <typename Make>
inline std::string part(const std::string &film, const std::string &name, const std::string &material, const Mat4 &frame, Make make) {
	const std::string file = ply(film, name, make);
	return file.empty() ? std::string() : attr(material, placement(frame) + plyShape(file));
}

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
