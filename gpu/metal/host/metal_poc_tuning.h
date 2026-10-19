// metal_poc_tuning.h -- the environment switches of the Metal renderer, read in ONE place.
//
// They used to be getenv() calls inside the dispatcher (a number parsed with atoi, so METAL_TG_W=abc became a threadgroup of width 0 and a failed dispatch). Now they are parsed here, once per
// process, a value that is not a positive whole number is ignored with a message instead of used, and what was set is printed once when it differs from the default, so a log says which
// switches a run had. Plain C++ (no Metal) so the host tests (metal_poc_math_tests.cpp) cover the parsing. The table of switches for people is docs/METAL_BACKEND.md, "Environment switches".
// (The test-only METAL_PARITY_* switches belong to the parity program and are read there; RT_METAL_POST_EFFECTS is a list with its own parser, parsePostEffects().)
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

struct MetalTuning {
	bool census = false;            // METAL_CENSUS: record and print how many serial passes the megakernel pays (per 8x4 tile and bounce)
	int pathRegen = -1;             // METAL_REGEN=0|1: force path regeneration off or on; -1 = the host picks per scene (on below about 1500 triangles)
	int forcedBands = 0;            // METAL_BANDS=N: N equal row bands instead of bands sized to about 0.3 s of GPU time; 0 = the time-targeted bands
	int threadgroupWidth = 0;       // METAL_TG_W, METAL_TG_H: the threadgroup shape; 0 = the default (8 wide, one SIMD group)
	int threadgroupHeight = 0;
	bool printThreadgroup = false;  // METAL_TG_PRINT: print the shape each dispatch uses
	bool fullKernel = false;        // METAL_FULL_KERNEL: build the pipeline with every material family, not only the ones the scene uses (metal_poc_families.h)

	static constexpr int kMaxBands = 4096;
	static constexpr int kMaxThreadgroupSide = 1024;

	// What a switch that could not be used said: parse() collects them, fromEnvironment() prints them.
	std::vector<std::string> problems;

	// `get(name)` returns the variable's value, or nullptr when it is not set (a lambda over getenv, or a table in a test).
	template <class Lookup>
	static MetalTuning parse(Lookup get) {
		MetalTuning t;
		t.census = get("METAL_CENSUS") != nullptr;
		t.printThreadgroup = get("METAL_TG_PRINT") != nullptr;
		t.fullKernel = get("METAL_FULL_KERNEL") != nullptr;
		if (const char* v = get("METAL_REGEN")) {
			if (v[0] == '0' && v[1] == 0) t.pathRegen = 0;
			else if (v[0] == '1' && v[1] == 0) t.pathRegen = 1;
			else t.problems.push_back(std::string("METAL_REGEN=") + v + " is ignored: it is 0 or 1");
		}
		t.forcedBands = positive(get, "METAL_BANDS", kMaxBands, t.problems);
		t.threadgroupWidth = positive(get, "METAL_TG_W", kMaxThreadgroupSide, t.problems);
		t.threadgroupHeight = positive(get, "METAL_TG_H", kMaxThreadgroupSide, t.problems);
		return t;
	}

	// The process's switches, read the first time they are asked for; problems and non-default switches are printed once.
	static const MetalTuning& fromEnvironment() {
		static const MetalTuning tuning = [] {
			MetalTuning t = parse([](const char* name) { return std::getenv(name); });
			for (const std::string& p : t.problems) std::fprintf(stderr, "Metal: %s\n", p.c_str());
			if (t.anyCustom()) std::fprintf(stderr, "Metal: tuning from the environment: %s\n", t.describe().c_str());
			return t;
		}();
		return tuning;
	}

	bool anyCustom() const { return census || pathRegen >= 0 || forcedBands > 0 || threadgroupWidth > 0 || threadgroupHeight > 0 || printThreadgroup || fullKernel; }

	std::string describe() const {
		std::string s;
		const auto add = [&s](const std::string& part) { s += (s.empty() ? "" : " ") + part; };
		if (census) add("METAL_CENSUS");
		if (pathRegen >= 0) add("METAL_REGEN=" + std::to_string(pathRegen));
		if (forcedBands > 0) add("METAL_BANDS=" + std::to_string(forcedBands));
		if (threadgroupWidth > 0) add("METAL_TG_W=" + std::to_string(threadgroupWidth));
		if (threadgroupHeight > 0) add("METAL_TG_H=" + std::to_string(threadgroupHeight));
		if (printThreadgroup) add("METAL_TG_PRINT");
		if (fullKernel) add("METAL_FULL_KERNEL");
		return s;
	}

private:
	// A whole number from 1 to `limit`: anything else (text, zero, a negative, a number too large for an int, one beyond the limit) is a problem and gives 0 (not set).
	template <class Lookup>
	static int positive(Lookup get, const char* name, int limit, std::vector<std::string>& problems) {
		const char* v = get(name);
		if (!v) return 0;
		char* end = nullptr;
		const long n = std::strtol(v, &end, 10);
		if (end == v || *end != 0 || n < 1 || n > limit) {
			problems.push_back(std::string(name) + "=" + v + " is ignored: it is a whole number from 1 to " + std::to_string(limit));
			return 0;
		}
		return static_cast<int>(n);
	}
};
