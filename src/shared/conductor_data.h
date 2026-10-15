#pragma once

// ---------------------------------------------------------------------------
// Conductor RGB IOR presets (η + i·k per channel)
//
// Values sampled from pbrt-v4's piecewise-linear spectral tables
// (src/pbrt/util/spectrum.cpp: Au_eta/Au_k, Ag_eta/Ag_k, Al_eta/Al_k, Cu_eta/Cu_k)
// at the three sRGB primary wavelengths:
//   R = 630 nm,  G = 532 nm,  B = 467 nm
//
// Sampling method: linear interpolation between the two nearest tabulated
// {wavelength, value} pairs in each array — identical to pbrt-v4's
// PiecewiseLinearSpectrum::operator()(lambda).
//
// k is NOT read at those wavelengths any more: pbrt's film turns the reflected spectrum into RGB, and three wavelengths gave copper a normal-incidence reflectance of
// (0.924, 0.610, 0.569) where the film sees (0.951, 0.638, 0.540) (pbrt_scenes/rough-metal-lamp.pbrt read red 3% and green 4% low and blue 6% high on every backend), and gold
// red 8% low and blue 12% high. eta is as sampled; k is solved so that R0 = ((n-1)^2 + k^2) / ((n+1)^2 + k^2) is the RGB reflectance of the spectra projected through the CIE
// quadrature of measured_rgb_quadrature.h (scripts/conductor_rgb_presets.py prints the numbers; R0 is capped at 0.995).
//
// Reference: pbrt-v4 §9.6 "Conductor BRDF", spectrum data in spectrum.cpp
// ---------------------------------------------------------------------------

#include "cpu_gpu.h"

struct ConductorPreset {
	const char* name;
	// Real part of complex IOR (η) per RGB channel
	float eta_r, eta_g, eta_b;
	// Imaginary part of complex IOR (k, extinction coefficient) per RGB channel
	float k_r, k_g, k_b;
};

// ---------------------------------------------------------------------------
// Table — sourced directly from pbrt-v4 spectrum.cpp spectral arrays
// ---------------------------------------------------------------------------

// Gold (Au) — warm yellow reflectance; strongly wavelength-dependent
// η drops sharply in the blue/green, k rises through the spectrum
static constexpr ConductorPreset kConductorAu = {
	"Au",
	0.184f,  0.457f,  1.354f,   // eta R/G/B  (630/532/467 nm)
	12.065f, 2.524f,  1.654f    // k   R/G/B (solved for the film's R0: 0.995, 0.785, 0.346)
};

// Silver (Ag) — near-achromatic; highest reflectance of any metal
static constexpr ConductorPreset kConductorAg = {
	"Ag",
	0.130f,  0.130f,  0.137f,   // eta R/G/B
	4.187f,  3.413f,  2.311f    // k   R/G/B (R0 0.971, 0.960, 0.917)
};

// Aluminium (Al) — bright bluish-white; used for brushed metal
static constexpr ConductorPreset kConductorAl = {
	"Al",
	1.357f,  0.884f,  0.669f,   // eta R/G/B
	7.553f,  6.442f,  5.700f    // k   R/G/B (R0 0.913, 0.921, 0.924)
};

// Copper (Cu) — warm orange/red tint; k rises into the blue
static constexpr ConductorPreset kConductorCu = {
	"Cu",
	0.246f,  1.072f,  1.155f,   // eta R/G/B
	4.308f,  2.750f,  2.325f    // k   R/G/B (R0 0.951, 0.638, 0.540)
};

// Iron (Fe) — neutral dark grey; placeholder using typical published values
// (not directly in pbrt-v4 tables; values from Palik Handbook at 630/532/467 nm)
static constexpr ConductorPreset kConductorFe = {
	"Fe",
	2.940f,  2.915f,  2.773f,   // eta R/G/B
	3.080f,  2.950f,  2.813f    // k   R/G/B
};

// Lookup by name — returns nullptr if not found
CPU_GPU const ConductorPreset* FindConductorPreset(const char* name) {
	static const ConductorPreset table[] = {
		kConductorAu, kConductorAg, kConductorAl, kConductorCu, kConductorFe
	};
#if defined(__CUDACC__)
	// Simple strcmp equivalent for device code
	auto streq = [](const char* a, const char* b) {
		while (*a && *b) { if (*a++ != *b++) return false; }
		return *a == *b;
	};
#else
	auto streq = [](const char* a, const char* b) {
		return std::string(a) == std::string(b);
	};
#endif
	for (int i = 0; i < 5; ++i)
		if (streq(table[i].name, name)) return &table[i];
	return nullptr;
}
