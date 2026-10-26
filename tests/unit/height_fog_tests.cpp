// A fog that lies in a layer (src/shared/height_fog.h): its optical depth in closed form against a numerical integral, a sampled flight landing where its sampled depth is
// reached, rays that run level or climb out of it; and the CPU's camera medium of that kind (constant_medium.h ambient_medium) scattering as often as the layer says.

#include <gtest/gtest.h>
#include "rtweekend.h"
#include "constant_medium.h"

#include <cmath>

#include "../../src/shared/height_fog.h"

namespace {

// The optical depth by the midpoint rule, the slow way.
double numericDepth(double sigma, double top, double thick, double oy, double dy, double dlen, double t0, double t1) {
	const int n = 200000;
	double sum = 0.0;
	for (int i = 0; i < n; ++i) {
		const double t = t0 + (t1 - t0) * (i + 0.5) / n;
		sum += sigma * height_fog::profile(oy + t * dy, top, thick);
	}
	return sum * dlen * (t1 - t0) / n;
}

}  // namespace

TEST(HeightFog, TheOpticalDepthIsTheLayersIntegral) {
	const double sigma = 0.08, top = 10.0, thick = 2.5;
	struct Ray { double oy, dy, dlen, t0, t1; };
	for (const Ray &r : {Ray{0.0, 0.3, 1.0, 0.0, 60.0}, Ray{25.0, -0.5, 2.0, 0.5, 40.0}, Ray{12.0, 0.0, 1.0, 0.0, 30.0}, Ray{8.0, 1e-9, 1.0, 0.0, 50.0}, Ray{-5.0, 0.05, 3.0, 0.0, 200.0}}) {
		const double exact = height_fog::opticalDepth(sigma, top, thick, r.oy, r.dy, r.dlen, r.t0, r.t1);
		EXPECT_NEAR(exact, numericDepth(sigma, top, thick, r.oy, r.dy, r.dlen, r.t0, r.t1), 1e-6 * (1.0 + exact)) << r.oy << " " << r.dy;
	}
	// deep below the top it is the homogeneous fog; far above it nothing
	EXPECT_NEAR(height_fog::opticalDepth(sigma, top, thick, -50.0, 0.0, 1.0, 0.0, 10.0), sigma * 10.0, 1e-9);
	EXPECT_LT(height_fog::opticalDepth(sigma, top, thick, 200.0, 0.0, 1.0, 0.0, 10.0), 1e-20);
	// a ray that climbs out to infinity has a finite depth (the sky shows through the layer)
	const double up = height_fog::opticalDepth(sigma, top, thick, 0.0, 1.0, 1.0, 0.0, 1e30);
	EXPECT_TRUE(std::isfinite(up));
	EXPECT_NEAR(up, numericDepth(sigma, top, thick, 0.0, 1.0, 1.0, 0.0, top + 40.0 * thick), 1e-5);
	// a level ray far below the top, to infinity: none gets through
	EXPECT_GT(height_fog::opticalDepth(sigma, top, thick, 0.0, 0.0, 1.0, 0.0, 1e30), 1e4);
}

TEST(HeightFog, AFlightEndsWhereItsDepthIsReached) {
	const double sigma = 0.08, top = 10.0, thick = 2.5;
	struct Ray { double oy, dy, dlen, t0, t1; };
	for (const Ray &r : {Ray{0.0, 0.3, 1.0, 0.0, 60.0}, Ray{25.0, -0.5, 2.0, 0.5, 40.0}, Ray{12.0, 0.0, 1.0, 0.0, 30.0}, Ray{-5.0, 0.05, 3.0, 0.0, 200.0}})
		for (const double depth : {0.01, 0.3, 1.0, 2.5}) {
			const double total = height_fog::opticalDepth(sigma, top, thick, r.oy, r.dy, r.dlen, r.t0, r.t1);
			const double t = height_fog::sampleFlight(sigma, top, thick, r.oy, r.dy, r.dlen, r.t0, r.t1, depth);
			if (depth >= total) {
				EXPECT_GT(t, r.t1) << "it passes through";
			} else {
				ASSERT_GE(t, r.t0);
				ASSERT_LE(t, r.t1);
				EXPECT_NEAR(height_fog::opticalDepth(sigma, top, thick, r.oy, r.dy, r.dlen, r.t0, t), depth, 1e-6 * (1.0 + depth)) << r.oy << " " << r.dy << " " << depth;
			}
		}
}

