#pragma once
// gltf_import.h -- a glTF 2.0 model (.glb or .gltf) read into what the Scene Builder can place: triangle parts with their materials and pictures.
//
// parse() takes the file's bytes (so a test builds a file in memory) and a callback that fetches an external file a .gltf names (a .bin, a picture); it applies the node hierarchy (matrices or
// translation / rotation / scale, so each part is in the model's own world), merges every primitive that uses the same material into one part, makes smooth normals where the file has none, and
// turns strips and fans into triangles. toPly() writes a part as a binary PLY after moving and scaling it (the Scene Builder's mesh objects are PLY files).
//
// What is read: positions, normals, the first set of texture coordinates, triangle / strip / fan primitives, indexed or not, any component type a glTF allows (normalised integers too), buffers
// in the .glb, as data: URIs or as files; the metallic-roughness material with its factors and base-colour picture, and the transmission, ior, clearcoat and emissive-strength extensions. What is not:
// animation and skinning (the model is in its bind pose), cameras and lights, sparse accessors, vertex colours, Draco- or meshopt-compressed geometry (refused: it needs a decoder, and the error
// says so), a second set of texture coordinates, texture transforms and alpha cutouts (said in `notes`). The file is not trusted: every accessor is checked against its buffer before it is read, and
// nothing here reads outside what the file's own buffers hold.
//
// Header-only, standard library only (it uses the JSON reader of scene_json.h); unit tested.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "scene_json.h"

namespace gltf_import {

constexpr size_t kMaxVertices = 60u * 1000u * 1000u;   // a model with more is refused: it would not fit in memory as doubles and could not be placed

struct Material {
	std::string name;
	double baseColor[4] = {1, 1, 1, 1};
	double metallic = 1.0;
	double roughness = 1.0;
	double transmission = 0.0;
	double ior = 1.5;
	double clearcoat = 0.0;
	double emissive[3] = {0, 0, 0};   // already multiplied by the emissive strength
	int baseColorImage = -1;          // index into Model::images, -1: none
	int metalRoughImage = -1;         // the metal (blue) and roughness (green) picture, which multiplies the two factors: a caller that can decode pictures averages it, else the factors stand
	bool doubleSided = false;
};

struct Image {
	std::string name;
	std::string mime;                 // "image/png", "image/jpeg", ... as the file says (or from the file name)
	std::vector<unsigned char> bytes;
};

// The triangles that use one material, in the model's own world.
struct Part {
	int material = 0;                 // index into Model::materials
	std::vector<double> positions;    // 3 per vertex
	std::vector<double> normals;      // 3 per vertex (made smooth where the file had none)
	std::vector<double> uvs;          // 2 per vertex, as the file has them (v runs down the picture); zeros where the file had none
	std::vector<uint32_t> indices;    // 3 per triangle
	bool hasUvs = false;
};

struct Model {
	std::vector<Part> parts;
	std::vector<Material> materials;
	std::vector<Image> images;
	double lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};   // the bounding box of every part
	size_t triangles = 0;
	std::vector<std::string> notes;
};

struct Result {
	bool ok = false;
	std::string error;
	Model model;
};

using FileFetcher = std::function<bool(const std::string& uri, std::vector<unsigned char>& bytes)>;

namespace detail {

using Json = scene_doc::detail::Json;

inline const Json* get(const Json& o, const char* key) { return o.type == Json::Type::Obj ? o.find(key) : nullptr; }
inline const Json* at(const Json* arr, long index) {
	if (!arr || arr->type != Json::Type::Arr || index < 0 || static_cast<size_t>(index) >= arr->a.size()) return nullptr;
	return &arr->a[static_cast<size_t>(index)];
}
inline double numOr(const Json* j, double fallback) { return j && j->type == Json::Type::Num && std::isfinite(j->n) ? j->n : fallback; }
inline long intOr(const Json* j, long fallback) { return j && j->type == Json::Type::Num && std::isfinite(j->n) && std::fabs(j->n) < 1e12 ? static_cast<long>(j->n) : fallback; }
inline std::string strOr(const Json* j, const std::string& fallback = std::string()) { return j && j->type == Json::Type::Str ? j->s : fallback; }

inline bool base64(const std::string& text, std::vector<unsigned char>& out) {
	static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int table[256];
	for (int& t : table) t = -1;
	for (size_t i = 0; i < alphabet.size(); ++i) table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
	out.clear();
	out.reserve(text.size() / 4 * 3);
	uint32_t acc = 0;
	int bits = 0;
	for (const char c : text) {
		if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
		const int v = table[static_cast<unsigned char>(c)];
		if (v < 0) return false;
		acc = (acc << 6) | static_cast<uint32_t>(v);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<unsigned char>((acc >> bits) & 255));
		}
	}
	return true;
}

