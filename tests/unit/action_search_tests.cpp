// action_search_tests.cpp -- which actions fit what was typed in the Scene Builder's search box (src/shared/action_search.h).
#include <gtest/gtest.h>

#include "../../src/shared/action_search.h"

using action_search::match;

namespace {
const std::vector<std::string> kNames = {"Add: Sphere", "Add: Box", "Arrange: Hide in the views", "Arrange: Show everything hidden", "Arrange: Lock", "Arrange: Unlock everything locked",
                                         "View: Top", "View: Front", "Edit: Duplicate", "Edit: Delete"};
}

TEST(ActionSearchTest, AnEmptyQueryFitsEverythingInTheOrderGiven) {
	const std::vector<int> all = match(kNames, "");
	ASSERT_EQ(all.size(), kNames.size());
	for (std::size_t i = 0; i < all.size(); ++i) EXPECT_EQ(all[i], static_cast<int>(i));
	EXPECT_EQ(match(kNames, "   ").size(), kNames.size());
}

TEST(ActionSearchTest, EveryWordMustBeFoundInAnyOrderAndTheCaseDoesNotMatter) {
	EXPECT_EQ(match(kNames, "hide"), (std::vector<int>{2})) << "\"hidden\" is not \"hide\"";
	EXPECT_EQ(match(kNames, "hid"), (std::vector<int>{2, 3}));
	EXPECT_EQ(match(kNames, "VIEW top"), (std::vector<int>{6}));
	EXPECT_EQ(match(kNames, "top view"), (std::vector<int>{6})) << "any order";
	EXPECT_TRUE(match(kNames, "sphere box").empty()) << "every word must fit";
	EXPECT_TRUE(match(kNames, "zebra").empty());
}

TEST(ActionSearchTest, NamesThatStartWithTheFirstWordComeFirstThenWordStartsThenTheRest) {
	const std::vector<std::string> names = {"Unlock all", "Re-lock", "Lock"};
	// "Lock" starts with it; "Re-lock" has a word starting with it (after the dash); "Unlock all" only has it inside a word.
	EXPECT_EQ(match(names, "lock"), (std::vector<int>{2, 1, 0}));
	const std::vector<std::string> spheres = {"Resphere", "Add: Sphere"};
	EXPECT_EQ(match(spheres, "sphere"), (std::vector<int>{1, 0}));
	EXPECT_EQ(match(kNames, "add"), (std::vector<int>{0, 1})) << "the same rank keeps the order given";
}

TEST(ActionSearchTest, NonAsciiLettersMustMatchExactly) {
	const std::vector<std::string> names = {"Ver: \xC3\x81rbol", "Ver: arbol"};
	EXPECT_EQ(match(names, "\xC3\x81rbol"), (std::vector<int>{0}));
	EXPECT_EQ(match(names, "arbol"), (std::vector<int>{1}));
}
