// optix_program_group.h -- OptiX program groups, one line each.
//
// Both OptiX backends (the recursive OptiXRenderer and the WavefrontPathTracer) create a couple of dozen program groups: a raygen, some misses, a hit group per
// geometry type and ray type. Each is a descriptor, a log buffer and one optixProgramGroupCreate() call; these helpers make a group one line, so the two
// createProgramGroups() functions read as a list of what exists. OPTIX_CHECK throws when OptiX refuses a group, as the open-coded calls did.
#pragma once

#include "optix_types_launch.h"

#include <optix.h>
#include <optix_stubs.h>

namespace optix_pg {

inline OptixProgramGroupDesc raygen(OptixModule module, const char *entry) {
	OptixProgramGroupDesc desc = {};
	desc.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
	desc.raygen.module = module;
	desc.raygen.entryFunctionName = entry;
	return desc;
}

inline OptixProgramGroupDesc miss(OptixModule module, const char *entry) {
	OptixProgramGroupDesc desc = {};
	desc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
	desc.miss.module = module;
	desc.miss.entryFunctionName = entry;
	return desc;
}

inline OptixProgramGroupDesc exception(OptixModule module, const char *entry) {
	OptixProgramGroupDesc desc = {};
	desc.kind = OPTIX_PROGRAM_GROUP_KIND_EXCEPTION;
	desc.exception.module = module;
	desc.exception.entryFunctionName = entry;
	return desc;
}

// A program of a hit group: its module and entry function name (a null name leaves the program out).
struct Prog {
	OptixModule module;
	const char *name;
};

// A hit group of up to three programs, each from its own module: intersection, closest-hit and any-hit (a null name leaves that program out).
inline OptixProgramGroupDesc hit(Prog intersection, Prog closestHit, Prog anyHit) {
	OptixProgramGroupDesc desc = {};
	desc.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
	if (intersection.name) { desc.hitgroup.moduleIS = intersection.module; desc.hitgroup.entryFunctionNameIS = intersection.name; }
	if (closestHit.name)   { desc.hitgroup.moduleCH = closestHit.module;   desc.hitgroup.entryFunctionNameCH = closestHit.name; }
	if (anyHit.name)       { desc.hitgroup.moduleAH = anyHit.module;       desc.hitgroup.entryFunctionNameAH = anyHit.name; }
	return desc;
}

// The same, every program from one module.
inline OptixProgramGroupDesc hit(OptixModule module, const char *intersection, const char *closestHit, const char *anyHit) {
	return hit(Prog{module, intersection}, Prog{module, closestHit}, Prog{module, anyHit});
}

inline void create(OptixDeviceContext context, const OptixProgramGroupDesc &desc, OptixProgramGroup *out) {
	OptixProgramGroupOptions options = {};
	char log[2048];
	size_t logSize = sizeof(log);
	OPTIX_CHECK(optixProgramGroupCreate(context, &desc, 1, &options, log, &logSize, out));
}

}  // namespace optix_pg