// 4 x 4 matrices, column-major as glTF writes them: m[column * 4 + row].
using Mat = std::array<double, 16>;
inline Mat identity() { Mat m{}; m[0] = m[5] = m[10] = m[15] = 1.0; return m; }
inline Mat multiply(const Mat& a, const Mat& b) {   // a * b
	Mat r{};
	for (int c = 0; c < 4; ++c)
		for (int row = 0; row < 4; ++row) {
			double s = 0.0;
			for (int k = 0; k < 4; ++k) s += a[static_cast<size_t>(k * 4 + row)] * b[static_cast<size_t>(c * 4 + k)];
			r[static_cast<size_t>(c * 4 + row)] = s;
		}
	return r;
}
inline Mat fromTrs(const double t[3], const double q[4], const double s[3]) {
	double x = q[0], y = q[1], z = q[2], w = q[3];
	const double l = std::sqrt(x * x + y * y + z * z + w * w);
	if (l > 1e-12) { x /= l; y /= l; z /= l; w /= l; } else { x = y = z = 0.0; w = 1.0; }
	Mat m = identity();
	m[0] = (1 - 2 * (y * y + z * z)) * s[0]; m[1] = (2 * (x * y + z * w)) * s[0];     m[2] = (2 * (x * z - y * w)) * s[0];
	m[4] = (2 * (x * y - z * w)) * s[1];     m[5] = (1 - 2 * (x * x + z * z)) * s[1]; m[6] = (2 * (y * z + x * w)) * s[1];
	m[8] = (2 * (x * z + y * w)) * s[2];     m[9] = (2 * (y * z - x * w)) * s[2];     m[10] = (1 - 2 * (x * x + y * y)) * s[2];
	m[12] = t[0]; m[13] = t[1]; m[14] = t[2];
	return m;
}
inline Mat nodeMatrix(const Json& node) {
	const Json* matrix = get(node, "matrix");
	if (matrix && matrix->type == Json::Type::Arr && matrix->a.size() == 16) {
		Mat m;
		for (size_t i = 0; i < 16; ++i) m[i] = numOr(&matrix->a[i], i % 5 == 0 ? 1.0 : 0.0);
		return m;
	}
	double t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
	const auto fill = [](const Json* j, double* out, size_t n) {
		if (!j || j->type != Json::Type::Arr || j->a.size() != n) return;
		for (size_t i = 0; i < n; ++i) out[i] = numOr(&j->a[i], out[i]);
	};
	fill(get(node, "translation"), t, 3);
	fill(get(node, "rotation"), q, 4);
	fill(get(node, "scale"), s, 3);
	return fromTrs(t, q, s);
}
inline double determinant3(const Mat& m) {
	return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
}

// What a parse works on.
struct Doc {
	Json root;
	std::vector<std::vector<unsigned char>> buffers;   // by buffer index; empty (and `missing`) when a buffer could not be had
	std::vector<bool> bufferOk;
	std::vector<std::string> notes;
	std::set<std::string> noted;
	void note(const std::string& what) { if (noted.insert(what).second) notes.push_back(what); }
};

inline int componentsOf(const std::string& type) {
	if (type == "SCALAR") return 1;
	if (type == "VEC2") return 2;
	if (type == "VEC3") return 3;
	if (type == "VEC4") return 4;
	return 0;
}
inline int componentBytes(long type) {
	switch (type) {
		case 5120: case 5121: return 1;
		case 5122: case 5123: return 2;
		case 5125: case 5126: return 4;
		default: return 0;
	}
}

