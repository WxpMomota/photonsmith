// camera_keyframes_tests.cpp - src/shared/camera_keyframes.h: the flythrough camera path (parsing the file, and the camera at a point along it).
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "../../src/shared/camera_keyframes.h"

using namespace camera_keyframes;

namespace {

Path straight(bool ease) {
	Path p;
	p.ease = ease;
	for (double x : {0.0, 10.0, 20.0}) {
		Key k;
		k.pos[0] = x;
		k.target[0] = x;
		k.target[2] = 5;
		p.keys.push_back(k);
	}
	return p;
}

double dist(const double a[3], const double b[3]) { return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }

}  // namespace

TEST(CameraKeyframes, ParsesKeysEaseAndComments) {
	const ParseResult r = parse(
		"# a flythrough\n"
		"ease 0   # constant pace\n"
		"\n"
		"key 0 1 8   0 1 0\n"
		"key 4 2 6.5  0 1 0 # closer\n"
		"key -3 2e0 5  0 1 0\n");
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_FALSE(r.path.ease);
	ASSERT_EQ(r.path.keys.size(), 3u);
	EXPECT_DOUBLE_EQ(r.path.keys[1].pos[0], 4);
	EXPECT_DOUBLE_EQ(r.path.keys[1].pos[2], 6.5);
	EXPECT_DOUBLE_EQ(r.path.keys[2].pos[1], 2);
	EXPECT_DOUBLE_EQ(r.path.keys[2].target[1], 1);
	EXPECT_TRUE(parse("key 0 0 1 0 0 0\nkey 1 0 1 0 0 0\n").path.ease) << "ease is on unless the file says 0";
}

TEST(CameraKeyframes, RejectsWhatCannotBeAPath) {
	EXPECT_FALSE(parse("").ok);
	EXPECT_FALSE(parse("key 0 0 1 0 0 0\n").ok) << "one key is not a path";
	const ParseResult bad = parse("key 0 0 1 0 0 0\nkey 1 2 3\n");
	EXPECT_FALSE(bad.ok);
	EXPECT_NE(bad.error.find("line 2"), std::string::npos) << bad.error;
	EXPECT_FALSE(parse("key 0 0 1 0 0 0\nkey 1 0 1 0 0 0 40 1\n").ok) << "eight numbers: a lens needs both its radius and its focus distance";
	EXPECT_FALSE(parse("key 0 0 1 0 0 0\nkey 1 0 1 0 0 0 40 1 5 9\n").ok) << "ten numbers";
	EXPECT_FALSE(parse("key 0 0 1 0 0 0\nkey a b c 0 0 0\n").ok);
	EXPECT_FALSE(parse("key 0 0 1 0 0 0\nkey 1 0 1 nan 0 0\n").ok);
	EXPECT_FALSE(parse("key 0 0 1 0 0 0\nkey 1 0 1 1 0 1\n").ok) << "a camera looking at its own place";
	EXPECT_FALSE(parse("ease 2\nkey 0 0 1 0 0 0\nkey 1 0 1 0 0 0\n").ok);
	EXPECT_FALSE(parse("fly 1 2 3\nkey 0 0 1 0 0 0\nkey 1 0 1 0 0 0\n").ok);
}

TEST(CameraKeyframes, TheFileWrittenIsTheFileRead) {
	Path p = straight(false);
	p.keys[1].pos[1] = 0.1234567890123456;
	const ParseResult r = parse(toText(p));
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_FALSE(r.path.ease);
	ASSERT_EQ(r.path.keys.size(), 3u);
	for (std::size_t i = 0; i < 3; ++i) {
		EXPECT_DOUBLE_EQ(dist(r.path.keys[i].pos, p.keys[i].pos), 0.0);
		EXPECT_DOUBLE_EQ(dist(r.path.keys[i].target, p.keys[i].target), 0.0);
	}
}

