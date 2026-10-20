// portal_image_infinite_light_tests.cpp
// Unit tests for PortalImageInfiniteLightData<T>
// Mirrors pbrt-v4 PortalImageInfiniteLight semantics (lights.cpp).
#include <gtest/gtest.h>
#include "../../src/shared/portal_image_infinite_light.h"
#include <cmath>
#include <vector>
#include <array>

static constexpr double PI = 3.14159265358979323846;

// Helper: build a uniform 1x1 (trivial) or NxN equal-area HDR image with constant colour.
static std::vector<float> make_uniform_image(int w, int h, float r, float g, float b) {
	std::vector<float> img(w * h * 3);
	for (int i = 0; i < w * h; ++i) {
		img[i*3  ] = r;
		img[i*3+1] = g;
		img[i*3+2] = b;
	}
	return img;
}

// Portal facing +Z (frame z-axis = +Z, shading points are in the -Z half-space).
// Corner ordering ensures p03 = portal[3]-portal[0] = right = (1,0,0)
// and p01 = portal[1]-portal[0] = up = (0,1,0),
// so Frame::FromXY(p03, p01).z = Cross((1,0,0),(0,1,0)) = (0,0,1) pointing toward origin.
using Vec3d = pil_detail::Vec3<double>;
static std::array<Vec3d,4> make_z_portal(double z = 10.0, double hw = 1.0) {
	return {{
		{-hw, -hw, z},   // portal[0]
		{-hw,  hw, z},   // portal[1]  p01 = up  = (0,1,0)
		{ hw,  hw, z},   // portal[2]  diagonal
		{ hw, -hw, z}    // portal[3]  p03 = right = (1,0,0)
	}};
}

// ---------------------------------------------------------------------------
// Basic construction
// ---------------------------------------------------------------------------
TEST(PortalImageInfiniteLight, ConstructsWithoutCrash) {
	auto img = make_uniform_image(4, 4, 1.f, 1.f, 1.f);
	auto portal = make_z_portal();
	PortalImageInfiniteLightData<double> light(img.data(), 4, 4, 1.0, portal);
	EXPECT_EQ(light.width(), 4);
	EXPECT_EQ(light.height(), 4);
}

TEST(PortalImageInfiniteLight, ScaleAccessor) {
	auto img = make_uniform_image(4, 4, 1.f, 0.f, 0.f);
	auto portal = make_z_portal();
	PortalImageInfiniteLightData<double> light(img.data(), 4, 4, 2.5, portal);
	EXPECT_DOUBLE_EQ(light.scale(), 2.5);
}

// ---------------------------------------------------------------------------
// eval_Le: direction through portal returns positive; direction away returns 0
// ---------------------------------------------------------------------------
TEST(PortalImageInfiniteLight, Le_ThroughPortalIsPositive) {
	auto img = make_uniform_image(8, 8, 1.f, 1.f, 1.f);
	auto portal = make_z_portal(10.0, 1.0);
	PortalImageInfiniteLightData<double> light(img.data(), 8, 8, 1.0, portal);
	// Ray origin at origin, direction straight through the portal (+Z)
	double val = light.eval_Le(0, 0, 0,  0, 0, 1);
	EXPECT_GT(val, 0.0);
}

TEST(PortalImageInfiniteLight, Le_AwayFromPortalIsZero) {
	auto img = make_uniform_image(8, 8, 1.f, 1.f, 1.f);
	auto portal = make_z_portal(10.0, 1.0);
	PortalImageInfiniteLightData<double> light(img.data(), 8, 8, 1.0, portal);
	// Direction pointing away from portal (-Z)
	double val = light.eval_Le(0, 0, 0,  0, 0, -1);
	EXPECT_DOUBLE_EQ(val, 0.0);
}

// ---------------------------------------------------------------------------
// eval_Le_rgb: RGB result proportional to input colours
// ---------------------------------------------------------------------------
TEST(PortalImageInfiniteLight, Le_rgb_ProportionalToInputColour) {
	// Red-only image
	auto img = make_uniform_image(4, 4, 2.f, 0.f, 0.f);
	auto portal = make_z_portal(10.0, 1.0);
	PortalImageInfiniteLightData<double> light(img.data(), 4, 4, 1.0, portal);
	double r, g, b;
	light.eval_Le_rgb(0, 0, 0,  0, 0, 1,  r, g, b);
	// Red channel should dominate, g and b near 0
	EXPECT_GT(r, 0.0);
	EXPECT_NEAR(g, 0.0, 1e-6);
	EXPECT_NEAR(b, 0.0, 1e-6);
}

