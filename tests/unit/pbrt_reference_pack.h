#pragma once
// pbrt_reference_pack.h -- reads tests/data/pbrt_reference_means.txt (written by scripts/pbrt_reference_pack.py): what real pbrt-v4 draws of each bundled scene and how far
// each of this project's renderers is from it. Header-only; shared by pbrt_reference_pack_tests.cpp (CPU, portable) and pbrt_reference_pack_gpu_tests.cpp (OptiX).

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct PbrtPackRow {
	std::string name;
	int spp = 64, depth = 8, size = 64;
	double mean[3] = {0, 0, 0};
	double noise = 0.0;
	double ratio[3] = {0, 0, 0}, tol[3] = {0, 0, 0};   // cpu, recursive OptiX, wavefront OptiX; ratio 0 = not measured, skip
	double pbrtMean() const { return (mean[0] + mean[1] + mean[2]) / 3.0; }
};

inline std::vector<PbrtPackRow> loadPbrtReferencePack() {
	std::vector<PbrtPackRow> rows;
	std::ifstream in;
	for (const char* path : {"tests/data/pbrt_reference_means.txt", "../tests/data/pbrt_reference_means.txt", "../../tests/data/pbrt_reference_means.txt"}) {
		in.open(path);
		if (in) break;
		in.clear();
	}
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ss(line);
		PbrtPackRow r;
		if (ss >> r.name >> r.spp >> r.depth >> r.size >> r.mean[0] >> r.mean[1] >> r.mean[2] >> r.noise >> r.ratio[0] >> r.tol[0] >> r.ratio[1] >> r.tol[1] >> r.ratio[2] >> r.tol[2])
			rows.push_back(r);
	}
	return rows;
}

// gtest parameter names must be alphanumeric/underscore.
inline std::string pbrtPackTestName(const std::string& scene) {
	std::string n = scene;
	for (char& c : n) if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
	return n;
}
