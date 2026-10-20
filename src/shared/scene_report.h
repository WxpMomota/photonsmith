#pragma once
// scene_report.h -- "how faithful is this scene?": what a pbrt file asked for that the renderers do not draw as written, in one page.
//
// Loading a scene produces a warning for everything the loader replaced, dropped or approximated (a texture drawn as its average, a light dropped, a material
// kind with no equivalent), and a long scene produces hundreds of them, most of them the same thing about different materials. The report groups them by what
// they are ("a texture's average value is used instead", ...), counts each group with one example, adds the scene's size and its film, and says what the scene
// was pinned against: the pbrt-v4 reference pack's row for it (tests/data/pbrt_reference_means.txt), when the file is there.
//
//     ray_tracer --scene-report <scene id, name or .pbrt path>
//
// Plain standard library plus the pbrt loader's types, so the grouping and the format are unit-tested without a render.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "pbrt_flatten.h"

namespace scene_report {

// One kind of warning: what it says with the names and numbers taken out, how many scenes' worth of it there were, and one real example.
struct Group {
	std::string kind;
	int count = 0;
	std::string example;
};

// The warning with every 'quoted name', "quoted string" and number replaced by a placeholder, so the same complaint about 40 materials is one kind.
inline std::string kindOf(const std::string &message) {
	std::string out;
	out.reserve(message.size());
	for (std::size_t i = 0; i < message.size();) {
		const char c = message[i];
		if (c == '\'' || c == '"') {
			const std::size_t close = message.find(c, i + 1);
			if (close != std::string::npos) {
				out += c == '\'' ? "'..'" : "\"..\"";
				i = close + 1;
				continue;
			}
		}
		if (std::isdigit(static_cast<unsigned char>(c))) {
			while (i < message.size() && (std::isdigit(static_cast<unsigned char>(message[i])) || message[i] == '.')) ++i;
			out += '#';
			continue;
		}
		out += c;
		++i;
	}
	return out;
}

// Groups the warnings, most frequent first (ties in first-seen order).
inline std::vector<Group> groupWarnings(const std::vector<pbrt_scene::Warning> &warnings) {
	std::vector<Group> groups;
	std::map<std::string, std::size_t> at;
	for (const pbrt_scene::Warning &w : warnings) {
		const std::string kind = kindOf(w.message);
		auto it = at.find(kind);
		if (it == at.end()) {
			it = at.emplace(kind, groups.size()).first;
			groups.push_back({kind, 0, w.message});
		}
		++groups[it->second].count;
	}
	std::stable_sort(groups.begin(), groups.end(), [](const Group &a, const Group &b) { return a.count > b.count; });
	return groups;
}

// The row of tests/data/pbrt_reference_means.txt for `name` (the scene file's stem): the ratios ours / pbrt it is pinned at. `found` false when there is no such row.
struct Pin {
	bool found = false;
	double cpu = 0.0, recursive = 0.0, wavefront = 0.0;
};

inline Pin pinFor(const std::string &tableText, const std::string &name) {
	Pin pin;
	std::istringstream in(tableText);
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream row(line);
		std::string n;
		double testSpp, depth, size, r, g, b, noise, cpuRatio, cpuTol, recRatio, recTol, wfRatio, wfTol;
		if (!(row >> n >> testSpp >> depth >> size >> r >> g >> b >> noise >> cpuRatio >> cpuTol >> recRatio >> recTol >> wfRatio >> wfTol)) continue;
		if (n != name) continue;
		pin.found = true;
		pin.cpu = cpuRatio; pin.recursive = recRatio; pin.wavefront = wfRatio;
		break;
	}
	return pin;
}

inline std::string stemOf(const std::string &path) {
	std::size_t slash = path.find_last_of("/\\");
	std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
	const std::size_t dot = file.find_last_of('.');
	return dot == std::string::npos ? file : file.substr(0, dot);
}

// The report as text. `pinTable` is the reference table's text ("" when it is not at hand).
inline std::string format(const std::string &path, const pbrt_flatten::FlatScene &scene, const std::string &pinTable = std::string()) {
	std::ostringstream os;
	os << "Scene report: " << path << "\n";
	os << "  geometry : " << scene.triangles.size() << " triangles, " << scene.spheres.size() << " spheres, " << scene.instances.size() << " instances, "
	   << scene.materials.size() << " materials, " << scene.areaLights.size() << " area lights, " << scene.punctualLights.size() << " point/spot/distant lights\n";
	os << "  environment : " << (scene.infiniteLight.present ? (scene.infiniteLight.hasPortal ? "a portal infinite light" : "an infinite light") : "none");
	if (!scene.extraPortalLights.empty()) os << " (+" << scene.extraPortalLights.size() << " more portal light" << (scene.extraPortalLights.size() > 1 ? "s" : "") << ")";
	os << "\n";
	char ratio[64];
	std::snprintf(ratio, sizeof(ratio), "%g", scene.filmImagingRatio);
	os << "  film : imaging ratio (shutter x iso / 100) " << ratio;
	if (scene.cropX0 != 0.0 || scene.cropX1 != 1.0 || scene.cropY0 != 0.0 || scene.cropY1 != 1.0) os << ", crop window set";
	os << "\n";
	if (!pinTable.empty()) {
		const Pin pin = pinFor(pinTable, stemOf(path));
		if (pin.found)
			os << "  pinned against pbrt-v4 : ours / pbrt = " << pin.cpu << " (CPU), " << pin.recursive << " (OptiX recursive), " << pin.wavefront << " (OptiX wavefront)\n";
		else
			os << "  pinned against pbrt-v4 : no row in the reference pack for '" << stemOf(path) << "'\n";
	}
	const std::vector<Group> groups = groupWarnings(scene.warnings);
	if (groups.empty()) {
		os << "  nothing was replaced, dropped or approximated: the scene is drawn as written.\n";
	} else {
		int total = 0;
		for (const Group &g : groups) total += g.count;
		os << "  " << total << " thing" << (total == 1 ? "" : "s") << " in " << groups.size() << " kind" << (groups.size() == 1 ? "" : "s") << " that are not drawn as written:\n";
		for (const Group &g : groups) {
			os << "    " << g.count << " x " << g.kind << "\n";
			if (g.count > 1 || g.example != g.kind) os << "        e.g. " << g.example << "\n";
		}
	}
	return os.str();
}

}  // namespace scene_report
