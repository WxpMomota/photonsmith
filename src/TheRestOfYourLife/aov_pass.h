#pragma once
// aov_pass.h -- the render "passes" (AOVs) a compositor wants next to the beauty image: what is at each pixel rather than how it is lit. Computed by a separate,
// cheap pass over the camera rays of the CPU scene (a first-hit trace per sample, no light transport), so it works for every backend: the Metal and OptiX renders
// get the same passes from the same scene and camera.
//
// Channels (EXR names, per-pixel filter-weighted averages over the samples):
//   albedo.R/G/B  the surface's reflectance seen at the first hit: the BSDF's own weight for a direction it samples, f * cos / pdf (a Lambertian surface gives its
//                 colour; a mirror or glass gives its tint), clamped to 0..1. 0 where nothing is hit or the surface only emits.
//   normal.X/Y/Z the first hit's shading normal in world space, turned to face the camera and renormalised. 0 where nothing is hit.
//   depth.Z       the distance from the camera to the first hit along the ray (not along the view axis). 0 where nothing is hit.
//   uv.U/V        the first hit's surface texture coordinates. 0 where nothing is hit.
//   A             coverage: 1 where every sample hit a surface, 0 where none did (the sky or background), between at silhouettes.
// Where a pixel is a mix of hit and miss samples the surface channels are averaged over the hits only, so an edge keeps the object's value instead of fading to 0.

#include "bdpt_adapter.h"
#include "thread_count.h"
#include "../shared/cryptomatte.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

