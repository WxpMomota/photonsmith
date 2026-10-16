// optix_programs_triangle.cu -- OptiX device programs of the recursive backend: triangle closest-hit and any-hit (the intersection test is OptiX's own).
// One of the recursive backend's PTX modules; see optix_programs.cu for the split and for how the program groups combine them.

#include "../../src/shared/cpu_gpu.h"  // kMaxMediumBoundaryCrossings
#include "optix_disk_cylinder_helpers.h"
#include "optix_device_helpers.h"
#include "optix_intersection_triangle.h"
