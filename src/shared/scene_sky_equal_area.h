#pragma once
// scene_sky_equal_area.h -- a latitude-longitude sky picture as the equal-area square pbrt-v4 reads.
//
// Every renderer here reads the picture of an "infinite" light as a latitude-longitude panorama (row 0 straight up: the Scene Builder's panoramas and its generated
// Sun & sky), but pbrt-v4 reads it as an EQUAL-AREA OCTAHEDRAL square (ImageInfiniteLight: a non-square picture is an error) with +z at the middle. A saved Builder scene
// must be an ordinary pbrt-v4 file, so Save writes the sky as a square equal-area copy of the panorama in a cache folder, and the LightSource line names that copy.
// pbrt_load.h's convertEqualAreaInfiniteLight() turns a square picture back into the lat-long form the renderers read, so the copy renders here as the panorama did.
//
// The copy holds the panorama by WORLD direction: the pixel under direction d (light frame = the file's world frame, the CTM before the LightSource line is the sky's
// spin only) is the panorama's colour toward d, in this project's lat-long convention (sky_light.h: theta = acos(d.y) down from +y, phi = atan2(-d.z, d.x)).
//
// A copy is named after the panorama it came from (its path, size and time) so it is made once and reused; a changed panorama makes a new copy.
//
// Uses stb_image and tinyexr directly (the same decoders pbrt_load.h's environment-image reader uses, so a picture reads the same colours) and exr_writer.h; no Qt. The
// program that includes it links stb_image's and tinyexr's implementations (the GUI: stb_image_impl.cpp, src/external/tinyexr_impl.cpp).

#include "exr_writer.h"
#include "sampling.h"
#include "../external/stb_image.h"
#include "../external/tinyexr.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace scene_doc {
namespace sky_equal_area {

// The side of the equal-area square for a w x h panorama: one equal-area pixel covers the solid angle one panorama pixel does at the equator (N = sqrt(2 w h / pi)),
// and a little more resolution than that so the diagonal edges of the octahedral map do not lose the panorama's detail. 64 to 4096.
inline int squareSideFor(int w, int h) {
	const double n = 1.2 * std::sqrt(2.0 * static_cast<double>(w) * static_cast<double>(h) / 3.14159265358979323846);
	return std::min(4096, std::max(64, static_cast<int>(std::ceil(n))));
}

// `pano` is w x h RGB floats, a lat-long panorama in this project's convention; `out` becomes n x n RGB floats, the equal-area square. Bilinear in the panorama (wrapping
// around, clamped at the poles).
inline void latLongToEqualArea(const std::vector<float> &pano, int w, int h, int n, std::vector<float> &out) {
	const double pi = 3.14159265358979323846;
	out.assign(static_cast<std::size_t>(n) * n * 3, 0.0f);
	const auto wrapX = [w](int x) { x %= w; return x < 0 ? x + w : x; };
	const auto clampY = [h](int y) { return y < 0 ? 0 : (y >= h ? h - 1 : y); };
	for (int j = 0; j < n; ++j) {
		for (int i = 0; i < n; ++i) {
			double x, y, z;
			EqualAreaSquareToSphere((i + 0.5) / n, (j + 0.5) / n, x, y, z);
			const double theta = std::acos(std::max(-1.0, std::min(1.0, y)));
			double phi = std::atan2(-z, x);
			if (phi < 0.0) phi += 2.0 * pi;
			const double fx = phi / (2.0 * pi) * w - 0.5, fy = theta / pi * h - 0.5;
			const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
			const double tx = fx - x0, ty = fy - y0;
			const int X0 = wrapX(x0), X1 = wrapX(x0 + 1), Y0 = clampY(y0), Y1 = clampY(y0 + 1);
			float *dst = &out[(static_cast<std::size_t>(j) * n + i) * 3];
			for (int c = 0; c < 3; ++c) {
				const double p00 = pano[(static_cast<std::size_t>(Y0) * w + X0) * 3 + c], p10 = pano[(static_cast<std::size_t>(Y0) * w + X1) * 3 + c];
				const double p01 = pano[(static_cast<std::size_t>(Y1) * w + X0) * 3 + c], p11 = pano[(static_cast<std::size_t>(Y1) * w + X1) * 3 + c];
				const double top = p00 + (p10 - p00) * tx, bot = p01 + (p11 - p01) * tx;
				dst[c] = static_cast<float>(top + (bot - top) * ty);
			}
		}
	}
}

// A panorama's pixels as linear RGB floats: .exr through tinyexr, everything else through stb_image (an 8-bit picture is decoded with stb's default gamma, as the
// renderers' loader does).
inline bool decodePanorama(const std::string &filename, const std::string &bytes, std::vector<float> &pixels, int &w, int &h) {
	std::string ext = std::filesystem::path(filename).extension().string();
	for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	if (ext == ".exr") {
		float *rgba = nullptr;
		const char *err = nullptr;
		const int rc = LoadEXRFromMemory(&rgba, &w, &h, reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), &err);
		if (err) FreeEXRErrorMessage(err);
		if (rc != TINYEXR_SUCCESS || !rgba || w <= 0 || h <= 0) { if (rgba) free(rgba); return false; }
		pixels.resize(static_cast<std::size_t>(w) * h * 3);
		for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
			pixels[i * 3 + 0] = rgba[i * 4 + 0]; pixels[i * 3 + 1] = rgba[i * 4 + 1]; pixels[i * 3 + 2] = rgba[i * 4 + 2];
		}
		free(rgba);
		return true;
	}
	int channels = 0;
	float *px = stbi_loadf_from_memory(reinterpret_cast<const unsigned char *>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &channels, 3);
	if (!px || w <= 0 || h <= 0) { if (px) stbi_image_free(px); return false; }
	pixels.assign(px, px + static_cast<std::size_t>(w) * h * 3);
	stbi_image_free(px);
	return true;
}