// An accessor's values as doubles (normalised integers scaled to 0..1 or -1..1), `components` per element. False with `err` when the accessor or what it points at is not valid.
inline bool readAccessor(Doc& d, long index, std::vector<double>& out, int& components, size_t& count, std::string& err) {
	const Json* accessor = at(get(d.root, "accessors"), index);
	if (!accessor) { err = "an accessor is missing"; return false; }
	components = componentsOf(strOr(get(*accessor, "type")));
	const long ctype = intOr(get(*accessor, "componentType"), 0);
	const int csize = componentBytes(ctype);
	const double countValue = numOr(get(*accessor, "count"), -1.0);
	if (components == 0 || csize == 0 || !(countValue >= 0.0) || countValue > static_cast<double>(kMaxVertices) * 3.0) { err = "an accessor is not valid"; return false; }
	count = static_cast<size_t>(countValue);
	out.assign(count * static_cast<size_t>(components), 0.0);
	if (get(*accessor, "sparse")) d.note("Sparse accessors are not read: the model may have morph targets or deltas missing.");
	const long viewIndex = intOr(get(*accessor, "bufferView"), -1);
	if (viewIndex < 0) return true;   // (no bufferView: all zeros, as the spec says)
	const Json* view = at(get(d.root, "bufferViews"), viewIndex);
	if (!view) { err = "a buffer view is missing"; return false; }
	const long bufferIndex = intOr(get(*view, "buffer"), -1);
	if (bufferIndex < 0 || static_cast<size_t>(bufferIndex) >= d.buffers.size() || !d.bufferOk[static_cast<size_t>(bufferIndex)]) { err = "a buffer could not be read"; return false; }
	const std::vector<unsigned char>& buffer = d.buffers[static_cast<size_t>(bufferIndex)];
	const double viewOffset = numOr(get(*view, "byteOffset"), 0.0), viewLength = numOr(get(*view, "byteLength"), -1.0);
	const double accessorOffset = numOr(get(*accessor, "byteOffset"), 0.0);
	const size_t elementBytes = static_cast<size_t>(components) * static_cast<size_t>(csize);
	const double strideValue = numOr(get(*view, "byteStride"), 0.0);
	const size_t stride = strideValue >= static_cast<double>(elementBytes) ? static_cast<size_t>(strideValue) : elementBytes;
	if (!(viewOffset >= 0.0) || !(viewLength >= 0.0) || !(accessorOffset >= 0.0) || viewOffset + viewLength > static_cast<double>(buffer.size())) { err = "a buffer view is outside its buffer"; return false; }
	if (count > 0) {
		const double last = accessorOffset + static_cast<double>(count - 1) * static_cast<double>(stride) + static_cast<double>(elementBytes);
		if (last > viewLength) { err = "an accessor reads past its buffer view"; return false; }
	}
	const bool normalized = get(*accessor, "normalized") && get(*accessor, "normalized")->type == Json::Type::Bool && get(*accessor, "normalized")->b;
	const unsigned char* base = buffer.data() + static_cast<size_t>(viewOffset) + static_cast<size_t>(accessorOffset);
	for (size_t e = 0; e < count; ++e) {
		const unsigned char* p = base + e * stride;
		for (int c = 0; c < components; ++c) {
			const unsigned char* q = p + static_cast<size_t>(c) * static_cast<size_t>(csize);
			double v = 0.0;
			switch (ctype) {
				case 5120: { const int8_t x = static_cast<int8_t>(q[0]); v = normalized ? std::max(x / 127.0, -1.0) : x; break; }
				case 5121: v = normalized ? q[0] / 255.0 : q[0]; break;
				case 5122: { int16_t x; std::memcpy(&x, q, 2); v = normalized ? std::max(x / 32767.0, -1.0) : x; break; }
				case 5123: { uint16_t x; std::memcpy(&x, q, 2); v = normalized ? x / 65535.0 : x; break; }
				case 5125: { uint32_t x; std::memcpy(&x, q, 4); v = x; break; }
				case 5126: { float x; std::memcpy(&x, q, 4); v = std::isfinite(x) ? x : 0.0; break; }
			}
			out[e * static_cast<size_t>(components) + static_cast<size_t>(c)] = v;
		}
	}
	return true;
}

// The bytes of a buffer view (a picture inside the file).
inline bool readView(const Doc& d, long viewIndex, std::vector<unsigned char>& out) {
	const Json* view = at(get(d.root, "bufferViews"), viewIndex);
	if (!view) return false;
	const long bufferIndex = intOr(get(*view, "buffer"), -1);
	if (bufferIndex < 0 || static_cast<size_t>(bufferIndex) >= d.buffers.size() || !d.bufferOk[static_cast<size_t>(bufferIndex)]) return false;
	const std::vector<unsigned char>& buffer = d.buffers[static_cast<size_t>(bufferIndex)];
	const double offset = numOr(get(*view, "byteOffset"), 0.0), length = numOr(get(*view, "byteLength"), -1.0);
	if (!(offset >= 0.0) || !(length >= 0.0) || offset + length > static_cast<double>(buffer.size())) return false;
	out.assign(buffer.begin() + static_cast<std::ptrdiff_t>(offset), buffer.begin() + static_cast<std::ptrdiff_t>(offset + length));
	return true;
}

