#pragma once
// light_group_tally.h -- the CPU path tracer's light groups (src/shared/light_groups.h): while a sample is traced, the light it gathers from each group's lights is
// credited to that group here (camera_path.h), and camera::render_pixel adds each group up over the pixel's samples as it adds the picture.

#include "../shared/light_groups.h"
#include "vec3.h"

namespace light_group_tally {

struct Tally {
	color c[light_groups::kMax];
	void reset() {
		for (color &x : c) x = color(0, 0, 0);
	}
};

// The tally of the sample this thread is tracing; null when the picture has no light groups.
inline thread_local Tally *current = nullptr;

inline void credit(int group, const color &light) {
	if (current && group >= 0 && group < light_groups::kMax) current->c[group] += light;
}

}  // namespace light_group_tally
