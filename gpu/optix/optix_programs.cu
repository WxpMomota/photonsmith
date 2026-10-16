// optix_programs.cu -- OptiX device programs of the recursive backend: the core module.
//
// The recursive programs are several PTX modules, not one: every closest-hit program inlines the whole material system, and a single module of all of them took
// 171 s to compile in one cicc process (54.8 MB of PTX). Each geometry type has its own module, compiled in parallel (build_optix.targets):
//   optix_programs.cu                    this one: raygen, the two miss programs, the shadow any-hit programs, the probe ray programs
//   optix_programs_sphere.cu             sphere intersection + closest-hit
//   optix_programs_quad.cu               quad and bilinear patch intersection + closest-hit
//   optix_programs_disk.cu, _cylinder.cu     disk, cylinder (one module each)
//   optix_programs_triangle.cu           triangle (hardware intersection) closest-hit + any-hit
// A hit program group takes its intersection, closest-hit and any-hit programs from whichever modules hold them (OptiXRenderer::createProgramGroups); all modules
// share the pipeline compile options and the `params` launch variable. Nothing here is shared between modules at run time: every module has its own copy of the
// inline helpers it uses.

#include "../../src/shared/cpu_gpu.h"  // kMaxMediumBoundaryCrossings, used by optix_raygen.h
#include "optix_disk_cylinder_helpers.h"
#include "optix_device_helpers.h"
#include "optix_anyhit_shadow.h"
#include "optix_miss.h"
// RAY_TYPE_PROBE closest-hit/miss programs (recursive backend only, Phase 1
// BSSRDF) - see optix_types.h's RAY_TYPE_PROBE comment. Their hit groups reuse
// the __intersection__<type>() programs of the geometry modules unchanged (only
// closest-hit differs per ray type).
#include "optix_probe_hit.h"
#include "optix_raygen.h"
