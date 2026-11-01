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
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "model_skin.h"

namespace cartoon {
namespace model {

struct StrandGroup {
	Vec3 colour{0.5, 0.4, 0.3};
	double width0 = 0.01, width1 = 0.001;
	std::vector<float> points;   // (12 a strand)
};
struct StrandSet {
	std::vector<StrandGroup> groups;
	Mesh roots;                  // (one vertex a strand, groups in order: what the bones' weights are worked out on)
	std::size_t count() const { return roots.verts.size(); }
};

// The strands of a file, read once. Null when it is missing or broken.
inline std::shared_ptr<const StrandSet> strandsOf(const std::string &path) {
	static std::mutex lock;
	static std::map<std::string, std::shared_ptr<const StrandSet>> read;
	const std::lock_guard<std::mutex> hold(lock);
	if (const auto it = read.find(path); it != read.end()) return it->second;
	std::shared_ptr<StrandSet> set;
	std::ifstream in(path, std::ios::binary);
	char magic[10] = {};
	std::uint32_t groups = 0;
	if (in.read(magic, 10) && std::memcmp(magic, "RTSTRANDS1", 10) == 0 && in.read(reinterpret_cast<char *>(&groups), 4)) {
		set = std::make_shared<StrandSet>();
		for (std::uint32_t g = 0; g < groups; ++g) {
			float head[5];
			std::uint32_t n = 0;
			if (!in.read(reinterpret_cast<char *>(head), sizeof head) || !in.read(reinterpret_cast<char *>(&n), 4)) {
				set.reset();
				break;
			}
			StrandGroup group;
			group.colour = {head[0], head[1], head[2]}, group.width0 = head[3], group.width1 = head[4];
			group.points.resize(static_cast<std::size_t>(n) * 12);
			if (n > 0 && !in.read(reinterpret_cast<char *>(group.points.data()), static_cast<std::streamsize>(group.points.size() * sizeof(float)))) {
				set.reset();
				break;
			}
			for (std::size_t s = 0; s < n; ++s) set->roots.verts.push_back({group.points[s * 12], group.points[s * 12 + 1], group.points[s * 12 + 2]});
			set->groups.push_back(std::move(group));
		}
	}
	read[path] = set;
	return set;
}

// The strands as pbrt curves, in the mirrored space a placed PLY is in (x negated: film_cache::placement). With `poses`, each follows the bones its root is weighted to. `lod` as a
// model's: at 2, one strand in four, twice as wide. Strands are drawn in a material of their group's colour, matte, each a tube of nine patches on the GPU, not the eighty a hair close to
// the camera takes: a groomed coat is hundreds of thousands of strands thinner than a pixel, and at eighty a strand they filled the graphics card.
inline std::string strandsText(const StrandSet &set, int lod, const Weights *weights = nullptr, const std::vector<Mat4> *poses = nullptr) {
	const std::size_t step = static_cast<std::size_t>(std::max(1, lod * lod));
	const double wide = std::max(1, lod);
	std::string out;
	out.reserve(set.count() / step * 170 + 400);
	char buf[320];
	std::size_t root = 0;
	const auto bent = [&](std::size_t r, Vec3 q) {
		if (!weights || !poses) return q;
		Vec3 p{0, 0, 0};
		double moved = 0.0;
		for (int k = 0; k < 3; ++k)
			if (weights->w[r][k] > 0.0 && weights->bone[r][k] >= 0) {
				p = p + transformPoint((*poses)[static_cast<std::size_t>(weights->bone[r][k])], q) * weights->w[r][k];
				moved += weights->w[r][k];
			}
		return p + q * (1.0 - moved);
	};
	for (const StrandGroup &g : set.groups) {
		const std::size_t n = g.points.size() / 12;
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ %.4f %.4f %.4f ]\n", g.colour.x, g.colour.y, g.colour.z);
		out += buf;
		for (std::size_t s = 0; s < n; s += step) {
			const float *f = g.points.data() + s * 12;
			const Vec3 a = bent(root + s, {f[0], f[1], f[2]}), b = bent(root + s, {f[3], f[4], f[5]}), c = bent(root + s, {f[6], f[7], f[8]}), d = bent(root + s, {f[9], f[10], f[11]});
			std::snprintf(buf, sizeof buf, "  Shape \"curve\" \"string type\" \"cylinder\" \"integer dicelength\" 3 \"integer diceradial\" 3 \"bool dicetriangles\" true \"float width0\" %.5f \"float width1\" %.5f \"point3 P\" [ %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f ]\n",
						  g.width0 * wide, g.width1 * wide, -a.x, a.y, a.z, -b.x, b.y, b.z, -c.x, c.y, c.z, -d.x, d.y, d.z);
			out += buf;
		}
		out += "AttributeEnd\n";
		root += n;
	}
	return out;
}

}  // namespace model
}  // namespace cartoon
