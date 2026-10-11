#include <gtest/gtest.h>

#include "../../qt_gui/object_drag_math.h"

#include <cmath>

using namespace object_drag;

namespace {

// A camera at (0, 5, 10) looking at the origin with a 90 degree vertical field of view and a square picture, in the form the renderer reports it.
CameraBasis testCamera() {
	const Vec3 origin{0.0, 5.0, 10.0};
	const Vec3 forward = camera_math::normalized(Vec3{0.0, 0.0, 0.0} - origin);
	const Vec3 right = camera_math::normalized(camera_math::cross(forward, Vec3{0.0, 1.0, 0.0}));
	const Vec3 up = camera_math::cross(right, forward);
	CameraBasis cb;
	cb.origin = origin;
	cb.horizontal = right * 2.0;
	cb.vertical = up * 2.0;
	cb.lowerLeftCorner = origin + forward - right - up;
	return cb;
}

// Where a world point shows in the picture.
void screenOf(const CameraBasis &cb, const Vec3 &p, double &s, double &t) {
	const camera_math::ScreenProjection sp = camera_math::projectToScreen(p, cb);
	ASSERT_TRUE(sp.inFront);
	s = sp.s;
	t = sp.t;
}

}  // namespace

TEST(ObjectDragMathTest, FloorDragFollowsTheCursorOnTheFloor) {
	const CameraBasis cb = testCamera();
	const Vec3 grab{2.0, 0.0, 0.0};
	double s, t;
	screenOf(cb, Vec3{5.0, 0.0, 3.0}, s, t);
	Vec3 delta;
	ASSERT_TRUE(dragDelta(cb, grab, s, t, false, 0.0, delta));
	EXPECT_NEAR(delta.x, 3.0, 1e-9);
	EXPECT_NEAR(delta.y, 0.0, 1e-12);
	EXPECT_NEAR(delta.z, 3.0, 1e-9);
}

TEST(ObjectDragMathTest, FloorDragOfAPointAboveTheFloorStaysInItsOwnPlane) {
	const CameraBasis cb = testCamera();
	const Vec3 grab{1.0, 2.0, 0.0};   // the top of a box
	double s, t;
	screenOf(cb, Vec3{-1.0, 2.0, 1.0}, s, t);
	Vec3 delta;
	ASSERT_TRUE(dragDelta(cb, grab, s, t, false, 0.0, delta));
	EXPECT_NEAR(delta.x, -2.0, 1e-9);
	EXPECT_NEAR(delta.y, 0.0, 1e-12) << "a floor drag never lifts";
	EXPECT_NEAR(delta.z, 1.0, 1e-9);
}

TEST(ObjectDragMathTest, VerticalDragMovesStraightUpAndDown) {
	const CameraBasis cb = testCamera();
	const Vec3 grab{0.0, 0.0, 0.0};
	double s, t;
	screenOf(cb, Vec3{0.0, 2.0, 0.0}, s, t);
	Vec3 delta;
	ASSERT_TRUE(dragDelta(cb, grab, s, t, true, 0.0, delta));
	EXPECT_NEAR(delta.x, 0.0, 1e-12);
	EXPECT_NEAR(delta.y, 2.0, 1e-9);
	EXPECT_NEAR(delta.z, 0.0, 1e-12);
	// A cursor to the side of the object does not move it sideways.
	screenOf(cb, Vec3{3.0, 2.0, 0.0}, s, t);
	ASSERT_TRUE(dragDelta(cb, grab, s, t, true, 0.0, delta));
	EXPECT_NEAR(delta.x, 0.0, 1e-12);
}

TEST(ObjectDragMathTest, ARayParallelToTheFloorGivesNoMove) {
	CameraBasis cb = testCamera();
	// Level camera at floor height looking along -z: the middle of the picture is parallel to the floor.
	cb.origin = Vec3{0.0, 0.0, 10.0};
	cb.lowerLeftCorner = Vec3{-1.0, -1.0, 9.0};
	cb.horizontal = Vec3{2.0, 0.0, 0.0};
	cb.vertical = Vec3{0.0, 2.0, 0.0};
	Vec3 delta;
	EXPECT_FALSE(dragDelta(cb, Vec3{0.0, 0.0, 0.0}, 0.5, 0.5, false, 0.0, delta));
}

TEST(ObjectDragMathTest, AFloorBehindTheCameraGivesNoMove) {
	const CameraBasis cb = testCamera();
	Vec3 delta;
	// The top edge of the picture looks above the horizon: the ray never meets the floor in front of the camera.
	EXPECT_FALSE(dragDelta(cb, Vec3{0.0, 0.0, 0.0}, 0.5, 1.0, false, 0.0, delta));
}

TEST(ObjectDragMathTest, MoveIsShortenedToTheLimit) {
	const CameraBasis cb = testCamera();
	double s, t;
	screenOf(cb, Vec3{20.0, 0.0, 0.0}, s, t);
	Vec3 delta;
	ASSERT_TRUE(dragDelta(cb, Vec3{0.0, 0.0, 0.0}, s, t, false, 5.0, delta));
	EXPECT_NEAR(camera_math::length(delta), 5.0, 1e-9);
	EXPECT_NEAR(delta.x, 5.0, 1e-9);
}

TEST(ObjectDragMathTest, NoVerticalPlaneFacesACameraLookingStraightDown) {
	CameraBasis cb = testCamera();
	cb.origin = Vec3{0.0, 10.0, 0.0};
	cb.lowerLeftCorner = Vec3{-1.0, 9.0, 1.0};
	cb.horizontal = Vec3{2.0, 0.0, 0.0};
	cb.vertical = Vec3{0.0, 0.0, -2.0};
	Vec3 delta;
	EXPECT_FALSE(dragDelta(cb, Vec3{0.0, 0.0, 0.0}, 0.5, 0.5, true, 0.0, delta));
}

