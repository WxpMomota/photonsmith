#pragma once
// tone_map_names.h -- the names `--tonemap` and a look line's `tonemap` take, with no types: what parses a name needs no more than this, and gpu/metal/host has a ToneMapMode
// of its own that would clash with tone_map.h's in a file that reached both. tone_map_mode_from_name() (tone_map.h) turns a name into its operator; a test keeps the two in step.

#include <string>

constexpr const char* kToneMapNames = "aces, agx, agx-punchy, filmic, neutral, reinhard, none";

inline bool tone_map_name_known(const std::string& name) {
	for (const char* known : {"aces", "agx", "agx-punchy", "filmic", "neutral", "reinhard", "none"})
		if (name == known) return true;
	return false;
}