TEST(CameraKeyframes, TheFirstFrameIsTheFirstKeyAndTheLastTheLast) {
	for (bool ease : {false, true}) {
		const Path p = straight(ease);
		EXPECT_DOUBLE_EQ(dist(at(p, 0.0).pos, p.keys.front().pos), 0.0);
		EXPECT_DOUBLE_EQ(dist(at(p, 1.0).pos, p.keys.back().pos), 0.0);
		EXPECT_DOUBLE_EQ(dist(at(p, 1.0).target, p.keys.back().target), 0.0);
		EXPECT_DOUBLE_EQ(dist(at(p, -3.0).pos, p.keys.front().pos), 0.0) << "clamped";
		EXPECT_DOUBLE_EQ(dist(at(p, 9.0).pos, p.keys.back().pos), 0.0) << "clamped";
	}
}

TEST(CameraKeyframes, ItGoesThroughEveryKeyWhenTheTimeIsProportionalToTheDistance) {
	Path p = straight(false);
	p.keys[2].pos[0] = 50;   // the stretches are 10 and 40 long: the middle key is a fifth of the way
	p.keys[2].target[0] = 50;
	// Each stretch counts the camera's and its target's distance: 20 and 80, so 20 / (20 + 80) = 0.2 of the time is spent reaching the middle key.
	const Key reached = at(p, 0.2);
	EXPECT_NEAR(reached.pos[0], 10.0, 1e-9);
	EXPECT_NEAR(reached.target[0], 10.0, 1e-9);
}

TEST(CameraKeyframes, AStraightLineStaysStraightAndNeverGoesBackwards) {
	const Path p = straight(false);
	double last = -1.0;
	for (int i = 0; i <= 100; ++i) {
		const Key k = at(p, i / 100.0);
		EXPECT_NEAR(k.pos[1], 0.0, 1e-9);
		EXPECT_NEAR(k.pos[2], 0.0, 1e-9);
		EXPECT_GE(k.pos[0], last - 1e-12);
		last = k.pos[0];
	}
	EXPECT_NEAR(at(p, 0.5).pos[0], 10.0, 1e-9) << "equal stretches: half way is the middle key";
}

TEST(CameraKeyframes, EasingStartsAndEndsSlowerThanTheConstantPace) {
	const double slowStart = at(straight(true), 0.1).pos[0], steady = at(straight(false), 0.1).pos[0];
	EXPECT_LT(slowStart, steady * 0.5);
	const double slowEnd = 20.0 - at(straight(true), 0.9).pos[0], steadyEnd = 20.0 - at(straight(false), 0.9).pos[0];
	EXPECT_LT(slowEnd, steadyEnd * 0.5);
	EXPECT_NEAR(at(straight(true), 0.5).pos[0], 10.0, 1e-9) << "the middle is the middle either way";
}

TEST(CameraKeyframes, TheCameraTurnsSmoothlyThroughACorner) {
	Path p;
	p.ease = false;
	for (const double* v : {new double[6]{0, 0, 0, 0, 0, 5}, new double[6]{10, 0, 0, 10, 0, 5}, new double[6]{10, 0, 10, 10, 0, 15}}) {
		Key k;
		for (int i = 0; i < 3; ++i) {
			k.pos[i] = v[i];
			k.target[i] = v[3 + i];
		}
		p.keys.push_back(k);
		delete[] v;
	}
	Key prev = at(p, 0.0);
	double biggest = 0.0, smallest = 1e9;
	for (int i = 1; i <= 200; ++i) {
		const Key k = at(p, i / 200.0);
		const double step = dist(k.pos, prev.pos);
		biggest = std::max(biggest, step);
		smallest = std::min(smallest, step);
		prev = k;
	}
	EXPECT_LT(biggest, 0.2) << "no jump anywhere: the corner is rounded, not cut";
	EXPECT_GT(smallest, 0.0);
	EXPECT_LT(biggest / smallest, 3.0) << "the pace stays about the same through the corner";
}

