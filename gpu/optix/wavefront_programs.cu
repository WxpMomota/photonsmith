// wavefront_programs.cu -- OptiX device programs for the wavefront GPU path
// tracer (top-level). Split into logical units, like the recursive backend's
// optix_programs.cu; this file just includes them all, in dependency order.
//
// Unlike optix_programs.cu (recursive), these programs do NOT shade or
// scatter. They only drive the intersection phase of the wavefront loop -
// see wavefront_common.h's own header comment for the full picture
// (raygen/intersect/shadow-phase summary).

#include "wavefront_common.h"
#include "wavefront_intersection_sphere.h"
#include "wavefront_intersection_quad.h"
#include "wavefront_intersection_bilinear_patch.h"
#include "wavefront_intersection_triangle.h"
#include "wavefront_intersection_disk_cylinder.h"
#include "wavefront_miss.h"
#include "wavefront_anyhit_shadow.h"   // (before the raygen programs: __raygen__wf_shadow uses its wf_shadow_through_medium)
#include "wavefront_raygen.h"
#include "wavefront_bevel.h"   // __raygen__wf_bevel: the bevel shader's normals of a bounce's hits
#include "wavefront_probe_cache.h"
