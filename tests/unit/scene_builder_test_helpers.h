// scene_builder_test_helpers.h -- what the scene_builder_*_tests.cpp files share: includes, the using-declarations for the scene document, and the helpers that read a file,
// load pbrt text, run the launcher and write a BMP. (One file of 2100 lines was split in two; nothing changed in the tests themselves.)
#pragma once


#include "agreement_test_helpers.h"
#include "external/stb_image_write.h"   // (stbi_write_png: its implementation is in mtl_material_tests.cpp)
#include "../../src/shared/scene_document.h"
#include "../../src/shared/scene_array.h"
#include "../../src/shared/scene_props.h"
#include "../../src/shared/scene_showcase.h"
#include "../../src/shared/pbrt_load.h"
#include "../../src/shared/pbrt_discover.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

using scene_doc::Document;
using scene_doc::Light;
using scene_doc::LightKind;
using scene_doc::MaterialKind;
using scene_doc::Object;
using scene_doc::Problem;
using scene_doc::ShapeKind;
using scene_doc::fromJson;
using scene_doc::fromPbrt;
using scene_doc::hasErrors;
using scene_doc::makeAreaLightPanel;
using scene_doc::makeObject;
using scene_doc::makeStarterScene;
using scene_doc::toJson;
using scene_doc::toPbrt;
using scene_doc::toString;
using scene_doc::validate;

inline std::string slurp(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

// Writes text to a temporary scene file and loads it the way the renderer does.
inline pbrt_load::LoadResult loadText(const std::string& text, const char* tag) {
	const std::string path = std::string("scene_builder_test_") + tag + ".pbrt";
	{
		std::ofstream out(path, std::ios::binary);
		out << text;
	}
	pbrt_load::LoadResult r = pbrt_load::loadFile(path);
	std::remove(path.c_str());
	return r;
}

inline std::string findLauncher() { return findRayTracerLauncher(); }   // agreement_test_helpers.h: RT_LAUNCHER, then the usual places

// Renders scene text with the launcher on the CPU; returns false if it cannot.
inline bool renderWithLauncher(const std::string& launcher, const std::string& sceneText, const char* tag, int size, int spp, int depth, std::vector<float>& rgb, int& w,
                        int& h) {
	const std::string scenePath = std::string("scene_builder_render_") + tag + ".pbrt";
	const std::string outPath = std::string("scene_builder_render_") + tag + ".exr";
	{
		std::ofstream out(scenePath, std::ios::binary);
		out << sceneText;
	}
	std::remove(outPath.c_str());
	std::ostringstream cmd;
	cmd << "\"" << launcher << "\" --cpu --output \"" << outPath << "\" " << size << " " << spp << " " << depth << " \"" << scenePath << "\" > "
	    << "scene_builder_render_" << tag << ".log 2>&1";
	std::string command = cmd.str();
#ifdef _WIN32
	command = "\"" + command + "\"";  // cmd.exe strips one pair of outer quotes
#endif
	const int rc = std::system(command.c_str());
	const bool ok = rc == 0 && loadLinearRgbPixels(outPath, w, h, rgb);
	if (!ok) ADD_FAILURE() << "launcher failed (exit " << rc << "); log:\n" << slurp(std::string("scene_builder_render_") + tag + ".log");
	std::remove(scenePath.c_str());
	std::remove(outPath.c_str());
	std::remove((std::string("scene_builder_render_") + tag + ".log").c_str());
	std::remove((outPath + ".run_marker.txt").c_str());  // the launcher drops a marker beside its output
	return ok;
}

inline double channelMean(const std::vector<float>& rgb, int c) {
	double s = 0;
	size_t n = rgb.size() / 3;
	for (size_t i = 0; i < n; ++i) s += rgb[3 * i + c];
	return s / static_cast<double>(n);
}

// A 24-bit BMP of w x h pixels; rgb is row-major from the TOP row, 3 bytes per pixel.
inline std::string bmpFile(int w, int h, const std::vector<unsigned char>& rgb) {
	const int rowBytes = (w * 3 + 3) / 4 * 4;
	const int dataBytes = rowBytes * h;
	std::string out;
	auto put32 = [&out](unsigned v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); };
	auto put16 = [&out](unsigned v) { for (int i = 0; i < 2; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); };
	out += "BM";
	put32(54 + dataBytes); put32(0); put32(54);
	put32(40); put32(static_cast<unsigned>(w)); put32(static_cast<unsigned>(h)); put16(1); put16(24); put32(0); put32(dataBytes); put32(2835); put32(2835); put32(0); put32(0);
	for (int y = h - 1; y >= 0; --y) {  // BMP rows run bottom to top
		for (int x = 0; x < w; ++x)
			for (int c = 2; c >= 0; --c) out.push_back(static_cast<char>(rgb[(y * w + x) * 3 + c]));  // stored as B, G, R
		for (int p = w * 3; p < rowBytes; ++p) out.push_back('\0');
	}
	return out;
}