// A data: URI or an external file's bytes.
inline bool readUri(const std::string& uri, const FileFetcher& fetch, std::vector<unsigned char>& out, std::string* mime = nullptr) {
	if (uri.compare(0, 5, "data:") == 0) {
		const size_t comma = uri.find(',');
		if (comma == std::string::npos) return false;
		const std::string header = uri.substr(5, comma - 5);
		if (mime) *mime = header.substr(0, header.find(';'));
		if (header.find(";base64") == std::string::npos) return false;
		return base64(uri.substr(comma + 1), out);
	}
	return fetch && fetch(uri, out);
}

inline std::string mimeOfName(const std::string& name) {
	const size_t dot = name.find_last_of('.');
	std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
	for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	if (ext == "png") return "image/png";
	if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
	return ext.empty() ? std::string() : "image/" + ext;
}

}  // namespace detail

// The model in `bytes` (a .glb, or the JSON text of a .gltf, whose buffers and pictures `fetch` supplies).
inline Result parse(const std::string& bytes, const FileFetcher& fetch = FileFetcher()) {
	using namespace detail;
	Result result;
	Doc d;
	std::string jsonText;
	std::vector<unsigned char> glbBin;
	bool haveGlbBin = false;
	if (bytes.size() >= 12 && std::memcmp(bytes.data(), "glTF", 4) == 0) {
		const auto read32 = [&](size_t at) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[at + static_cast<size_t>(i)])) << (8 * i); return v; };
		if (read32(4) != 2) { result.error = "This is not a glTF 2.0 file."; return result; }
		size_t pos = 12;
		bool haveJson = false;
		while (pos + 8 <= bytes.size()) {
			const uint32_t length = read32(pos), type = read32(pos + 4);
			pos += 8;
			if (length > bytes.size() - pos) { result.error = "The file is cut short (a chunk is longer than the file)."; return result; }
			if (type == 0x4E4F534A && !haveJson) { jsonText.assign(bytes, pos, length); haveJson = true; }
			else if (type == 0x004E4942 && !haveGlbBin) { glbBin.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos), bytes.begin() + static_cast<std::ptrdiff_t>(pos + length)); haveGlbBin = true; }
			pos += (static_cast<size_t>(length) + 3) & ~static_cast<size_t>(3);
		}
		if (!haveJson) { result.error = "The file has no JSON chunk."; return result; }
	} else {
		jsonText = bytes;
	}
	std::string jsonError;
	scene_doc::detail::JsonParser parser(jsonText);
	if (!parser.parse(d.root, jsonError) || d.root.type != Json::Type::Obj) { result.error = "This is not a glTF file: " + (jsonError.empty() ? std::string("it is not a JSON object") : jsonError); return result; }
	const Json* asset = get(d.root, "asset");
	if (!asset || strOr(get(*asset, "version")).compare(0, 2, "2.") != 0) { result.error = "This is not a glTF 2.0 file."; return result; }
	if (const Json* required = get(d.root, "extensionsRequired"))
		for (const Json& e : required->a) {
			const std::string name = strOr(&e);
			if (name == "KHR_draco_mesh_compression") { result.error = "This model uses Draco compression, which needs a decoder this program does not have. Export it again without compression (in Blender: tick off 'Compression')."; return result; }
			if (name == "EXT_meshopt_compression") { result.error = "This model uses meshopt compression, which needs a decoder this program does not have. Export it again without compression."; return result; }
			if (name != "KHR_materials_transmission" && name != "KHR_materials_ior" && name != "KHR_materials_clearcoat" && name != "KHR_materials_emissive_strength" && name != "KHR_texture_transform" &&
			    name != "KHR_mesh_quantization" && name != "KHR_materials_unlit" && name != "KHR_lights_punctual")
				d.note("The model requires the extension " + name + ", which is not read: it may look different.");
		}

	// the buffers
	const Json* buffers = get(d.root, "buffers");
	const size_t bufferCount = buffers && buffers->type == Json::Type::Arr ? buffers->a.size() : 0;
	d.buffers.resize(bufferCount);
	d.bufferOk.assign(bufferCount, false);
	for (size_t i = 0; i < bufferCount; ++i) {
		const std::string uri = strOr(get(buffers->a[i], "uri"));
		if (uri.empty()) {
			if (i == 0 && haveGlbBin) { d.buffers[i] = glbBin; d.bufferOk[i] = true; }
		} else {
			d.bufferOk[i] = readUri(uri, fetch, d.buffers[i]);
		}
		if (!d.bufferOk[i]) { result.error = "A data file the model needs could not be read (" + (uri.empty() ? std::string("its binary part") : uri) + ")."; return result; }
	}

	// the materials (a glTF triangle with no material gets one more, plain, at the end)
	Model& model = result.model;
	const Json* materials = get(d.root, "materials");
	const size_t declared = materials && materials->type == Json::Type::Arr ? materials->a.size() : 0;
	const Json* textures = get(d.root, "textures");
	const Json* images = get(d.root, "images");
	std::map<long, int> imageOfSource;   // glTF image index -> Model::images index
	const auto importImage = [&](long source) -> int {
		if (source < 0) return -1;
		const auto found = imageOfSource.find(source);
		if (found != imageOfSource.end()) return found->second;
		const Json* image = at(images, source);
		if (!image) return -1;
		Image out;
		out.name = strOr(get(*image, "name"));
		out.mime = strOr(get(*image, "mimeType"));
		const std::string uri = strOr(get(*image, "uri"));
		bool ok = false;
		if (!uri.empty()) {
			std::string uriMime;
			ok = readUri(uri, fetch, out.bytes, &uriMime);
			if (out.mime.empty()) out.mime = !uriMime.empty() ? uriMime : mimeOfName(uri);
		} else {
			ok = readView(d, intOr(get(*image, "bufferView"), -1), out.bytes);
		}
		if (!ok || out.bytes.empty()) { d.note("A picture on the model could not be read: its surface has its plain colour."); imageOfSource[source] = -1; return -1; }
		model.images.push_back(std::move(out));
		imageOfSource[source] = static_cast<int>(model.images.size()) - 1;
		return imageOfSource[source];
	};
	for (size_t i = 0; i < declared; ++i) {
		const Json& m = materials->a[i];
		Material mat;
		mat.name = strOr(get(m, "name"), "material " + std::to_string(i));
		if (const Json* pbr = get(m, "pbrMetallicRoughness")) {
			if (const Json* c = get(*pbr, "baseColorFactor"))
				if (c->type == Json::Type::Arr && c->a.size() == 4)
					for (size_t k = 0; k < 4; ++k) mat.baseColor[k] = std::min(1.0, std::max(0.0, numOr(&c->a[k], 1.0)));
			mat.metallic = std::min(1.0, std::max(0.0, numOr(get(*pbr, "metallicFactor"), 1.0)));
			mat.roughness = std::min(1.0, std::max(0.0, numOr(get(*pbr, "roughnessFactor"), 1.0)));
			if (const Json* tex = get(*pbr, "baseColorTexture")) {
				const Json* texture = at(textures, intOr(get(*tex, "index"), -1));
				if (texture) mat.baseColorImage = importImage(intOr(get(*texture, "source"), -1));
				if (get(*tex, "texCoord") && intOr(get(*tex, "texCoord"), 0) != 0) d.note("A picture uses a second set of texture coordinates, which is not read.");
			}
			if (const Json* tex = get(*pbr, "metallicRoughnessTexture")) {
				const Json* texture = at(textures, intOr(get(*tex, "index"), -1));
				if (texture) mat.metalRoughImage = importImage(intOr(get(*texture, "source"), -1));
				d.note("Metal and roughness pictures are replaced by their average over the surface.");
			}
		}
		if (get(m, "normalTexture")) d.note("Normal maps are not read.");
		const bool emissivePicture = get(m, "emissiveTexture") != nullptr;   // (the factor multiplies the picture, which is mostly black: read alone it would make the whole surface glow)
		if (emissivePicture) d.note("Emissive pictures are not read: those surfaces do not glow.");
		if (get(m, "alphaMode") && strOr(get(m, "alphaMode")) != "OPAQUE") d.note("Transparent and cut-out surfaces (alpha) are drawn opaque.");
		mat.doubleSided = get(m, "doubleSided") && get(m, "doubleSided")->type == Json::Type::Bool && get(m, "doubleSided")->b;
		double strength = 1.0;
		const Json* extensions = get(m, "extensions");
		if (extensions) {
			if (const Json* t = get(*extensions, "KHR_materials_transmission")) mat.transmission = std::min(1.0, std::max(0.0, numOr(get(*t, "transmissionFactor"), 0.0)));
			if (const Json* o = get(*extensions, "KHR_materials_ior")) mat.ior = std::min(3.0, std::max(1.0, numOr(get(*o, "ior"), 1.5)));
			if (const Json* c = get(*extensions, "KHR_materials_clearcoat")) mat.clearcoat = std::min(1.0, std::max(0.0, numOr(get(*c, "clearcoatFactor"), 0.0)));
			if (const Json* s = get(*extensions, "KHR_materials_emissive_strength")) strength = std::max(0.0, numOr(get(*s, "emissiveStrength"), 1.0));
			if (get(*extensions, "KHR_materials_pbrSpecularGlossiness")) d.note("The specular-glossiness material is not read: its surfaces use plain colours.");
		}
		if (const Json* e = get(m, "emissiveFactor"))
			if (!emissivePicture && e->type == Json::Type::Arr && e->a.size() == 3)
				for (size_t k = 0; k < 3; ++k) mat.emissive[k] = std::max(0.0, numOr(&e->a[k], 0.0)) * strength;
		model.materials.push_back(std::move(mat));
	}
	int defaultMaterial = -1;   // made when a primitive names none
	std::map<int, size_t> partOfMaterial;

	// the meshes, through the node hierarchy
	const Json* nodes = get(d.root, "nodes");
	const Json* meshes = get(d.root, "meshes");
	const size_t nodeCount = nodes && nodes->type == Json::Type::Arr ? nodes->a.size() : 0;
	std::vector<long> roots;
	if (const Json* scenes = get(d.root, "scenes")) {
		const Json* scene = at(scenes, intOr(get(d.root, "scene"), 0));
		if (scene)
			if (const Json* list = get(*scene, "nodes"))
				for (const Json& n : list->a) roots.push_back(intOr(&n, -1));
	}
	if (roots.empty()) {   // (no scene named: every node nothing is a child of, or the meshes themselves)
		std::vector<bool> isChild(nodeCount, false);
		for (size_t i = 0; i < nodeCount; ++i)
			if (const Json* c = get(nodes->a[i], "children"))
				for (const Json& k : c->a) { const long child = intOr(&k, -1); if (child >= 0 && static_cast<size_t>(child) < nodeCount) isChild[static_cast<size_t>(child)] = true; }
		for (size_t i = 0; i < nodeCount; ++i)
			if (!isChild[i]) roots.push_back(static_cast<long>(i));
	}
	struct Visit { long node; Mat parent; int depth; };
	std::vector<Visit> stack;
	for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back({*it, identity(), 0});
	size_t visited = 0;
	size_t vertices = 0;
	const auto addPrimitive = [&](const Json& primitive, const Mat& world) -> bool {
		const Json* attributes = get(primitive, "attributes");
		if (!attributes || !get(*attributes, "POSITION")) return true;
		const long mode = intOr(get(primitive, "mode"), 4);
		if (mode != 4 && mode != 5 && mode != 6) { d.note("Points and lines in the model are not read: only triangles."); return true; }
		std::vector<double> pos, nrm, uv, idx;
		int pc = 0, nc = 0, uc = 0, ic = 0;
		size_t pn = 0, nn = 0, un = 0, in = 0;
		std::string err;
		if (!readAccessor(d, intOr(get(*attributes, "POSITION"), -1), pos, pc, pn, err) || pc != 3) { result.error = "A mesh's positions could not be read: " + (err.empty() ? std::string("they are not 3-vectors") : err) + "."; return false; }
		if (vertices + pn > kMaxVertices) { result.error = "The model has too many vertices to place."; return false; }
		const bool hasNormals = get(*attributes, "NORMAL") && readAccessor(d, intOr(get(*attributes, "NORMAL"), -1), nrm, nc, nn, err) && nc == 3 && nn == pn;
		const bool hasUv = get(*attributes, "TEXCOORD_0") && readAccessor(d, intOr(get(*attributes, "TEXCOORD_0"), -1), uv, uc, un, err) && uc == 2 && un == pn;
		if (get(*attributes, "COLOR_0")) d.note("Vertex colours are not read.");
		if (get(*attributes, "JOINTS_0")) d.note("The model has a skeleton: it is placed in its bind pose, not animated.");
		if (get(primitive, "targets")) d.note("Morph targets are not read.");
		if (get(*attributes, "TEXCOORD_1")) d.note("A second set of texture coordinates is not read.");
		std::vector<uint32_t> list;
		if (get(primitive, "indices")) {
			if (!readAccessor(d, intOr(get(primitive, "indices"), -1), idx, ic, in, err) || ic != 1) { result.error = "A mesh's indices could not be read: " + (err.empty() ? std::string("they are not scalars") : err) + "."; return false; }
			list.resize(in);
			for (size_t i = 0; i < in; ++i) {
				if (!(idx[i] >= 0.0) || idx[i] >= static_cast<double>(pn)) { result.error = "A mesh's indices point outside its vertices."; return false; }
				list[i] = static_cast<uint32_t>(idx[i]);
			}
		} else {
			list.resize(pn);
			for (size_t i = 0; i < pn; ++i) list[i] = static_cast<uint32_t>(i);
		}
		// strips and fans as triangles
		std::vector<uint32_t> tri;
		if (mode == 4) {
			tri.assign(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(list.size() / 3 * 3));
		} else if (mode == 5) {
			for (size_t i = 2; i < list.size(); ++i) {
				if (i % 2 == 0) { tri.push_back(list[i - 2]); tri.push_back(list[i - 1]); tri.push_back(list[i]); }
				else { tri.push_back(list[i - 1]); tri.push_back(list[i - 2]); tri.push_back(list[i]); }
			}
		} else {
			for (size_t i = 2; i < list.size(); ++i) { tri.push_back(list[0]); tri.push_back(list[i - 1]); tri.push_back(list[i]); }
		}
		if (tri.empty()) return true;
		// the part of this primitive's material
		int material = static_cast<int>(intOr(get(primitive, "material"), -1));
		if (material < 0 || static_cast<size_t>(material) >= declared) {
			if (defaultMaterial < 0) { Material plain; plain.name = "default"; plain.metallic = 0.0; plain.roughness = 1.0; model.materials.push_back(plain); defaultMaterial = static_cast<int>(model.materials.size()) - 1; }
			material = defaultMaterial;
		}
		auto found = partOfMaterial.find(material);
		if (found == partOfMaterial.end()) {
			Part p;
			p.material = material;
			model.parts.push_back(std::move(p));
			found = partOfMaterial.emplace(material, model.parts.size() - 1).first;
		}
		Part& part = model.parts[found->second];
		const uint32_t base = static_cast<uint32_t>(part.positions.size() / 3);
		for (size_t i = 0; i < pn; ++i) {
			const double x = pos[i * 3], y = pos[i * 3 + 1], z = pos[i * 3 + 2];
			part.positions.push_back(world[0] * x + world[4] * y + world[8] * z + world[12]);
			part.positions.push_back(world[1] * x + world[5] * y + world[9] * z + world[13]);
			part.positions.push_back(world[2] * x + world[6] * y + world[10] * z + world[14]);
		}
		const bool flip = determinant3(world) < 0.0;   // (a mirrored node turns the triangles inside out)
		const size_t firstTri = part.indices.size();
		for (size_t i = 0; i + 2 < tri.size(); i += 3) {
			part.indices.push_back(base + tri[i]);
			part.indices.push_back(base + (flip ? tri[i + 2] : tri[i + 1]));
			part.indices.push_back(base + (flip ? tri[i + 1] : tri[i + 2]));
		}
		// normals: the file's (turned like the surface), or smooth ones from the faces
		if (hasNormals) {
			// the inverse transpose of the upper 3 x 3 (cofactors over the determinant)
			const double det = determinant3(world);
			double inv[9];
			if (std::fabs(det) > 1e-18) {
				inv[0] = (world[5] * world[10] - world[9] * world[6]) / det; inv[1] = (world[9] * world[2] - world[1] * world[10]) / det; inv[2] = (world[1] * world[6] - world[5] * world[2]) / det;
				inv[3] = (world[8] * world[6] - world[4] * world[10]) / det; inv[4] = (world[0] * world[10] - world[8] * world[2]) / det; inv[5] = (world[4] * world[2] - world[0] * world[6]) / det;
				inv[6] = (world[4] * world[9] - world[8] * world[5]) / det;  inv[7] = (world[8] * world[1] - world[0] * world[9]) / det;  inv[8] = (world[0] * world[5] - world[4] * world[1]) / det;
			} else {
				for (int k = 0; k < 9; ++k) inv[k] = k % 4 == 0 ? 1.0 : 0.0;
			}
			for (size_t i = 0; i < pn; ++i) {
				const double x = nrm[i * 3], y = nrm[i * 3 + 1], z = nrm[i * 3 + 2];
				// `inv` holds the rows of the inverse transpose, which is what turns a normal
				double n[3] = {inv[0] * x + inv[1] * y + inv[2] * z, inv[3] * x + inv[4] * y + inv[5] * z, inv[6] * x + inv[7] * y + inv[8] * z};
				const double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
				if (l > 1e-12) { n[0] /= l; n[1] /= l; n[2] /= l; } else { n[0] = 0; n[1] = 1; n[2] = 0; }
				part.normals.insert(part.normals.end(), n, n + 3);
			}
		} else {
			std::vector<double> sum(pn * 3, 0.0);
			for (size_t t = firstTri; t + 2 < part.indices.size(); t += 3) {
				const size_t a = part.indices[t] - base, b = part.indices[t + 1] - base, c = part.indices[t + 2] - base;
				const double* A = &part.positions[(base + a) * 3]; const double* B = &part.positions[(base + b) * 3]; const double* C = &part.positions[(base + c) * 3];
				const double e1[3] = {B[0] - A[0], B[1] - A[1], B[2] - A[2]}, e2[3] = {C[0] - A[0], C[1] - A[1], C[2] - A[2]};
				const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};   // (its length is twice the area: area weighting)
				for (const size_t v : {a, b, c})
					for (int k = 0; k < 3; ++k) sum[v * 3 + static_cast<size_t>(k)] += n[k];
			}
			for (size_t i = 0; i < pn; ++i) {
				double* n = &sum[i * 3];
				const double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
				if (l > 1e-18) { n[0] /= l; n[1] /= l; n[2] /= l; } else { n[0] = 0; n[1] = 1; n[2] = 0; }
				part.normals.insert(part.normals.end(), n, n + 3);
			}
		}
		if (hasUv) { part.uvs.insert(part.uvs.end(), uv.begin(), uv.end()); part.hasUvs = true; }
		else part.uvs.insert(part.uvs.end(), pn * 2, 0.0);
		vertices += pn;
		return true;
	};
	while (!stack.empty()) {
		const Visit v = stack.back();
		stack.pop_back();
		const Json* node = at(nodes, v.node);
		if (!node || v.depth > 64 || ++visited > 200000) continue;   // (a node that is missing, a hierarchy too deep or a loop: skipped)
		const Mat world = multiply(v.parent, nodeMatrix(*node));
		const long meshIndex = intOr(get(*node, "mesh"), -1);
		if (meshIndex >= 0) {
			const Json* mesh = at(meshes, meshIndex);
			const Json* primitives = mesh ? get(*mesh, "primitives") : nullptr;
			if (primitives && primitives->type == Json::Type::Arr)
				for (const Json& primitive : primitives->a)
					if (!addPrimitive(primitive, world)) return result;
		}
		if (const Json* children = get(*node, "children"))
			for (auto it = children->a.rbegin(); it != children->a.rend(); ++it) stack.push_back({intOr(&*it, -1), world, v.depth + 1});
	}
	if (model.parts.empty()) { result.error = "The model has no triangles."; return result; }
	bool first = true;
	for (const Part& p : model.parts) {
		model.triangles += p.indices.size() / 3;
		for (size_t i = 0; i + 2 < p.positions.size(); i += 3)
			for (size_t a = 0; a < 3; ++a) {
				const double x = p.positions[i + a];
				if (first) model.lo[a] = model.hi[a] = x;
				model.lo[a] = std::min(model.lo[a], x);
				model.hi[a] = std::max(model.hi[a], x);
				if (a == 2) first = false;
			}
	}
	model.notes = d.notes;
	result.ok = true;
	return result;
}