TEST(CameraKeyframes, ARepeatedKeyAddsNoTimeAndNoJump) {
	Path p = straight(false);
	p.keys.insert(p.keys.begin() + 1, p.keys[1]);   // 0, 10, 10, 20
	EXPECT_NEAR(at(p, 0.5).pos[0], 10.0, 1e-9);
	Key prev = at(p, 0.0);
	for (int i = 1; i <= 100; ++i) {
		const Key k = at(p, i / 100.0);
		EXPECT_LT(dist(k.pos, prev.pos), 1.0);
		prev = k;
	}
	Path same;
	Key k;
	k.target[2] = 1;
	same.keys = {k, k};
	EXPECT_DOUBLE_EQ(dist(at(same, 0.5).pos, k.pos), 0.0) << "two identical keys: the camera stays";
}

TEST(CameraKeyframes, ProgressOfAFrameRunsFromZeroToOne) {
	EXPECT_DOUBLE_EQ(progressOf(0, 120), 0.0);
	EXPECT_DOUBLE_EQ(progressOf(119, 120), 1.0);
	EXPECT_NEAR(progressOf(60, 121), 0.5, 1e-12);
	EXPECT_DOUBLE_EQ(progressOf(0, 1), 0.0);
	EXPECT_DOUBLE_EQ(progressOf(5, 0), 0.0);
	EXPECT_DOUBLE_EQ(progressOf(500, 10), 1.0);
}

TEST(CameraKeyframes, LoadReadsAFileAndSaysWhichFileIsWrong) {
	const std::string missing = (std::filesystem::temp_directory_path() / "rt_no_such_keyframes.txt").string();
	const ParseResult none = load(missing);
	EXPECT_FALSE(none.ok);
	EXPECT_TRUE(none.unreadable);
	EXPECT_NE(none.error.find("rt_no_such_keyframes.txt"), std::string::npos);

	const std::filesystem::path file = std::filesystem::temp_directory_path() / "rt_keyframes_test.txt";
	{
		std::ofstream out(file);
		out << "key 0 1 8  0 1 0\nkey 4 1 6  0 1 0\n";
	}
	const ParseResult good = load(file.string());
	EXPECT_TRUE(good.ok) << good.error;
	EXPECT_EQ(good.path.keys.size(), 2u);
	{
		std::ofstream out(file);
		out << "key 0 1 8  0 1 0\nkey oops\n";
	}
	const ParseResult bad = load(file.string());
	EXPECT_FALSE(bad.ok);
	EXPECT_FALSE(bad.unreadable) << "the file opened; its contents are wrong";
	EXPECT_NE(bad.error.find("rt_keyframes_test.txt"), std::string::npos);
	EXPECT_NE(bad.error.find("line 2"), std::string::npos);
	std::error_code ec;
	std::filesystem::remove(file, ec);
}

// ---- field of view and lens ------------------------------------------------------------------------------------------------------------

namespace {

Path withLens(double fovA, double fovB, double lensA, double lensB) {
	Path p;
	p.ease = false;
	for (int i = 0; i < 2; ++i) {
		Key k;
		k.pos[0] = i * 10.0;
		k.target[0] = i * 10.0;
		k.target[2] = 5;
		k.fov = i == 0 ? fovA : fovB;
		k.lensRadius = i == 0 ? lensA : lensB;
		k.focus = 4.0 + i * 2.0;
		p.keys.push_back(k);
	}
	return p;
}

}  // namespace

