// metal_poc_measured_table.h -- a measured BRDF (pbrt "measured") packed for the Metal shader (materialType 32, metal_poc_materials_extra.metal).
//
// The five PiecewiseLinear2D tables of the BRDF go into the shared float buffer `g` behind a descriptor of ints stored bit-for-bit in float slots; every offset is an absolute index into `g`
// and the descriptor comes FIRST so its own index (stored in a float, exact below 2^24) stays small however large the tables are. The descriptor is 76 ints for the five tables and
// `isotropic`, then three more 15-int table descriptors (R, G, B) for the spectral table already turned into RGB (the spectral table itself is not uploaded; its slots, 60..74, stay zero):
//
// The spectral table `spectra` is a function of (x, y, phi_i, theta_i, wavelength). A measured BSDF's colour is that table read at the 18 wavelengths of
// src/shared/measured_rgb_quadrature.h and weighted into R, G and B (each value clamped at zero first, the sum too). Done per evaluation that is 18 lookups spread over the whole table: the Metal
// shader took 24 times longer to render a measured scene than with three wavelengths (measured-lights, 64 pixels at 256 samples: 31 s against 1.3 s). It is linear in the table's own values
// (the clamp of each value does not matter: the bundled tables are not negative), so the wavelength axis is summed away ONCE, here: three tables R, G, B over (x, y, phi_i, theta_i), which the shader
// reads with the same PiecewiseLinear2D code it already has for the other tables (and 3 lookups of the 2 remaining parameters instead of 18 of 3).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../../src/shared/measured_bxdf_loader.h"   // MeasuredBRDFData
#include "../../src/shared/measured_rgb_quadrature.h"

template <size_t Dim>
inline void putMeasuredPL2D(const PiecewiseLinear2D<Dim>& t, std::vector<float>& g, size_t descBase) {
	int d[15] = {t.XSize(), t.YSize(), (int)Dim, 1, 1, 1, 0, 0, 0, -1, -1, -1, 0, -1, -1};
	for (size_t i = 0; i < Dim; ++i) {
		d[3 + i] = (int)t.ParamRes()[i];
		d[6 + i] = (int)t.ParamStride()[i];
		d[9 + i] = (int)g.size();
		g.insert(g.end(), t.ParamValues(i).begin(), t.ParamValues(i).end());
	}
	d[12] = (int)g.size();
	g.insert(g.end(), t.Data().begin(), t.Data().end());
	if (!t.Mcdf().empty()) { d[13] = (int)g.size(); g.insert(g.end(), t.Mcdf().begin(), t.Mcdf().end()); }
	if (!t.Ccdf().empty()) { d[14] = (int)g.size(); g.insert(g.end(), t.Ccdf().begin(), t.Ccdf().end()); }
	std::memcpy(&g[descBase], d, sizeof(d));
}

