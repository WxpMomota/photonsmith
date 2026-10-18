#pragma once
// day_cycle.h -- where the sun is, frame by frame, in a video of a day (the Scene Builder's "Day cycle..."): from sunrise to sunset along a smooth arc, crossing where it is now at noon.
//
// The height follows a sine (the sun rises quickly at first, hangs near its highest, and sets as it rose), from `horizonElevation` at the first frame, up to `noonElevation` in the middle, and back; the
// compass direction sweeps half a turn, clockwise seen from above (the way the sun goes in the northern hemisphere: the compass number falls), from 90 degrees one side of `noonAzimuth` to 90 degrees the
// other. Azimuth is the Builder's compass (scene_sky.h): 0 is +X, 90 is -Z, growing counter-clockwise seen from above. Header-only, std-only; unit tested.

#include <algorithm>
#include <cmath>

namespace day_cycle {

constexpr double kPi = 3.14159265358979323846;

struct Sun {
	double elevation = 0.0;   // degrees above the horizon
	double azimuth = 0.0;     // degrees, 0 to 360
};

// The sun at `t`, 0 (sunrise) to 1 (sunset).
inline Sun sunAt(double t, double noonElevation, double noonAzimuth, double horizonElevation = 3.0) {
	t = std::min(1.0, std::max(0.0, t));
	const double high = std::max(noonElevation, horizonElevation);
	Sun s;
	s.elevation = horizonElevation + (high - horizonElevation) * std::sin(kPi * t);
	s.azimuth = std::fmod(std::fmod(noonAzimuth + 90.0 - 180.0 * t, 360.0) + 360.0, 360.0);
	return s;
}

// Frame `index` of `frames`: the first is sunrise and the last sunset (a one-frame day is noon).
inline Sun sunOfFrame(int index, int frames, double noonElevation, double noonAzimuth, double horizonElevation = 3.0) {
	const double t = frames > 1 ? static_cast<double>(std::min(std::max(index, 0), frames - 1)) / (frames - 1) : 0.5;
	return sunAt(t, noonElevation, noonAzimuth, horizonElevation);
}

}  // namespace day_cycle