TEST(CameraKeyframes, KeysCanCarryAFieldOfViewAndALens) {
	const ParseResult r = parse(
		"key 0 1 8  0 1 0\n"
		"key 4 1 6  0 1 0  60\n"
		"key 0 1 4  0 1 0  35 0.25 3.5\n");
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_DOUBLE_EQ(r.path.keys[0].fov, 0.0) << "not given";
	EXPECT_FALSE(r.path.keys[0].hasLens());
	EXPECT_DOUBLE_EQ(r.path.keys[1].fov, 60.0);
	EXPECT_FALSE(r.path.keys[1].hasLens()) << "a field of view alone has no lens";
	EXPECT_DOUBLE_EQ(r.path.keys[2].lensRadius, 0.25);
	EXPECT_DOUBLE_EQ(r.path.keys[2].focus, 3.5);
	EXPECT_TRUE(r.path.keys[2].hasLens());
	EXPECT_TRUE(parse("key 0 1 8  0 1 0  40 0 6\nkey 1 1 8  0 1 0  40 0 6\n").ok) << "a lens radius of 0 is a real value: sharp everywhere";
}

TEST(CameraKeyframes, RejectsAFieldOfViewOrLensThatCannotBe) {
	const std::string two = "key 0 0 1 0 0 0\n";
	EXPECT_FALSE(parse(two + "key 1 0 1 0 0 0 0\n").ok) << "fov 0";
	EXPECT_FALSE(parse(two + "key 1 0 1 0 0 0 180\n").ok);
	EXPECT_FALSE(parse(two + "key 1 0 1 0 0 0 -10\n").ok);
	EXPECT_FALSE(parse(two + "key 1 0 1 0 0 0 40 -0.1 5\n").ok) << "negative lens radius";
	EXPECT_FALSE(parse(two + "key 1 0 1 0 0 0 40 0.1 0\n").ok) << "no focus distance";
	EXPECT_FALSE(parse(two + "key 1 0 1 0 0 0 forty\n").ok);
}

TEST(CameraKeyframes, TheFieldOfViewAndLensAreOnTheCurveWhenEveryKeyGivesThem) {
	const Path p = withLens(30.0, 70.0, 0.0, 0.4);
	const Key start = at(p, 0.0), end = at(p, 1.0), mid = at(p, 0.5);
	EXPECT_DOUBLE_EQ(start.fov, 30.0);
	EXPECT_DOUBLE_EQ(end.fov, 70.0);
	EXPECT_NEAR(mid.fov, 50.0, 1e-9);
	EXPECT_DOUBLE_EQ(start.lensRadius, 0.0);
	EXPECT_DOUBLE_EQ(end.lensRadius, 0.4);
	EXPECT_NEAR(mid.lensRadius, 0.2, 1e-9);
	EXPECT_NEAR(mid.focus, 5.0, 1e-9);
	EXPECT_TRUE(start.hasLens());
}

TEST(CameraKeyframes, AKeyWithoutAFieldOfViewLeavesItToTheScene) {
	Path p = withLens(30.0, 70.0, 0.0, 0.4);
	p.keys[1].fov = 0;   // one key does not say: nothing is interpolated
	const Key mid = at(p, 0.5);
	EXPECT_DOUBLE_EQ(mid.fov, 0.0);
	EXPECT_FALSE(mid.hasLens()) << "no field of view given for every key, no lens either";
	Path q = withLens(30.0, 70.0, 0.0, 0.4);
	q.keys[0].lensRadius = -1;   // fields of view given, lens not
	const Key m = at(q, 0.5);
	EXPECT_NEAR(m.fov, 50.0, 1e-9);
	EXPECT_FALSE(m.hasLens());
	EXPECT_DOUBLE_EQ(m.lensRadius, -1.0);
	EXPECT_DOUBLE_EQ(m.focus, 0.0);
}

TEST(CameraKeyframes, TheFieldOfViewStaysWhatACameraCanDoThroughASwing) {
	Path p;
	p.ease = false;
	for (double fov : {20.0, 160.0, 20.0, 160.0}) {   // a spline overshoots between sharp changes
		Key k;
		k.pos[0] = p.keys.size() * 5.0;
		k.target[2] = 3;
		k.fov = fov;
		p.keys.push_back(k);
	}
	for (int i = 0; i <= 300; ++i) {
		const double fov = at(p, i / 300.0).fov;
		EXPECT_GE(fov, 1.0);
		EXPECT_LE(fov, 170.0);
	}
	EXPECT_DOUBLE_EQ(at(p, 0.0).fov, 20.0);
	EXPECT_DOUBLE_EQ(at(p, 1.0).fov, 160.0);
}

