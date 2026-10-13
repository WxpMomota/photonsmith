#ifndef OBJECT_DRAG_MATH_H
#define OBJECT_DRAG_MATH_H
// object_drag_math.h -- Qt-free arithmetic of dragging an object in the Live Preview picture: where the mouse's ray meets the plane the object slides on.
// The picture's own camera (camera_math::CameraBasis, read back from the renderer with every frame) is the only camera used, so what moves under the cursor is
// exactly what the picture shows. World "up" is +y, as everywhere else in the Live Preview (camera_math.h, MainWindow::applyTranslateDelta()).

#include <cmath>

#include "camera_math.h"

namespace object_drag {

using camera_math::CameraBasis;
using camera_math::Vec3;

// The picture position (s, t) as camera_math::projectToScreen() gives it: s left to right, t bottom to top, both in [0, 1] across the image.
inline Vec3 rayDirection(const CameraBasis &cb, double s, double t) {
	return cb.lowerLeftCorner + cb.horizontal * s + cb.vertical * t - cb.origin;
}

// Where the ray from the camera through (s, t) meets the plane through `point` with normal `normal`. False when the ray runs (nearly) parallel to the plane or
// the plane is behind the camera.
inline bool intersectPlane(const CameraBasis &cb, double s, double t, const Vec3 &point, const Vec3 &normal, Vec3 &out) {
	const Vec3 dir = rayDirection(cb, s, t);
	const double dirLength = camera_math::length(dir);
	const double denom = camera_math::dot(dir, normal);
	if (dirLength < 1e-12 || std::fabs(denom) < 1e-4 * dirLength * camera_math::length(normal)) return false;
	const double k = camera_math::dot(point - cb.origin, normal) / denom;
	if (k <= 0.0) return false;
	out = cb.origin + dir * k;
	return true;
}

// How far the grabbed point (`grab`, where the mouse first met the object's surface) moves when the mouse is now at picture position (s1, t1).
//   floor (vertical == false): the point slides on the horizontal plane it sits in, so an object on a floor stays on it.
//   vertical == true: it moves straight up or down, to wherever the mouse points on the vertical plane facing the camera through it.
// `maxLength` limits the move (a ray that grazes the floor meets it very far away); the move is shortened to it. False when the mouse has no meaningful position on
// the plane (the ray is parallel to it, or it is behind the camera): the caller then keeps the last move.
inline bool dragDelta(const CameraBasis &cb, const Vec3 &grab, double s1, double t1, bool vertical, double maxLength, Vec3 &delta) {
	Vec3 hit;
	if (!vertical) {
		if (!intersectPlane(cb, s1, t1, grab, Vec3{0.0, 1.0, 0.0}, hit)) return false;
		delta = Vec3{hit.x - grab.x, 0.0, hit.z - grab.z};
	} else {
		Vec3 toGrab = grab - cb.origin;
		toGrab.y = 0.0;
		const Vec3 normal = camera_math::normalized(toGrab);
		if (camera_math::length(normal) < 0.5) return false;   // looking straight down: no vertical plane faces the camera
		if (!intersectPlane(cb, s1, t1, grab, normal, hit)) return false;
		delta = Vec3{0.0, hit.y - grab.y, 0.0};
	}
	const double len = camera_math::length(delta);
	if (maxLength > 0.0 && len > maxLength) delta = delta * (maxLength / len);
	return true;
}

// One drag from the first press to the release, which may change between the floor and the vertical mode (Shift) any number of times. Each switch keeps what has
// been moved so far and starts the new mode from where the mouse is then, so the object does not jump: before this, Shift held half way through a drag threw
// the horizontal part of the move away (the vertical plane gives no horizontal move at all).
struct DragSession {
	Vec3 base;          // the move so far, kept at the last switch of mode
	Vec3 modeOffset;    // what the new mode's plane gives for the mouse at that switch, taken off so the object stays put
	bool lastVertical = false;
	bool started = false;

	void reset() { base = Vec3{}; modeOffset = Vec3{}; lastVertical = false; started = false; }

	// The total move of the object since the press, for the mouse now at (s1, t1). `grab` is where the mouse first met the object. False when the mouse has no
	// position on the current mode's plane (the caller keeps the last move); `total` is left alone then.
	bool update(const CameraBasis &cb, const Vec3 &grab, double s1, double t1, bool vertical, double maxLength, Vec3 &total) {
		if (!started) {
			started = true;
			lastVertical = vertical;
		} else if (vertical != lastVertical) {
			base = total;   // the move so far, as the caller last saw it
			Vec3 atSwitch;
			modeOffset = dragDelta(cb, grab + base, s1, t1, vertical, maxLength, atSwitch) ? atSwitch : Vec3{};   // clamped like the deltas that follow, or the object would jump when the cap applies
			lastVertical = vertical;
		}
		Vec3 delta;
		if (!dragDelta(cb, grab + base, s1, t1, vertical, maxLength, delta)) return false;
		total = base + (delta - modeOffset);
		return true;
	}
};

// The world-space move of a keypress for an object, in the camera's own terms (the same W/A/S/D and Up/Down as the camera's free fly): `forwardSteps` along the way
// the camera looks (flattened onto the floor, so the object stays on it), `rightSteps` to the camera's right, `upSteps` straight up, each times `step`.
// False when the camera looks straight down or up (no horizontal forward).
inline bool keyMove(const CameraBasis &cb, int forwardSteps, int rightSteps, int upSteps, double step, Vec3 &delta) {
	Vec3 forward = cb.lowerLeftCorner + cb.horizontal * 0.5 + cb.vertical * 0.5 - cb.origin;
	forward.y = 0.0;
	forward = camera_math::normalized(forward);
	if (camera_math::length(forward) < 0.5) return false;
	// To the right of the PICTURE: the viewport's own horizontal axis, flattened onto the floor (a pbrt scene's picture is mirrored, so this is not always cross(forward, up)).
	Vec3 right = camera_math::normalized(Vec3{cb.horizontal.x, 0.0, cb.horizontal.z});
	if (camera_math::length(right) < 0.5) right = camera_math::normalized(camera_math::cross(forward, Vec3{0.0, 1.0, 0.0}));
	delta = forward * (forwardSteps * step) + right * (rightSteps * step) + Vec3{0.0, upSteps * step, 0.0};
	return true;
}

}  // namespace object_drag

#endif  // OBJECT_DRAG_MATH_H
