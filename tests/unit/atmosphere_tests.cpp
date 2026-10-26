// atmosphere_tests.cpp -- the physical sky (src/shared/atmosphere.h): a blue sky over a whitish sun at noon, a red sun at the horizon, twilight after sunset fading to
// night, the Earth's shadow and the belt of Venus opposite a setting sun, a darker sky from a mountain, and magnitudes of the right size.

#include <gtest/gtest.h>

#include <cmath>

#include "../../src/shared/atmosphere.h"

namespace {

const double kD2R = atmosphere::kPi / 180.0;
// a direction at an elevation and an azimuth (degrees; y up)
void dir(double elevation, double azimuth, double out[3]) {
	out[0] = std::cos(elevation * kD2R) * std::sin(azimuth * kD2R);
	out[1] = std::sin(elevation * kD2R);
	out[2] = std::cos(elevation * kD2R) * std::cos(azimuth * kD2R);
}
const atmosphere::Model &model() {
	static const std::shared_ptr<const atmosphere::Model> m = atmosphere::modelFor(1.0);
	return *m;
}

}  // namespace

TEST(Atmosphere, TheNoonSkyIsBlueOverAWhiteSun) {
	double sun[3], zenith[3];
	dir(70.0, 0.0, sun);
	dir(90.0, 0.0, zenith);
	const atmosphere::RGB T = model().sunAt(sun, 0.0), sky = model().sky(zenith, sun, 0.0);
	EXPECT_GT(T.r, T.g);
	EXPECT_GT(T.g, T.b);
	EXPECT_NEAR(T.b, 0.74, 0.05) << "Rayleigh 0.28 and a little haze through the whole air, a little more than straight up";
	EXPECT_GT(T.r, 0.88);
	EXPECT_GT(sky.b, sky.g);
	EXPECT_GT(sky.g, sky.r);
	const double ratio = sky.luminance() / T.luminance();
	EXPECT_GT(ratio, 0.008) << "the zenith's luminance against the sun's illuminance: a few thousand cd/m2 to 100,000 lux";
	EXPECT_LT(ratio, 0.08);
	for (int a = 0; a < 360; a += 30)
		for (int e = -20; e <= 90; e += 10) {
			double v[3];
			dir(e, a, v);
			const atmosphere::RGB L = model().sky(v, sun, 0.0);
			EXPECT_TRUE(std::isfinite(L.r) && std::isfinite(L.g) && std::isfinite(L.b) && L.r >= 0.0 && L.g >= 0.0 && L.b >= 0.0) << a << " " << e;
		}
}

TEST(Atmosphere, TheLowSunIsRedAndTheGlowIsRoundIt) {
	double sun[3];
	dir(1.0, 90.0, sun);
	const atmosphere::RGB T = model().sunAt(sun, 0.0);
	EXPECT_GT(T.r, 6.0 * T.b) << "most of the blue scattered out on the long way through the air";
	double toward[3], away[3];
	dir(5.0, 90.0, toward);
	dir(5.0, 270.0, away);
	EXPECT_GT(model().sky(toward, sun, 0.0).luminance(), 2.0 * model().sky(away, sun, 0.0).luminance()) << "the haze throws the light forward round the sun";
	const atmosphere::RGB glow = model().sky(toward, sun, 0.0);
	EXPECT_GT(glow.r, glow.b) << "the sky round a setting sun is warm";
}

TEST(Atmosphere, TwilightFadesIntoNightWithTheEarthsShadowOppositeTheSun) {
	double zenith[3];
	dir(90.0, 0.0, zenith);
	double noonSun[3];
	dir(60.0, 90.0, noonSun);
	const double noon = model().sky(zenith, noonSun, 0.0).luminance();
	double sun[3];
	dir(-4.0, 90.0, sun);
	const atmosphere::RGB civil = model().sky(zenith, sun, 0.0);
	EXPECT_GT(civil.luminance(), 1e-4 * noon) << "the sky still lit high up 4 degrees after sunset";
	EXPECT_LT(civil.luminance(), 0.2 * noon);
	EXPECT_GT(civil.b, civil.r) << "the twilight zenith is blue (ozone)";
	dir(-18.0, 90.0, sun);
	EXPECT_LT(model().sky(zenith, sun, 0.0).luminance(), 1e-4 * noon) << "night";
	// opposite a sun just set: the Earth's shadow low down, the belt of Venus above it, pinker than the sky over it
	dir(-2.0, 90.0, sun);
	double low[3], belt[3], high[3];
	dir(1.0, 270.0, low);
	dir(10.0, 270.0, belt);
	dir(45.0, 270.0, high);
	const atmosphere::RGB l = model().sky(low, sun, 0.0), b = model().sky(belt, sun, 0.0), h = model().sky(high, sun, 0.0);
	EXPECT_LT(l.luminance(), b.luminance()) << "the shadow under the belt";
	EXPECT_GT(b.r / b.b, h.r / h.b) << "the belt redder than the sky above it";
}

TEST(Atmosphere, FromAMountainTheSkyIsDarkerAndTheSunWhiter) {
	double sun[3], zenith[3];
	dir(40.0, 0.0, sun);
	dir(90.0, 0.0, zenith);
	EXPECT_LT(model().sky(zenith, sun, 4000.0).luminance(), 0.8 * model().sky(zenith, sun, 0.0).luminance());
	EXPECT_GT(model().sunAt(sun, 4000.0).b, model().sunAt(sun, 0.0).b);
	// haze: a hazier day has a paler, brighter sky near the horizon round the sun and a weaker sun
	double toward[3];
	dir(10.0, 0.0, toward);
	const std::shared_ptr<const atmosphere::Model> hazy = atmosphere::modelFor(4.0);
	EXPECT_LT(hazy->sunAt(sun, 0.0).luminance(), model().sunAt(sun, 0.0).luminance());
	const atmosphere::RGB a = hazy->sky(toward, sun, 0.0), c = model().sky(toward, sun, 0.0);
	EXPECT_LT(a.b / a.r, c.b / c.r) << "whiter";
}
