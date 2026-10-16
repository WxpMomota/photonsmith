// optix_programs_quad.cu -- OptiX device programs of the recursive backend: quad and bilinear patch intersection and closest-hit.
// One of the recursive backend's PTX modules; see optix_programs.cu for the split and for how the program groups combine them.

#include "../../src/shared/cpu_gpu.h"  // kMaxMediumBoundaryCrossings
#include "optix_disk_cylinder_helpers.h"
#include "optix_device_helpers.h"
#include "optix_intersection_quad.h"
#include "optix_intersection_bilinear_patch.h"
