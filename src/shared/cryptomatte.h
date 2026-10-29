#pragma once
// cryptomatte.h -- ID mattes in the Cryptomatte layout (Psyop's open specification, Friedman & Jones 2015; what Blender's Cycles writes and what Blender's, Nuke's and
// Fusion's Cryptomatte nodes read): a matte for every object or material of a picture, with soft edges, from one render.
//
// A name (an object's, a material's) is hashed with MurmurHash3_x86_32 (seed 0), and the 32 bits are stored as a float: the bits as they are, except that an exponent
// of all zeros or all ones (a denormal, an infinity or a NaN) has its lowest exponent bit flipped. A layer "CryptoObject" holds, per pixel, the IDs that cover it most
// and how much each covers, ranked: CryptoObject00.R = the first ID, .G = its coverage, .B = the second, .A = its coverage (CryptoObject01 the third and fourth, ...).
// The file's header names each layer and lists its names with their hashes (the manifest), under the key of the layer: the first 7 hex digits of the layer name's hash.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cryptomatte {

// MurmurHash3_x86_32 (Austin Appleby, public domain).
inline std::uint32_t murmur3_32(const std::string &key, std::uint32_t seed = 0) {
	const std::uint8_t *data = reinterpret_cast<const std::uint8_t *>(key.data());
	const std::size_t len = key.size(), nblocks = len / 4;
	const std::uint32_t c1 = 0xcc9e2d51u, c2 = 0x1b873593u;
	const auto rotl = [](std::uint32_t x, int r) { return (x << r) | (x >> (32 - r)); };
	std::uint32_t h = seed;
	for (std::size_t i = 0; i < nblocks; ++i) {
		std::uint32_t k = static_cast<std::uint32_t>(data[4 * i]) | (static_cast<std::uint32_t>(data[4 * i + 1]) << 8) | (static_cast<std::uint32_t>(data[4 * i + 2]) << 16) |
						  (static_cast<std::uint32_t>(data[4 * i + 3]) << 24);
		k *= c1;
		k = rotl(k, 15);
		k *= c2;
		h ^= k;
		h = rotl(h, 13);
		h = h * 5 + 0xe6546b64u;
	}
	const std::uint8_t *tail = data + nblocks * 4;
	std::uint32_t k1 = 0;
	switch (len & 3) {
	case 3: k1 ^= static_cast<std::uint32_t>(tail[2]) << 16; [[fallthrough]];
	case 2: k1 ^= static_cast<std::uint32_t>(tail[1]) << 8; [[fallthrough]];
	case 1:
		k1 ^= tail[0];
		k1 *= c1;
		k1 = rotl(k1, 15);
		k1 *= c2;
		h ^= k1;
	}
	h ^= static_cast<std::uint32_t>(len);
	h ^= h >> 16;
	h *= 0x85ebca6bu;
	h ^= h >> 13;
	h *= 0xc2b2ae35u;
	h ^= h >> 16;
	return h;
}

// The hash as the float a Cryptomatte channel stores: no denormal, infinity or NaN.
inline float idToFloat(std::uint32_t hash) {
	const std::uint32_t exponent = (hash >> 23) & 255u;
	if (exponent == 0 || exponent == 255) hash ^= 1u << 23;
	float f;
	std::memcpy(&f, &hash, sizeof f);
	return f;
}

inline std::string hex8(std::uint32_t h) {
	char buf[9];
	std::snprintf(buf, sizeof buf, "%08x", h);
	return buf;
}

// The key a layer's metadata is under: "cryptomatte/<key>/name" and so on.
inline std::string layerKey(const std::string &layerName) { return hex8(murmur3_32(layerName)).substr(0, 7); }

// The manifest: {"name":"<hex of its hash>", ...}, the names JSON-escaped.
inline std::string manifestJson(const std::map<std::string, std::uint32_t> &names) {
	std::string out = "{";
	bool first = true;
	for (const auto &[name, hash] : names) {
		if (!first) out += ",";
		first = false;
		out += "\"";
		for (const char c : name) {
			if (c == '"' || c == '\\') out += '\\';
			if (static_cast<unsigned char>(c) < 0x20) continue;
			out += c;
		}
		out += "\":\"" + hex8(hash) + "\"";
	}
	return out + "}";
}

// The header attributes of a layer `layerName` whose names are `names`.
inline std::vector<std::pair<std::string, std::string>> layerMetadata(const std::string &layerName, const std::map<std::string, std::uint32_t> &names) {
	const std::string k = "cryptomatte/" + layerKey(layerName) + "/";
	return {{k + "name", layerName}, {k + "hash", "MurmurHash3_32"}, {k + "conversion", "uint32_to_float32"}, {k + "manifest", manifestJson(names)}};
}

}  // namespace cryptomatte