// The spectral table summed over the wavelength axis into R, G and B (see the header comment): three PiecewiseLinear2D-shaped tables with the parameters (phi_i, theta_i), written behind the
// descriptors at descBase, descBase + 15 and descBase + 30. The bracketing of a wavelength and its weights are PiecewiseLinear2D::FillPW's (the largest node not above it, the pair clamped
// to the axis, the weight clamped to [0, 1]); the values are the table's raw ones (the shader's Eval applies the 1 / (nx - 1)(ny - 1) factor, as it does for every table).
inline void putMeasuredSpectraRgb(const PiecewiseLinear2D<3>& spectra, std::vector<float>& g, size_t descBase) {
	const int nx = spectra.XSize(), ny = spectra.YSize();
	const int nPhi = (int)spectra.ParamRes()[0], nTheta = (int)spectra.ParamRes()[1], nLambda = (int)spectra.ParamRes()[2];
	const std::vector<float>& lambdaAxis = spectra.ParamValues(2);
	const float lambdas[RT_MEASURED_BINS] = { RT_MEASURED_LAMBDAS };
	const float wr[RT_MEASURED_BINS] = { RT_MEASURED_WR }, wg[RT_MEASURED_BINS] = { RT_MEASURED_WG }, wb[RT_MEASURED_BINS] = { RT_MEASURED_WB };
	int lo[RT_MEASURED_BINS];
	float t[RT_MEASURED_BINS];
	for (int k = 0; k < RT_MEASURED_BINS; ++k) {
		if (nLambda <= 1) { lo[k] = 0; t[k] = 0.0f; continue; }
		int a = 0, b = nLambda;
		while (a + 1 < b) {
			const int mid = (a + b) >> 1;
			if (lambdaAxis[mid] <= lambdas[k]) a = mid; else b = mid;
		}
		if (a + 1 >= nLambda) a = nLambda - 2;
		lo[k] = a;
		t[k] = std::min(1.0f, std::max(0.0f, (lambdas[k] - lambdaAxis[a]) / (lambdaAxis[a + 1] - lambdaAxis[a])));
	}
	const size_t cell = (size_t)nx * ny;
	const size_t strideLambda = spectra.ParamStride()[2], strideTheta = spectra.ParamStride()[1], stridePhi = spectra.ParamStride()[0];
	const std::vector<float>& data = spectra.Data();
	// the new tables: index = ix + iy * nx + (iPhi * 1 + iTheta * nPhi) * cell, as the descriptor's strides (1, nPhi; 0 for an axis of one node) say
	const size_t slices = (size_t)nPhi * nTheta;
	std::vector<float> rgb[3];
	for (int c = 0; c < 3; ++c) rgb[c].assign(slices * cell, 0.0f);
	for (int iTheta = 0; iTheta < nTheta; ++iTheta) {
		for (int iPhi = 0; iPhi < nPhi; ++iPhi) {
			const size_t srcSlice = iPhi * stridePhi + iTheta * strideTheta;
			const size_t dstSlice = (size_t)iPhi + (size_t)iTheta * nPhi;
			for (size_t i = 0; i < cell; ++i) {
				float r = 0.0f, gg = 0.0f, b = 0.0f;
				for (int k = 0; k < RT_MEASURED_BINS; ++k) {
					const size_t at = i + (srcSlice + (size_t)lo[k] * strideLambda) * cell;
					const float v0 = data[at];
					const float v1 = nLambda > 1 ? data[at + strideLambda * cell] : v0;
					const float v = std::max(0.0f, (1.0f - t[k]) * v0 + t[k] * v1);
					r += wr[k] * v; gg += wg[k] * v; b += wb[k] * v;
				}
				rgb[0][dstSlice * cell + i] = r;
				rgb[1][dstSlice * cell + i] = gg;
				rgb[2][dstSlice * cell + i] = b;
			}
		}
	}
	for (int c = 0; c < 3; ++c) {
		int d[15] = {nx, ny, 2, nPhi, nTheta, 1, nPhi > 1 ? 1 : 0, nTheta > 1 ? nPhi : 0, 0, -1, -1, -1, 0, -1, -1};
		for (int axis = 0; axis < 2; ++axis) {
			d[9 + axis] = (int)g.size();
			g.insert(g.end(), spectra.ParamValues((size_t)axis).begin(), spectra.ParamValues((size_t)axis).end());
		}
		d[12] = (int)g.size();
		g.insert(g.end(), rgb[c].begin(), rgb[c].end());
		std::memcpy(&g[descBase + 15 * (size_t)c], d, sizeof(d));
	}
}

// Appends the descriptor and the tables of one measured BRDF to `g`; returns the descriptor's index.
inline size_t appendMeasuredBrdf(const MeasuredBRDFData& m, std::vector<float>& g) {
	const size_t base = g.size();
	g.resize(base + 121, 0.0f);
	putMeasuredPL2D(m.ndf, g, base);
	putMeasuredPL2D(m.sigma, g, base + 15);
	putMeasuredPL2D(m.vndf, g, base + 30);
	putMeasuredPL2D(m.luminance, g, base + 45);
	putMeasuredPL2D(m.spectra, g, base + 60);   // (the shader no longer reads it: it reads the RGB tables below; kept so the descriptor's first 76 ints are what they always were)
	const int iso = m.isotropic ? 1 : 0;
	std::memcpy(&g[base + 75], &iso, sizeof(iso));
	putMeasuredSpectraRgb(m.spectra, g, base + 76);
	return base;
}
