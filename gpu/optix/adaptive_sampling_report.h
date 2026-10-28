#pragma once
// adaptive_sampling_report.h -- the log line both OptiX renderers print after an --adaptive render: how many samples the pixels took.
#include <cstdio>

inline void printAdaptiveSummary(unsigned long long samplesTaken, unsigned long long pixels, unsigned int samplesPerPixel) {
	if (pixels == 0 || samplesPerPixel == 0) return;
	const double perPixel = static_cast<double>(samplesTaken) / static_cast<double>(pixels);
	std::printf("[OptiX] Adaptive sampling: %.1f samples a pixel on average, of %u (%.0f%% of the full render's samples)\n", perPixel, samplesPerPixel,
	            100.0 * perPixel / samplesPerPixel);
	std::fflush(stdout);
}
