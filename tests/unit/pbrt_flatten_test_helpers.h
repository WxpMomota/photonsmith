// pbrt_flatten_test_helpers.h -- what the pbrt_flatten_*_tests.cpp files share: the flatten-from-text helper, the "did it warn about" check and the unit quad.
//
// (One file of 4300 lines was split by feature: shapes, media and hair, cameras, materials, lights. Nothing changed in the tests themselves.)
#pragma once

#include <gtest/gtest.h>

#include "pbrt_flatten.h"
#include "pbrt_scene.h"

#include <cmath>
#include <string>
#include <vector>

using namespace pbrt_flatten;

inline FlatScene flattenSource(const std::string &text, const MeshResolver &meshes = {}) {
	const pbrt_scene::ParseResult r = pbrt_scene::parse(text);
	EXPECT_TRUE(r.ok) << r.error;
	return flatten(r.scene, meshes);
}

inline bool warnedAbout(const FlatScene &s, const std::string &needle) {
	for (const pbrt_scene::Warning &w : s.warnings)
		if (w.message.find(needle) != std::string::npos) return true;
	return false;
}

// A unit quad in the XY plane, as two triangles.
inline const char *const kQuadMesh =
	"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ]\n"
	"  \"point3 P\" [ 0 0 0  1 0 0  1 1 0  0 1 0 ]\n";