TEST(CameraKeyframes, TheFileWrittenKeepsTheFieldOfViewAndLens) {
	const Path p = withLens(30.0, 70.0, 0.0, 0.4);
	const ParseResult r = parse(toText(p));
	ASSERT_TRUE(r.ok) << r.error;
	for (std::size_t i = 0; i < 2; ++i) {
		EXPECT_DOUBLE_EQ(r.path.keys[i].fov, p.keys[i].fov);
		EXPECT_DOUBLE_EQ(r.path.keys[i].lensRadius, p.keys[i].lensRadius);
		EXPECT_DOUBLE_EQ(r.path.keys[i].focus, p.keys[i].focus);
	}
	Path plain = straight(true);
	const std::string text = toText(plain);
	EXPECT_EQ(text.find("fov"), std::string::npos);
	EXPECT_FALSE(parse(text).path.keys[0].hasLens());
}

// ---- holds ---------------------------------------------------------------------------------------------------------------------------

namespace {

// A (x 0), B (x 10), C (x 20): holds in seconds on each.
Path heldPath(double a, double b, double c, bool ease) {
	Path p = straight(ease);
	p.keys[0].hold = a;
	p.keys[1].hold = b;
	p.keys[2].hold = c;
	return p;
}

}  // namespace

TEST(CameraKeyframes, AHoldIsWrittenAndReadBack) {
	const ParseResult r = parse("key 0 1 8  0 1 0  hold 1.5\nkey 4 1 6  0 1 0  40  hold 2\nkey 0 1 4  0 1 0  35 0.2 3 hold 0\n");
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_DOUBLE_EQ(r.path.keys[0].hold, 1.5);
	EXPECT_DOUBLE_EQ(r.path.keys[1].hold, 2.0);
	EXPECT_DOUBLE_EQ(r.path.keys[1].fov, 40.0);
	EXPECT_DOUBLE_EQ(r.path.keys[2].hold, 0.0);
	EXPECT_TRUE(r.path.keys[2].hasLens());
	const ParseResult again = parse(toText(r.path));
	ASSERT_TRUE(again.ok) << again.error;
	EXPECT_DOUBLE_EQ(again.path.keys[0].hold, 1.5);
	EXPECT_DOUBLE_EQ(again.path.keys[1].hold, 2.0);
	EXPECT_EQ(toText(r.path).find("hold 0"), std::string::npos) << "no hold, nothing written";
	EXPECT_FALSE(parse("key 0 0 1 0 0 0 hold -1\nkey 1 0 1 0 0 0\n").ok);
	EXPECT_FALSE(parse("key 0 0 1 0 0 0 hold\nkey 1 0 1 0 0 0\n").ok);
	EXPECT_FALSE(parse("key 0 0 1 0 0 0 hold 1e9\nkey 1 0 1 0 0 0\n").ok);
	EXPECT_FALSE(parse("key 0 0 1 0 0 0 hold abc\nkey 1 0 1 0 0 0\n").ok);
	EXPECT_FALSE(parse("key 0 0 1 0 0 0 40 hold\n").ok);
}

TEST(CameraKeyframes, TheCameraStandsStillForTheHoldAndThenMoves) {
	// 10 seconds: one second at A, then the moves share 9 seconds (to B and on to C, equal stretches), no holds elsewhere.
	const Path p = heldPath(1.0, 0.0, 0.0, false);
	EXPECT_DOUBLE_EQ(at(p, 0.0, 10.0).pos[0], 0.0);
	EXPECT_DOUBLE_EQ(at(p, 0.05, 10.0).pos[0], 0.0) << "half a second in: still held";
	EXPECT_DOUBLE_EQ(at(p, 0.1, 10.0).pos[0], 0.0) << "the hold ends at one second";
	EXPECT_GT(at(p, 0.12, 10.0).pos[0], 0.0);
	EXPECT_NEAR(at(p, 0.1 + 0.45, 10.0).pos[0], 10.0, 1e-9) << "half way through the 9 seconds of moving: the middle key";
	EXPECT_NEAR(at(p, 1.0, 10.0).pos[0], 20.0, 1e-9);
}