namespace aov_pass {

inline const std::vector<std::string>& channelNames() {
	static const std::vector<std::string> names = {"albedo.R", "albedo.G", "albedo.B", "normal.X", "normal.Y", "normal.Z", "depth.Z", "uv.U", "uv.V", "A"};
	return names;
}

// width*height floats per channel, in channelNames() order.
inline void render(const BDPTSceneAdapter& scene, int width, int height, int spp, std::vector<std::vector<float>>& planes) {
	const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
	constexpr int kChannels = 10;
	planes.assign(kChannels, std::vector<float>(count, 0.0f));
	std::atomic<int> nextRow(0);
	auto worker = [&]() {
		for (;;) {
			const int iy = nextRow.fetch_add(1);
			if (iy >= height) break;
			for (int ix = 0; ix < width; ++ix) {
				double sum[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};   // albedo rgb, normal xyz, depth, uv
				double hitWeight = 0.0, allWeight = 0.0;
				for (int s = 0; s < spp; ++s) {
					double cam_p[3], cam_n[3], ray_d[3], w = 1.0;
					if (!scene.PixelToRayFiltered(ix, iy, random_double(), random_double(), cam_p, ray_d, cam_n, w)) continue;
					allWeight += w;
					BDPTHit<double> hit;
					if (!scene.Intersect(cam_p, ray_d, infinity, hit)) continue;
					hitWeight += w;
					double n[3] = {hit.shading_n[0], hit.shading_n[1], hit.shading_n[2]};
					if (n[0] * hit.wo[0] + n[1] * hit.wo[1] + n[2] * hit.wo[2] < 0.0)
						for (double& c : n) c = -c;
					double albedo[3] = {0, 0, 0};
					double wi[3], f[3], pdf = 0.0;
					bool specular = false;
					if (hit.light_id < 0 && !hit.is_medium_boundary && scene.BSDFSampleF(hit.bsdf_id, hit.wo, n, random_double(), random_double(), wi, f, pdf, specular) && pdf > 0.0) {
						const double cosine = std::fabs(wi[0] * n[0] + wi[1] * n[1] + wi[2] * n[2]);
						for (int c = 0; c < 3; ++c) albedo[c] = std::fmin(1.0, std::fmax(0.0, f[c] * cosine / pdf));
					}
					for (int c = 0; c < 3; ++c) { sum[c] += w * albedo[c]; sum[3 + c] += w * n[c]; }
					sum[6] += w * hit.t_hit;
					sum[7] += w * hit.uv[0];
					sum[8] += w * hit.uv[1];
				}
				const size_t i = static_cast<size_t>(iy) * static_cast<size_t>(width) + static_cast<size_t>(ix);
				if (hitWeight > 0.0) {
					double nx = sum[3], ny = sum[4], nz = sum[5];
					const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
					if (len > 0.0) { nx /= len; ny /= len; nz /= len; }
					planes[0][i] = static_cast<float>(sum[0] / hitWeight);
					planes[1][i] = static_cast<float>(sum[1] / hitWeight);
					planes[2][i] = static_cast<float>(sum[2] / hitWeight);
					planes[3][i] = static_cast<float>(nx);
					planes[4][i] = static_cast<float>(ny);
					planes[5][i] = static_cast<float>(nz);
					planes[6][i] = static_cast<float>(sum[6] / hitWeight);
					planes[7][i] = static_cast<float>(sum[7] / hitWeight);
					planes[8][i] = static_cast<float>(sum[8] / hitWeight);
				}
				planes[9][i] = allWeight > 0.0 ? static_cast<float>(hitWeight / allWeight) : 0.0f;
			}
		}
	};
	const unsigned int threads = std::max(1u, determine_render_thread_count());
	std::vector<std::thread> pool;
	for (unsigned int t = 0; t < threads; ++t) pool.emplace_back(worker);
	for (std::thread& th : pool) th.join();
}

// ---- the ID passes (src/shared/cryptomatte.h) ----
// What is at each pixel by name, for a matte of any object or material in compositing (Blender's and Nuke's Cryptomatte nodes read the layers) and for the toon look's
// lines between materials and between objects. An "object" is a shape's link group (light_link.h: a film's characters are "cast/<name>"; a shape in no group is
// "rest"); a material is its scene name ("material N" when it has none).
//   id.object, id.material      the ID (cryptomatte::idToFloat of the name's hash) that covers the pixel most: one value, for lines and simple masks
//   CryptoObject00.R/G/B/A      the two objects that cover the pixel most and their coverage (a share of all the pixel's samples, misses included)
//   CryptoMaterial00.R/G/B/A    the same for materials

inline const std::vector<std::string>& idChannelNames() {
	static const std::vector<std::string> names = {"id.object", "id.material", "CryptoObject00.R", "CryptoObject00.G", "CryptoObject00.B", "CryptoObject00.A",
												   "CryptoMaterial00.R", "CryptoMaterial00.G", "CryptoMaterial00.B", "CryptoMaterial00.A"};
	return names;
}

struct IdPasses {
	std::vector<std::vector<float>> planes;                     // idChannelNames()' order, width*height floats each
	std::map<std::string, std::uint32_t> objects, materials;    // the names seen and their hashes: the layers' manifests
	// The file header's Cryptomatte metadata for the two layers.
	std::vector<std::pair<std::string, std::string>> metadata() const {
		std::vector<std::pair<std::string, std::string>> m = cryptomatte::layerMetadata("CryptoObject", objects);
		const auto mat = cryptomatte::layerMetadata("CryptoMaterial", materials);
		m.insert(m.end(), mat.begin(), mat.end());
		return m;
	}
};

inline void renderIds(const BDPTSceneAdapter& scene, int width, int height, int spp, IdPasses& out) {
	const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
	out.planes.assign(idChannelNames().size(), std::vector<float>(count, 0.0f));
	std::mutex namesLock;
	std::atomic<int> nextRow(0);
	auto worker = [&]() {
		std::map<std::string, std::uint32_t> objects, materials;   // (this thread's: merged at the end)
		std::vector<std::pair<std::uint32_t, double>> objW, matW;   // a pixel's IDs and their summed filter weights
		const auto add = [](std::vector<std::pair<std::uint32_t, double>>& v, std::uint32_t id, double w) {
			for (auto& e : v)
				if (e.first == id) { e.second += w; return; }
			v.emplace_back(id, w);
		};
		for (;;) {
			const int iy = nextRow.fetch_add(1);
			if (iy >= height) break;
			for (int ix = 0; ix < width; ++ix) {
				objW.clear();
				matW.clear();
				double allWeight = 0.0;
				for (int s = 0; s < spp; ++s) {
					double cam_p[3], cam_n[3], ray_d[3], w = 1.0;
					if (!scene.PixelToRayFiltered(ix, iy, random_double(), random_double(), cam_p, ray_d, cam_n, w)) continue;
					allWeight += w;
					BDPTHit<double> hit;
					if (!scene.Intersect(cam_p, ray_d, infinity, hit)) continue;
					const std::string object = hit.group_name ? hit.group_name : "rest";
					const std::string material = hit.material_name ? hit.material_name : "material";
					const std::uint32_t oh = cryptomatte::murmur3_32(object), mh = cryptomatte::murmur3_32(material);
					objects.emplace(object, oh);
					materials.emplace(material, mh);
					add(objW, oh, w);
					add(matW, mh, w);
				}
				if (allWeight <= 0.0) continue;
				const size_t i = static_cast<size_t>(iy) * static_cast<size_t>(width) + static_cast<size_t>(ix);
				const auto ranked = [&](std::vector<std::pair<std::uint32_t, double>>& v, size_t idPlane, size_t cryptoPlane) {
					std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
					for (size_t r = 0; r < 2 && r < v.size(); ++r) {
						out.planes[cryptoPlane + 2 * r][i] = cryptomatte::idToFloat(v[r].first);
						out.planes[cryptoPlane + 2 * r + 1][i] = static_cast<float>(v[r].second / allWeight);
					}
					if (!v.empty()) out.planes[idPlane][i] = cryptomatte::idToFloat(v[0].first);
				};
				ranked(objW, 0, 2);
				ranked(matW, 1, 6);
			}
		}
		std::lock_guard<std::mutex> hold(namesLock);
		out.objects.insert(objects.begin(), objects.end());
		out.materials.insert(materials.begin(), materials.end());
	};
	const unsigned int threads = std::max(1u, determine_render_thread_count());
	std::vector<std::thread> pool;
	for (unsigned int t = 0; t < threads; ++t) pool.emplace_back(worker);
	for (std::thread& th : pool) th.join();
}

}  // namespace aov_pass
