// cartoon_scenes.cpp -- the cartoon films' stills as scenes of the scene list (pbrt_discover.h's listGenerated / materializeGenerated).
//
// The films are code, and code that builds a couple of megabytes of scene text per frame; the scene list is included from a great many files, so none of that is visible there. These two
// functions are the only link: listGenerated() describes each film's still from its header alone, and materializeGenerated() writes a still's file when something is about to load it
// (cartoon_stills.h). Part of cpu_renderer, where the scene list is built.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "../pbrt_discover.h"
#include "cartoon_stills.h"

namespace {

// Where the stills are written: the per-user folder when the program has one (the GUI sets RAY_TRACER_USER_ASSETS), else a folder of the system temp folder, so a run from the
// command line lists and renders the stills too.
std::string cacheDir() {
	std::string dir = pbrt_asset_check::cartoonStillDir();
	if (dir.empty()) {
		std::error_code ec;
		dir = (std::filesystem::temp_directory_path(ec) / "photonsmith_cartoon_stills").string();
	}
	return dir;
}

std::string pathOf(const std::string &dir, const cartoon::Film &film) { return (std::filesystem::path(dir) / cartoon_stills::fileName(film)).string(); }

}  // namespace

namespace pbrt_discover {

void listGenerated(std::vector<Discovered> &out) {
	const std::string dir = cacheDir();
	for (const cartoon::Film *filmPtr : cartoon::films()) {
		const cartoon::Film &film = *filmPtr;
		Discovered d = describe(pathOf(dir, film), cartoon_stills::headerText(film));
		if (d.ok) out.push_back(std::move(d));
	}
}

void materializeGenerated(const std::string &path) {
	const std::string dir = cacheDir();
	std::error_code ec;   // (a file outside the stills' folder - which may not be made yet - is no still: the films, every film file read, are not asked)
	if (std::filesystem::weakly_canonical(std::filesystem::path(path).parent_path(), ec) != std::filesystem::weakly_canonical(dir, ec)) return;
	for (const cartoon::Film *filmPtr : cartoon::films()) {
		const cartoon::Film &film = *filmPtr;
		if (path != pathOf(dir, film)) continue;
		cartoon_stills::pruneUnknown(dir);
		cartoon_stills::ensureOne(dir, film);
		return;
	}
}

const char *filmTable() {
	static const std::string table = cartoon::filmTable();
	return table.c_str();
}

const char *filmProblems() {
	static const std::string text = [] {
		std::string out;
		for (const std::string &p : cartoon::filmFileProblems()) out += p + "\n";
		return out;
	}();
	return text.c_str();
}

std::string filmMoment(const std::string &id) {
	if (id.rfind("cartoon:", 0) != 0) return std::string();
	const std::size_t at = id.find('@');
	const std::string key = id.substr(8, at == std::string::npos ? std::string::npos : at - 8);
	const double t = at == std::string::npos ? -1.0 : std::atof(id.c_str() + at + 1);
	cartoon::refreshFilmFile(key);
	const cartoon::Film *film = cartoon::find(key.c_str());
	if (!film) return std::string();
	const double when = t >= 0.0 ? std::min(t, film->duration) : film->stillTime;
	std::error_code ec;
	const std::filesystem::path dir = std::filesystem::path(cacheDir()) / "moments";
	std::filesystem::create_directories(dir, ec);
	char name[160];
	std::snprintf(name, sizeof name, "%s@%.2f.pbrt", key.c_str(), when);
	const std::filesystem::path file = dir / name;
	const std::string text = "# @rt-name " + std::string(film->name) + " at " + std::to_string(when) + " s\n" + film->sceneAt(when);
	std::string old;
	{
		std::ifstream in(file, std::ios::binary);
		old.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}
	if (old != text) std::ofstream(file, std::ios::binary | std::ios::trunc) << text;   // (the same moment of an unchanged film is not written again)
	// only the latest moment of a film is kept: scrubbing a film with big meshes would otherwise leave a file of many megabytes behind at every stop
	for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
		const std::string other = entry.path().filename().string();
		if (other.rfind(key + "@", 0) == 0 && entry.path() != file) std::filesystem::remove(entry.path(), ec);
	}
	return file.string();
}

bool filmCamera(const char *key, double t, double out[6]) {
	if (!key) return false;
	cartoon::refreshFilmFile(key);
	const cartoon::Film *film = cartoon::find(key);
	if (!film || !film->headerAt) return false;
	const std::string header = film->headerAt(std::max(0.0, std::min(t, film->duration)));
	const std::size_t at = header.find("LookAt");
	if (at == std::string::npos) return false;
	std::istringstream in(header.substr(at + 6));
	for (int i = 0; i < 6; ++i)
		if (!(in >> out[i])) return false;
	return true;
}

int filmChanged(const char *key) { return key && cartoon::refreshFilmFile(key) ? 1 : 0; }

std::string filmFileShots(const char *key) {
	if (!key) return std::string();
	const cartoon::FileShots f = cartoon::fileShotsOf(key);
	std::string starts, names;
	for (const double t : f.starts) starts += (starts.empty() ? "" : ",") + std::to_string(t);
	for (const std::string &n : f.names) names += (names.empty() ? "" : "|") + n;
	return f.path + "\n" + starts + "\n" + names;
}

}  // namespace pbrt_discover