TEST(CameraKeyframes, AHoldInTheMiddleAndAtTheEnd) {
	const Path p = heldPath(0.0, 2.0, 1.0, false);   // 10 seconds: 2 at B, 1 at the end; 7 seconds of moving, 3.5 each way
	EXPECT_NEAR(at(p, 3.5 / 10.0, 10.0).pos[0], 10.0, 1e-9) << "arrives at B after 3.5 seconds";
	EXPECT_DOUBLE_EQ(at(p, 4.0 / 10.0, 10.0).pos[0], 10.0) << "held at B";
	EXPECT_DOUBLE_EQ(at(p, 5.5 / 10.0, 10.0).pos[0], 10.0) << "still held until 5.5 seconds";
	EXPECT_GT(at(p, 6.0 / 10.0, 10.0).pos[0], 10.0);
	EXPECT_NEAR(at(p, 9.0 / 10.0, 10.0).pos[0], 20.0, 1e-9) << "at C from 9 seconds on";
	EXPECT_DOUBLE_EQ(at(p, 9.6 / 10.0, 10.0).pos[0], 20.0) << "the pause at the end";
	EXPECT_DOUBLE_EQ(at(p, 1.0, 10.0).pos[0], 20.0);
}

TEST(CameraKeyframes, WithoutADurationOrWithoutHoldsTheHoldsDoNotApply) {
	const Path p = heldPath(2.0, 2.0, 2.0, true);
	const Path plain = straight(true);
	for (int i = 0; i <= 20; ++i) {
		const double t = i / 20.0;
		EXPECT_DOUBLE_EQ(at(p, t).pos[0], at(plain, t).pos[0]) << "holds are seconds: no length, no pause";
		EXPECT_DOUBLE_EQ(at(heldPath(0, 0, 0, true), t, 10.0).pos[0], at(plain, t).pos[0]) << "no holds: as before";
	}
}

TEST(CameraKeyframes, HoldsThatAreMostOfTheVideoAreShortenedToLeaveTimeToMove) {
	const Path p = heldPath(8.0, 8.0, 8.0, false);   // 24 seconds of holds in a 10 second video: scaled to 9 seconds, 1 left to move
	// Each hold is 3 seconds; the 1 second of moving is half to B, half to C: A held 0-3, move 3-3.5, B held 3.5-6.5, move 6.5-7, C held 7-10.
	EXPECT_DOUBLE_EQ(at(p, 0.25, 10.0).pos[0], 0.0);
	EXPECT_NEAR(at(p, 0.35, 10.0).pos[0], 10.0, 1e-9);
	EXPECT_DOUBLE_EQ(at(p, 0.5, 10.0).pos[0], 10.0);
	EXPECT_DOUBLE_EQ(at(p, 0.8, 10.0).pos[0], 20.0);
}

TEST(CameraKeyframes, EasingStopsSmoothlyAtEveryHoldNotJustTheEnds) {
	// A, then B held for 2 seconds, then C: with ease each of the two moves starts and ends gently, so just after leaving the hold the camera has barely moved.
	const Path eased = heldPath(0.0, 2.0, 0.0, true), steady = heldPath(0.0, 2.0, 0.0, false);
	const double leaving = 6.0 / 10.0 + 0.01;   // a moment after the hold ends at 6 seconds (4 seconds of moving each way)
	EXPECT_LT(at(eased, leaving, 10.0).pos[0] - 10.0, 0.2 * (at(steady, leaving, 10.0).pos[0] - 10.0));
	const double arriving = 4.0 / 10.0 - 0.01;   // and just before it reaches B
	EXPECT_LT(10.0 - at(eased, arriving, 10.0).pos[0], 0.2 * (10.0 - at(steady, arriving, 10.0).pos[0]));
	EXPECT_NEAR(at(eased, 0.5, 10.0).pos[0], 10.0, 1e-9);
	EXPECT_NEAR(at(eased, 1.0, 10.0).pos[0], 20.0, 1e-9);
}

