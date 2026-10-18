#pragma once
// gltf_export.h -- a loaded scene (pbrt_flatten::FlatScene) as a binary glTF (.glb): triangles, materials as glTF's metallic-roughness (with the transmission, ior, clearcoat and
// emissive-strength extensions where the material has them) and the point, spot and distant lights (KHR_lights_punctual), for Blender, a web viewer or a game engine.
//
// Every shape becomes triangles: the quadrics (spheres, disks, cylinders, cones, paraboloids), bilinear patches and curves are tessellated by the same code the GPU backends use
// (gpu_tessellate.h, quadric_tessellate.h), so what is exported is a close approximation of the curved shapes, not exact geometry. A glTF has no path tracer's materials, so they are
// mapped as closely as glTF can say them and the approximations are listed in `notes`: a diffuse surface is rough and non-metallic, a conductor metallic (its colour the reflectance at
// normal incidence worked out from the metal's eta and k when it has them), glass is transmissive with its index of refraction, a coated material has a clearcoat, an emitter is
// emissive. Not exported: pictures on surfaces (textures), the environment light, media, hair, the camera. Triangles with Material "interface" (they only bound a medium) are left out.
//
// The world is as the scene has it (+Y up); glTF is right-handed with +Y up too, and the Scene Builder's scenes are written to match, so nothing is turned. Surfaces are double sided, because
// a renderer does not mind which way a triangle faces and a viewer would otherwise cull some. Standard library plus the flat-scene headers, so the unit tests reach it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gpu_tessellate.h"
#include "pbrt_flatten.h"
#include "quadric_tessellate.h"

