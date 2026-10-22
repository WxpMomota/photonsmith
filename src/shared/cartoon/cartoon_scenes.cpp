// cartoon_scenes.cpp -- the cartoon films' stills as scenes of the scene list (pbrt_discover.h's listGenerated / materializeGenerated).
//
// The films are code, and code that builds a couple of megabytes of scene text per frame; the scene list is included from a great many files, so none of that is visible there. These two
// functions are the only link: listGenerated() describes each film's still from its header alone, and materializeGenerated() writes a still's file when something is about to load it
// (cartoon_stills.h). Part of cpu_renderer, where the scene list is built.

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
	for (const cartoon::Film *filmPtr : cartoon::films()) {
		const cartoon::Film &film = *filmPtr;
		if (path != pathOf(dir, film)) continue;
		cartoon_stills::pruneUnknown(dir);
		cartoon_stills::ensureOne(dir, film);
		return;
	}
}

}  // namespace pbrt_discover
