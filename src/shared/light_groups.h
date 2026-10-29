#pragma once
// light_groups.h -- light groups, as Blender's Cycles has them (intern/cycles/kernel/film/light_passes.h, "lightgroup" passes): the picture's light split by which lights
// it came from, written as passes beside the picture, so that a key, a fill and a rim - or the sun and the lamps - can be turned up or down after the render without
// rendering again. The passes add up to the picture, but for what no group's light reaches (light from shapes and lights in no group, emission of fog).
//
// In a pbrt scene (our extension of the format): a light's `"string lightgroup" "key"` - any LightSource (point, spot, distant, goniometric, projection, infinite) or
// AreaLightSource. Up to kMax groups, numbered in the order the scene names them. The passes are `lightgroup.<name>.R/G/B` in the picture's EXR (or in
// `<picture>.lightgroups.exr` beside a PNG or PPM).
//
// Which path tracers split the light: the CPU path tracer (camera_path.h) and the OptiX wavefront renderer (a GPU render of a scene with light groups is made by it);
// a light's direct light (next-event estimation), an emitter a bounce hits and the sky a ray escapes to are each credited to the light's group.

#include <cstdint>
#include <string>
#include <vector>

namespace light_groups {

constexpr int kMax = 8;

// The index of group `name` among `names`, adding it when it is new; -1 for no name, or when there are kMax already (said in `warning`).
inline int indexOf(std::vector<std::string> &names, const std::string &name, std::string *warning = nullptr) {
	if (name.empty()) return -1;
	for (std::size_t i = 0; i < names.size(); ++i)
		if (names[i] == name) return static_cast<int>(i);
	if (static_cast<int>(names.size()) >= kMax) {
		if (warning) *warning = "more than " + std::to_string(kMax) + " light groups: \"" + name + "\" is in none";
		return -1;
	}
	names.push_back(name);
	return static_cast<int>(names.size()) - 1;
}

// The EXR channel names of group `name`.
inline std::vector<std::string> channelsOf(const std::string &name) {
	return {"lightgroup." + name + ".R", "lightgroup." + name + ".G", "lightgroup." + name + ".B"};
}

}  // namespace light_groups