TEST(CameraKeyframes, AKeyWithoutAHoldIsPassedThroughWithoutStoppingEvenWhenOthersHold) {
	// A (hold 1), B, C, D (hold 1): B and C are not stops, so the run A..D is one smooth move (with ease, slow only at its ends).
	Path p;
	p.ease = true;
	for (double x : {0.0, 10.0, 20.0, 30.0}) {
		Key k;
		k.pos[0] = x;
		k.target[0] = x;
		k.target[2] = 5;
		p.keys.push_back(k);
	}
	p.keys[0].hold = 1.0;
	p.keys[3].hold = 1.0;
	double last = -1.0;
	for (int i = 0; i <= 200; ++i) {
		const double x = at(p, i / 200.0, 10.0).pos[0];
		EXPECT_GE(x, last - 1e-9) << "never backwards";
		last = x;
	}
	EXPECT_NEAR(at(p, 0.5, 10.0).pos[0], 15.0, 1e-9) << "the middle of the 8 seconds of moving is the middle of the run";
}

// `at T`: the camera is at a key at the time it says, however far the keys are apart; standing still it can change its focus; all or none of the keys say it.
TEST(CameraKeyframes, KeysAtTimesAreReachedAtThoseTimes) {
	const ParseResult r = parse("ease 0\nkey 0 0 0  0 0 1  at 1\nkey 1 0 0  1 0 1  at 2\nkey 100 0 0  100 0 1  at 8\n");
	ASSERT_TRUE(r.ok) << r.error;
	const double duration = 10.0;
	EXPECT_DOUBLE_EQ(at(r.path, 0.0, duration).pos[0], 0.0) << "held before the first";
	EXPECT_NEAR(at(r.path, 0.2, duration).pos[0], 1.0, 1e-9) << "at 2 s, though the far key is 99 m further";
	EXPECT_NEAR(at(r.path, 0.8, duration).pos[0], 100.0, 1e-9);
	EXPECT_DOUBLE_EQ(at(r.path, 1.0, duration).pos[0], 100.0) << "held after the last";
	const double half = at(r.path, 0.5, duration).pos[0];
	EXPECT_GT(half, 30.0);
	EXPECT_LT(half, 70.0);
}

TEST(CameraKeyframes, AFocusPullWithTheCameraStandingStill) {
	const ParseResult r = parse("key 0 0 0  0 0 1  40 0.05 3  at 0\nkey 0 0 0  0 0 1  40 0.05 9  at 2\n");
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_NEAR(at(r.path, 0.5, 2.0).focus, 6.0, 1e-9);
	EXPECT_NEAR(at(r.path, 1.0, 2.0).focus, 9.0, 1e-9);
}

TEST(CameraKeyframes, AtMustBeOnEveryKeyAndGoUp) {
	EXPECT_FALSE(parse("key 0 0 0 0 0 1 at 1\nkey 1 0 0 0 0 1\n").ok);
	EXPECT_FALSE(parse("key 0 0 0 0 0 1 at 2\nkey 1 0 0 0 0 1 at 1\n").ok);
	EXPECT_FALSE(parse("key 0 0 0 0 0 1 at 1 hold 2\nkey 1 0 0 0 0 1 at 5\n").ok);
	const ParseResult r = parse("key 0 0 0 0 0 1 at 1\nkey 1 0 0 0 0 1 at 3\n");
	ASSERT_TRUE(r.ok);
	EXPECT_NE(toText(r.path).find("at 3"), std::string::npos) << "written back";
}