TEST(ObjectDragMathTest, ShiftHalfWayThroughADragKeepsTheHorizontalMoveAndDoesNotJump) {
	const CameraBasis cb = testCamera();
	const Vec3 grab{2.0, 0.0, 0.0};
	DragSession drag;
	Vec3 total;
	double s, t;
	// 1. drag over the floor to (5, 0, 3)
	screenOf(cb, Vec3{5.0, 0.0, 3.0}, s, t);
	ASSERT_TRUE(drag.update(cb, grab, s, t, false, 0.0, total));
	EXPECT_NEAR(total.x, 3.0, 1e-9);
	EXPECT_NEAR(total.z, 3.0, 1e-9);
	// 2. Shift goes down with the mouse where it is: nothing moves (no jump), and the horizontal move stays
	ASSERT_TRUE(drag.update(cb, grab, s, t, true, 0.0, total));
	EXPECT_NEAR(total.x, 3.0, 1e-9);
	EXPECT_NEAR(total.y, 0.0, 1e-9);
	EXPECT_NEAR(total.z, 3.0, 1e-9);
	// 3. the mouse goes up the picture: the object goes up, still 3 and 3 across
	double s2, t2;
	screenOf(cb, Vec3{5.0, 1.5, 3.0}, s2, t2);
	ASSERT_TRUE(drag.update(cb, grab, s2, t2, true, 0.0, total));
	EXPECT_GT(total.y, 0.5);
	EXPECT_NEAR(total.x, 3.0, 1e-9);
	EXPECT_NEAR(total.z, 3.0, 1e-9);
	// 4. Shift up again: back on the floor plane through the lifted point, the lift stays
	const double lifted = total.y;
	ASSERT_TRUE(drag.update(cb, grab, s2, t2, false, 0.0, total));
	EXPECT_NEAR(total.y, lifted, 1e-9);
	EXPECT_NEAR(total.x, 3.0, 1e-9);
	EXPECT_NEAR(total.z, 3.0, 1e-9);
}

TEST(ObjectDragMathTest, ADragThatNeverSwitchesBehavesAsDragDelta) {
	const CameraBasis cb = testCamera();
	const Vec3 grab{2.0, 0.0, 0.0};
	DragSession drag;
	double s, t;
	screenOf(cb, Vec3{5.0, 0.0, 3.0}, s, t);
	Vec3 total, expected;
	ASSERT_TRUE(drag.update(cb, grab, s, t, false, 0.0, total));
	ASSERT_TRUE(dragDelta(cb, grab, s, t, false, 0.0, expected));
	EXPECT_NEAR(total.x, expected.x, 1e-12);
	EXPECT_NEAR(total.z, expected.z, 1e-12);
}

TEST(ObjectDragMathTest, KeysMoveAnObjectRelativeToTheView) {
	const CameraBasis cb = testCamera();   // at (0, 5, 10) looking at the origin: forward is -z on the floor, right is +x
	Vec3 delta;
	ASSERT_TRUE(keyMove(cb, 1, 0, 0, 2.0, delta));
	EXPECT_NEAR(delta.x, 0.0, 1e-9);
	EXPECT_NEAR(delta.y, 0.0, 1e-12) << "forward stays on the floor even though the camera looks down";
	EXPECT_NEAR(delta.z, -2.0, 1e-9);
	ASSERT_TRUE(keyMove(cb, 0, 1, 0, 2.0, delta));
	EXPECT_NEAR(delta.x, 2.0, 1e-9);
	EXPECT_NEAR(delta.z, 0.0, 1e-9);
	ASSERT_TRUE(keyMove(cb, 0, 0, -1, 2.0, delta));
	EXPECT_NEAR(delta.y, -2.0, 1e-12);
	EXPECT_NEAR(delta.x, 0.0, 1e-12);
}

TEST(ObjectDragMathTest, KeysFollowAnOrbitedCamera) {
	// The same camera turned a quarter circle round the origin (now at +x looking at -x): forward is -x, and with y up the camera's right is -z.
	CameraBasis cb;
	cb.origin = Vec3{10.0, 5.0, 0.0};
	const Vec3 forward = camera_math::normalized(Vec3{0.0, 0.0, 0.0} - cb.origin);
	const Vec3 right = camera_math::normalized(camera_math::cross(forward, Vec3{0.0, 1.0, 0.0}));
	const Vec3 up = camera_math::cross(right, forward);
	cb.horizontal = right * 2.0;
	cb.vertical = up * 2.0;
	cb.lowerLeftCorner = cb.origin + forward - right - up;
	Vec3 delta;
	ASSERT_TRUE(keyMove(cb, 1, 0, 0, 1.0, delta));
	EXPECT_NEAR(delta.x, -1.0, 1e-9);
	EXPECT_NEAR(delta.z, 0.0, 1e-9);
	ASSERT_TRUE(keyMove(cb, 0, 1, 0, 1.0, delta));
	EXPECT_NEAR(delta.z, -1.0, 1e-9) << "to the right of a camera looking down -x is -z";
}

TEST(ObjectDragMathTest, NoHorizontalForwardWhenLookingStraightDown) {
	CameraBasis cb;
	cb.origin = Vec3{0.0, 10.0, 0.0};
	cb.lowerLeftCorner = Vec3{-1.0, 9.0, 1.0};
	cb.horizontal = Vec3{2.0, 0.0, 0.0};
	cb.vertical = Vec3{0.0, 0.0, -2.0};
	Vec3 delta;
	EXPECT_FALSE(keyMove(cb, 1, 0, 0, 1.0, delta));
}
