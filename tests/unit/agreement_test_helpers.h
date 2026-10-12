#pragma once
// agreement_test_helpers.h -- shared by the render-agreement tests (pbrt_example_scenes_tests.cpp: CPU vs OptiX; cpu_integrator_agreement_tests.cpp: the CPU
// integrators against each other and against closed forms). Header-only, CPU-only (no OptiX), so the portable CMake target can use it too.

#include <gtest/gtest.h>
#include "test_accelerator.h"   // the accelerator choice a Debug build of the scene registry asserts was made
#include "../../src/external/tinyexr.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
	#include "cpu_interface.h"
}

#include "scene_registry.h"

// Resolved by the scene's FILE NAME at test-run time rather than a hardcoded id string: pbrt-loaded scenes are numbered by discovery order across every
// .pbrt that pbrt_discover finds (see scene_registry.h's SceneDescriptor::id comment and pbrt_scenes/README.md), so "example-cornell.pbrt" is not
// reliably "K1" - a machine with additional downloaded pbrt-v4-scenes collections sitting in pbrt_scenes/ sorts differently and shifts every id after
// whatever sorts earlier. The file's stem is stable regardless of what else happens to be discovered alongside it.
inline const SceneDescriptor* find_example_scene(const char* stem) {
	return find_scene_by_file_stem(stem);
}

// Mean of a linear float EXR over its RGB channels (non-finite samples ignored); false if it cannot be read.
inline bool loadLinearMean(const std::string& path, double& mean) {
	float* rgba = nullptr;
	int w = 0, h = 0;
	const char* err = nullptr;
	if (LoadEXR(&rgba, &w, &h, path.c_str(), &err) != TINYEXR_SUCCESS) {
		if (err) FreeEXRErrorMessage(err);
		return false;
	}
	double sum = 0.0;
	for (int i = 0; i < w * h; ++i)
		for (int c = 0; c < 3; ++c) {
			const float v = rgba[4 * i + c];
			if (std::isfinite(v)) sum += v;
		}
	free(rgba);
	mean = (w > 0 && h > 0) ? sum / (3.0 * w * h) : 0.0;
	return true;
}

// BDPT and the debug integrators used to shoot every camera ray through a uniform box pixel, while the path tracer importance-samples the film position through
// the scene's reconstruction filter (the default Gaussian is wider than a pixel). Same scene, same expectation, but a hard-edged emitter seen directly came out
// as crisp pixels in BDPT and as a soft, wider edge in the path tracer. The camera-path estimate of BDPT and of --simplepath/--randomwalk/--ao is now averaged
// with the filter weights too (pbrt-v4's own GetCameraSample); light-tracing splats and MLT stay unfiltered, as in pbrt-v4. The window is the light of the
// Cornell box and its edge, where the two differ by ~25% when one is filtered and the other is not and by noise (a few %) when both are.
inline bool loadLinearRgbPixels(const std::string& path, int& w, int& h, std::vector<float>& rgb) {
	float* rgba = nullptr;
	const char* err = nullptr;
	if (LoadEXR(&rgba, &w, &h, path.c_str(), &err) != TINYEXR_SUCCESS) {
		if (err) FreeEXRErrorMessage(err);
		return false;
	}
	rgb.resize(static_cast<size_t>(w) * h * 3);
	for (int i = 0; i < w * h; ++i)
		for (int c = 0; c < 3; ++c) rgb[3 * static_cast<size_t>(i) + c] = std::isfinite(rgba[4 * i + c]) ? rgba[4 * i + c] : 0.0f;
	free(rgba);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Rendering a scene text through the launcher built beside the tests (used by the Scene Builder's render tests: lighting, projector, mix, texture placement).
// ---------------------------------------------------------------------------------------------------------------------------------

// The `ray_tracer` launcher next to the tests (or one level up), as an absolute path; empty when it was not built (the render tests then skip).
inline std::string findRayTracerLauncher() {
	namespace fs = std::filesystem;
	std::error_code ec;
	for (const char* c : {"ray_tracer.exe", "../ray_tracer.exe", "../../ray_tracer.exe", "x64/Release/ray_tracer.exe", "../x64/Release/ray_tracer.exe",
	                      "../../x64/Release/ray_tracer.exe", "ray_tracer", "../ray_tracer", "../../ray_tracer"})
		if (fs::is_regular_file(c, ec)) return fs::absolute(c, ec).string();
	return "";
}

// Writes `pbrtText` to a scratch file, renders it on the CPU at `spp` samples through the launcher and reads the linear picture (w x h, rgb floats); the scratch files are removed.
// False if the launcher is missing, fails, or the picture cannot be read.
inline bool renderPbrtTextCpu(const std::string& pbrtText, const char* tag, int spp, std::vector<float>& rgb, int& w, int& h) {
	const std::string launcher = findRayTracerLauncher();
	if (launcher.empty()) return false;
	const std::string scenePath = std::string("render_test_") + tag + ".pbrt", outPath = std::string("render_test_") + tag + ".exr", logPath = scenePath + ".log";
	{
		std::ofstream out(scenePath, std::ios::binary);
		out << pbrtText;
	}
	std::remove(outPath.c_str());
	std::ostringstream cmd;
	cmd << "\"" << launcher << "\" --cpu --output \"" << outPath << "\" " << spp << " 256 1 \"" << scenePath << "\" > \"" << logPath << "\" 2>&1";
	std::string command = cmd.str();
#ifdef _WIN32
	command = "\"" + command + "\"";
#endif
	const bool ran = std::system(command.c_str()) == 0 && loadLinearRgbPixels(outPath, w, h, rgb);
	std::remove(scenePath.c_str());
	std::remove(outPath.c_str());
	std::remove((outPath + ".run_marker.txt").c_str());
	std::remove(logPath.c_str());
	return ran;
}
