#pragma once
// model_strands.h -- fur groomed outside the film: `strands FILE` on a piece. The file holds curves made by a tool of your own (colours from a picture, a comb, clumps), and they
// follow the piece's bones as its skin does, each strand by the bones at its root, so it bends with an ear or a tail and stays the length it was. Far from the camera a model draws
// fewer strands, wider, so a wide shot does not pay for fur it cannot show.
//
// The file (little-endian): "RTSTRANDS1", a uint32 count of groups, then each group: float r g b (its colour), float width0 width1 (at root and tip), uint32 strands, and
// 12 floats a strand (four control points, root first), in the piece's own space, as its mesh's vertices are.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace model {

struct StrandGroup {
	Vec3 colour{0.5, 0.4, 0.3};
	double width0 = 0.01, width1 = 0.001;
	std::vector<float> points;   // (12 a strand)
};
struct StrandSet {
	std::vector<StrandGroup> groups;
	std::size_t count() const {
		std::size_t n = 0;
		for (const StrandGroup &g : groups) n += g.points.size() / 12;
		return n;
	}
};

// The strands of a file, read once for as long as it stays as it is (its size and time are part of the key, so a coat groomed again is read again, and a file that was broken is tried
// again once it has changed). Null when it is missing or broken.
inline std::shared_ptr<const StrandSet> strandsOf(const std::string &path) {
	std::error_code ec;
	const std::uintmax_t bytes = std::filesystem::file_size(path, ec);
	if (ec) return nullptr;
	const std::string key = path + "#" + std::to_string(bytes) + "#" + std::to_string(std::filesystem::last_write_time(path, ec).time_since_epoch().count());
	static std::mutex lock;
	static std::map<std::string, std::shared_ptr<const StrandSet>> read;
	const std::lock_guard<std::mutex> hold(lock);
	if (const auto it = read.find(key); it != read.end()) return it->second;
	std::shared_ptr<StrandSet> set;
	std::ifstream in(path, std::ios::binary);
	char magic[10] = {};
	std::uint32_t groups = 0;
	std::uintmax_t left = bytes >= 14 ? bytes - 14 : 0;
	if (in.read(magic, 10) && std::memcmp(magic, "RTSTRANDS1", 10) == 0 && in.read(reinterpret_cast<char *>(&groups), 4)) {
		set = std::make_shared<StrandSet>();
		for (std::uint32_t g = 0; g < groups; ++g) {
			float head[5];
			std::uint32_t n = 0;
			if (left < sizeof head + 4 || !in.read(reinterpret_cast<char *>(head), sizeof head) || !in.read(reinterpret_cast<char *>(&n), 4)) {
				set.reset();
				break;
			}
			left -= sizeof head + 4;
			const std::uintmax_t need = static_cast<std::uintmax_t>(n) * 12 * sizeof(float);
			if (need > left) {   // (a count the file cannot hold: not a size to allocate)
				set.reset();
				break;
			}
			left -= need;
			StrandGroup group;
			group.colour = {head[0], head[1], head[2]}, group.width0 = head[3], group.width1 = head[4];
			group.points.resize(static_cast<std::size_t>(n) * 12);
			if (n > 0 && !in.read(reinterpret_cast<char *>(group.points.data()), static_cast<std::streamsize>(need))) {
				set.reset();
				break;
			}
			set->groups.push_back(std::move(group));
		}
	}
	read[key] = set;
	return set;
}

// A set's control points in order, to reshape with the skin: four a strand.
inline std::vector<Vec3> strandPoints(const StrandSet &set) {
	std::vector<Vec3> out;
	out.reserve(set.count() * 4);
	for (const StrandGroup &g : set.groups)
		for (std::size_t i = 0; i + 2 < g.points.size(); i += 3) out.push_back({g.points[i], g.points[i + 1], g.points[i + 2]});
	return out;
}

// The strands as pbrt curves, in the mirrored space a placed PLY is in (x negated: film_cache::placement). `moved`: their control points as they are now, in strandPoints' order (null: at
// rest). `lod` as a model's: at 2, one strand in four, four times as wide, so the coat covers what it did. `fall`: the share of strands that have fallen away (a piece that vanishes).
// Each strand is drawn in its group's colour, matte, a tube of `rings` x `sides` x 2 triangles on the GPU: a groomed coat is hundreds of thousands of strands thinner than a pixel, and at
// the eighty patches a hair close to the camera takes they filled the graphics card.
inline std::string strandsText(const StrandSet &set, int lod, const Vec3 *moved = nullptr, int rings = 3, int sides = 3, double fall = 0.0) {
	const std::size_t step = static_cast<std::size_t>(std::max(1, lod * lod));
	const double wide = static_cast<double>(step);
	std::string out;
	out.reserve(set.count() / step * 270 + 400);
	char buf[640];
	std::size_t first = 0;
	for (const StrandGroup &g : set.groups) {
		const std::size_t n = g.points.size() / 12;
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ %.4f %.4f %.4f ]\n", g.colour.x, g.colour.y, g.colour.z);
		out += buf;
		for (std::size_t s = 0; s < n; s += step) {
			if (fall > 0.0 && hash01(static_cast<int>(first + s), 91) < fall) continue;
			Vec3 p[4];
			for (int k = 0; k < 4; ++k) {
				const float *f = g.points.data() + (s * 4 + k) * 3;
				p[k] = moved ? moved[(first + s) * 4 + k] : Vec3{f[0], f[1], f[2]};
			}
			std::snprintf(buf, sizeof buf, "  Shape \"curve\" \"string type\" \"cylinder\" \"integer dicelength\" %d \"integer diceradial\" %d \"bool dicetriangles\" true \"float width0\" %.5f \"float width1\" %.5f \"point3 P\" [ %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f ]\n",
						  rings, sides, g.width0 * wide, g.width1 * wide, -p[0].x, p[0].y, p[0].z, -p[1].x, p[1].y, p[1].z, -p[2].x, p[2].y, p[2].z, -p[3].x, p[3].y, p[3].z);
			out += buf;
		}
		out += "AttributeEnd\n";
		first += n;
	}
	return out;
}

}  // namespace model
}  // namespace cartoon