// ---------------------------------------------------------------------------
// sample_li returns valid direction pointing through the portal
// ---------------------------------------------------------------------------
TEST(PortalImageInfiniteLight, SampleLi_ReturnsTrueForVisiblePortal) {
	auto img = make_uniform_image(8, 8, 1.f, 1.f, 1.f);
	auto portal = make_z_portal(10.0, 1.0);
	PortalImageInfiniteLightData<double> light(img.data(), 8, 8, 1.0, portal);
	double wx, wy, wz, pdf;
	bool ok = light.sample_li(0.5, 0.5,  0.0, 0.0, 0.0,  wx, wy, wz, pdf);
	EXPECT_TRUE(ok);
	EXPECT_GT(pdf, 0.0);
	// Sampled direction should have positive Z (through portal)
	EXPECT_GT(wz, 0.0);
	// Direction should be normalised
	double len = std::sqrt(wx*wx + wy*wy + wz*wz);
	EXPECT_NEAR(len, 1.0, 1e-6);
}

TEST(PortalImageInfiniteLight, SampleLi_ReturnsFalseFromBehindPortal) {
	auto img = make_uniform_image(4, 4, 1.f, 1.f, 1.f);
	// Portal in +Z hemisphere, origin at z=20 (behind the portal from shading point of view)
	auto portal = make_z_portal(10.0, 0.01);  // tiny portal, shading point at z=20 is behind
	PortalImageInfiniteLightData<double> light(img.data(), 4, 4, 1.0, portal);
	double wx, wy, wz, pdf;
	// Shading point behind the portal (z=20 > z=10) - portal behind the point
	bool ok = light.sample_li(0.5, 0.5, 0.0, 0.0, 20.0, wx, wy, wz, pdf);
	// ImageBounds will fail since the portal is behind the shading point
	EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// pdf_li is consistent with sample_li (same direction -> positive PDF)
// ---------------------------------------------------------------------------
TEST(PortalImageInfiniteLight, PdfLi_PositiveForSampledDirection) {
	auto img = make_uniform_image(8, 8, 1.f, 1.f, 1.f);
	auto portal = make_z_portal(10.0, 1.0);
	PortalImageInfiniteLightData<double> light(img.data(), 8, 8, 1.0, portal);
	double wx, wy, wz, sample_pdf;
	bool ok = light.sample_li(0.3, 0.7,  0.0, 0.0, 0.0, wx, wy, wz, sample_pdf);
	ASSERT_TRUE(ok);
	double pdf = light.pdf_li(0.0, 0.0, 0.0,  wx, wy, wz);
	EXPECT_GT(pdf, 0.0);
	// Both PDFs should be in reasonable agreement (within 50% since nearest-vs-bilinear)
	EXPECT_NEAR(pdf, sample_pdf, 0.5 * sample_pdf + 1e-12);
}

// ---------------------------------------------------------------------------
// pdf_li returns 0 for direction outside portal angular window
// ---------------------------------------------------------------------------
TEST(PortalImageInfiniteLight, PdfLi_ZeroForDirectionAwayFromPortal) {
	auto img = make_uniform_image(8, 8, 1.f, 1.f, 1.f);
	auto portal = make_z_portal(10.0, 1.0);
	PortalImageInfiniteLightData<double> light(img.data(), 8, 8, 1.0, portal);
	// Direction pointing -Z (away from the portal)
	double pdf = light.pdf_li(0.0, 0.0, 0.0,  0.0, 0.0, -1.0);
	EXPECT_DOUBLE_EQ(pdf, 0.0);
}

// ---------------------------------------------------------------------------
// scale factor scales Le proportionally
// ---------------------------------------------------------------------------
TEST(PortalImageInfiniteLight, ScaleFactorAffectsLe) {
	auto img = make_uniform_image(4, 4, 1.f, 1.f, 1.f);
	auto portal = make_z_portal(10.0, 1.0);
	PortalImageInfiniteLightData<double> light1(img.data(), 4, 4, 1.0, portal);
	PortalImageInfiniteLightData<double> light2(img.data(), 4, 4, 3.0, portal);
	double v1 = light1.eval_Le(0,0,0, 0,0,1);
	double v2 = light2.eval_Le(0,0,0, 0,0,1);
	EXPECT_GT(v1, 0.0);
	EXPECT_NEAR(v2, 3.0 * v1, 1e-10);
}

// ---------------------------------------------------------------------------
// power() is positive for non-black image
// ---------------------------------------------------------------------------
TEST(PortalImageInfiniteLight, PowerIsPositive) {
	auto img = make_uniform_image(4, 4, 1.f, 1.f, 1.f);
	auto portal = make_z_portal(10.0, 1.0);
	PortalImageInfiniteLightData<double> light(img.data(), 4, 4, 1.0, portal);
	EXPECT_GT(light.power(), 0.0);
}

TEST(PortalImageInfiniteLight, PowerIsZeroForBlackImage) {
	auto img = make_uniform_image(4, 4, 0.f, 0.f, 0.f);
	auto portal = make_z_portal(10.0, 1.0);
	PortalImageInfiniteLightData<double> light(img.data(), 4, 4, 1.0, portal);
	EXPECT_DOUBLE_EQ(light.power(), 0.0);
}

// ---------------------------------------------------------------------------
// Multiple samples from sample_li all produce directions in the +Z hemisphere
// (consistent with a portal facing +Z)
// ---------------------------------------------------------------------------
TEST(PortalImageInfiniteLight, MultiSample_AllDirectionsThroughPortal) {
	auto img = make_uniform_image(8, 8, 1.f, 1.f, 1.f);
	auto portal = make_z_portal(10.0, 1.0);
	PortalImageInfiniteLightData<double> light(img.data(), 8, 8, 1.0, portal);
	// Test a grid of sample points
	for (int i = 1; i <= 4; ++i) {
		for (int j = 1; j <= 4; ++j) {
			double ru = i / 5.0, rv = j / 5.0;
			double wx, wy, wz, pdf;
			bool ok = light.sample_li(ru, rv, 0.0, 0.0, 0.0, wx, wy, wz, pdf);
			if (ok) {
				EXPECT_GT(pdf, 0.0);
				EXPECT_GT(wz, 0.0) << "ru=" << ru << " rv=" << rv;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// PortalLightSet (portal_light_set.h): several portal lights as one (pbrt sums every light of a file)
// ---------------------------------------------------------------------------
#include "../../src/shared/portal_light_set.h"

namespace {
// A portal facing +Z at z, and one facing +X at x = -z (a second window on another wall); shading points near the origin see both.
std::array<Vec3d,4> make_x_portal(double x = -10.0, double hw = 1.0) {
	return {{
		{x, -hw, -hw},   // portal[0]
		{x,  hw, -hw},   // portal[1]  p01 = up    = (0,1,0)
		{x,  hw,  hw},   // portal[2]  diagonal
		{x, -hw,  hw}    // portal[3]  p03 = right = (0,0,1): Cross((0,0,1),(0,1,0)) = (-1,0,0), outward
	}};
}

struct TwoPortals {
	std::vector<float> imgA = make_uniform_image(8, 8, 1.f, 0.f, 0.f);
	std::vector<float> imgB = make_uniform_image(8, 8, 0.f, 0.f, 1.f);
	std::shared_ptr<PortalImageInfiniteLightData<double>> a, b;
	PortalLightSet<double> set;
	TwoPortals() : a(std::make_shared<PortalImageInfiniteLightData<double>>(imgA.data(), 8, 8, 1.0, make_z_portal(10.0, 1.0))),
				   b(std::make_shared<PortalImageInfiniteLightData<double>>(imgB.data(), 8, 8, 2.0, make_x_portal(-10.0, 1.0))),
				   set(std::vector<std::shared_ptr<PortalImageInfiniteLightData<double>>>{a, b}) {}
};
}  // namespace

TEST(PortalLightSet, ASetOfOneIsThatLight) {
	auto img = make_uniform_image(8, 8, 1.f, 1.f, 1.f);
	auto one = std::make_shared<PortalImageInfiniteLightData<double>>(img.data(), 8, 8, 1.0, make_z_portal(10.0, 1.0));
	PortalLightSet<double> set({one});
	double sr, sg, sb, lr, lg, lb;
	set.eval_Le_rgb(0, 0, 0, 0, 0, 1, sr, sg, sb);
	one->eval_Le_rgb(0, 0, 0, 0, 0, 1, lr, lg, lb);
	EXPECT_DOUBLE_EQ(sr, lr);
	EXPECT_DOUBLE_EQ(sb, lb);
	EXPECT_DOUBLE_EQ(set.pdf_li(0, 0, 0, 0, 0, 1), one->pdf_li(0, 0, 0, 0, 0, 1));
}

TEST(PortalLightSet, LeIsTheSumAndEachWindowShowsItsOwnLight) {
	TwoPortals t;
	double r, g, b;
	t.set.eval_Le_rgb(0, 0, 0, 0, 0, 1, r, g, b);   // through the +Z window: only A (red)
	EXPECT_GT(r, 0.0);
	EXPECT_DOUBLE_EQ(b, 0.0);
	t.set.eval_Le_rgb(0, 0, 0, -1, 0, 0, r, g, b);  // through the -X window: only B (blue, scale 2)
	EXPECT_DOUBLE_EQ(r, 0.0);
	EXPECT_GT(b, 0.0);
	t.set.eval_Le_rgb(0, 0, 0, 0, 1, 0, r, g, b);   // through neither
	EXPECT_DOUBLE_EQ(r + g + b, 0.0);
}

TEST(PortalLightSet, TheMixtureDensityCountsOnlyTheLightsThatCanProduceTheDirection) {
	TwoPortals t;
	// Towards window A: B's table has a value there but B does not shine from that direction, so the mixture is half of A's density.
	const double pdfA = t.a->pdf_li(0, 0, 0, 0, 0, 1);
	EXPECT_GT(pdfA, 0.0);
	EXPECT_NEAR(t.set.pdf_li(0, 0, 0, 0, 0, 1), 0.5 * pdfA, 1e-9);
	const double pdfB = t.b->pdf_li(0, 0, 0, -1, 0, 0);
	EXPECT_NEAR(t.set.pdf_li(0, 0, 0, -1, 0, 0), 0.5 * pdfB, 1e-9);
	EXPECT_DOUBLE_EQ(t.set.pdf_li(0, 0, 0, 0, 1, 0), 0.0);
}

TEST(PortalLightSet, ASampleReportsTheMixtureDensityAndPicksEachLightHalfTheTime) {
	TwoPortals t;
	int viaA = 0, viaB = 0, edge = 0;
	const int n = 2000;
	for (int i = 0; i < n; ++i) {
		const double ru = (i + 0.5) / n, rv = std::fmod(i * 0.6180339887, 1.0);
		double wx, wy, wz, pdf;
		ASSERT_TRUE(t.set.sample_li(ru, rv, 0, 0, 0, wx, wy, wz, pdf));
		// (a sample on the very edge of its window can read as just outside it when the direction is rounded: the sample's own density is what counts then)
		const double again = t.set.pdf_li(0, 0, 0, wx, wy, wz);
		if (again > 0.0) EXPECT_NEAR(pdf, again, 1e-6 * std::max(1.0, pdf)) << "sample " << i;
		else ++edge;
		(wz > std::fabs(wx) ? viaA : viaB)++;   // A shines from +Z, B from -X
	}
	EXPECT_NEAR(static_cast<double>(viaA) / n, 0.5, 0.01);
	EXPECT_EQ(viaA + viaB, n);
	EXPECT_LE(edge, 5);
}

TEST(PortalLightSet, TheSetIntegratesToOneOverTheSphere) {
	TwoPortals t;
	// Sum of pdf * solid angle over the directions, midpoint rule on a (cos theta, phi) grid: a density integrates to 1 (the portals' windows are all inside the sphere).
	const int nt = 400, np = 800;
	double total = 0.0;
	for (int i = 0; i < nt; ++i) {
		const double z = 1.0 - 2.0 * (i + 0.5) / nt, s = std::sqrt(1.0 - z * z);
		for (int j = 0; j < np; ++j) {
			const double phi = 2.0 * PI * (j + 0.5) / np;
			total += t.set.pdf_li(0, 0, 0, s * std::cos(phi), s * std::sin(phi), z);
		}
	}
	total *= 4.0 * PI / (static_cast<double>(nt) * np);
	EXPECT_NEAR(total, 1.0, 0.03);
}
