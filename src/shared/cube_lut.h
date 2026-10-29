#pragma once
// cube_lut.h -- a colour look-up table in the .cube format (Adobe's / Resolve's, the industry's way to hand a grade from one program to another: a film emulation, a
// show's look, a colourist's grade): a 3D table of N x N x N output colours (or a 1D curve) over the input cube, read once and applied to the picture as it is shown
// (sRGB, 0..1), trilinearly between its entries. A look line's `lut FILE.cube` (print_look.h), `lut-mix M` how much of it.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace cube_lut {

struct Lut {
	int size = 0;                 // N
	bool is3d = true;             // a 3D table, else a 1D curve per channel
	double domainMin[3] = {0, 0, 0}, domainMax[3] = {1, 1, 1};
	std::vector<float> rgb;       // N^3 x 3 (red fastest, as .cube lists them), or N x 3
};

// A .cube file's text: "" or what is wrong.
inline std::string parse(const std::string &text, Lut &out) {
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const std::size_t start = line.find_first_not_of(" \t");
		if (start == std::string::npos || line[start] == '#') continue;
		std::istringstream w(line.substr(start));
		std::string key;
		w >> key;
		if (key == "TITLE") continue;
		if (key == "LUT_3D_SIZE" || key == "LUT_1D_SIZE") {
			w >> out.size;
			out.is3d = key == "LUT_3D_SIZE";
			if (out.size < 2 || out.size > 256) return "a LUT size of " + std::to_string(out.size) + " (2 to 256)";
			continue;
		}
		if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX") {
			double *d = key == "DOMAIN_MIN" ? out.domainMin : out.domainMax;
			w >> d[0] >> d[1] >> d[2];
			continue;
		}
		double r = 0, g = 0, b = 0;
		std::istringstream nums(line.substr(start));
		if (!(nums >> r >> g >> b)) return "a line that is not three numbers: " + line.substr(start, 40);
		out.rgb.push_back(static_cast<float>(r)), out.rgb.push_back(static_cast<float>(g)), out.rgb.push_back(static_cast<float>(b));
	}
	if (out.size < 2) return "no LUT_3D_SIZE or LUT_1D_SIZE line";
	const std::size_t want = static_cast<std::size_t>(out.is3d ? out.size * out.size * out.size : out.size) * 3;
	if (out.rgb.size() != want) return "the table has " + std::to_string(out.rgb.size() / 3) + " entries, not the " + std::to_string(want / 3) + " its size says";
	for (int k = 0; k < 3; ++k)
		if (!(out.domainMax[k] > out.domainMin[k])) return "its DOMAIN_MAX is not above its DOMAIN_MIN";
	return std::string();
}

// A LUT read once a program (by its path); null with `why` when it cannot be.
inline std::shared_ptr<const Lut> load(const std::string &path, std::string &why) {
	static std::mutex lock;
	static std::map<std::string, std::shared_ptr<const Lut>> kept;
	const std::lock_guard<std::mutex> hold(lock);
	if (const auto it = kept.find(path); it != kept.end()) return it->second;
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		why = "the LUT " + path + " cannot be read";
		return nullptr;
	}
	const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	auto lut = std::make_shared<Lut>();
	if (const std::string e = parse(text, *lut); !e.empty()) {
		why = path + ": " + e;
		return nullptr;
	}
	kept[path] = lut;
	return lut;
}

// The LUT on one colour (0..1 in its domain), `mix` of it over the colour.
inline void apply(const Lut &l, float c[3], double mix) {
	const int n = l.size;
	double u[3];
	for (int k = 0; k < 3; ++k) u[k] = std::clamp((c[k] - l.domainMin[k]) / (l.domainMax[k] - l.domainMin[k]), 0.0, 1.0) * (n - 1);
	double out[3];
	if (!l.is3d) {
		for (int k = 0; k < 3; ++k) {
			const int i = std::min(n - 2, static_cast<int>(u[k]));
			const double f = u[k] - i;
			out[k] = l.rgb[static_cast<std::size_t>(i) * 3 + k] * (1.0 - f) + l.rgb[static_cast<std::size_t>(i + 1) * 3 + k] * f;
		}
	} else {
		const int i0 = std::min(n - 2, static_cast<int>(u[0])), j0 = std::min(n - 2, static_cast<int>(u[1])), k0 = std::min(n - 2, static_cast<int>(u[2]));
		const double fr = u[0] - i0, fg = u[1] - j0, fb = u[2] - k0;
		const auto at = [&](int r, int g, int b, int ch) { return static_cast<double>(l.rgb[(static_cast<std::size_t>(b) * n * n + static_cast<std::size_t>(g) * n + r) * 3 + ch]); };
		for (int ch = 0; ch < 3; ++ch) {
			double v = 0.0;
			for (int dr = 0; dr < 2; ++dr)
				for (int dg = 0; dg < 2; ++dg)
					for (int db = 0; db < 2; ++db)
						v += at(i0 + dr, j0 + dg, k0 + db, ch) * (dr ? fr : 1.0 - fr) * (dg ? fg : 1.0 - fg) * (db ? fb : 1.0 - fb);
			out[ch] = v;
		}
	}
	for (int k = 0; k < 3; ++k) c[k] = static_cast<float>(std::clamp(c[k] + (out[k] - c[k]) * mix, 0.0, 1.0));
}

}  // namespace cube_lut