namespace gltf_export {

struct Result {
	bool ok = false;
	std::string error;
	std::string glb;                 // the file's bytes
	int triangles = 0;
	int materials = 0;               // glTF materials written
	int lights = 0;
	std::vector<std::string> notes;  // what was approximated or left out
};

namespace detail {

inline std::string num(double v) {
	if (!std::isfinite(v)) v = 0.0;
	char buf[40];
	std::snprintf(buf, sizeof buf, "%.9g", v);
	return buf;
}

inline std::string quote(const std::string& s) {
	std::string out = "\"";
	for (const char c : s) {
		if (c == '"' || c == '\\') { out += '\\'; out += c; }
		else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
		else out += c;
	}
	return out + "\"";
}

inline double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }

// The reflectance of a metal at normal incidence from its complex index (eta + i k), per channel.
inline double fresnelNormal(double eta, double k) { return ((eta - 1.0) * (eta - 1.0) + k * k) / ((eta + 1.0) * (eta + 1.0) + k * k); }

// glTF's roughness r has alpha = r * r; pbrt's roughness is remapped to alpha by a square root unless the scene gave alpha itself.
inline double gltfRoughness(double roughness, bool remapped) {
	const double alpha = remapped ? std::sqrt(std::max(0.0, roughness)) : std::max(0.0, roughness);
	return clamp01(std::sqrt(alpha));
}

// One glTF material: the pbrt material (and the area light, -1 for none) it is made from.
struct Key {
	int material, areaLight;
	bool operator<(const Key& o) const { return material != o.material ? material < o.material : areaLight < o.areaLight; }
};

inline std::string materialJson(const pbrt_flatten::FlatScene& scene, const Key& key, std::vector<std::string>& notes, std::map<std::string, int>& noted) {
	using pbrt_flatten::MaterialKind;
	pbrt_flatten::Material m;   // a triangle with no material is pbrt's default: grey and diffuse
	if (key.material >= 0 && key.material < static_cast<int>(scene.materials.size())) m = scene.materials[static_cast<size_t>(key.material)];
	double base[3] = {m.color[0], m.color[1], m.color[2]};
	double metallic = 0.0, rough = 1.0;
	std::string extensions;
	const auto note = [&](const std::string& what) { if (noted[what]++ == 0) notes.push_back(what); };
	switch (m.kind) {
		case MaterialKind::Diffuse: break;
		case MaterialKind::Conductor:
		case MaterialKind::CoatedConductor: {
			metallic = 1.0;
			if (m.hasConductorPreset)
				for (int c = 0; c < 3; ++c) base[c] = fresnelNormal(m.conductorEta[c], m.conductorK[c]);
			rough = gltfRoughness(m.kind == MaterialKind::CoatedConductor && m.conductorRoughness_u >= 0.0 ? m.conductorRoughness_u : m.roughness, m.remapRoughness);
			if (m.kind == MaterialKind::CoatedConductor)
				extensions += std::string(extensions.empty() ? "" : ",") + "\"KHR_materials_clearcoat\":{\"clearcoatFactor\":1,\"clearcoatRoughnessFactor\":" + num(gltfRoughness(m.roughness, m.remapRoughness)) + "}";
			break;
		}
		case MaterialKind::Dielectric:
		case MaterialKind::ThinDielectric: {
			for (int c = 0; c < 3; ++c) base[c] = m.transmissionFilter[c];
			rough = gltfRoughness(m.roughness, m.remapRoughness);
			extensions += std::string("\"KHR_materials_transmission\":{\"transmissionFactor\":1},\"KHR_materials_ior\":{\"ior\":") + num(std::min(5.0, std::max(1.0, m.ior))) + "}";
			break;
		}
		case MaterialKind::CoatedDiffuse:
			extensions += std::string("\"KHR_materials_clearcoat\":{\"clearcoatFactor\":1,\"clearcoatRoughnessFactor\":") + num(gltfRoughness(m.roughness, m.remapRoughness)) + "}";
			break;
		case MaterialKind::DiffuseTransmission:
			extensions += "\"KHR_materials_transmission\":{\"transmissionFactor\":0.5}";
			note("A translucent surface (diffuse transmission) is exported as half transmissive.");
			break;
		default:
			note("Some materials have no glTF equivalent (" + m.pbrtType + ") and are exported as plain diffuse surfaces of their colour.");
			break;
	}
	if (!m.textureFilename.empty() || m.hasCheckerReflectance) note("Pictures and patterns on surfaces are not exported: those surfaces have their plain colour.");
	std::string out = "{\"name\":" + quote(m.pbrtType.empty() ? std::string("material") : m.pbrtType) + ",\"doubleSided\":true,\"pbrMetallicRoughness\":{\"baseColorFactor\":[" + num(clamp01(base[0])) + "," + num(clamp01(base[1])) + "," +
	                  num(clamp01(base[2])) + ",1],\"metallicFactor\":" + num(metallic) + ",\"roughnessFactor\":" + num(rough) + "}";
	if (key.areaLight >= 0 && key.areaLight < static_cast<int>(scene.areaLights.size())) {
		const pbrt_flatten::Emission& e = scene.areaLights[static_cast<size_t>(key.areaLight)];
		const double r = e.L[0] * e.scale, g = e.L[1] * e.scale, b = e.L[2] * e.scale, peak = std::max(r, std::max(g, b));
		if (peak > 0.0) {
			out += ",\"emissiveFactor\":[" + num(r / peak) + "," + num(g / peak) + "," + num(b / peak) + "]";
			extensions += std::string(extensions.empty() ? "" : ",") + "\"KHR_materials_emissive_strength\":{\"emissiveStrength\":" + num(peak) + "}";
		}
	}
	if (!extensions.empty()) out += ",\"extensions\":{" + extensions + "}";
	return out + "}";
}

// The rotation (a quaternion x, y, z, w) that turns glTF's light direction -Z into the unit vector `to`.
inline void rotationFromMinusZ(const double to[3], double q[4]) {
	const double from[3] = {0.0, 0.0, -1.0};
	const double d = from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
	if (d < -0.999999) { q[0] = 0.0; q[1] = 1.0; q[2] = 0.0; q[3] = 0.0; return; }   // pointing the other way: half a turn about Y
	const double x = from[1] * to[2] - from[2] * to[1], y = from[2] * to[0] - from[0] * to[2], z = from[0] * to[1] - from[1] * to[0], w = 1.0 + d;
	const double l = std::sqrt(x * x + y * y + z * z + w * w);
	q[0] = x / l; q[1] = y / l; q[2] = z / l; q[3] = w / l;
}

inline void unit(double v[3]) {
	const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (l > 1e-12) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

inline void appendBytes(std::vector<unsigned char>& bin, const void* data, size_t size) {
	const unsigned char* p = static_cast<const unsigned char*>(data);
	bin.insert(bin.end(), p, p + size);
}

// A .glb stores its own length and its chunks' as 32-bit numbers: whether a file with these two chunks fits.
inline bool fitsInGlb(uint64_t jsonBytes, uint64_t binBytes) { return 12ull + 8 + jsonBytes + 8 + binBytes <= 0xFFFFFFFFull; }

inline void put32(std::string& s, uint32_t v) {
	for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 255));
}

}  // namespace detail

