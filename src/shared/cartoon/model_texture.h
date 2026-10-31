#pragma once
// model_texture.h -- modeling, part 6: painted surfaces with no UVs to make by hand. `paint base R G B [wear R G B A] [dirt R G B A] [mottle A] [stripes R G B W [x|y|z]]
// [spots R G B A] [res N]` on a part bakes a picture for it:
//
//   UVs        unwrapped automatically: each triangle goes with the side of a box it faces most (six groups), each group laid flat as seen from that side and packed into
//              a 3 x 2 atlas (box projection - the triplanar look, without the blending, made into one texture any renderer reads)
//   base       the colour, `mottle A` breaking it with soft blotches of lighter and darker
//   wear       the colour where the surface bulges (edges, corners, knuckles: model_detail.h curvatureOf), A strong - paint worn off the edges, a toy's highlights
//   dirt       the colour in the creases and hollows, A strong - grime, shadowed folds
//   stripes    bands W wide across the x, y or z axis: a sailor's shirt, a barber's pole, a lighthouse
//   spots      round spots, A of the surface covered: a ladybird, a toadstool, a dalmatian
//
// The picture is kept in the film's cache folder by a hash of the part and its paint, so it is baked once. Texels that no triangle covers take their nearest neighbour's
// colour (a few pixels out), so the atlas's seams do not show dark lines.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../../external/stb_image.h"

#include "cartoon_lib.h"
#include "film_sky.h"
#include "model_detail.h"

namespace cartoon {
namespace model {

// (modeling round 2) a picture stuck onto the surface (`decal FILE at X Y Z toward X Y Z size W H`): projected along `toward`, W x H metres, onto what faces it
struct Decal {
	std::string file;
	Vec3 at{0, 0, 0}, toward{0, 0, -1};
	double w = 0.5, h = 0.5;
};
struct Paint {
	bool on = false;
	std::vector<Decal> decals;
	std::string image;          // (`texture FILE [scale S]` on a shape piece: the picture as its base colour, box-projected, S metres across)
	double imageScale = 1.0;
	bool normals = false;       // (a `displace ... bake` piece: its pattern baked into a normal map beside the colour)
	Vec3 base{0.8, 0.8, 0.8}, wear{1.0, 0.95, 0.85}, dirt{0.15, 0.12, 0.1}, stripe{1, 1, 1}, spot{0.05, 0.05, 0.05};
	double wearAmount = 0.0, dirtAmount = 0.0, mottle = 0.0, stripeWidth = 0.0, spotAmount = 0.0;
	char stripeAxis = 'y';
	int res = 512, seed = 1;
};

// The mesh with box-projected atlas UVs (vertices shared by triangles of different sides are copied; `from` gets each copy's vertex in `in`). All six tiles have the same
// scale, so the paint's stripes and spots are the same size everywhere.
inline Mesh boxUnwrapped(const Mesh &in, std::vector<int> *from = nullptr) {
	Mesh out;
	Vec3 lo, hi;
	boxOf(in, lo, hi);
	const double span = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-6});
	std::vector<std::array<int, 6>> copy(in.verts.size());
	for (auto &c : copy) c.fill(-1);
	for (const auto &t : in.tris) {
		const Vec3 n = cross(in.verts[t[1]] - in.verts[t[0]], in.verts[t[2]] - in.verts[t[0]]);
		const double ax = std::abs(n.x), ay = std::abs(n.y), az = std::abs(n.z);
		const int side = ax >= ay && ax >= az ? (n.x > 0 ? 0 : 1) : ay >= az ? (n.y > 0 ? 2 : 3) : (n.z > 0 ? 4 : 5);
		std::array<int, 3> tri{};
		for (int q = 0; q < 3; ++q) {
			int &c = copy[t[q]][side];
			if (c < 0) {
				const Vec3 p = in.verts[t[q]] - lo;
				const double u = side < 2 ? p.z : p.x, v = side == 2 || side == 3 ? p.z : p.y;
				const double tu = side % 3, tv = side / 3;   // (the tile: 3 across, 2 down)
				c = static_cast<int>(out.verts.size());
				out.verts.push_back(in.verts[t[q]]);
				out.normals.push_back(t[q] < static_cast<int>(in.normals.size()) ? in.normals[t[q]] : Vec3{0, 1, 0});
				out.uvs.push_back({(tu + 0.04 + 0.92 * u / span) / 3.0, (tv + 0.04 + 0.92 * v / span) / 2.0});
				if (from) from->push_back(t[q]);
			}
			tri[q] = c;
		}
		out.tris.push_back(tri);
	}
	return out;
}

