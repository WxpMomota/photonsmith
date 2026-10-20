// scene_registry_snapshot_tests.cpp - the built-in scenes, pinned row by row in tests/data/scene_registry_snapshot.txt.
//
// One line per compiled-in scene: id, legacy id, slug, category, name, performance, flags, the file it reads and a fingerprint of its description. A change
// to any of them shows up as a diff of that file in review, instead of a count that someone edits by hand. When a scene is added, renamed or retuned ON
// PURPOSE, regenerate the file and look at the diff:
//
//     RT_UPDATE_SNAPSHOT=1 ray_tracer_tests --gtest_filter=SceneRegistrySnapshot*
//
// (run from the repository root). The test also tells the documents that quote the number of scenes (README, the scene guide) when they are out of date.
#include <gtest/gtest.h>

#include "scene_registry.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

// FNV-1a over the description's bytes, in hex: enough to see that a description changed without storing all of its text.
std::string fingerprint(const std::string& text) {
	std::uint64_t h = 1469598103934665603ull;
	for (unsigned char c : text) {
		h ^= c;
		h *= 1099511628211ull;
	}
	char buf[17];
	std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
	return buf;
}

// The file a scene reads, as the part from "pbrt_scenes/" on (the registry gives an absolute path for a scene downloaded to the per-user folder).
std::string fileOf(const std::string& id) {
	const auto it = pbrt_scene_registry::paths().find(id);
	if (it == pbrt_scene_registry::paths().end()) return "-";
	std::string p = std::filesystem::path(it->second).generic_string();
	const std::size_t at = p.find("pbrt_scenes/");
	if (at != std::string::npos) p = p.substr(at + 12);
	return p;
}

std::vector<std::string> currentSnapshot() {
	(void)get_scene_registry();   // paths() is filled as a side effect
	std::vector<std::string> lines;
	for (const SceneDescriptor& s : get_builtin_scene_registry()) {
		const SceneDescriptor* listed = find_scene(s.id);   // (the full registry gives the slug; the built-in one is before it is set)
		std::ostringstream line;
		line << s.id << '|' << s.legacy_id << '|' << (listed ? listed->slug : s.slug) << '|' << s.category << '|' << s.subcategory << '|' << s.name << '|' << s.performance << '|'
		     << (s.requires_files ? "files" : "self") << (s.gpu_compatible ? ",gpu" : ",cpu-only") << '|' << fileOf(s.id) << '|' << fingerprint(s.description);
		lines.push_back(line.str());
	}
	return lines;
}

std::string header() {
	return "# The built-in scenes, one per line: id|legacy id|slug|category|subcategory|name|performance|flags|file|description fingerprint.\n"
	       "# Regenerate with RT_UPDATE_SNAPSHOT=1 (see tests/unit/scene_registry_snapshot_tests.cpp) and review the diff.\n";
}

}  // namespace

TEST(SceneRegistrySnapshot, TheBuiltInScenesAreTheOnesInTheSnapshotFile) {
	const std::string path = "tests/data/scene_registry_snapshot.txt";
	const std::vector<std::string> now = currentSnapshot();
	if (std::getenv("RT_UPDATE_SNAPSHOT")) {
		std::ofstream out(path, std::ios::binary);
		ASSERT_TRUE(out.good()) << "cannot write " << path << " (run from the repository root)";
		out << header();
		for (const std::string& l : now) out << l << "\n";
		GTEST_SKIP() << "snapshot rewritten: " << now.size() << " scenes";
	}
	std::ifstream in(path, std::ios::binary);
	if (!in) GTEST_SKIP() << path << " is not next to the working directory";
	std::vector<std::string> pinned;
	for (std::string l; std::getline(in, l);) {
		if (!l.empty() && l.back() == '\r') l.pop_back();
		if (l.empty() || l[0] == '#') continue;
		pinned.push_back(l);
	}
	// A line-by-line comparison, so the failure names the scene that changed.
	const std::size_t common = std::min(now.size(), pinned.size());
	for (std::size_t i = 0; i < common; ++i)
		EXPECT_EQ(now[i], pinned[i]) << "row " << i + 1 << " differs from the snapshot (RT_UPDATE_SNAPSHOT=1 rewrites it if the change is intended)";
	EXPECT_EQ(now.size(), pinned.size()) << "the number of built-in scenes changed (RT_UPDATE_SNAPSHOT=1 rewrites the snapshot if that is intended)";
}

// The documents that say how many scenes there are must say the number the registry has: they used to be edited by hand and went stale.
TEST(SceneRegistrySnapshot, TheDocumentsQuoteTheRealSceneCount) {
	const std::string n = std::to_string(builtin_scene_count());
	const std::vector<std::pair<std::string, std::regex>> docs = {
		{"README.md", std::regex(R"((\d+) built-in scenes)")},
		{"docs/SCENE_SELECTION.md", std::regex(R"(has (\d+) built-in scenes)")},
		{".github/copilot-instructions.md", std::regex(R"((\d+) built-in (?:scenes|ones))")},
	};
	for (const auto& [file, re] : docs) {
		std::ifstream in(file, std::ios::binary);
		if (!in) { GTEST_SKIP() << file << " is not next to the working directory"; }
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		int found = 0;
		for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) {
			++found;
			EXPECT_EQ((*it)[1].str(), n) << file << " says " << (*it)[1].str() << " built-in scenes; the registry has " << n;
		}
		EXPECT_GT(found, 0) << file << " no longer says how many built-in scenes there are (or the wording this test looks for changed)";
	}
}
