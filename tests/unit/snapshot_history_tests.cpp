// snapshot_history_tests.cpp - src/shared/snapshot_history.h: the Scene Builder's undo and redo lists.
#include <gtest/gtest.h>

#include "../../src/shared/snapshot_history.h"

TEST(SnapshotHistoryTest, UndoThenRedoWalksBackAndForward) {
	SnapshotHistory h;
	h.record("a");   // the document was "a", it is about to become "b"
	h.record("b");   // ... then "c"
	ASSERT_TRUE(h.canUndo());
	EXPECT_EQ(h.undoTop(), "b");
	h.undone("c");   // restored "b"; "c" is what we undid
	EXPECT_EQ(h.undoTop(), "a");
	ASSERT_TRUE(h.canRedo());
	EXPECT_EQ(h.redoTop(), "c");
	h.redone("b");
	EXPECT_EQ(h.undoTop(), "b");
	EXPECT_FALSE(h.canRedo());
}

TEST(SnapshotHistoryTest, ANewChangeForgetsRedo) {
	SnapshotHistory h;
	h.record("a");
	h.undone("b");
	ASSERT_TRUE(h.canRedo());
	h.record("a2");
	EXPECT_FALSE(h.canRedo());
}

TEST(SnapshotHistoryTest, OldestStepsGoFirstPastTheStepLimit) {
	SnapshotHistory h(3, 1000);
	for (int i = 0; i < 10; ++i) h.record(std::to_string(i));
	EXPECT_EQ(h.undoSize(), 3u);
	EXPECT_EQ(h.undoTop(), "9");
}

TEST(SnapshotHistoryTest, ASizeLimitDropsOldSnapshotsButKeepsTheNewest) {
	SnapshotHistory h(200, 100);
	for (int i = 0; i < 5; ++i) h.record(std::string(40, char('a' + i)));
	EXPECT_LE(h.undoSize(), 2u);   // 2 x 40 bytes fit, 3 do not
	EXPECT_EQ(h.undoTop(), std::string(40, 'e'));
	h.record(std::string(500, 'z'));   // a single snapshot over the limit is still kept
	EXPECT_EQ(h.undoSize(), 1u);
	EXPECT_TRUE(h.canUndo());
}

TEST(SnapshotHistoryTest, ClearEmptiesBoth) {
	SnapshotHistory h;
	h.record("a");
	h.undone("b");
	h.clear();
	EXPECT_FALSE(h.canUndo());
	EXPECT_FALSE(h.canRedo());
}

TEST(SnapshotHistoryTest, TheSerialKeepsCountingWhenTheSizeLimitDropsOldSteps) {
	SnapshotHistory h(/*maxSteps=*/3);
	for (int i = 0; i < 6; ++i) h.record("step " + std::to_string(i));
	EXPECT_EQ(h.undoSize(), 3u) << "the list is full: the oldest steps are gone";
	EXPECT_EQ(h.serial(), 6u) << "but the serial is how many changes were made, so a caller can remember it and undo back to it";
	const std::size_t remembered = h.serial();
	h.record("one more");
	h.record("and another");
	EXPECT_EQ(h.undoSize(), 3u);
	EXPECT_EQ(h.serial(), remembered + 2);
	while (h.serial() > remembered && h.canUndo()) h.undone("now");   // what SceneBuilderWidget::undoTo does
	EXPECT_EQ(h.serial(), remembered) << "two undos take it back to where it was remembered, though the list had been full the whole time";
	h.redone("again");
	EXPECT_EQ(h.serial(), remembered + 1);
	h.clear();
	EXPECT_EQ(h.serial(), 0u);
}