// The paint's colour at a point with curvature k (the part's own coordinates).
// ---- pictures (modeling round 2) ----
// A picture file read once (8 bits a channel, made linear; its alpha kept): under film_assets, or a path. Empty when it cannot be read.
struct Picture {
	int w = 0, h = 0;
	std::vector<float> rgba;
	// The colour and alpha at (u, v) (0..1, v up the picture; repeated outside), bilinear.
	std::array<double, 4> at(double u, double v) const {
		std::array<double, 4> c{0, 0, 0, 0};
		if (w == 0) return c;
		const double x = (u - std::floor(u)) * w - 0.5, y = (1.0 - (v - std::floor(v))) * h - 0.5;
		const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
		const double fx = x - x0, fy = y - y0;
		for (int dy = 0; dy < 2; ++dy)
			for (int dx = 0; dx < 2; ++dx) {
				const int xx = ((x0 + dx) % w + w) % w, yy = ((y0 + dy) % h + h) % h;
				const double wgt = (dx ? fx : 1.0 - fx) * (dy ? fy : 1.0 - fy);
				for (int k = 0; k < 4; ++k) c[k] += wgt * rgba[(static_cast<std::size_t>(yy) * w + xx) * 4 + k];
			}
		return c;
	}
};
inline const Picture &pictureFile(const std::string &file) {
	static std::mutex lock;
	static std::map<std::string, std::shared_ptr<Picture>> known;
	const std::lock_guard<std::mutex> hold(lock);
	std::shared_ptr<Picture> &slot = known[file];
	if (!slot) {
		slot = std::make_shared<Picture>();
		const std::string found = film_cache::asset(file);
		int w = 0, h = 0, n = 0;
		unsigned char *px = stbi_load((found.empty() ? file : found).c_str(), &w, &h, &n, 4);
		if (px) {
			slot->w = w, slot->h = h, slot->rgba.resize(static_cast<std::size_t>(w) * h * 4);
			for (std::size_t i = 0; i < slot->rgba.size(); ++i) {
				const double c = px[i] / 255.0;
				slot->rgba[i] = static_cast<float>(i % 4 == 3 ? c : (c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4)));
			}
			stbi_image_free(px);
		}
	}
	return *slot;
}

inline Vec3 paintAt(const Paint &p, Vec3 at, double k, Vec3 n = {0, 0, 0}) {
	Vec3 c = p.base;
	if (!p.image.empty()) {   // the picture as the base, box-projected: seen from the side the surface faces most
		const Picture &img = pictureFile(p.image);
		const double s = 1.0 / std::max(1e-6, p.imageScale), ax = std::abs(n.x), ay = std::abs(n.y), az = std::abs(n.z);
		const std::array<double, 4> q = ax >= ay && ax >= az ? img.at(at.z * s, at.y * s) : ay >= az ? img.at(at.x * s, at.z * s) : img.at(at.x * s, at.y * s);
		if (img.w > 0) c = {q[0], q[1], q[2]};
	}
	if (p.mottle > 0.0) c = c * (1.0 + p.mottle * (2.0 * fbm3(at * 6.0, p.seed) - 1.0));
	if (p.stripeWidth > 0.0) {
		const double x = p.stripeAxis == 'x' ? at.x : p.stripeAxis == 'z' ? at.z : at.y;
		if (std::fmod(std::abs(x) / p.stripeWidth, 2.0) < 1.0) c = p.stripe;
	}
	if (p.spotAmount > 0.0 && cellular(at * 8.0, p.seed + 5) < 0.6 * std::sqrt(p.spotAmount)) c = p.spot;
	const double grime = 0.75 + 0.5 * fbm3(at * 11.0, p.seed + 9);   // (wear and dirt in patches, not even bands)
	if (p.wearAmount > 0.0) c = lerp(c, p.wear, std::clamp(p.wearAmount * smoothstep(0.15, 0.6, k) * grime, 0.0, 1.0));
	if (p.dirtAmount > 0.0) c = lerp(c, p.dirt, std::clamp(p.dirtAmount * smoothstep(-0.1, -0.5, k) * grime, 0.0, 1.0));
	for (const Decal &d : p.decals) {   // each decal over it, where the surface faces the way it is projected and is near its plane
		const Vec3 dir = normalize(d.toward), right = normalize(std::abs(dir.y) < 0.95 ? cross(Vec3{0, 1, 0}, dir) : cross(Vec3{1, 0, 0}, dir)), up = cross(dir, right);
		const Vec3 r = at - d.at;
		const double u = dot(r, right) / d.w + 0.5, v = dot(r, up) / d.h + 0.5, depth = dot(r, dir);
		if (u < 0 || u > 1 || v < 0 || v > 1 || std::abs(depth) > 0.5 * std::max(d.w, d.h) || (length(n) > 0.5 && dot(n, dir) > -0.2)) continue;
		const std::array<double, 4> q = pictureFile(d.file).at(u, v);
		c = lerp(c, Vec3{q[0], q[1], q[2]}, std::clamp(q[3], 0.0, 1.0));
	}
	return {std::clamp(c.x, 0.0, 1.0), std::clamp(c.y, 0.0, 1.0), std::clamp(c.z, 0.0, 1.0)};
}

