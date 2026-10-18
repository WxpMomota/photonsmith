/**
 * @file day_cycle_tests.cpp
 * @brief The sun's path through a day-cycle video (src/shared/day_cycle.h)
 */

#include <gtest/gtest.h>

#include "../../src/shared/day_cycle.h"

TEST(DayCycleTest, TheSunRisesAtTheFirstFrameIsHighestInTheMiddleAndSetsAtTheLast) {
	const day_cycle::Sun rise = day_cycle::sunOfFrame(0, 101, 60.0, 120.0);
	const day_cycle::Sun noon = day_cycle::sunOfFrame(50, 101, 60.0, 120.0);
	const day_cycle::Sun set = day_cycle::sunOfFrame(100, 101, 60.0, 120.0);
	EXPECT_NEAR(rise.elevation, 3.0, 1e-9);
	EXPECT_NEAR(noon.elevation, 60.0, 1e-9);
	EXPECT_NEAR(set.elevation, 3.0, 1e-9);
	EXPECT_NEAR(noon.azimuth, 120.0, 1e-9) << "it crosses where it is now";
	EXPECT_NEAR(rise.azimuth, 210.0, 1e-9);
	EXPECT_NEAR(set.azimuth, 30.0, 1e-9) << "clockwise seen from above: the compass number falls";
}

TEST(DayCycleTest, TheHeightIsSmoothAndTheDirectionNeverTurnsBack) {
	double lastAzimuth = 1e9, lastElevation = -1.0;
	bool rising = true;
	for (int i = 0; i < 200; ++i) {
		const day_cycle::Sun s = day_cycle::sunOfFrame(i, 200, 45.0, 180.0);
		EXPECT_GE(s.elevation, 3.0 - 1e-9);
		EXPECT_LE(s.elevation, 45.0 + 1e-9);
		if (i > 0) {
			EXPECT_LT(s.azimuth, lastAzimuth) << "the compass number keeps falling (no wrap here)";
			if (s.elevation < lastElevation) rising = false;
			else EXPECT_TRUE(rising) << "once it starts to set it does not rise again";
		}
		lastAzimuth = s.azimuth;
		lastElevation = s.elevation;
	}
}

TEST(DayCycleTest, TheCompassWrapsAndAOneFrameDayIsNoon) {
	const day_cycle::Sun early = day_cycle::sunAt(0.0, 50.0, 20.0);
	EXPECT_NEAR(early.azimuth, 110.0, 1e-9);
	const day_cycle::Sun late = day_cycle::sunAt(1.0, 50.0, 20.0);
	EXPECT_NEAR(late.azimuth, 290.0, 1e-9) << "20 - 90 wraps round to 290";
	const day_cycle::Sun one = day_cycle::sunOfFrame(0, 1, 50.0, 20.0);
	EXPECT_NEAR(one.elevation, 50.0, 1e-9);
	EXPECT_NEAR(one.azimuth, 20.0, 1e-9);
	const day_cycle::Sun low = day_cycle::sunAt(0.5, 1.0, 0.0);
	EXPECT_NEAR(low.elevation, 3.0, 1e-9) << "a noon lower than the horizon height is the horizon height";
}