// The CPU's camera medium of this kind: the share of rays that scatter before a distance is 1 - exp(-depth), and a shadow ray's transmittance is exp(-depth).
TEST(HeightFog, TheCpusCameraMediumScattersAsTheLayerSays) {
	ambient_medium fog(color(0.004, 0.004, 0.004), color(0.06, 0.06, 0.06), 0.5, color(0, 0, 0));
	fog.set_height(10.0, 2.5);
	ASSERT_TRUE(fog.layered());
	const ray r(point3(0, 2, 0), vec3(0.6, 0.35, 0.0));   // (not a unit direction)
	const double len = r.direction().length(), tMax = 50.0;
	const double depth = height_fog::opticalDepth(0.064, 10.0, 2.5, 2.0, 0.35, len, 0.0, tMax);
	int scattered = 0;
	const int n = 40000;
	for (int i = 0; i < n; ++i) {
		hit_record rec;
		if (fog.sample_scatter(r, tMax, rec)) {
			++scattered;
			ASSERT_LT(rec.t, tMax);
		}
	}
	const double expected = 1.0 - std::exp(-depth);
	EXPECT_NEAR(static_cast<double>(scattered) / n, expected, 4.0 * std::sqrt(expected * (1.0 - expected) / n)) << "depth " << depth;
	const color T = fog.transmittance_along(r, tMax * len);
	EXPECT_NEAR(T.x(), std::exp(-depth), 1e-9);
	// straight up into the sky: the light comes down through the layer
	const color up = fog.transmittance_along(ray(point3(0, 0, 0), vec3(0, 1, 0)), infinity);
	EXPECT_GT(up.x(), 0.0);
	EXPECT_LT(up.x(), 1.0);
}

// A layer whose extinction differs per channel (air): drawn per channel in units of the optical depth per unit of extinction, its weights estimate each channel's own
// transmittance on the way through and its own scattering on a collision - unbiased in every channel at once.
TEST(HeightFog, AColouredLayerIsUnbiasedInEveryChannel) {
	ambient_medium air(color(0, 0, 0), color(0.01, 0.03, 0.08), 0.0, color(0, 0, 0));
	air.set_height(10.0, 2.5);
	ASSERT_TRUE(air.layered());
	ASSERT_TRUE(air.chromatic());
	const ray r(point3(0, 2, 0), vec3(0.6, 0.2, 0.0));
	const double len = r.direction().length(), tMax = 60.0;
	const double D = height_fog::opticalDepth(1.0, 10.0, 2.5, 2.0, 0.2, len, 0.0, tMax);
	const double sig[3] = {0.01, 0.03, 0.08};
	double pass[3] = {0, 0, 0}, hit[3] = {0, 0, 0};
	const int n = 200000;
	for (int i = 0; i < n; ++i) {
		hit_record rec;
		color w(1, 1, 1);
		if (air.sample_scatter(r, tMax, rec, nullptr, &w)) {
			ASSERT_TRUE(rec.has_medium_event);
			ASSERT_LE(rec.t, tMax);
			for (int c = 0; c < 3; ++c) hit[c] += rec.medium_weight[c];
		} else {
			for (int c = 0; c < 3; ++c) pass[c] += w[c];
		}
	}
	const color T = air.transmittance_along(r, tMax * len);
	for (int c = 0; c < 3; ++c) {
		const double expect = std::exp(-sig[c] * D);
		EXPECT_NEAR(T[c], expect, 1e-9);
		EXPECT_NEAR(pass[c] / n, expect, 0.01) << "channel " << c << ": what gets through";
		EXPECT_NEAR(hit[c] / n, 1.0 - expect, 0.01) << "channel " << c << ": what scatters (no absorption)";
	}
	EXPECT_LT(T[2], T[0]) << "the blue scattered more";
}