// The paint baked onto `uvMesh` (boxUnwrapped's), `curvature` its vertices' (curvatureOf the mesh it came from, by copied vertex), into a picture res x res (rgb). bakePaint:
// into the film's cache as `name` (unique to the part and its paint), its path.
inline void paintInto(std::vector<float> &rgb, const Mesh &uvMesh, const std::vector<double> &curvature, const Paint &p) {
	const int N = std::clamp(p.res, 64, 4096);
	{
		std::vector<char> done(static_cast<std::size_t>(N) * N, 0);
		for (const auto &t : uvMesh.tris) {
			double U[3], V[3];
			for (int q = 0; q < 3; ++q) U[q] = uvMesh.uvs[t[q]].first * N - 0.5, V[q] = uvMesh.uvs[t[q]].second * N - 0.5;
			const double area = (U[1] - U[0]) * (V[2] - V[0]) - (U[2] - U[0]) * (V[1] - V[0]);
			if (std::abs(area) < 1e-12) continue;
			const int x0 = std::max(0, static_cast<int>(std::floor(std::min({U[0], U[1], U[2]})))), x1 = std::min(N - 1, static_cast<int>(std::ceil(std::max({U[0], U[1], U[2]}))));
			const int y0 = std::max(0, static_cast<int>(std::floor(std::min({V[0], V[1], V[2]})))), y1 = std::min(N - 1, static_cast<int>(std::ceil(std::max({V[0], V[1], V[2]}))));
			for (int y = y0; y <= y1; ++y)
				for (int x = x0; x <= x1; ++x) {
					const double w0 = ((U[1] - x) * (V[2] - y) - (U[2] - x) * (V[1] - y)) / area, w1 = ((U[2] - x) * (V[0] - y) - (U[0] - x) * (V[2] - y)) / area, w2 = 1.0 - w0 - w1;
					if (w0 < -0.02 || w1 < -0.02 || w2 < -0.02) continue;
					const Vec3 at = uvMesh.verts[t[0]] * w0 + uvMesh.verts[t[1]] * w1 + uvMesh.verts[t[2]] * w2;
					const double k = curvature[t[0]] * w0 + curvature[t[1]] * w1 + curvature[t[2]] * w2;
					const Vec3 nrm = uvMesh.normals.size() == uvMesh.verts.size() ? uvMesh.normals[t[0]] * w0 + uvMesh.normals[t[1]] * w1 + uvMesh.normals[t[2]] * w2 : Vec3{0, 0, 0};
					const Vec3 c = paintAt(p, at, k, nrm);
					const std::size_t i = (static_cast<std::size_t>(N - 1 - y) * N + x);   // (the picture's first row is v = 1)
					rgb[i * 3] = static_cast<float>(c.x), rgb[i * 3 + 1] = static_cast<float>(c.y), rgb[i * 3 + 2] = static_cast<float>(c.z);
					done[i] = 1;
				}
		}
		for (int pass = 0; pass < 4; ++pass) {   // (the uncovered texels round each island take their neighbour's colour)
			std::vector<char> next = done;
			for (int y = 0; y < N; ++y)
				for (int x = 0; x < N; ++x) {
					const std::size_t i = static_cast<std::size_t>(y) * N + x;
					if (done[i]) continue;
					for (const auto &[dx, dy] : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
						const int xx = x + dx, yy = y + dy;
						if (xx < 0 || yy < 0 || xx >= N || yy >= N || !done[static_cast<std::size_t>(yy) * N + xx]) continue;
						const std::size_t j = static_cast<std::size_t>(yy) * N + xx;
						for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = rgb[j * 3 + c];
						next[i] = 1;
						break;
					}
				}
			done = next;
		}
	}
}
// A displacement pattern (model_detail.h `displace ... bake`) baked as a tangent-space normal map onto `uvMesh`'s atlas, into rgb (N x N): pbrt's normal map frame -
// x along the uvs' u (dpdu), z the surface normal - its normal tipped by the pattern's slope. Texels no triangle covers are flat (0.5 0.5 1).
inline void normalsInto(std::vector<float> &rgb, int N, const Mesh &uvMesh, const std::vector<Displace> &bakes) {
	for (std::size_t i = 0; i + 2 < rgb.size(); i += 3) rgb[i] = 0.5f, rgb[i + 1] = 0.5f, rgb[i + 2] = 1.0f;
	for (const auto &t : uvMesh.tris) {
		double U[3], V[3];
		for (int q = 0; q < 3; ++q) U[q] = uvMesh.uvs[t[q]].first * N - 0.5, V[q] = uvMesh.uvs[t[q]].second * N - 0.5;
		const double area = (U[1] - U[0]) * (V[2] - V[0]) - (U[2] - U[0]) * (V[1] - V[0]);
		if (std::abs(area) < 1e-12) continue;
		// dp/du of the triangle (pbrt's tangent): from its corners' positions and uvs
		const Vec3 e1 = uvMesh.verts[t[1]] - uvMesh.verts[t[0]], e2 = uvMesh.verts[t[2]] - uvMesh.verts[t[0]];
		const double du1 = (U[1] - U[0]) / N, du2 = (U[2] - U[0]) / N, dv1 = (V[1] - V[0]) / N, dv2 = (V[2] - V[0]) / N, det = du1 * dv2 - du2 * dv1;
		if (std::abs(det) < 1e-18) continue;
		const Vec3 dpdu = (e1 * dv2 - e2 * dv1) * (1.0 / det);
		const int x0 = std::max(0, static_cast<int>(std::floor(std::min({U[0], U[1], U[2]})))), x1 = std::min(N - 1, static_cast<int>(std::ceil(std::max({U[0], U[1], U[2]}))));
		const int y0 = std::max(0, static_cast<int>(std::floor(std::min({V[0], V[1], V[2]})))), y1 = std::min(N - 1, static_cast<int>(std::ceil(std::max({V[0], V[1], V[2]}))));
		for (int y = y0; y <= y1; ++y)
			for (int x = x0; x <= x1; ++x) {
				const double w0 = ((U[1] - x) * (V[2] - y) - (U[2] - x) * (V[1] - y)) / area, w1 = ((U[2] - x) * (V[0] - y) - (U[0] - x) * (V[2] - y)) / area, w2 = 1.0 - w0 - w1;
				if (w0 < -0.02 || w1 < -0.02 || w2 < -0.02) continue;
				const Vec3 at = uvMesh.verts[t[0]] * w0 + uvMesh.verts[t[1]] * w1 + uvMesh.verts[t[2]] * w2;
				const Vec3 n = normalize(uvMesh.normals[t[0]] * w0 + uvMesh.normals[t[1]] * w1 + uvMesh.normals[t[2]] * w2);
				const Vec3 tx = normalize(dpdu - n * dot(dpdu, n)), ty = cross(n, tx);
				// the height's slope along the two tangents, from the patterns themselves
				double sx = 0.0, sy = 0.0;
				for (const Displace &d : bakes) {
					const double e = 0.05 * d.size;
					sx += d.amount * (patternAt(d.kind, at + tx * e, d.size, d.seed) - patternAt(d.kind, at - tx * e, d.size, d.seed)) / (2.0 * e);
					sy += d.amount * (patternAt(d.kind, at + ty * e, d.size, d.seed) - patternAt(d.kind, at - ty * e, d.size, d.seed)) / (2.0 * e);
				}
				const Vec3 local = normalize(Vec3{-sx, -sy, 1.0});
				const std::size_t i = (static_cast<std::size_t>(N - 1 - y) * N + x) * 3;
				rgb[i] = static_cast<float>(0.5 + 0.5 * local.x), rgb[i + 1] = static_cast<float>(0.5 + 0.5 * local.y), rgb[i + 2] = static_cast<float>(0.5 + 0.5 * local.z);
			}
	}
}
inline std::string bakePaint(const std::string &film, const std::string &name, const Mesh &uvMesh, const std::vector<double> &curvature, const Paint &p) {
	const int N = std::clamp(p.res, 64, 4096);
	return film_sky::picture(film, name, N, N, [&](std::vector<float> &rgb) { paintInto(rgb, uvMesh, curvature, p); });
}

}  // namespace model
}  // namespace cartoon
