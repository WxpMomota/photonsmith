// optix_programs_cylinder.cu -- OptiX device programs of the recursive backend: cylinder intersection and closest-hit.
// One of the recursive backend's PTX modules; see optix_programs.cu for the split and for how the program groups combine them.

#define RT_OPTIX_CYLINDER_ONLY   // optix_intersection_disk_cylinder.h: only this shape's section
#include "../../src/shared/cpu_gpu.h"  // kMaxMediumBoundaryCrossings
#include "optix_disk_cylinder_helpers.h"
#include "optix_device_helpers.h"
#include "optix_intersection_disk_cylinder.h"
