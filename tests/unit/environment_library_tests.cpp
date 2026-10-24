// environment_library_tests.cpp - src/shared/environment_library.h: the catalog of photographic skies, and the files it names.
#include <gtest/gtest.h>
#include "fixture_skip.h"

#include "../../src/shared/environment_library.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

TEST(EnvironmentLibraryTest, TheCatalogHasUniqueStemsAndNamesAndKnownCategories) {
	std::set<std::string> stems, names;
	const std::vector<std::string> categories = environment_library::categories();
	for (const environment_library::Entry& e : environment_library::catalog()) {
		EXPECT_TRUE(stems.insert(e.stem).second) << "duplicate stem " << e.stem;
		EXPECT_TRUE(names.insert(e.name).second) << "duplicate name " << e.name;
		EXPECT_NE(std::find(categories.begin(), categories.end(), e.category), categories.end());
	}
	EXPECT_GE(stems.size(), 25u);
	EXPECT_GE(categories.size(), 5u);
	EXPECT_NE(environment_library::findEntry("fireplace"), nullptr);
	EXPECT_EQ(environment_library::findEntry("no-such-sky"), nullptr);
}

TEST(EnvironmentLibraryTest, EveryEnvironmentIsInTheFolderWithAThumbnailAndIsARadianceFile) {
	// The tests run from the repository root (ctest sets the working directory); skip quietly when run elsewhere.
	if (!std::filesystem::exists("environments/CREDITS.md")) RT_SKIP_MISSING_FIXTURE("environments/ is not next to the working directory");
	for (const environment_library::Entry& e : environment_library::catalog()) {
		const std::string file = "environments/" + environment_library::fileOf(e);
		ASSERT_TRUE(std::filesystem::exists(file)) << file;
		EXPECT_TRUE(std::filesystem::exists(std::string("environments/thumbnails/") + e.stem + ".png")) << e.stem << " has no thumbnail";
		std::ifstream in(file, std::ios::binary);
		char magic[10] = {};
		in.read(magic, 10);
		EXPECT_EQ(std::string(magic, 10), "#?RADIANCE") << file << " is not a Radiance .hdr";
	}
}