// The equal-area copy of the panorama at `source`, made in `cacheFolder` if it is not there yet. Returns its path, or an empty string when the picture is not a
// lat-long panorama worth converting (it cannot be read, or it is already square: pbrt would read it as equal-area as it stands) or the copy cannot be written.
inline std::string ensureCopy(const std::string &source, const std::string &cacheFolder) {
	namespace fs = std::filesystem;
	std::error_code ec;
	if (source.empty() || cacheFolder.empty() || !fs::exists(fs::path(source), ec)) return std::string();
	const std::uintmax_t size = fs::file_size(fs::path(source), ec);
	const auto stamp = fs::last_write_time(fs::path(source), ec).time_since_epoch().count();
	const std::size_t key = std::hash<std::string>()(source + "|" + std::to_string(size) + "|" + std::to_string(static_cast<long long>(stamp)));
	char name[64];
	std::snprintf(name, sizeof name, "equalarea_v1_%016llx.exr", static_cast<unsigned long long>(key));
	const fs::path copy = fs::path(cacheFolder) / name;
	if (fs::exists(copy, ec) && fs::file_size(copy, ec) > 0) return copy.string();

	std::string bytes;
	std::vector<float> pano;
	int w = 0, h = 0;
	{
		std::ifstream in(source, std::ios::binary);
		if (!in) return std::string();
		std::ostringstream ss;
		ss << in.rdbuf();
		bytes = ss.str();
	}
	if (!decodePanorama(source, bytes, pano, w, h) || w <= 0 || h <= 0) return std::string();
	if (w == h) return std::string();   // already square: pbrt reads it as equal-area (and so does the loader here)
	std::vector<float> square;
	const int n = squareSideFor(w, h);
	latLongToEqualArea(pano, w, h, n, square);
	fs::create_directories(fs::path(cacheFolder), ec);
	std::string error;
	const std::string tmp = copy.string() + ".tmp.exr";
	if (!write_exr_image(tmp, square.data(), n, n, error)) return std::string();
	fs::rename(fs::path(tmp), copy, ec);
	if (ec) { fs::remove(fs::path(tmp), ec); return std::string(); }
	return copy.string();
}

}  // namespace sky_equal_area
}  // namespace scene_doc