// The scene as a .glb. Takes the scene by value: its curved shapes are replaced by triangles on the way.
inline Result fromFlatScene(pbrt_flatten::FlatScene scene) {
	using namespace detail;
	Result result;
	// every shape as triangles
	gpu_tessellate::tessellateForBackend(scene, gpu_tessellate::TessellationCaps::all(), nullptr);
	const auto isInterface = [&scene](int m) { return m >= 0 && m < static_cast<int>(scene.materials.size()) && scene.materials[static_cast<size_t>(m)].kind == pbrt_flatten::MaterialKind::Interface; };
	for (const pbrt_flatten::Sphere& s : scene.spheres) if (!isInterface(s.material)) pbrt_flatten::quadric_tessellate::tessellateSphere(s, scene.triangles);
	for (const pbrt_flatten::Disk& d : scene.disks) if (!isInterface(d.material)) pbrt_flatten::quadric_tessellate::tessellateDisk(d, scene.triangles);
	for (const pbrt_flatten::Cylinder& c : scene.cylinders) if (!isInterface(c.material)) pbrt_flatten::quadric_tessellate::tessellateCylinder(c, scene.triangles);

	// by material: one primitive each
	std::map<Key, std::vector<const pbrt_flatten::Triangle*>> groups;
	for (const pbrt_flatten::Triangle& t : scene.triangles) {
		if (isInterface(t.material)) continue;
		groups[{t.material, t.areaLight}].push_back(&t);
	}
	if (groups.empty()) {
		result.error = "The scene has no surfaces to export.";
		return result;
	}

	std::vector<unsigned char> bin;
	std::string bufferViews, accessors, primitives, materials;
	std::map<std::string, int> noted;
	bool needsLights = false;
	int materialIndex = 0, viewIndex = 0, accessorIndex = 0;
	const auto addView = [&](size_t offset, size_t length, int target) {
		if (!bufferViews.empty()) bufferViews += ",";
		bufferViews += "{\"buffer\":0,\"byteOffset\":" + std::to_string(offset) + ",\"byteLength\":" + std::to_string(length) + ",\"target\":" + std::to_string(target) + "}";
		return viewIndex++;
	};
	const auto addAccessor = [&](int view, size_t count, const char* type, const std::string& bounds, int componentType = 5126) {
		if (!accessors.empty()) accessors += ",";
		accessors += "{\"bufferView\":" + std::to_string(view) + ",\"componentType\":" + std::to_string(componentType) + ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"" + bounds + "}";
		return accessorIndex++;
	};
	for (const auto& group : groups) {
		const std::vector<const pbrt_flatten::Triangle*>& tris = group.second;
		bool allUv = true;
		for (const pbrt_flatten::Triangle* t : tris) allUv = allUv && t->hasUVs;
		// the vertices, shared between triangles where position, normal and uv are all the same (the indices say which)
		std::vector<float> positions, normals, uvs;
		std::vector<uint32_t> indices;
		indices.reserve(tris.size() * 3);
		struct VertexHash {
			size_t operator()(const std::array<float, 8>& v) const {
				size_t h = 1469598103934665603ull;
				for (const float f : v) {
					uint32_t u;
					std::memcpy(&u, &f, sizeof u);
					h = (h ^ u) * 1099511628211ull;
				}
				h ^= h >> 32;   // (the table keeps the low bits: a float's low mantissa bits are mostly zero, so mix the high ones down)
				h *= 0x9E3779B97F4A7C15ull;
				h ^= h >> 29;
				return h;
			}
		};
		std::unordered_map<std::array<float, 8>, uint32_t, VertexHash> seen;
		seen.reserve(tris.size() + 16);
		double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
		for (const pbrt_flatten::Triangle* t : tris) {
			double face[3];   // the geometric normal, for a triangle with no shading normals
			const double e1[3] = {t->v[3] - t->v[0], t->v[4] - t->v[1], t->v[5] - t->v[2]}, e2[3] = {t->v[6] - t->v[0], t->v[7] - t->v[1], t->v[8] - t->v[2]};
			face[0] = e1[1] * e2[2] - e1[2] * e2[1]; face[1] = e1[2] * e2[0] - e1[0] * e2[2]; face[2] = e1[0] * e2[1] - e1[1] * e2[0];
			unit(face);
			for (int k = 0; k < 3; ++k) {
				std::array<float, 8> vertex = {};
				double n[3] = {face[0], face[1], face[2]};
				if (t->hasNormals) {
					const double s[3] = {t->n[k * 3], t->n[k * 3 + 1], t->n[k * 3 + 2]};
					if (s[0] * s[0] + s[1] * s[1] + s[2] * s[2] > 1e-16) { n[0] = s[0]; n[1] = s[1]; n[2] = s[2]; unit(n); }   // (a zero shading normal falls back to the face's; a long one is made unit)
				}
				for (int a = 0; a < 3; ++a) {
					const double p = t->v[k * 3 + a];
					vertex[static_cast<size_t>(a)] = static_cast<float>(p);
					vertex[static_cast<size_t>(3 + a)] = static_cast<float>(n[a]);
					lo[a] = std::min(lo[a], p);
					hi[a] = std::max(hi[a], p);
				}
				if (allUv) { vertex[6] = static_cast<float>(t->uv[k * 2]); vertex[7] = static_cast<float>(t->uv[k * 2 + 1]); }
				const auto found = seen.find(vertex);
				if (found != seen.end()) { indices.push_back(found->second); continue; }
				const uint32_t index = static_cast<uint32_t>(positions.size() / 3);
				seen.emplace(vertex, index);
				indices.push_back(index);
				positions.insert(positions.end(), vertex.begin(), vertex.begin() + 3);
				normals.insert(normals.end(), vertex.begin() + 3, vertex.begin() + 6);
				if (allUv) uvs.insert(uvs.end(), vertex.begin() + 6, vertex.begin() + 8);
			}
		}
		const size_t vertexCount = positions.size() / 3;
		const auto addBlock = [&](const void* data, size_t bytes, int target) {
			while (bin.size() % 4) bin.push_back(0);
			const size_t offset = bin.size();
			appendBytes(bin, data, bytes);
			return addView(offset, bytes, target);
		};
		const int positionAccessor = addAccessor(addBlock(positions.data(), positions.size() * sizeof(float), 34962), vertexCount, "VEC3", ",\"min\":[" + num(lo[0]) + "," + num(lo[1]) + "," + num(lo[2]) + "],\"max\":[" + num(hi[0]) + "," + num(hi[1]) + "," + num(hi[2]) + "]");
		const int normalAccessor = addAccessor(addBlock(normals.data(), normals.size() * sizeof(float), 34962), vertexCount, "VEC3", "");
		int uvAccessor = -1;
		if (allUv) uvAccessor = addAccessor(addBlock(uvs.data(), uvs.size() * sizeof(float), 34962), vertexCount, "VEC2", "");
		const int indexAccessor = addAccessor(addBlock(indices.data(), indices.size() * sizeof(uint32_t), 34963), indices.size(), "SCALAR", "", 5125);
		if (!materials.empty()) materials += ",";
		materials += materialJson(scene, group.first, result.notes, noted);
		if (!primitives.empty()) primitives += ",";
		primitives += "{\"attributes\":{\"POSITION\":" + std::to_string(positionAccessor) + ",\"NORMAL\":" + std::to_string(normalAccessor) + (uvAccessor >= 0 ? ",\"TEXCOORD_0\":" + std::to_string(uvAccessor) : std::string()) +
		              "},\"indices\":" + std::to_string(indexAccessor) + ",\"material\":" + std::to_string(materialIndex++) + ",\"mode\":4}";
		result.triangles += static_cast<int>(tris.size());
	}
	result.materials = materialIndex;

	// the lights: nodes after the mesh node
	std::string nodes = "{\"name\":\"scene\",\"mesh\":0}", lightDefs;
	int nodeCount = 1;
	for (const pbrt_flatten::PunctualLight& l : scene.punctualLights) {
		using pbrt_flatten::PunctualLightKind;
		if (l.kind != PunctualLightKind::Point && l.kind != PunctualLightKind::Spot && l.kind != PunctualLightKind::Distant) {
			if (noted["lights"]++ == 0) result.notes.push_back("Goniometric and projector lights are not exported.");
			continue;
		}
		const double r = l.intensity[0] * l.scale, g = l.intensity[1] * l.scale, b = l.intensity[2] * l.scale, peak = std::max(r, std::max(g, b));
		if (!(peak > 0.0)) continue;
		const char* type = l.kind == PunctualLightKind::Point ? "point" : l.kind == PunctualLightKind::Spot ? "spot" : "directional";
		const int lightIndex = result.lights++;
		if (!lightDefs.empty()) lightDefs += ",";
		lightDefs += std::string("{\"type\":\"") + type + "\",\"color\":[" + num(r / peak) + "," + num(g / peak) + "," + num(b / peak) + "],\"intensity\":" + num(peak);
		if (l.kind == PunctualLightKind::Spot) {
			const double outer = std::min(90.0, std::max(0.1, l.coneAngleDeg)) * 0.017453292519943295, inner = std::min(outer, std::max(0.0, l.falloffStartAngleDeg * 0.017453292519943295));
			lightDefs += ",\"spot\":{\"innerConeAngle\":" + num(inner) + ",\"outerConeAngle\":" + num(outer) + "}";
		}
		lightDefs += "}";
		double dir[3] = {l.dir[0], l.dir[1], l.dir[2]};
		if (l.kind == PunctualLightKind::Distant) { dir[0] = -dir[0]; dir[1] = -dir[1]; dir[2] = -dir[2]; }   // (the stored direction points toward the light; the light shines the other way)
		unit(dir);
		std::string node = "{\"name\":\"light " + std::to_string(lightIndex) + "\"";
		if (l.kind != PunctualLightKind::Distant) node += ",\"translation\":[" + num(l.pos[0]) + "," + num(l.pos[1]) + "," + num(l.pos[2]) + "]";
		if (l.kind != PunctualLightKind::Point) {
			double q[4];
			rotationFromMinusZ(dir, q);
			node += ",\"rotation\":[" + num(q[0]) + "," + num(q[1]) + "," + num(q[2]) + "," + num(q[3]) + "]";
		}
		node += ",\"extensions\":{\"KHR_lights_punctual\":{\"light\":" + std::to_string(lightIndex) + "}}}";
		nodes += "," + node;
		++nodeCount;
		needsLights = true;
	}
	if (scene.infiniteLight.present) result.notes.push_back("The environment light is not exported.");
	if (!scene.media.empty()) result.notes.push_back("Fog, smoke and other media are not exported.");

	std::string sceneNodes = "0";
	for (int i = 1; i < nodeCount; ++i) sceneNodes += "," + std::to_string(i);
	while (bin.size() % 4) bin.push_back(0);
	std::string used;   // the extensions the materials and nodes written above name
	for (const char* name : {"KHR_materials_transmission", "KHR_materials_ior", "KHR_materials_clearcoat", "KHR_materials_emissive_strength", "KHR_lights_punctual"})
		if (materials.find(name) != std::string::npos || nodes.find(name) != std::string::npos) used += std::string(used.empty() ? "" : ",") + quote(name);
	std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Photonsmith\"},\"extensionsUsed\":[" + used + "],\"scene\":0,\"scenes\":[{\"nodes\":[" + sceneNodes + "]}],\"nodes\":[" + nodes + "],"
	                   "\"meshes\":[{\"name\":\"scene\",\"primitives\":[" + primitives + "]}],\"materials\":[" + materials + "],\"accessors\":[" + accessors + "],\"bufferViews\":[" + bufferViews + "],"
	                   "\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}]";
	if (needsLights) json += ",\"extensions\":{\"KHR_lights_punctual\":{\"lights\":[" + lightDefs + "]}}";
	json += "}";
	while (json.size() % 4) json.push_back(' ');
	if (!fitsInGlb(json.size(), bin.size())) {
		result.error = "The scene is too large for one .glb file (4 GB): export a smaller part of it.";
		return result;
	}
	std::string glb;
	put32(glb, 0x46546C67);   // "glTF"
	put32(glb, 2);
	put32(glb, static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
	put32(glb, static_cast<uint32_t>(json.size()));
	put32(glb, 0x4E4F534A);   // "JSON"
	glb += json;
	put32(glb, static_cast<uint32_t>(bin.size()));
	put32(glb, 0x004E4942);   // "BIN\0"
	glb.append(reinterpret_cast<const char*>(bin.data()), bin.size());
	result.glb = std::move(glb);
	result.ok = true;
	return result;
}

}  // namespace gltf_export