// A part as a binary little-endian PLY, every position moved by -offset and multiplied by `scale`; the texture coordinates' v is turned over (a glTF picture starts at its top left, pbrt's at
// its bottom left).
inline std::string toPly(const Part& part, const double offset[3], double scale) {
	const size_t n = part.positions.size() / 3, faces = part.indices.size() / 3;
	std::string out = "ply\nformat binary_little_endian 1.0\nelement vertex " + std::to_string(n) +
	                  "\nproperty float x\nproperty float y\nproperty float z\nproperty float nx\nproperty float ny\nproperty float nz\nproperty float s\nproperty float t\nelement face " + std::to_string(faces) +
	                  "\nproperty list uchar int vertex_indices\nend_header\n";
	out.reserve(out.size() + n * 32 + faces * 13);
	const auto put = [&out](float f) { char b[4]; std::memcpy(b, &f, 4); out.append(b, 4); };
	for (size_t i = 0; i < n; ++i) {
		for (size_t a = 0; a < 3; ++a) put(static_cast<float>((part.positions[i * 3 + a] - offset[a]) * scale));
		for (size_t a = 0; a < 3; ++a) put(static_cast<float>(part.normals[i * 3 + a]));
		put(static_cast<float>(part.uvs[i * 2]));
		put(static_cast<float>(1.0 - part.uvs[i * 2 + 1]));
	}
	for (size_t f = 0; f < faces; ++f) {
		out.push_back(3);
		for (size_t k = 0; k < 3; ++k) { const int32_t v = static_cast<int32_t>(part.indices[f * 3 + k]); char b[4]; std::memcpy(b, &v, 4); out.append(b, 4); }
	}
	return out;
}

}  // namespace gltf_import
