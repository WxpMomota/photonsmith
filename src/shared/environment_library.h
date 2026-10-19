#pragma once
// environment_library.h -- the Scene Builder's environment library: photographic skies and rooms (high-dynamic-range panoramas in the environments/ folder, from Poly Haven, CC0) that light a scene
// and show behind it, chosen from a gallery of thumbnails. Qt-free; environments/CREDITS.md says where each one comes from.
//
// An environment is an ordinary equirectangular .hdr as the Infinite light's picture (not the generated Sun & sky): the sun, the clouds, the lamps and the windows in the picture are the light, so
// there is no separate Sun light to keep (the library removes the Sun light the generated sky had made).

#include <algorithm>
#include <string>
#include <vector>

namespace environment_library {

struct Entry {
	const char* stem;       // the file name without ".hdr" (environments/<stem>.hdr, environments/thumbnails/<stem>.png)
	const char* name;       // what the gallery shows
	const char* category;   // the kind: "Sunrise and sunset", "Day", "Overcast", "City", "Night", "Studio", "Interior"
	const char* source;     // where it comes from, in a few words
};

inline const std::vector<Entry>& catalog() {
	static const std::vector<Entry> all = {
		{"the_sky_is_on_fire", "The sky is on fire", "Sunrise and sunset", "Poly Haven, CC0"},
		{"spruit_sunrise", "Spruit sunrise", "Sunrise and sunset", "Poly Haven, CC0"},
		{"belfast_sunset_puresky", "Belfast sunset", "Sunrise and sunset", "Poly Haven, CC0"},
		{"venice_sunset", "Venice sunset", "Sunrise and sunset", "Poly Haven, CC0"},
		{"kiara_1_dawn", "Kiara dawn", "Sunrise and sunset", "Poly Haven, CC0"},
		{"industrial_sunset_puresky", "Industrial sunset", "Sunrise and sunset", "Poly Haven, CC0"},
		{"umhlanga_sunrise", "Umhlanga sunrise", "Sunrise and sunset", "Poly Haven, CC0"},
		{"kloofendal_48d_partly_cloudy_puresky", "Partly cloudy sky", "Day", "Poly Haven, CC0"},
		{"meadow_2", "Meadow", "Day", "Poly Haven, CC0"},
		{"noon_grass", "Noon grass", "Day", "Poly Haven, CC0"},
		{"autumn_field_puresky", "Autumn field", "Day", "Poly Haven, CC0"},
		{"golden_gate_hills", "Golden Gate hills", "Day", "Poly Haven, CC0"},
		{"alps_field", "Alps field", "Day", "Poly Haven, CC0"},
		{"spiaggia_di_mondello", "Beach at Mondello", "Day", "Poly Haven, CC0"},
		{"autumn_park", "Autumn park", "Day", "Poly Haven, CC0"},
		{"overcast_soil_puresky", "Overcast sky", "Overcast", "Poly Haven, CC0"},
		{"cannon", "Cannon (overcast)", "Overcast", "Poly Haven, CC0"},
		{"wide_street_01", "Wide street", "City", "Poly Haven, CC0"},
		{"canary_wharf", "Canary Wharf", "City", "Poly Haven, CC0"},
		{"rural_asphalt_road", "Country road", "City", "Poly Haven, CC0"},
		{"moonless_golf", "Moonless golf course", "Night", "Poly Haven, CC0"},
		{"kloppenheim_02", "Starry night", "Night", "Poly Haven, CC0"},
		{"dikhololo_night", "Night in the bush", "Night", "Poly Haven, CC0"},
		{"satara_night_no_lamps", "Satara night", "Night", "Poly Haven, CC0"},
		{"shanghai_bund", "Shanghai Bund at night", "Night", "Poly Haven, CC0"},
		{"brown_photostudio_02", "Brown photo studio", "Studio", "Poly Haven, CC0"},
		{"studio_small_09", "Small studio", "Studio", "Poly Haven, CC0"},
		{"photo_studio_01", "Photo studio", "Studio", "Poly Haven, CC0"},
		{"fireplace", "Fireplace", "Interior", "Poly Haven, CC0"},
		{"cayley_interior", "Cayley interior", "Interior", "Poly Haven, CC0"},
	};
	return all;
}

// The categories in the order the gallery lists them (each appears once, in the order of first use in the catalog).
inline std::vector<std::string> categories() {
	std::vector<std::string> out;
	for (const Entry& e : catalog())
		if (std::find(out.begin(), out.end(), e.category) == out.end()) out.push_back(e.category);
	return out;
}

inline const Entry* findEntry(const std::string& stem) {
	for (const Entry& e : catalog())
		if (stem == e.stem) return &e;
	return nullptr;
}

inline std::string fileOf(const Entry& e) { return std::string(e.stem) + ".hdr"; }

}  // namespace environment_library
