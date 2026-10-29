#pragma once
// light_link.h -- light linking and shadow linking, as Blender's Cycles has them (intern/cycles/kernel/light/light.h light_link_object_match, kernel/bvh/util.h
// intersection_skip_shadow_link): a light can light only some objects, and only some objects need cast its shadows. The film crew's way of lighting a character:
// a rim or key light that lights the character and not the set behind, a fill that the character does not shadow.
//
// In a pbrt scene (our extension of the format):
//   Shape "..." "string linkgroup" "pip"                 the shape is in group "pip" (a shape in no group is in group 0, "rest")
//   LightSource "spot" ... "string illuminates" [ "pip" ]   the light lights only groups listed ("rest": the shapes in no group; "cast" also every group "cast/..."); absent: every shape
//   AreaLightSource "diffuse" ... "string illuminates" [ "pip" "rest" ]
//   LightSource "spot" ... "string shadowsfrom" [ "pip" ]   only these groups' shapes cast the light's shadows; absent: every shape (point, spot and distant lights)
// A group is a number from 1 to 63 (the scene's first new name is 1); a mask has bit g for group g. The parser gives a linked shape its own copy of its material with
// its group, so every renderer reads a hit's group from the material it already has.
//
// What each path tracer does with them (CPU, OptiX recursive, OptiX wavefront):
//   * a light's direct light (next-event estimation) reaches a surface only when the light lights the surface's group;
//   * an emitter hit by a bounce adds its light only when it lights the group of the surface the bounce left (a camera ray sees every light, as in Cycles);
//   * a shadow ray towards a point, spot or distant light passes through the shapes whose group is not in the light's `shadowsfrom`.
// Not in the bidirectional, Metropolis and photon-mapping integrators, and not for the sky (they ignore the words). Shadow linking is not done for area lights: a
// bounce towards the light would still be stopped by the shape the shadow ray passes, so the two halves of multiple importance sampling would disagree.

#include <cstdint>

#include "cpu_gpu.h"

namespace light_link {

constexpr int kGroups = 64;                     // 0 (the shapes in no group) and 1..63
constexpr std::uint64_t kAll = ~0ull;           // a light with no `illuminates`/`shadowsfrom`

// Whether a light (or a shadow mask) with `mask` covers `group`. A group below 0 is the camera: it sees every light.
CPU_GPU bool covers(std::uint64_t mask, int group) {
	return group < 0 || group >= kGroups || ((mask >> group) & 1ull) != 0;
}

}  // namespace light_link
