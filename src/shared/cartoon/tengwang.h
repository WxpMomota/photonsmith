#pragma once
// tengwang.h -- "Preface to the Pavilion of Prince Teng" (滕王阁序), a ten-minute film after Wang Bo's preface of AD 675: one autumn day at Hongzhou on the Gan River, from the
// sea of clouds before sunrise to the stars over the river at night, in 2.39:1 with its words on the screen (Chinese with the English beneath) and a score.
//
//   1  The land of Hongzhou        the sea of clouds before sunrise, the river coming out of the mountains, the city, the Pavilion revealed in the morning sun
//   2  A gathering of great men    the procession in at the west gate, the things of the feast, the hall made ready, the guests, the young traveller at the edge of them
//   3  Ascending the Pavilion      Wang Bo climbs the stair and steps out on the high terrace into the wind; the world below; a slow turn round him
//   4  The world below             the market, boats under the Pavilion, a shepherd, egrets rising, the boats coming home, the geese
//   5  Autumn                      a ginkgo leaf falling past the railing; the sunset; the solitary duck and the setting clouds; the river and the sky one colour
//   6  The banquet                 wine, the pipa, poems; the paper comes round and reaches Wang Bo; the brush touches it
//   7  The world inside the brush  the ink becomes the river, the ridge, a bird, a storm; the flight back into the world; the Pavilion small far below
//   8  Distance                    alone on the balcony; ridges without end; the pass in the rain; the crossroads
//   9  Unrecognised talent         the palace doors closing on an old official; the general alone in the snow; the ink drop, the stone sinking
//   10 Defiance                    he looks up; the wind; the terrace against the whole sky; the eagle; everything at once
//   11 The young Wang Bo           he writes the last lines in silence; the faces; the brush laid down
//   12 Epilogue                    the lanterns go out; down the stair; the horse; the ride away; the stars over the river; the manuscript; dawn; the title
//
// The film's places are in tengwang_land.h (the land, the river, the sky), tengwang_pavilion.h, tengwang_city.h, tengwang_life.h (trees, people, horses, boats, birds),
// tengwang_sets.h (the hall, the props) and tengwang_dreams.h (the dreams, the clouds); its music is tengwang_score.h; its words are pictures made by
// scripts/make_tengwang_assets.py (pbrt_scenes/film_assets/tengwang).

#include <functional>

#include "tengwang_dreams.h"

namespace cartoon {
namespace tengwang {

constexpr double kDuration = 620.0;
constexpr int kFps = 24;
constexpr int kSamples = 32;
constexpr int kWidth = 1920, kHeight = 804;   // 2.39:1

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The skies of the day
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
enum Sky { PreDawn = 0, Sunrise, Morning, Afternoon, Golden, LateGolden, Sunset, SunsetLow, Dusk, Twilight, BlueHour, Night, LastDawn, kSkyCount };
inline film_sky::Clouds cloudLayer(double altitude, double coverage, double along, double across, double direction, Vec3 lit, Vec3 shade, int seed, double silver = 1.2) {
	film_sky::Clouds c;
	c.altitude = altitude;
	c.coverage = coverage;
	c.scaleAlong = along;
	c.scaleAcross = across;
	c.direction = direction;
	c.lit = lit;
	c.shade = shade;
	c.seed = seed;
	c.silver = silver;
	return c;
}
inline SunPlace sunOf(int sky) {
	static const SunPlace s[kSkyCount] = {{-3.5, 200.0}, {3.5, 200.0}, {9.0, 205.0}, {32.0, -55.0}, {14.0, -32.0}, {7.0, -27.0}, {2.2, -22.0}, {0.3, -21.0}, {-2.5, -20.0}, {-4.5, -20.0}, {-6.0, -20.0}, {-25.0, -20.0}, {-2.5, 200.0}};
	return s[sky];
}
inline film_sky::Spec skySpec(int k) {
	film_sky::Spec s;
	static const char *names[kSkyCount] = {"predawn", "sunrise", "morning", "afternoon", "golden", "lategolden", "sunset", "sunsetlow", "dusk", "twilight", "bluehour", "night", "lastdawn"};
	s.name = std::string("tw_") + names[k];
	s.sunElevation = sunOf(k).elevation;
	s.sunAzimuth = sunOf(k).azimuth;
	switch (k) {
		case PreDawn:
			s.turbidity = 3.0; s.night = 0.25; s.stars = 0.3; s.afterglow = 0.55; s.afterglowColour = {1.0, 0.38, 0.22}; s.belt = 0.12;
			s.clouds = {cloudLayer(7000, 0.30, 16000, 2500, 60, {1.5, 0.52, 0.55}, {0.08, 0.07, 0.15}, 11)};
			break;
		case Sunrise:
			s.turbidity = 3.5; s.glow = 0.6; s.glowColour = {1.0, 0.55, 0.28}; s.afterglow = 0.35; s.belt = 0.05;
			s.clouds = {cloudLayer(6500, 0.33, 15000, 2600, 60, {2.4, 1.05, 0.55}, {0.20, 0.12, 0.24}, 12)};
			break;
		case Morning:
			s.turbidity = 3.0; s.glow = 0.2;
			s.clouds = {cloudLayer(6000, 0.30, 14000, 3000, 70, {2.0, 1.6, 1.2}, {0.40, 0.38, 0.46}, 13)};
			break;
		case Afternoon:
			s.turbidity = 2.6;
			s.clouds = {cloudLayer(2600, 0.30, 5200, 3200, 40, {3.0, 2.9, 2.7}, {0.55, 0.58, 0.70}, 14, 0.8), cloudLayer(8000, 0.22, 18000, 2500, 75, {2.4, 2.3, 2.2}, {0.6, 0.62, 0.72}, 15)};
			break;
		case Golden:
			s.turbidity = 3.2; s.glow = 0.12; s.glowColour = {1.0, 0.70, 0.40};
			s.clouds = {cloudLayer(6500, 0.34, 15000, 3000, 70, {2.6, 1.9, 1.1}, {0.45, 0.40, 0.50}, 16), cloudLayer(2400, 0.18, 5000, 3000, 30, {2.4, 1.8, 1.1}, {0.40, 0.36, 0.44}, 17, 0.8)};
			break;
		case LateGolden:
			s.turbidity = 3.5; s.glow = 0.15; s.afterglow = 0.1;
			s.clouds = {cloudLayer(6500, 0.36, 15000, 2800, 72, {2.6, 1.5, 0.7}, {0.40, 0.30, 0.40}, 18), cloudLayer(2600, 0.20, 5000, 3000, 30, {2.4, 1.3, 0.6}, {0.32, 0.24, 0.32}, 19, 0.8)};
			break;
		case Sunset:   // orange, crimson, violet and gold, streaming across the whole sky
			s.turbidity = 4.2; s.glow = 0.7; s.glowColour = {1.0, 0.48, 0.18}; s.afterglow = 0.45; s.afterglowColour = {1.0, 0.35, 0.12}; s.belt = 0.05;
			s.clouds = {cloudLayer(7500, 0.46, 20000, 2400, 78, {2.9, 1.05, 0.38}, {0.42, 0.12, 0.32}, 21), cloudLayer(3000, 0.36, 9000, 1800, 82, {2.3, 0.55, 0.26}, {0.20, 0.06, 0.17}, 22)};
			break;
		case SunsetLow:
			s.turbidity = 4.5; s.glow = 0.85; s.glowColour = {1.0, 0.42, 0.14}; s.afterglow = 0.6; s.afterglowColour = {1.0, 0.32, 0.10}; s.belt = 0.06;
			s.clouds = {cloudLayer(7500, 0.46, 20000, 2400, 78, {2.4, 0.75, 0.32}, {0.36, 0.10, 0.30}, 21), cloudLayer(3000, 0.36, 9000, 1800, 82, {1.9, 0.42, 0.22}, {0.16, 0.05, 0.15}, 22)};
			break;
		case Dusk:
			s.turbidity = 3.8; s.afterglow = 0.32; s.afterglowColour = {1.0, 0.45, 0.20}; s.belt = 0.08;
			s.clouds = {cloudLayer(7500, 0.42, 20000, 2400, 78, {1.0, 0.52, 0.58}, {0.07, 0.06, 0.13}, 21)};
			break;
		case Twilight:
			s.turbidity = 3.5; s.afterglow = 0.25; s.afterglowColour = {0.9, 0.45, 0.25}; s.tint = {0.9, 0.95, 1.1};
			s.clouds = {cloudLayer(7500, 0.40, 20000, 2400, 78, {0.35, 0.30, 0.45}, {0.04, 0.04, 0.08}, 23)};
			break;
		case BlueHour:
			s.turbidity = 3.0; s.afterglow = 0.40; s.afterglowColour = {1.0, 0.55, 0.20}; s.night = 0.2; s.stars = 0.25;
			s.clouds = {cloudLayer(6000, 0.38, 12000, 2000, 60, {0.28, 0.27, 0.45}, {0.03, 0.03, 0.07}, 24)};
			break;
		case Night:
			s.night = 1.0; s.milkyWay = 1.6; s.stars = 1.0; s.nightBrightness = 1.7;
			break;
		case LastDawn:
			s.turbidity = 3.0; s.night = 0.35; s.stars = 0.3; s.afterglow = 0.45; s.afterglowColour = {1.0, 0.45, 0.25}; s.belt = 0.08;
			s.clouds = {cloudLayer(7000, 0.25, 16000, 2500, 60, {1.2, 0.5, 0.5}, {0.06, 0.05, 0.12}, 25)};
			break;
	}
	return s;
}
inline std::string skyText(int k, double scale, double turn = 0.0) { return film_sky::light(kFilmKey, skySpec(k), scale, turn); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The lights of the evening and the night: the lanterns (lit, or `out` of them put out), the halls glowing through their lattices
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string lanternLights(double strength, int out = 0, bool cast = true) {   // (`cast` false: they glow, but light nothing -- seen from inside the hall)
	if (strength <= 0.0) return std::string();
	std::string s;
	int k = 0;
	for (const Vec3 &top : lanternPlaces()) {
		if (k++ < out) continue;
		const Vec3 c = top - Vec3{0.0, 1.05, 0.0};
		s += emissiveSphere(c, 0.30, Vec3{1.0, 0.45, 0.18} * (6.0 * strength));
		if (!cast) continue;
		char buf[200];
		const Vec3 I = Vec3{1.0, 0.55, 0.25} * (18.0 * strength);
		std::snprintf(buf, sizeof buf, "LightSource \"point\" \"point3 from\" [ %.3f %.3f %.3f ] \"rgb I\" [ %.3f %.3f %.3f ]\n", -c.x, c.y - 0.7, c.z, I.x, I.y, I.z);
		s += buf;
	}
	return s;
}
// The halls lit from within (lamps behind the lattice of every tier, seen from outside).
inline std::string hallGlow(double strength) {
	if (strength <= 0.0) return std::string();
	std::string s;
	for (const double y : {kUpperTerrace + 3.0, kDeck2 + 3.5, kDeck3 + 3.0})
		for (const double x : {-9.0, 0.0, 9.0}) {
			char buf[200];
			const Vec3 I = Vec3{1.0, 0.62, 0.32} * (90.0 * strength);
			std::snprintf(buf, sizeof buf, "LightSource \"point\" \"point3 from\" [ %.3f %.3f 0 ] \"rgb I\" [ %.3f %.3f %.3f ]\n", -x, y, I.x, I.y, I.z);
			s += buf;
		}
	return s;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The shots
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// What a moment of a shot needs: the camera, the light, the look, and (built only for the scene) everything in it.
struct Frame {
	CameraView view{{0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, 40.0, 0.0, 0};
	int sky = Golden;
	double skyScale = 0.55, skyTurn = 0.0;
	bool sun = true;
	double sunStrength = 1.0;
	double iso = 150.0;
	double bloom = 0.16, vignette = 0.32, warmth = 0.0, saturation = 1.15;
	bool mist = true;
	nature::Mist haze;
	int depth = 6;
	std::string skyOverride;   // a light of its own instead of the day's sky (the dreams)
	std::string content;
};
inline nature::Mist hazeAround(Vec3 eye, double density, double radius = 40000.0, Vec3 tint = {0.85, 0.95, 1.15}) {
	nature::Mist m;
	m.centre = eye;
	m.radius = radius;
	m.density = density;
	m.tint = tint;
	m.g = 0.7;
	m.absorption = density * 0.3;
	return m;
}
inline double ease(double t0, double t1, double t) { return smoothstep(t0, t1, t); }
inline Vec3 glide(double t0, double t1, double t, Vec3 a, Vec3 b) { return lerp(a, b, ease(t0, t1, t)); }
inline Vec3 glideLinear(double t0, double t1, double t, Vec3 a, Vec3 b) { return lerp(a, b, clamp01((t - t0) / (t1 - t0))); }
inline CameraView cam(Vec3 eye, Vec3 target, double fov, double lens = 0.0) { return CameraView{eye, target, fov, lens, 0}; }
// The world round `eye`: the land, the fields, the river, the Pavilion, the city and the villages, the trees near the camera.
inline std::string worldText(Vec3 eye, double t, double treeRange = 3200.0, bool city = true, double wind = 0.4, bool fields = true) {
	std::string out = landText() + (fields ? fieldsText() : std::string()) + riverText() + pavilionText();
	if (city) out += cityText();
	out += treesText(eye, treeRange, t, wind);
	return out;
}
// Wang Bo standing (a pose) at `at`, facing `yaw` degrees (0 faces +z).
inline std::string wangBoAt(const Pose &p, Vec3 at, double yaw, double cell = 0.016) { return wangBoText(p, mul(translate(at), rotate({0, 1, 0}, yaw)), cell); }
inline Pose standPose(Vec3 wind = {0.0, 0.0, 0.0}) {
	Pose p;
	p.handL = {-0.20, 0.90, 0.10};
	p.handR = {0.20, 0.90, 0.10};
	p.wind = wind;
	return p;
}

struct ShotDef {
	double start;
	const char *name;
	std::function<void(double, Frame &, bool)> fill;   // (film time, the frame, whether to build the content)
};

// Scene 1 -------------------------------------------------------------------------------------------------------------------------------------------------------
inline void shotClouds(double t, Frame &f, bool content) {
	// the sea of clouds before sunrise: black, a bell, a line of gold, the light coming up; the camera floating forwards and beginning to sink
	const Vec3 eye = glide(0.0, 20.0, t, {-9200.0, 1650.0, 2600.0}, {-11800.0, 1320.0, 1200.0});
	const Vec3 target = glide(0.0, 20.0, t, {-15000.0, 1250.0, -2600.0}, {-17500.0, 900.0, -3200.0});
	f.view = cam(eye, target, 46.0);
	f.sky = Sunrise;
	f.skyScale = 1.0;
	f.sunStrength = 0.8;
	f.iso = 150.0 * smoothstep(3.0, 9.0, t);   // (out of black)
	f.haze = hazeAround(eye, 0.000022, 40000.0, {0.85, 0.92, 1.15});
	f.iso = 210.0 * smoothstep(3.0, 9.0, t);
	f.bloom = 0.24;
	f.warmth = 0.10;
	if (content) f.content = peaksText() + cloudSurfaceText();
}
inline void shotRiverEmerges(double t, Frame &f, bool content) {
	// the river coming out of the mountains: settlements, roads, boats; the camera following it downstream at a few hundred metres
	// (over the water, between the mountains: the river's own line, high above it)
	const double x = lerp(-8400.0, -6300.0, clamp01((t - 20.0) / 8.0));
	const Vec3 eye{x, 360.0, riverMiddle(x) + 40.0};
	f.view = cam(eye, {x + 1600.0, 60.0, riverMiddle(x + 1600.0)}, 48.0);
	f.sky = Morning;
	f.iso = 160.0;
	f.haze = hazeAround(eye, 0.00002);
	if (content) {
		f.content = worldText(eye, t, 2500.0, false);
		for (int k = 0; k < 4; ++k) f.content += boatText(k % 2 == 0, {-6400.0 + 500.0 * k + 6.0 * t, kWater, riverMiddle(-6400.0 + 500.0 * k) + 80.0 * (k % 2 ? 1 : -1)}, 90.0, 0.0, k);
	}
}
inline void shotRiverExpands(double t, Frame &f, bool content) {
	// the Gan widening across the plain, Hongzhou in the distance: walls, roofs, morning smoke, boats
	const Vec3 eye = glide(28.0, 37.0, t, {-4200.0, 760.0, -900.0}, {-3600.0, 640.0, -500.0});
	f.view = cam(eye, {600.0, 20.0, 700.0}, 40.0);
	f.sky = Morning;
	f.iso = 160.0;
	f.haze = hazeAround(eye, 0.000022);
	if (content) {
		f.content = worldText(eye, t, 2000.0);
		for (int k = 0; k < 9; ++k) {
			const double x = -2500.0 + 520.0 * k + 4.0 * t;
			f.content += boatText(k % 3 == 0, {x, kWater, riverMiddle(x) + 220.0 * (hash01(k, 201) - 0.5)}, k % 2 ? 90.0 : -90.0, 0.0, k);
		}
	}
}
inline void shotReveal(double t, Frame &f, bool content) {
	// behind the city's west wall: for a moment nothing; the camera rises over the wall and the Pavilion is there, its roofs above the riverbank and its red columns glowing in the
	// morning sun behind the camera, the river and the West Hills beyond it
	const double rise = smoothstep(37.0, 40.5, t), forward = smoothstep(39.5, 45.0, t);
	const Vec3 eye{lerp(60.0, 36.0, forward), lerp(5.0, 21.0, rise) + 5.0 * forward, lerp(166.0, 105.0, forward)};
	f.view = cam(eye, {lerp(40.0, 0.0, rise), lerp(12.0, 26.0, rise), lerp(100.0, 0.0, rise)}, 40.0);
	f.sky = Morning;
	f.iso = 170.0;
	f.bloom = 0.2;
	f.haze = hazeAround(eye, 0.00003);
	if (content) f.content = worldText(eye, t);
}

// Scene 2 -------------------------------------------------------------------------------------------------------------------------------------------------------
// The procession: riders, a carriage, officials on foot and banners, going in at the west gate (towards +z) along the road from the river.
inline std::string processionText(double t, double t0) {
	std::string out;
	// (it is at the gate's road between z0 and the gate, coming nearer the gate as the shot goes on)
	const double walk = 1.3 * (t - t0), z0 = 62.0;
	std::vector<PersonPlace> walkers;
	for (int k = 0; k < 14; ++k) {
		const double z = z0 + walk + 3.2 * (k / 2) - 0.9 * (k % 2);
		if (z > kCityZ0 + 6.0) continue;
		walkers.push_back({Walking, (k * 3 + 1) % 10, false, {kGateX + (k % 2 ? 1.5 : -1.5), kGround, z}, 0.0});
	}
	out += crowdText(walkers);
	static const Vec3 coats[] = {{0.35, 0.22, 0.12}, {0.82, 0.78, 0.72}, {0.12, 0.10, 0.09}, {0.55, 0.40, 0.25}};
	for (int k = 0; k < 4; ++k) {
		const double z = z0 + 26.0 + walk + 7.0 * k;
		if (z > kCityZ0 + 10.0) continue;
		const Vec3 at{kGateX + (k % 2 ? 2.2 : -2.2), kGround, z};
		out += horseText(at, 0.0, (t - t0) * 5.0 + k, 0.8, coats[k], robeColours()[static_cast<std::size_t>(k * 2) % robeColours().size()]);
		Pose rider = personPose(Kneeling);
		rider.kneel = true;
		Dress d;
		d.robe = robeColours()[static_cast<std::size_t>(k * 3 + 2) % robeColours().size()];
		static std::map<int, FigureMeshes> riders;
		if (!riders.count(k)) riders.emplace(k, figureMeshes(rider, d, 0.03));
		out += figureText(riders.at(k), d, mul(translate(at + Vec3{0.0, 1.25, 0.0}), scale(1.0)));
	}
	const double cz = z0 - 9.0 + walk;
	if (cz < kCityZ0 + 10.0) out += carriageText({kGateX, kGround, cz}, 0.0, (t - t0) * 1.3 / 0.75, {0.50, 0.08, 0.06}) + horseText({kGateX, kGround, cz + 3.8}, 0.0, (t - t0) * 5.0, 0.8, {0.20, 0.14, 0.10}, {0.70, 0.55, 0.12});
	static const Vec3 bannerColours[] = {{0.75, 0.08, 0.06}, {0.92, 0.62, 0.08}, {0.10, 0.30, 0.65}};
	for (int k = 0; k < 6; ++k) {
		const double z = z0 + 2.0 + walk + 6.0 * k;
		if (z > kCityZ0 + 6.0) continue;
		out += bannerText({kGateX + (k % 2 ? 3.6 : -3.6), kGround, z}, 5.0, bannerColours[k % 3], t, 0.7, 90.0);
	}
	return out;
}
inline void shotGate(double t, Frame &f, bool content) {
	const Vec3 eye = glide(45.0, 53.0, t, {kGateX - 24.0, 3.0, 34.0}, {kGateX - 17.0, 3.4, 56.0});
	f.view = cam(eye, {kGateX + 2.0, 5.0, 112.0}, 44.0);
	f.sky = Afternoon;
	f.iso = 150.0;
	f.haze = hazeAround(eye, 0.00003);
	if (content) f.content = worldText(eye, t, 1500.0) + processionText(t, 45.0);
}
// The montage of the feast's things: a hoof on the stones, a wheel, jade, scrolls carried, ink stones, a wine jar opened, incense, strings tuned (two seconds each).
inline void shotMontageFeast(double t, Frame &f, bool content) {
	const int k = (std::min)(5, static_cast<int>((t - 53.0) / 2.0));
	const double u = (t - 53.0 - 2.0 * k) / 2.0;
	f.sky = Afternoon;
	f.iso = 170.0;
	f.mist = false;
	f.bloom = 0.18;
	const Vec3 here{kGateX + 30.0, kGround, 60.0};
	switch (k) {
		case 0: {   // a hoof striking the paving
			f.view = cam(here + Vec3{1.3, 0.32, 0.2}, here + Vec3{-0.1, 0.18, -0.55 + 0.6 * u}, 30.0, 0.012);
			if (content) f.content = landText() + horseText(here + Vec3{-0.16, 0.0, -1.2 + 0.6 * u}, 0.0, u * 5.0, 1.0, {0.35, 0.22, 0.12}, {0.6, 0.1, 0.1}) + pavilionText();
			break;
		}
		case 1: {   // a carriage wheel turning
			f.view = cam(here + Vec3{2.6, 0.9, 0.5}, here + Vec3{0.85, 0.75, 0.2}, 34.0, 0.01);
			if (content) f.content = landText() + carriageText(here + Vec3{0.0, 0.0, -1.0 + 1.2 * u}, 0.0, u * 1.5, {0.50, 0.08, 0.06});
			break;
		}
		case 2: {   // a jade pendant swinging at a sash, the robe behind it
			const Vec3 c = here + Vec3{0.0, 1.0, 0.0};
			f.view = cam(c + Vec3{0.0, -0.05, -0.55}, c + Vec3{0.0, -0.18, 0.0}, 26.0, 0.008);
			if (content) {
				f.content = landText() + jadeText(c + Vec3{0.05, 0.0, -0.17}, 14.0 * std::sin(u * 7.0));
				Dress d;
				d.robe = robeColours()[0];
				f.content += figureText(figureMeshes(walkingPose(u * 2.0), d, 0.02), d, mul(translate(here), rotate({0, 1, 0}, 180.0)));
			}
			break;
		}
		case 3: {   // hands carrying bamboo scrolls and ink stones up the stair
			const double z = -26.0 + 1.6 * u, y = kLowerTerrace + (z + 28.3) / 9.0 * (kUpperTerrace - kLowerTerrace);   // (on the upper flight of the river stairs)
			const Vec3 c{0.0, y + 1.1, z};
			f.view = cam(c + Vec3{-0.9, 0.35, -0.9}, c, 30.0, 0.01);
			if (content) f.content = landText() + pavilionText() + bambooScrollText(mul(translate(c), rotate({1, 0, 0}, 70.0))) + bambooScrollText(mul(translate(c + Vec3{0.25, -0.05, 0.05}), rotate({1, 0, 0}, 75.0)));
			break;
		}
		case 4: {   // a wine jar opened
			const Vec3 c{6.0, kDeck2, 4.8};
			f.view = cam(c + Vec3{-0.9, 0.7, -0.8}, c + Vec3{0.0, 0.45, 0.0}, 32.0, 0.01);
			if (content) f.content = pavilionText() + hallText(t) + wineJarText(c, smoothstep(0.2, 0.8, u));
			break;
		}
		default: {   // incense burning, and a musician's fingers tuning the strings of a pipa
			const Vec3 c{9.0, kDeck2 + 0.36, 0.3};
			f.view = cam(c + Vec3{-0.75, 0.35, -0.5}, c + Vec3{0.0, 0.3, 0.1}, 30.0, 0.012);
			if (content) f.content = pavilionText() + hallText(t) + incenseText(c, t) + pipaText(mul(translate(c + Vec3{0.25, 0.45, 0.35}), rotate({0, 1, 0}, -30.0), rotate({0, 0, 1}, 25.0)));
			break;
		}
	}
}
inline void hallFrame(Frame &f, Vec3 eye, Vec3 target, double fov, int sky, double iso) {
	f.view = cam(eye, target, fov, 0.008);
	f.sky = sky;
	f.iso = iso;
	f.mist = false;
	f.bloom = 0.22;
	f.warmth = 0.12;
	f.depth = 6;
}
inline void shotHallPrepared(double t, Frame &f, bool content) {
	// (by day the hall is lit by the open side and its lamps)
	// the hall made ready: lamps lit, bronze, silk, the polished floor, the open side over the river
	hallFrame(f, glide(65.0, 75.0, t, {12.5, kDeck2 + 2.4, -6.8}, {-6.0, kDeck2 + 1.8, -6.0}), glide(65.0, 75.0, t, {0.0, kDeck2 + 1.0, 2.0}, {-14.0, kDeck2 + 1.4, -2.0}), 46.0, Afternoon, 420.0);
	if (content) {
		f.content = worldText(f.view.eye, t, 2500.0) + hallText(t);
		std::vector<PersonPlace> servants;
		for (int k = 0; k < 5; ++k) servants.push_back({Carrying, 8, k % 2 == 1, {6.0 - 4.0 * k + 0.6 * (t - 65.0) * (k % 2 ? 1.0 : -1.0), kDeck2, k % 2 ? 1.0 : -1.0}, k % 2 ? 90.0 : -90.0});
		f.content += crowdText(servants);
	}
}
inline std::vector<PersonPlace> arrivingGuests(double t) {
	std::vector<PersonPlace> v;
	for (int k = 0; k < 22; ++k) {
		const double x = 10.0 - 1.1 * k + (k % 3 == 0 ? 0.4 * (t - 75.0) : 0.0);
		const double z = (k % 2 ? 1.4 : -1.6) + 0.6 * (hash01(k, 211) - 0.5);
		const int pose = k % 5 == 0 ? Bowing : (k % 3 == 0 ? Walking : Clasping);
		v.push_back({pose, k % 10, k % 7 == 3, {x, kDeck2, z}, k % 2 ? 200.0 + 40.0 * hash01(k, 212) : -20.0 + 40.0 * hash01(k, 213)});
	}
	return v;
}
inline void shotGuests(double t, Frame &f, bool content) {
	// the guests arriving: the camera drifts through them without a cut; greetings, bows, wine poured, the musicians beginning
	hallFrame(f, glide(75.0, 85.0, t, {12.8, kDeck2 + 1.65, 2.6}, {-2.0, kDeck2 + 1.6, 0.6}), glide(75.0, 85.0, t, {4.0, kDeck2 + 1.4, 0.4}, {-10.0, kDeck2 + 1.4, -1.0}), 50.0, Afternoon, 420.0);
	if (content) {
		f.content = worldText(f.view.eye, t, 2500.0) + hallText(t) + crowdText(arrivingGuests(t));
		std::vector<PersonPlace> music;
		for (int m = 0; m < 4; ++m) music.push_back({Kneeling, (m * 2 + 1) % 10, true, {12.6 - 0.95 * m, kDeck2, 3.2}, 200.0});
		f.content += crowdText(music);
	}
}
inline void shotTraveller(double t, Frame &f, bool content) {
	// at the edge of the crowd, a young traveller, dust on his robe, younger than everyone, watching: not introduced as a hero, small among the powerful
	hallFrame(f, glide(85.0, 95.0, t, {-1.0, kDeck2 + 1.7, 1.2}, {-4.5, kDeck2 + 1.65, 0.2}), {-12.0, kDeck2 + 1.4, -4.6}, 38.0, Afternoon, 420.0);
	f.view.lensRadius = 0.02;
	if (content) {
		Pose p = standPose();
		p.headTurn = -12.0 + 4.0 * std::sin(t * 0.4);
		f.content = worldText(f.view.eye, t, 2500.0) + hallText(t) + crowdText(arrivingGuests(t)) + wangBoAt(p, {-12.2, kDeck2, -4.8}, 75.0);
	}
}

// Scene 3 -------------------------------------------------------------------------------------------------------------------------------------------------------
inline void shotToStair(double t, Frame &f, bool content) {
	// he walks towards the stair; the camera follows behind
	const double x = lerp(-6.0, kStairX0 - 0.6, clamp01((t - 95.0) / 6.0));
	const Vec3 at{x, kDeck2, kStairZ0 + 0.1 - 1.4 * (1.0 - clamp01((t - 95.0) / 6.0))};
	hallFrame(f, at + Vec3{-3.2, 1.9, -1.2}, at + Vec3{1.5, 1.3, 0.4}, 42.0, Golden, 420.0);
	if (content) {
		Pose p = walkingPose(t);
		f.content = worldText(f.view.eye, t, 2500.0) + hallText(t) + crowdText(arrivingGuests(85.0)) + wangBoAt(p, at, 80.0);
	}
}
inline void shotClimb(double t, Frame &f, bool content) {
	// up the stair: the camera below and behind him, the windows of the hall and then the open sky beyond
	const double u = clamp01((t - 101.0) / 12.0);
	const double x = lerp(kStairX0 - 0.3, kHoleX1 + 1.2, u);   // (the shot ends as he comes to the opening; the next finds him at the top)
	const Vec3 at{x, stairHeightAt(x), (kStairZ0 + kStairZ1) / 2.0};
	// (from the hall floor below him and to one side, rising a little as he climbs: his figure going up the steep red stair, the lattice glowing beyond, and at last the
	// opening above him)
	const Vec3 eye{lerp(kStairX0 + 1.5, kStairX1 + 6.0, u), kDeck2 + 1.3 + 1.6 * u, kStairZ0 - 2.2};
	hallFrame(f, eye, at + Vec3{0.0, 1.1, 0.0}, 46.0, Golden, 420.0);
	if (content) {
		Pose p = walkingPose(t, 1.3);
		p.lean = 12.0;
		f.content = worldText(f.view.eye, t, 2500.0) + hallText(t) + wangBoAt(p, at, -90.0);
	}
}
// Wang Bo at the high terrace's railing on the river side, looking out (at x).
constexpr double kTerraceZ = -kRail3Z + 0.7;
inline Vec3 terraceSpot(double x = -2.0) { return {x, kDeck3, kTerraceZ}; }
inline void shotTerrace(double t, Frame &f, bool content) {
	// he steps out on the highest terrace: the wind takes his robe, the feast's noise falls away
	const double u = clamp01((t - 113.0) / 6.5);
	const Vec3 at = lerp(Vec3{-1.0, kDeck3, -5.4}, terraceSpot(-1.0), u);
	f.view = cam(at + Vec3{0.7, 1.75, 2.6}, at + Vec3{-0.6, 1.3, -6.0}, 50.0);
	f.sky = Golden;
	f.iso = 120.0;
	f.haze = hazeAround(f.view.eye, 0.00003);
	if (content) {
		Pose p = u < 1.0 ? walkingPose(t) : standPose();
		p.wind = {0.5 * u, 0.2, -0.6 * u};
		f.content = worldText(f.view.eye, t, 3000.0) + wangBoAt(p, at, 180.0 + 20.0 * (1.0 - u));
	}
}
inline void shotLookDown(double t, Frame &f, bool content) {
	// what he sees: the city below him, then the river, the plain, the hills, the clouds
	const Vec3 eye = terraceSpot(-4.5) + Vec3{0.0, 1.62, 0.2};
	f.view = cam(eye, glide(121.0, 127.0, t, eye + Vec3{-15.0, -40.0, -48.0}, eye + Vec3{-300.0, -110.0, -1400.0}), 56.0);
	f.sky = Golden;
	f.iso = 105.0;
	f.haze = hazeAround(eye, 0.00003);
	if (content) {
		f.content = worldText(eye, t, 3200.0);
		for (int k = 0; k < 5; ++k) f.content += boatText(k % 2 == 0, {-300.0 + 160.0 * k + 3.0 * t, kWater, -220.0 - 90.0 * k}, 90.0 + 25.0 * k, 0.0, k + 20);
	}
}
inline void shotCircle(double t, Frame &f, bool content) {
	// the camera circles him slowly; past his shoulder the whole land opens
	// (he stands midway between two columns; the camera goes round him close, from in front of him -- over the railing, his face and the Pavilion behind him -- past his right
	// side to behind his shoulder, where the view is his: the whole land)
	const Vec3 at = terraceSpot(-1.775);
	const double phi = lerp(25.0, 165.0, ease(127.0, 140.0, t)) * kPi / 180.0, R = 1.2;
	const Vec3 eye = at + Vec3{R * std::sin(phi), 1.70, -R * std::cos(phi)};
	const double behind = smoothstep(115.0, 165.0, phi * 180.0 / kPi);
	// (the aim turns from his face to the land as a direction, so he stays in the frame until the camera is behind him)
	const Vec3 toFace = normalize(at + Vec3{0.0, 1.55, 0.0} - eye), toLand = normalize(Vec3{-60.0, -20.0, -400.0});
	f.view = cam(eye, eye + normalize(lerp(toFace, toLand, behind)), lerp(40.0, 52.0, behind));
	f.sky = Golden;
	f.iso = lerp(160.0, 100.0, behind);   // (behind him the camera looks into the low sun)
	f.haze = hazeAround(f.view.eye, 0.00003);
	if (content) f.content = worldText(f.view.eye, t, 3200.0) + wangBoAt(standPose({0.4, 0.2, -0.5}), at, 180.0);
}

// Scene 4 -------------------------------------------------------------------------------------------------------------------------------------------------------
inline void shotMarket(double t, Frame &f, bool content) {
	const Vec3 eye = glide(140.0, 147.0, t, {kGateX - 1.5, 1.7, kMarketZ0 + 8.0}, {kGateX + 0.5, 1.75, kMarketZ0 + 30.0});
	f.view = cam(eye, eye + Vec3{0.6, -0.1, 12.0}, 50.0, 0.01);
	f.sky = Golden;
	f.iso = 170.0;
	f.haze = hazeAround(eye, 0.00008, 3000.0);
	if (content) {
		f.content = worldText(eye, t, 900.0);
		std::vector<PersonPlace> crowd;
		for (int k = 0; k < 46; ++k) {
			const double z = kMarketZ0 + 6.0 + 2.2 * k + (k % 3 == 1 ? 1.2 * (t - 140.0) : 0.0);
			crowd.push_back({k % 3 == 1 ? Walking : (k % 4 == 0 ? Carrying : Standing), (k * 7) % 10, k % 5 == 2, {kGateX + 4.5 * (hash01(k, 221) - 0.5) + (k % 2 ? 2.8 : -2.8) * (k % 4 == 0), kGround, z}, k % 3 == 1 ? 0.0 : 360.0 * hash01(k, 222)});
		}
		f.content += crowdText(crowd);
		// children running down the lane
		for (int c = 0; c < 2; ++c) {
			Dress d;
			d.robe = robeColours()[static_cast<std::size_t>(c + 4)];
			d.height = 1.1;
			d.hat = false;
			f.content += figureText(figureMeshes(walkingPose(t * 1.8 + c, 2.0), d, 0.02), d, translate(kGateX + 1.0 - 2.0 * c, kGround, kMarketZ0 + 18.0 + 3.5 * (t - 140.0) + 3.0 * c));
		}
	}
}
inline void shotBoatsBelow(double t, Frame &f, bool content) {
	// fishing boats crossing below the Pavilion: low on the water, the boats sliding past close, the Pavilion towering behind them in the golden light
	const Vec3 eye = glide(147.0, 153.0, t, {-44.0, kWater + 2.2, -112.0}, {-36.0, kWater + 2.6, -116.0});
	f.view = cam(eye, {0.0, 18.0, -4.0}, 48.0);
	f.sky = Golden;
	f.iso = 150.0;
	f.haze = hazeAround(eye, 0.00002);
	if (content) {
		f.content = worldText(eye, t, 2500.0);
		for (int k = 0; k < 3; ++k) f.content += boatText(false, {-58.0 + 20.0 * k + 2.4 * (t - 147.0), kWater, -88.0 - 6.0 * k}, 90.0 - 6.0 * k, 1.2 * std::sin(t * 0.9 + k), k + 30);
	}
}
inline void shotShepherd(double t, Frame &f, bool content) {
	// a shepherd driving his sheep along the far bank towards the camera, the river gold behind them
	const double x0 = 600.0, zb = riverMiddle(x0) - riverHalfWidth(x0) - 30.0;
	const double walk = 1.1 * (t - 153.0);
	const Vec3 eye{x0 + 9.0, groundHeight(x0 + 9.0, zb - 6.0) + 0.9, zb - 6.0};
	f.view = cam(eye, {x0 - 6.0 + walk, groundHeight(x0, zb) + 0.6, zb + 3.0}, 42.0, 0.02);
	f.sky = Golden;
	f.iso = 160.0;
	f.haze = hazeAround(eye, 0.00003);
	if (content) {
		f.content = worldText(eye, t, 1500.0);
		for (int k = 0; k < 18; ++k) {
			const double x = x0 - 12.0 + walk + 2.2 * (k % 6) + 0.7 * hash01(k, 231), z = zb + 3.0 * (k / 6) + 1.2 * hash01(k, 232);
			f.content += sheepText({x, groundHeight(x, z), z}, 90.0 + 10.0 * (hash01(k, 233) - 0.5), t * 6.0 + k);
		}
		Dress d;
		d.robe = {0.45, 0.38, 0.26};
		d.hat = false;
		const double sx = x0 - 18.0 + walk;
		f.content += figureText(figureMeshes(walkingPose(t), d, 0.02), d, mul(translate(sx, groundHeight(sx, zb + 2.0), zb + 2.0), rotate({0, 1, 0}, 90.0)));
		f.content += attr(barkMaterial(), bar({sx + 0.3, groundHeight(sx, zb + 2.0) + 1.6, zb + 2.3}, {sx + 0.6, groundHeight(sx, zb + 2.0), zb + 2.6}, 0.02, 5));
	}
}
inline void shotEgrets(double t, Frame &f, bool content) {
	// egrets rising from the reeds of the wetland downstream, close, white against the clouds
	const Vec3 reeds{1800.0, kWater, nearBank(1800.0) - 15.0};
	// (from the river side, the low sun behind the camera: the birds white against the dark bank, then against the blue of the eastern sky)
	const Vec3 eye = reeds + Vec3{-8.0, 1.8, -24.0};
	f.view = cam(eye, reeds + Vec3{0.0, 3.0 + 2.4 * (t - 159.0), 2.0}, 46.0);
	f.sky = LateGolden;
	f.iso = 130.0;
	f.haze = hazeAround(eye, 0.00002);
	if (content) {
		f.content = worldText(eye, t, 1500.0) + reedsText(reeds, 22.0, 140, t);
		for (int k = 0; k < 14; ++k) {
			const double start = 159.0 + 0.3 * k, age = (std::max)(0.0, t - start);
			const Vec3 at = reeds + Vec3{-10.0 + 20.0 * hash01(k, 241), 0.8 + age * (1.6 + 0.7 * hash01(k, 242)), -8.0 + 12.0 * hash01(k, 243) - age * 2.0};
			f.content += egretText(at, {0.4, 0.5, -1.0}, t, k);
		}
	}
}
inline void shotBoatsHome(double t, Frame &f, bool content) {
	// the fishing junks coming home, their sails catching the evening wind, dark against the low sun on the water
	const Vec3 eye{-200.0, 5.0, -120.0};
	const Vec3 dir = normalize(Vec3{-450.0, 2.0, -730.0}), across = normalize(Vec3{-dir.z, 0.0, dir.x});
	f.view = cam(eye, eye + dir * 100.0 + Vec3{0.0, 6.0, 0.0}, 40.0);
	f.sky = LateGolden;
	f.iso = 95.0;
	f.bloom = 0.24;
	f.haze = hazeAround(eye, 0.00002);
	if (content) {
		f.content = worldText(eye, t, 2500.0);
		for (int k = 0; k < 5; ++k) {
			const double d = 170.0 + 70.0 * k - 5.0 * (t - 165.0);
			const Vec3 at = eye + dir * d + across * (-110.0 + 55.0 * k + 15.0 * hash01(k, 251));
			f.content += boatText(true, {at.x, kWater, at.z}, 30.0 + 8.0 * k, 2.0 * std::sin(t * 0.8 + k), k + 40);
		}
	}
}
inline void shotGeese(double t, Frame &f, bool content) {
	// skeins of geese crossing the sky towards the south, their calls echoing between the hills (looking away from the sun, the geese against the lit clouds)
	const Vec3 eye{-60.0, kDeck3 + 1.7, -14.0};
	f.view = cam(eye, eye + Vec3{300.0, 160.0, -500.0}, 44.0);
	f.sky = LateGolden;
	f.iso = 140.0;
	f.haze = hazeAround(eye, 0.00002);
	if (content) {
		f.content = worldText(eye, t, 2000.0);
		for (int s = 0; s < 3; ++s) {
			const Vec3 lead = eye + Vec3{75.0 - 18.0 * s - 7.0 * (t - 174.0), 32.0 + 9.0 * s, -110.0 - 30.0 * s};
			f.content += skeinText(lead, {-1.0, 0.0, 0.25}, 9 + 2 * s, t + s, 3.0);
		}
	}
}

// Scene 5 -------------------------------------------------------------------------------------------------------------------------------------------------------
inline void shotLeaf(double t, Frame &f, bool content) {
	// a yellow ginkgo leaf lets go of its branch over the railing and falls; the camera follows it down past the railing, looking out, and the river opens below
	const Vec3 branch{-6.0, kDeck3 + 3.4, -kRail3Z - 1.6};
	const double u = clamp01((t - 187.0) / 9.0);
	const Vec3 leaf = branch + Vec3{0.9, -0.12, 0.0} + Vec3{0.6 * std::sin(u * 9.0), -14.0 * u * u - 2.0 * u, -7.0 * u};
	// (the camera stays just above the leaf, looking down past it, so the leaf stays near the middle of the picture as the river opens beyond it)
	const Vec3 eye = t < 187.0 ? branch + Vec3{0.9, 0.05, 0.45} : leaf + Vec3{0.1, 0.45 + 0.9 * u, 0.35 + 0.2 * u};
	const Vec3 target = t < 187.0 ? branch + Vec3{0.9, -0.1, 0.0} : leaf + Vec3{0.0, -0.6 - 6.0 * u, -0.4 - 9.0 * u};
	f.view = cam(eye, target, lerp(30.0, 52.0, u), 0.004 * (1.0 - u));
	f.sky = LateGolden;
	f.iso = 140.0;
	f.haze = hazeAround(eye, 0.00002);
	if (content) f.content = worldText(eye, t, 2500.0) + ginkgoBranchText(branch, normalize(Vec3{1.0, -0.1, 0.1}), t, true, t < 187.0 ? branch + Vec3{0.9, -0.12, 0.0} : leaf, 200.0 * u);
}
inline void shotSunGoesDown(double t, Frame &f, bool content) {
	// the sun coming down to the West Hills, the clouds turning orange, crimson, violet and gold, the river holding the whole sky
	const Vec3 eye{-30.0, kDeck3 + 2.0, -18.0};
	f.view = cam(eye, glide(196.0, 210.0, t, eye + Vec3{-500.0, 120.0, -2000.0}, eye + Vec3{-700.0, 40.0, -2000.0}), 52.0);
	f.sky = Sunset;
	f.skyScale = 0.62;
	f.skyTurn = 0.05 * (t - 196.0);
	f.iso = 85.0;
	f.bloom = 0.26;
	f.warmth = 0.12;
	f.saturation = 1.22;
	f.haze = hazeAround(eye, 0.000008, 40000.0, {1.0, 0.92, 0.88});
	if (content) f.content = worldText(eye, t, 2500.0);
}
// The solitary wild duck (fixed in the sky by the shot's camera, crossing the setting clouds).
inline void shotDuck(double t, Frame &f, bool content) {
	// one long, slow lateral track; the duck crosses the frame, then crosses the sun's reflection; the river and the sky one colour
	const Vec3 eye = glideLinear(210.0, 228.0, t, {-150.0, 9.0, -150.0}, {-60.0, 9.0, -160.0});
	f.view = cam(eye, eye + Vec3{-500.0, 30.0, -2000.0}, 40.0);
	f.sky = Sunset;
	f.skyScale = 0.62;
	f.iso = 85.0;
	f.bloom = 0.3;
	f.warmth = 0.14;
	f.saturation = 1.24;
	f.haze = hazeAround(eye, 0.000008, 40000.0, {1.0, 0.92, 0.88});
	if (content) {
		f.content = worldText(eye, t, 2500.0);
		const double u = clamp01((t - 210.5) / 16.0);
		// (near enough to read as a bird against the sky: 35 m off, gliding down across the clouds and then over the sun's reflection, which is along the sun's direction just
		// under the horizon, about halfway through)
		const Vec3 duck = eye + Vec3{-24.0 + 30.0 * u, 3.0 - 7.8 * std::pow(u, 1.2), -19.5};
		f.content += birdText(duck, {1.0, -0.22, 0.0}, std::sin(t * 2.0 * kPi * 2.2), 1.0, {0.04, 0.03, 0.03}, {0.05, 0.04, 0.035});
	}
}
inline void shotWaterLevel(double t, Frame &f, bool content) {
	// the camera down at the water: half river, half sky, hard to tell apart; a boat crosses and its wake breaks the sky
	const Vec3 eye = glideLinear(228.0, 245.0, t, {-120.0, kWater + 0.6, -260.0}, {-100.0, kWater + 0.45, -275.0});
	f.view = cam(eye, eye + Vec3{-500.0, 0.0, -2000.0}, 42.0);
	f.sky = SunsetLow;
	f.skyScale = 0.62;
	f.iso = 85.0;
	f.bloom = 0.3;
	f.warmth = 0.14;
	f.saturation = 1.24;
	f.haze = hazeAround(eye, 0.000008, 40000.0, {1.0, 0.92, 0.88});
	if (content) {
		f.content = worldText(eye, t, 2500.0);
		const double bx = -280.0 + 9.0 * (t - 228.0);
		f.content += boatText(false, {bx, kWater, -420.0}, 90.0, 0.8 * std::sin(t), 50);
		// the wake: a widening V of small ripples behind the boat, a strip of rippled water that breaks the reflection
		std::string wake;
		for (int k = 0; k < 120; ++k) {
			const double back = 1.0 + 0.7 * k, spread = 0.35 * back;
			for (const double side : {-1.0, 1.0}) {
				const Vec3 c{bx - back, kWater + 0.02, -420.0 + side * spread};
				wake += emitMesh(torus(0.35 + 0.004 * k, 0.05, 12, 4), mul(translate(c), scale(1.0, 0.25, 1.0)));
			}
		}
		f.content += attr("Material \"dielectric\" \"float eta\" [ 1.33 ]", wake);
	}
}

// Scene 6 -------------------------------------------------------------------------------------------------------------------------------------------------------
// The feast at dusk: amber lamplight inside, the blue of the evening outside the open side.
inline std::string feastText(double t, Vec3 eye, int wangBoColumns, const Pose *wangBoPose) {
	std::string out = worldText(eye, t, 1500.0) + hallText(t) + crowdText(hallGuests()) + lanternLights(1.0, 0, false);
	if (wangBoPose) out += wangBoAt(*wangBoPose, wangBoSeat(), 0.0, 0.012);
	if (wangBoColumns >= 0) out += paperText(tableBefore(hallSeats()[12]) + Vec3{0.0, 0.36, 0.0}, 0.0, wangBoColumns);
	return out;
}
inline void feastFrame(Frame &f, Vec3 eye, Vec3 target, double fov, double lens = 0.01) {
	hallFrame(f, eye, target, fov, Dusk, 170.0);
	f.view.lensRadius = lens;
	f.bloom = 0.26;
	f.warmth = 0.05;
}
inline Pose kneelPose() {
	Pose p = personPose(Kneeling);
	return p;
}
inline void shotWine(double t, Frame &f, bool content) {
	const Seat &s = hallSeats()[2];
	const Vec3 cup = tableBefore(s) + Vec3{0.25, 0.36, 0.0};
	feastFrame(f, cup + Vec3{-0.45, 0.22, -0.35}, cup + Vec3{0.0, 0.12, 0.0}, 30.0, 0.006);
	if (content) {
		const double u = clamp01((t - 245.0) / 5.0);
		f.content = feastText(t, f.view.eye, -1, nullptr) + cupText(cup, u) + ewerText(mul(translate(cup + Vec3{0.12, 0.30, 0.0}), rotate({0, 1, 0}, 180.0)), 55.0, cup + Vec3{0.0, 0.10, 0.0}, smoothstep(0.0, 0.15, u) * (1.0 - smoothstep(0.85, 1.0, u)));
	}
}
inline void shotPipa(double t, Frame &f, bool content) {
	// (the first musician kneels at `at` facing yaw -60: the pipa stands in her lap, its neck up to her left, its strings to us)
	const Vec3 at{9.6, kDeck2, 0.4};
	const double yaw = -60.0 * kPi / 180.0;
	const Vec3 facing{std::sin(yaw), 0.0, std::cos(yaw)}, side{std::cos(yaw), 0.0, -std::sin(yaw)};
	const Vec3 pipaAt = at + facing * 0.30 + Vec3{0.0, 0.60, 0.0};
	feastFrame(f, at + facing * 1.5 + side * 0.4 + Vec3{0.0, 0.85, 0.0}, pipaAt + Vec3{0.0, 0.15, 0.0}, 36.0, 0.012);
	if (content) {
		f.content = feastText(t, f.view.eye, -1, nullptr);
		f.content += pipaText(mul(translate(pipaAt), rotate({0, 1, 0}, -60.0), rotate({0, 0, 1}, 24.0 + 2.0 * std::sin(t * 3.0))));
	}
}
inline void shotPoems(double t, Frame &f, bool content) {
	const Vec3 a = hallSeats()[4].at, b = hallSeats()[5].at;
	feastFrame(f, (a + b) * 0.5 + Vec3{-2.4, 1.4, 0.0} + Vec3{0.0, 0.0, 0.2 * std::sin(t * 0.3)}, (a + b) * 0.5 + Vec3{0.0, 0.85, 0.0}, 40.0, 0.012);
	if (content) f.content = feastText(t, f.view.eye, -1, nullptr) + bambooScrollText(mul(translate(b + Vec3{0.0, 0.85, -0.35}), rotate({1, 0, 0}, 60.0)));
}
inline void shotPaperGoesRound(double t, Frame &f, bool content) {
	// a servant brings paper; the senior guest gestures to the scholars to write; the older ones decline; the paper comes at last to Wang Bo
	const double u = clamp01((t - 263.0) / 18.0);
	const Vec3 from = hallSeats()[0].at, to = wangBoSeat();
	const Vec3 servant = lerp(from, to, ease(0.0, 1.0, u)) + Vec3{0.0, 0.0, 0.9};
	feastFrame(f, servant + Vec3{1.8, 1.5, 2.4}, servant + Vec3{-1.5, 0.8, -1.0}, 44.0, 0.012);
	if (content) {
		Pose wb = kneelPose();
		f.content = feastText(t, f.view.eye, u > 0.95 ? 0 : -1, &wb);
		std::vector<PersonPlace> sv = {{Carrying, 8, false, servant, -90.0}};
		f.content += crowdText(sv);
		Mesh sheet = box(0.5, 0.004, 0.35);
		f.content += attr(diffuseMaterial({0.85, 0.80, 0.68}), emitMesh(sheet, translate(servant + Vec3{-0.36, 1.10, 0.0})));
	}
}
inline void shotBlankPaper(double t, Frame &f, bool content) {
	// close on Wang Bo and the blank paper; his breathing; he lifts the brush; ink touches the paper -- black
	const Vec3 table = tableBefore(hallSeats()[12]) + Vec3{0.0, 0.36, 0.0};
	const double u = clamp01((t - 281.0) / 13.0);
	feastFrame(f, glide(281.0, 294.0, t, table + Vec3{-0.9, 0.75, 1.3}, table + Vec3{-0.35, 0.42, 0.55}), glide(281.0, 294.0, t, wangBoSeat() + Vec3{0.0, 1.2, 0.0}, table + Vec3{0.05, 0.02, 0.0}), 34.0, 0.01);
	if (t > 294.5) f.iso = 0.0;
	if (content) {
		Pose p = kneelPose();
		p.headTilt = -22.0;
		const Vec3 brushTip = table + Vec3{0.06, 0.02 + 0.22 * (1.0 - ease(0.7, 0.98, u)), 0.06};
		p.handR = brushTip - wangBoSeat() + Vec3{0.0, 0.10, -0.05};
		f.content = feastText(t, f.view.eye, 0, &p) + brushText(brushTip, {0.08, 1.0, 0.1});
	}
}

// Scene 7 -------------------------------------------------------------------------------------------------------------------------------------------------------
// The ink world on paper: a camera looking straight down at a sheet with strokes spreading. `kind` 0 the river, 1 the ridge, 2 the bird, 3 the storm.
inline void inkPaperFrame(Frame &f, double t, double t0, int kind) {
	const double u = clamp01((t - t0) / 5.0);
	f.view = cam({0.0, 0.95 - 0.12 * u, -0.06}, {0.0, 0.0, 0.0}, 38.0);   // (the camera a little to -z of straight above: +z is up the picture)
	f.sky = Afternoon;
	f.skyOverride = inkStudioLight();
	f.sun = false;
	f.mist = false;
	f.iso = 100.0;
	f.saturation = 0.0;
	f.bloom = 0.0;
	f.vignette = 0.45;
	std::string c = inkPaperText();
	// (the paper fills the picture: about 1.4 m by 0.6 m of it, x across, z up the picture; the brush lays each stroke as `u` grows)
	switch (kind) {
		case 0: {   // the river seen from the sky: a pale wash of water between two dry bank lines, villages as dots beside it
			const auto bank = [](double s, double side) { return Vec3{lerp(-0.72, 0.72, s), 0.0, 0.14 * std::sin(s * 5.0 + 0.4) + side * (0.035 + 0.02 * std::sin(s * 7.0))}; };
			c += inkWash({-0.2, 0.0, 0.02}, 0.05 + 0.10 * u, 0.25, 308);
			c += inkStroke([&](double s) { return bank(s, 0.0); }, [&](double) { return 0.06 + 0.02 * u; }, (std::min)(1.0, 0.2 + u), 309, 0.0, 0.18, 0.0012);
			c += inkBrush([&](double s) { return bank(s, -1.0); }, [](double s) { return 0.006 + 0.005 * std::sin(kPi * s); }, (std::min)(1.0, u * 1.3), 310);
			c += inkBrush([&](double s) { return bank(s, 1.0); }, [](double s) { return 0.005 + 0.005 * std::sin(kPi * s); }, (std::min)(1.0, (std::max)(0.0, u * 1.3 - 0.2)), 320);
			for (int k = 0; k < 7; ++k)
				if (u > 0.3 + 0.08 * k) c += inkWash(bank(0.12 + 0.12 * k, (k % 2 ? 1.0 : -1.0) * 2.4), 0.008 + 0.004 * hash01(k, 331), 0.95, 332 + k);
			break;
		}
		case 1: {   // a ridge: one bold stroke rising and falling, washes under it, a paler ridge behind
			const auto ridge = [](double s) { return Vec3{lerp(-0.72, 0.72, s), 0.0, 0.08 * std::sin(s * 6.5) + 0.20 * std::exp(-std::pow((s - 0.58) / 0.09, 2.0)) + 0.10 * std::exp(-std::pow((s - 0.25) / 0.07, 2.0)) - 0.10}; };
			const auto behind = [](double s) { return Vec3{lerp(-0.72, 0.72, s), 0.0, 0.22 + 0.06 * std::sin(s * 4.7 + 1.0) + 0.05 * std::exp(-std::pow((s - 0.3) / 0.08, 2.0))}; };
			c += inkBrush(behind, [](double) { return 0.010; }, u, 340, 0.3);
			// (the mountain's body: washes under the ridge line, darkest at the ridge and fading down the slope)
			for (int k = 0; k < 9; ++k) c += inkStroke([&](double s) { return ridge(s) - Vec3{0.0, 0.0, 0.012 + 0.022 * k}; }, [&](double) { return 0.026; }, u, 341 + k, 0.0, 0.42 * std::pow(1.0 - k / 9.0, 1.5), 0.0011 + 0.00002 * (9 - k));
			c += inkBrush(ridge, [](double s) { return 0.010 + 0.010 * std::sin(kPi * s); }, u, 345);
			break;
		}
		case 2: {   // a single dot of ink, which opens into a bird: two curved strokes for its wings
			c += inkWash({0.05, 0.0, 0.05}, 0.012 + 0.006 * (std::min)(1.0, u * 3.0), 1.0, 350);
			const double w = smoothstep(0.35, 0.9, u);
			c += inkBrush([](double s) { return Vec3{0.05 - 0.09 * s, 0.0, 0.05 + 0.04 * std::sin(kPi * s * 0.9)}; }, [](double s) { return 0.005 * (1.0 - s); }, w, 351);
			c += inkBrush([](double s) { return Vec3{0.05 + 0.08 * s, 0.0, 0.05 + 0.035 * std::sin(kPi * s * 0.9)}; }, [](double s) { return 0.0045 * (1.0 - s); }, w, 352);
			break;
		}
		default: {   // wet ink washes blooming into a storm
			const double v = 0.3 + 0.7 * u;
			c += inkWash({0.0, 0.0, 0.05}, 0.12 + 0.22 * v, 0.55, 360) + inkWash({-0.25, 0.0, -0.05}, 0.08 + 0.18 * v, 0.75, 361);
			c += inkWash({0.28, 0.0, 0.10}, 0.06 + 0.20 * v, 0.85, 362) + inkWash({0.05, 0.0, -0.12}, 0.04 + 0.14 * v, 0.95, 363);
			c += inkBrush([](double s) { return Vec3{lerp(-0.6, 0.6, s), 0.0, -0.05 + 0.08 * std::sin(s * 4.0)}; }, [](double) { return 0.016; }, v, 364);
			break;
		}
	}
	f.content = c;
}
// The land as an ink painting: no colour, white mist, a paper sky (`colour` 0) turning back into the world (`colour` 1).
inline void inkLandFrame(Frame &f, double t, CameraView v, double colour) {
	f.view = v;
	f.sky = Dusk;
	f.sun = colour > 0.5;
	if (colour < 0.5) f.skyOverride = "LightSource \"infinite\" \"rgb L\" [ 0.9 0.9 0.88 ]\n";
	f.iso = lerp(150.0, 520.0, colour);
	f.saturation = colour;
	f.bloom = 0.1 + 0.15 * colour;
	f.haze = hazeAround(v.eye, lerp(0.00022, 0.00003, std::pow(colour, 0.5)), lerp(6000.0, 30000.0, colour), {1.0, 1.0, 1.0});
	f.content = worldText(v.eye, t, 2500.0, true, 0.2, colour > 0.5);   // (no field plots in the ink painting: the plain is one wash)
}
inline void shotInkRiver(double t, Frame &f, bool content) {
	if (t < 300.0) inkPaperFrame(f, t, 295.0, 0);
	else inkLandFrame(f, t, cam(glide(300.0, 305.0, t, {-2000.0, 2400.0, -500.0}, {-1600.0, 2200.0, -450.0}), {-1500.0, 0.0, -500.0}, 50.0), 0.0);
	if (!content) f.content.clear();
}
inline void shotInkRidge(double t, Frame &f, bool content) {
	if (t < 308.0) inkPaperFrame(f, t, 305.0, 1);
	else inkLandFrame(f, t, cam({-1000.0, 260.0, -2000.0}, {-1500.0, 280.0, -7000.0}, 40.0), 0.0);
	if (!content) f.content.clear();
}
inline void shotInkBird(double t, Frame &f, bool content) {
	if (t < 313.5) inkPaperFrame(f, t, 311.0, t < 312.5 ? 2 : 3);
	else {
		inkLandFrame(f, t, cam({-800.0, 300.0, -1500.0}, {-1200.0, 600.0, -5000.0}, 46.0), 0.0);
		if (content) f.content += birdText({-900.0, 330.0, -1700.0}, {1.0, 0.1, 0.0}, std::sin(t * 6.0), 1.6, {0.02, 0.02, 0.02}, {0.02, 0.02, 0.02});
	}
	if (!content) f.content.clear();
}
inline void shotThroughClouds(double t, Frame &f, bool content) {
	// through the painted clouds and back into the world: the colour returns as the mist parts
	const double u = clamp01((t - 318.0) / 17.0);
	const Vec3 eye = glide(318.0, 335.0, t, {-2600.0, 900.0, -2600.0}, {-900.0, 260.0, -900.0});
	inkLandFrame(f, t, cam(eye, {0.0, 30.0, 0.0}, 48.0), smoothstep(0.35, 0.85, u));
	if (!content) f.content.clear();
}
inline void shotPullAway(double t, Frame &f, bool content) {
	// further and further away: the Pavilion small, the city small, the river a line of silver
	const Vec3 eye = glide(335.0, 350.0, t, {-180.0, 90.0, -260.0}, {-2600.0, 2400.0, -3600.0});
	f.view = cam(eye, {0.0, 20.0, 200.0}, glide(335.0, 350.0, t, {34.0, 0.0, 0.0}, {52.0, 0.0, 0.0}).x);
	f.sky = Dusk;
	f.skyScale = 0.62;
	f.iso = 380.0;
	f.warmth = 0.04;
	f.haze = hazeAround(eye, 0.00003);
	if (content) f.content = worldText(eye, t, 2500.0) + lanternLights(1.0) + hallGlow(0.5);
}

// Scene 8 -------------------------------------------------------------------------------------------------------------------------------------------------------
inline void shotAlone(double t, Frame &f, bool content) {
	// alone at the railing of the hall's balcony; the feast behind him out of focus, its laughter far away
	const Vec3 at{-5.0, kDeck2, -kRail2Z + 0.6};
	f.view = cam(at + Vec3{-2.0, 1.6, -2.6}, at + Vec3{0.0, 1.45, 0.0}, 34.0, 0.03);
	f.sky = Twilight;
	f.iso = 900.0;
	f.warmth = -0.12;
	f.saturation = 0.95;
	f.haze = hazeAround(f.view.eye, 0.00004);
	if (content) f.content = worldText(f.view.eye, t, 1500.0) + hallText(t) + crowdText(hallGuests()) + lanternLights(1.0, 0, false) + wangBoAt(standPose({0.2, 0.0, -0.2}), at, 180.0);
}
inline void shotRidges(double t, Frame &f, bool content) {
	// ridges going away into the mist, one after another, without end (a place of its own: layered hills, the haze paling each layer more than the one before)
	const Vec3 eye{0.0, 170.0, 0.0};
	f.view = cam(eye, glide(360.0, 372.0, t, {-400.0, 120.0, -3000.0}, {400.0, 140.0, -3000.0}), 30.0);
	f.sky = Dusk;
	f.skyOverride = "LightSource \"infinite\" \"rgb L\" [ 0.20 0.24 0.34 ]\n";   // (a cool even twilight: the layers pale into blue)
	f.sun = false;
	f.iso = 420.0;
	f.warmth = -0.18;
	f.saturation = 0.85;
	f.haze = hazeAround(eye, 0.00022, 16000.0, {0.8, 0.9, 1.2});
	if (content) f.content = ridgeLayersText();
}
inline void passFrame(Frame &f) {
	f.sky = Twilight;
	f.skyOverride = "LightSource \"infinite\" \"rgb L\" [ 0.45 0.48 0.55 ]\n";
	f.sun = false;
	f.iso = 380.0;
	f.warmth = -0.25;
	f.saturation = 0.7;
	f.bloom = 0.12;
}
inline void shotPass(double t, Frame &f, bool content) {
	// his inner landscape: rain, wind, a small figure crossing a great mountain pass
	passFrame(f);
	const double walk = 1.0 * (t - 372.0);
	const Vec3 at{6.0 * std::sin((-60.0 - walk) / 40.0), 0.0, -60.0 - walk};
	f.view = cam({8.0, 46.0, 70.0}, at + Vec3{0.0, -4.0, -40.0}, 40.0);   // (high over the road behind him: the small figure walking on into the pass)
	f.haze = hazeAround(f.view.eye, 0.0035, 320.0, {0.9, 0.95, 1.05});
	if (content) f.content = passText(t, 1.0) + wangBoAt(walkingPose(t, 1.0), {at.x, passHeight(at.x, at.z), at.z}, 180.0, 0.03);
}
inline void shotCrossroads(double t, Frame &f, bool content) {
	// the crossroads: one road to the capital, one to the south; he stops; only the wind
	passFrame(f);
	f.skyOverride = "LightSource \"infinite\" \"rgb L\" [ 0.24 0.25 0.28 ]\n";
	f.iso = 300.0;
	const Vec3 at{0.0, plateauHeight(0.0, 2.0), 2.0};
	f.view = cam(glide(390.0, 405.0, t, {6.0, 9.0, 34.0}, {4.0, 14.0, 46.0}), at + Vec3{0.0, 1.0, -8.0}, 40.0);
	f.haze = hazeAround(f.view.eye, 0.0011, 1600.0, {0.9, 0.95, 1.05});
	if (content) f.content = crossroadsText() + wangBoAt(standPose({0.6, 0.0, 0.2}), at, 180.0, 0.025);
}

// Scene 9 -------------------------------------------------------------------------------------------------------------------------------------------------------
inline void shotPalace(double t, Frame &f, bool content) {
	// an old official waiting below the palace doors at night; they close before him
	f.sky = Night;
	f.skyOverride = "LightSource \"infinite\" \"rgb L\" [ 0.010 0.012 0.022 ]\n";
	f.sun = false;
	f.mist = true;
	f.haze = hazeAround({0.0, 4.0, -10.0}, 0.006, 120.0, {0.8, 0.9, 1.1});
	f.iso = 900.0;
	f.warmth = -0.1;
	f.view = cam(glide(405.0, 419.0, t, {-3.0, 2.0, -17.0}, {-1.5, 2.5, -13.5}), {0.0, 4.0, 0.0}, 40.0);
	if (content) {
		const double close = smoothstep(409.0, 416.0, t);
		f.content = palaceGateText(close, t);
		Dress d;
		d.robe = {0.38, 0.32, 0.30};
		d.beard = true;
		Pose p = personPose(Bowing);
		p.lean = 18.0;
		f.content += figureText(figureMeshes(p, d, 0.02), d, mul(translate(0.0, 0.0, -9.5), rotate({0, 1, 0}, 0.0)));
		f.content += "LightSource \"distant\" \"point3 from\" [ 1 2 -1.5 ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ 0.05 0.06 0.10 ]\n";
	}
}
inline void shotGeneral(double t, Frame &f, bool content) {
	// the general alone under his banner in the snow, his soldiers marching past him
	f.sky = Night;
	f.skyOverride = "LightSource \"infinite\" \"rgb L\" [ 0.16 0.17 0.21 ]\n";
	f.sun = false;
	f.haze = hazeAround({0.0, 3.0, -4.0}, 0.012, 90.0, {0.95, 0.97, 1.05});
	f.iso = 260.0;
	f.warmth = -0.2;
	f.saturation = 0.8;
	f.view = cam(glide(419.0, 431.0, t, {-3.5, 1.6, -9.0}, {-2.5, 1.7, -7.5}), {0.8, 4.0, 0.0}, 46.0);
	if (content) {
		f.content = snowFieldText(t);
		Dress g;
		g.robe = {0.30, 0.06, 0.05};
		g.helmet = true;
		g.hat = false;
		g.beard = true;
		f.content += figureText(figureMeshes(standPose({0.6, 0.0, 0.0}), g, 0.02), g, mul(translate(0.0, 0.0, 0.0), rotate({0, 1, 0}, -150.0)));
		Dress s;
		s.robe = {0.22, 0.20, 0.20};
		s.helmet = true;
		s.hat = false;
		static const FigureMeshes soldier = figureMeshes(walkingPose(0.3), s, 0.03);
		for (int k = 0; k < 16; ++k) {
			const double x = -14.0 + 2.2 * (k % 8) + 1.3 * (t - 419.0), z = 5.0 + 1.6 * (k / 8);
			f.content += figureText(soldier, s, mul(translate(x, 0.0, z), rotate({0, 1, 0}, 90.0)));
			f.content += attr(bronze(0.4), bar({x + 0.2, 0.2, z + 0.25}, {x + 0.25, 2.9, z + 0.25}, 0.02, 5));
		}
	}
}
inline void shotHandTightens(double t, Frame &f, bool content) {
	// his face calm; his hand tightening round the brush; ink dripping onto the page
	const Vec3 table = tableBefore(hallSeats()[12]) + Vec3{0.0, 0.36, 0.0};
	feastFrame(f, table + Vec3{0.30, 0.12, 0.12}, table + Vec3{0.06, 0.10, 0.06}, 24.0, 0.003);   // (across the table, low: the brush tip and the drop forming on it, his grip above out of focus)
	f.sky = Twilight;
	f.iso = 240.0;
	if (content) {
		Pose p = kneelPose();
		const Vec3 tip = table + Vec3{0.06, 0.16, 0.06};
		p.handR = tip - wangBoSeat() + Vec3{0.0, 0.10, -0.05};
		f.content = feastText(t, f.view.eye, 6, &p) + brushText(tip, {0.08, 1.0, 0.1});
		const double drop = std::fmod(t - 431.0, 3.3);
		f.content += attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.01 0.01 0.01 ] \"float roughness\" [ 0.01 ]",
						  emitMesh(uvSphere(0.006 + 0.002 * (std::min)(1.0, drop), 12, 8), translate(tip + Vec3{0.0, -0.01 - 0.6 * std::pow((std::max)(0.0, drop - 2.4), 2.0) * 8.0, 0.0})));
	}
}
inline void shotStone(double t, Frame &f, bool content) {
	// one drop of ink falling in slow motion, then a stone falling into deep water and sinking into the dark
	if (t < 445.0) {
		const Vec3 table = tableBefore(hallSeats()[12]) + Vec3{0.0, 0.36, 0.0};
		feastFrame(f, table + Vec3{0.18, 0.06, 0.12}, table + Vec3{0.06, 0.03, 0.06}, 22.0, 0.003);
		f.sky = Twilight;
		f.iso = 240.0;
		if (content) {
			const double u = clamp01((t - 441.0) / 4.0);
			f.content = feastText(t, f.view.eye, 6, nullptr);
			f.content += attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.01 0.01 0.01 ] \"float roughness\" [ 0.01 ]", emitMesh(uvSphere(0.008, 16, 10), translate(table + Vec3{0.06, 0.16 - 0.155 * u * u, 0.06})));
		}
		return;
	}
	const double u = clamp01((t - 445.0) / 10.0);
	const Vec3 stone{0.0, -1.0 - 26.0 * u, 0.0};
	f.view = cam({1.8, -4.0 - 18.0 * u, 6.5}, stone, 50.0);
	f.sky = Afternoon;   // (a high sun: its light comes down through the water in shafts)
	f.skyScale = 0.8;
	f.iso = 500.0 * (1.0 - 0.7 * u);
	f.mist = true;
	f.haze = deepWaterMedium(f.view.eye);
	f.depth = 8;
	if (content) f.content = deepWaterText(stone, t, 90.0 * u);
}

// Scene 10 ------------------------------------------------------------------------------------------------------------------------------------------------------
inline void shotRaisesHead(double t, Frame &f, bool content) {
	// he raises his head
	const Vec3 at{-5.0, kDeck2, -kRail2Z + 0.6};
	f.view = cam(at + Vec3{-0.6, 1.45, -1.1}, at + Vec3{0.0, 1.62, 0.0}, 30.0, 0.012);
	f.sky = BlueHour;
	f.iso = 1100.0;
	f.warmth = -0.05;
	f.haze = hazeAround(f.view.eye, 0.00004);
	if (content) {
		Pose p = standPose({0.4, 0.1, -0.2});
		p.headTilt = lerp(-14.0, 12.0, ease(456.0, 462.0, t));
		f.content = worldText(f.view.eye, t, 1500.0) + hallText(t) + lanternLights(1.0, 0, false) + wangBoAt(p, at, 180.0, 0.012);
	}
}
inline void shotWind(double t, Frame &f, bool content) {
	// the wind rising: clouds racing, the banners on the terrace snapping
	const Vec3 eye{-14.0, kUpperTerrace + 1.8, -22.0};
	f.view = cam(eye, eye + Vec3{6.0, 10.0, 8.0}, 44.0);
	f.sky = BlueHour;
	f.skyTurn = 0.9 * (t - 465.0);
	f.iso = 1100.0;
	f.haze = hazeAround(eye, 0.00004);
	if (content) {
		f.content = worldText(eye, t, 1500.0, true, 1.0) + lanternLights(1.0) + hallGlow(0.5);
		static const Vec3 colours[] = {{0.75, 0.08, 0.06}, {0.92, 0.62, 0.08}, {0.10, 0.30, 0.65}, {0.08, 0.50, 0.30}};
		for (int k = 0; k < 6; ++k) f.content += bannerText({-25.0 + 10.0 * k, kUpperTerrace, -18.6}, 8.0, colours[k % 4], t + k, 1.0, -70.0);
	}
}
inline void shotStandsUp(double t, Frame &f, bool content) {
	// he steps onto the terrace; low behind him, the enormous sky; then an eagle rising from a cliff
	if (t >= 482.0) {
		const double u = clamp01((t - 482.0) / 8.0);
		f.view = cam({-6.0, -1.5 + 5.0 * u, 11.0}, {0.4, 4.0 + 22.0 * u, -0.5}, 40.0);
		f.sky = BlueHour;
		f.iso = 1100.0;
		f.mist = false;
		if (content) f.content = cliffText() + birdText({0.4, 2.0 + 12.0 * u * u + 4.0 * u, -2.0 * u}, {0.15, 1.0, -0.3}, std::sin(t * 2.0 * kPi * 1.1), 2.3, {0.24, 0.16, 0.10}, {0.20, 0.13, 0.08});
		return;
	}
	const double u = clamp01((t - 473.0) / 5.0);
	const Vec3 at = lerp(Vec3{-1.0, kDeck3, -5.4}, terraceSpot(-1.0), u);
	f.view = cam(at + Vec3{0.6, 0.45, 2.6}, at + Vec3{-0.4, 2.6, -6.0}, 58.0);
	f.sky = BlueHour;
	f.iso = 1100.0;
	f.haze = hazeAround(f.view.eye, 0.00004);
	if (content) {
		Pose p = u < 1.0 ? walkingPose(t) : standPose({0.8, 0.3, -0.6});
		p.headTilt = 8.0;
		f.content = worldText(f.view.eye, t, 2500.0, true, 1.0) + lanternLights(1.0) + hallGlow(0.4) + wangBoAt(p, at, 180.0);
	}
}
inline void shotWholeSky(double t, Frame &f, bool content) {
	// the music opens; far away, a tiny upright figure on the terrace against the whole sky
	const Vec3 eye = glide(490.0, 497.0, t, {-420.0, kDeck3 - 4.0, -560.0}, {-460.0, kDeck3 - 6.0, -600.0});
	f.view = cam(eye, terraceSpot(-3.0) + Vec3{0.0, 6.0, 0.0}, 9.0);
	f.sky = BlueHour;
	f.iso = 1100.0;
	f.haze = hazeAround(eye, 0.00004);
	if (content) f.content = worldText(eye, t, 1500.0, true, 1.0) + lanternLights(1.0) + hallGlow(0.5) + wangBoAt(standPose({0.8, 0.3, -0.6}), terraceSpot(-3.0), 180.0, 0.03);
}
inline void shotEverything(double t, Frame &f, bool content) {
	// everything at once, a second each: the peaks, the rushing river, ships on the waves, the autumn woods, the walls, a scholar writing, farmers, riders, children, the bird flying up
	const int k = (std::min)(7, static_cast<int>(t - 497.0));
	f.sky = BlueHour;
	f.iso = 1100.0;
	f.haze = hazeAround({0.0, 0.0, 0.0}, 0.00004);
	switch (k) {
		case 0: f.view = cam({-11500.0, 1500.0, 900.0}, {-16000.0, 1300.0, -2500.0}, 40.0); if (content) f.content = peaksText() + landText(); break;
		case 1: f.view = cam({-9000.0, 30.0, riverMiddle(-9000.0)}, {-7000.0, 0.0, riverMiddle(-7000.0)}, 44.0); if (content) f.content = worldText(f.view.eye, t, 1500.0, false); break;
		case 2:
			f.view = cam({-200.0, 3.0, -300.0}, {-300.0, 6.0, -420.0}, 36.0);
			if (content) {
				f.content = worldText(f.view.eye, t, 1500.0);
				for (int b = 0; b < 3; ++b) f.content += boatText(true, {-300.0 + 30.0 * b, kWater, -430.0 - 25.0 * b}, 80.0, 6.0 * std::sin(t * 3.0 + b), b + 60);
			}
			break;
		case 3: {
			const Vec3 v = villagePlaces()[2];
			f.view = cam(v + Vec3{40.0, 12.0, 40.0}, v + Vec3{0.0, 6.0, 0.0}, 40.0);
			if (content) f.content = worldText(f.view.eye, t, 600.0, true, 1.0);
			break;
		}
		case 4: f.view = cam({kGateX - 60.0, 4.0, kCityZ0 - 50.0}, {kGateX, 10.0, kCityZ0}, 44.0); if (content) f.content = worldText(f.view.eye, t, 800.0); break;
		case 5: {
			const Vec3 table = tableBefore(hallSeats()[12]) + Vec3{0.0, 0.36, 0.0};
			f.view = cam(table + Vec3{-0.4, 0.5, 0.5}, table, 30.0, 0.008);
			f.mist = false;
			if (content) {
				Pose p = kneelPose();
				p.handR = table + Vec3{0.0, 0.1, 0.0} - wangBoSeat();
				f.content = feastText(t, f.view.eye, 9, &p) + brushText(table + Vec3{0.0, 0.01, 0.0}, {0.1, 1.0, 0.1});
			}
			break;
		}
		case 6: {
			const Vec3 at{kGateX - 4.0, kGround, 40.0};
			f.view = cam(at + Vec3{-8.0, 1.5, -6.0}, at + Vec3{0.0, 1.5, 0.0}, 40.0);
			if (content) {
				f.content = worldText(f.view.eye, t, 800.0);
				for (int r = 0; r < 3; ++r) f.content += horseText(at + Vec3{2.5 * r, 0.0, 6.0 * (t - 503.0) + 2.0 * r}, 0.0, t * 9.0 + r, 1.0, {0.15, 0.12, 0.10}, {0.6, 0.1, 0.08});
			}
			break;
		}
		default:
			f.view = cam({-40.0, kDeck3 + 2.0, -20.0}, {-60.0, kDeck3 + 80.0, -200.0}, 50.0);
			if (content) f.content = worldText(f.view.eye, t, 1500.0) + birdText({-55.0, kDeck3 + 20.0 + 25.0 * (t - 504.0), -120.0}, {0.0, 1.0, -0.4}, std::sin(t * 12.0), 0.9, {0.05, 0.04, 0.035}, {0.06, 0.05, 0.04});
			break;
	}
}

// Scene 11 ------------------------------------------------------------------------------------------------------------------------------------------------------
inline void shotWritesLast(double t, Frame &f, bool content) {
	// back in the hall: he writes the last lines; the guests have stopped talking; only the brush
	const Vec3 table = tableBefore(hallSeats()[12]) + Vec3{0.0, 0.36, 0.0};
	feastFrame(f, glide(505.0, 517.0, t, wangBoSeat() + Vec3{2.4, 1.3, 2.2}, wangBoSeat() + Vec3{1.4, 1.05, 1.5}), wangBoSeat() + Vec3{0.0, 0.7, 0.4}, 40.0, 0.015);
	f.sky = Night;
	f.iso = 240.0;
	if (content) {
		Pose p = kneelPose();
		p.headTilt = -25.0;
		const double w = t * 2.6;
		const Vec3 tip = table + Vec3{0.12 - 0.02 * std::sin(w * 0.5), 0.004 + 0.004 * (std::max)(0.0, std::sin(w)), 0.10 - 0.08 * std::fmod(w * 0.08, 1.0)};
		p.handR = tip - wangBoSeat() + Vec3{0.0, 0.10, -0.05};
		f.content = feastText(t, f.view.eye, 11, &p) + brushText(tip, {0.06, 1.0, 0.12});
	}
}
inline void shotFaces(double t, Frame &f, bool content) {
	// the faces: a cup lowered, a guest leaning forward, the governor watching; nobody speaks
	const int k = (std::min)(2, static_cast<int>((t - 517.0) / 4.0));
	const Seat &s = hallSeats()[k == 2 ? 14 : (k == 0 ? 3 : 6)];
	const double yaw = s.yaw * kPi / 180.0;
	const Vec3 face = s.at + Vec3{0.0, 1.05, 0.0};
	feastFrame(f, face + Vec3{std::sin(yaw) * 1.6 - std::cos(yaw) * 0.6, 0.15, std::cos(yaw) * 1.6 + std::sin(yaw) * 0.6}, face, 30.0, 0.012);
	f.sky = Night;
	f.iso = 240.0;
	if (content) f.content = feastText(t, f.view.eye, 11, nullptr);
}
inline void shotBrushDown(double t, Frame &f, bool content) {
	// he lays the brush down; silence; he looks at what he has written as though he has only just found out what he wanted to say
	const Vec3 table = tableBefore(hallSeats()[12]) + Vec3{0.0, 0.36, 0.0};
	// (from the brush laid down, up to his face, three-quarters from the front, looking down at what he has written)
	feastFrame(f, glide(529.0, 545.0, t, table + Vec3{0.9, 0.9, -0.2}, wangBoSeat() + Vec3{1.0, 1.2, 1.6}), glide(529.0, 545.0, t, table, wangBoSeat() + Vec3{0.0, 1.0, 0.25}), 36.0, 0.012);
	f.sky = Night;
	f.iso = 240.0;
	if (content) {
		Pose p = kneelPose();
		p.headTilt = lerp(-25.0, -10.0, ease(533.0, 540.0, t));
		const Vec3 rest = table + Vec3{0.40, 0.03, 0.18};
		p.handR = lerp(table + Vec3{0.1, 0.05, 0.1}, rest, ease(529.0, 532.0, t)) - wangBoSeat() + Vec3{0.0, 0.08, 0.0};
		f.content = feastText(t, f.view.eye, 12, &p) + brushText(t < 532.0 ? table + Vec3{0.1, 0.02, 0.1} : rest + Vec3{0.0, -0.02, 0.0}, t < 532.0 ? Vec3{0.06, 1.0, 0.1} : Vec3{1.0, 0.05, 0.2});
	}
}

// Scene 12 ------------------------------------------------------------------------------------------------------------------------------------------------------
inline void nightFrame(Frame &f, Vec3 eye) {
	f.sky = Night;
	f.skyScale = 1.0;
	f.sun = false;
	f.iso = 2400.0;
	f.warmth = -0.04;
	f.bloom = 0.3;
	f.haze = hazeAround(eye, 0.00003, 40000.0, {0.8, 0.9, 1.1});
}
inline void shotLanternsOut(double t, Frame &f, bool content) {
	const Vec3 eye{-60.0, 8.0, -110.0};
	f.view = cam(eye, {0.0, 24.0, 0.0}, 40.0);
	nightFrame(f, eye);
	if (content) f.content = worldText(eye, t, 1500.0) + lanternLights(1.0, (std::min)(14, static_cast<int>((t - 545.0) * 2.0))) + hallGlow(1.0 - smoothstep(545.0, 553.0, t));
}
inline void shotDownStairs(double t, Frame &f, bool content) {
	const double u = clamp01((t - 553.0) / 6.0);
	const Vec3 at{1.5, lerp(kUpperTerrace, kGround + 0.2, u), lerp(-19.3, -44.0, u)};
	f.view = cam({-10.0, 4.0, -58.0}, at + Vec3{0.0, 1.2, 0.0}, 38.0);
	nightFrame(f, f.view.eye);
	if (content) f.content = worldText(f.view.eye, t, 1500.0) + lanternLights(1.0, 14) + wangBoAt(walkingPose(t, 1.2), at, 180.0, 0.02);
}
inline Vec3 horsePlace() { return {14.0, kGround + 0.2, nearBank(14.0) - 6.0}; }
inline void shotMount(double t, Frame &f, bool content) {
	// at the gate below the terrace a servant holds his horse; he mounts
	const Vec3 h = horsePlace();
	f.view = cam(h + Vec3{-6.0, 2.2, -5.0}, h + Vec3{0.0, 1.5, 0.0}, 40.0);
	nightFrame(f, f.view.eye);
	if (content) {
		f.content = worldText(f.view.eye, t, 1500.0) + lanternLights(1.0, 14) + horseText(h, 90.0, 0.0, 0.0, {0.30, 0.20, 0.12}, {0.55, 0.10, 0.08});
		std::vector<PersonPlace> servant = {{Standing, 7, false, h + Vec3{0.6, 0.0, 1.2}, 200.0}};
		f.content += crowdText(servant);
		const double u = clamp01((t - 561.0) / 2.5);
		Pose p = u < 1.0 ? standPose() : kneelPose();
		f.content += wangBoAt(p, lerp(h + Vec3{0.0, 0.0, -1.3}, h + Vec3{0.0, 1.25, 0.0}, u), 90.0, 0.02);
	}
}
inline void shotRideAway(double t, Frame &f, bool content) {
	// he rides away along the river; the Pavilion grows smaller behind him
	const double u = clamp01((t - 565.0) / 10.0);
	const double x = 14.0 + 120.0 * u;
	const Vec3 h{x, kGround + 0.2, nearBank(x) - 6.0};
	f.view = cam({-10.0, 3.0, -60.0}, lerp(Vec3{20.0, 2.0, -50.0}, h + Vec3{0.0, 1.5, 0.0}, 0.8), 34.0);
	nightFrame(f, f.view.eye);
	if (content) {
		f.content = worldText(f.view.eye, t, 1500.0) + lanternLights(1.0, 14) + horseText(h, 90.0, t * 5.0, 0.8, {0.30, 0.20, 0.12}, {0.55, 0.10, 0.08});
		f.content += wangBoAt(kneelPose(), h + Vec3{0.0, 1.25, 0.0}, 90.0, 0.025);
	}
}
inline void shotRiseNight(double t, Frame &f, bool content) {
	// the camera does not follow him: it rises, higher and higher; the Pavilion below, the city, the river, the mountains
	const Vec3 eye = glide(575.0, 583.0, t, {-40.0, 30.0, -120.0}, {-900.0, 1800.0, -2200.0});
	f.view = cam(eye, {0.0, 10.0, 200.0}, 48.0);
	nightFrame(f, eye);
	if (content) f.content = worldText(eye, t, 2000.0) + lanternLights(1.0, 14);
}
inline void shotStars(double t, Frame &f, bool content) {
	// the sky full of stars, the Milky Way over the land; the river that held the sunset holds the stars
	const Vec3 eye{-300.0, 60.0, -260.0};
	f.view = cam(eye, glide(583.0, 591.0, t, eye + Vec3{-600.0, 380.0, -1000.0}, eye + Vec3{-700.0, 250.0, -1000.0}), 62.0);
	nightFrame(f, eye);
	f.iso = 3200.0;
	if (content) {
		f.content = worldText(eye, t, 1500.0);
		for (int k = 0; k < 160; ++k) {
			const double a = 2.0 * kPi * hash01(k, 401), y = 0.08 + 0.9 * std::pow(hash01(k, 402), 0.8);
			const Vec3 d = normalize({std::cos(a) * std::sqrt(1.0 - y * y), y, std::sin(a) * std::sqrt(1.0 - y * y)});
			f.content += emissiveSphere(eye + d * 20000.0, 12.0 + 10.0 * hash01(k, 403), Vec3{0.9, 0.92, 1.0} * (30.0 * (0.3 + 0.7 * hash01(k, 404))));
		}
	}
}
inline void shotManuscript(double t, Frame &f, bool content) {
	// down to the empty hall: on the table the manuscript, its edge stirred by the wind; the wet black characters; then out through the window to the river
	const Vec3 table = tableBefore(hallSeats()[12]) + Vec3{0.0, 0.36, 0.0};
	const double u = clamp01((t - 591.0) / 10.0);
	const Vec3 eye = u < 0.6 ? lerp(table + Vec3{-0.3, 1.2, 0.6}, table + Vec3{-0.1, 0.32, 0.12}, ease(0.0, 0.6, u)) : lerp(table + Vec3{-0.1, 0.32, 0.12}, table + Vec3{-2.0, 1.4, -6.0}, ease(0.6, 1.0, u));
	f.view = cam(eye, u < 0.6 ? table : lerp(table, table + Vec3{-6.0, 0.5, -60.0}, ease(0.6, 1.0, u)), 36.0, 0.008);
	nightFrame(f, eye);
	f.mist = false;
	f.iso = 600.0;
	if (content) f.content = worldText(eye, t, 1500.0) + hallText(t, 0.6) + paperText(table, 0.0, 12) + lanternLights(0.4, 14);
}
inline void shotLastDawn(double t, Frame &f, bool content) {
	// dawn coming at the horizon; a solitary bird crossing the sky; wings, wind, water; then black
	const Vec3 eye{-60.0, kDeck3 + 3.0, -40.0};
	f.view = cam(eye, eye + Vec3{1200.0, 160.0, 2000.0}, 50.0);
	f.sky = LastDawn;
	f.sun = false;
	f.iso = 900.0 * (1.0 - smoothstep(606.0, 609.0, t));
	f.haze = hazeAround(eye, 0.00004);
	if (content) {
		f.content = worldText(eye, t, 1500.0);
		const double u = clamp01((t - 601.0) / 7.0);
		f.content += birdText(eye + Vec3{-160.0 + 420.0 * u, 70.0 + 10.0 * u, 380.0}, {1.0, 0.02, 0.1}, std::sin(t * 2.0 * kPi * 2.0), 0.9, {0.03, 0.03, 0.03}, {0.04, 0.04, 0.04});
	}
}
inline void shotTitle(double t, Frame &f, bool content) {
	// black, and the title (the captions lay its words)
	f.view = cam({0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, 40.0);
	f.iso = 0.0;
	f.sun = false;
	f.mist = false;
	f.skyOverride = "LightSource \"infinite\" \"rgb L\" [ 0 0 0 ]\n";
	(void)t;
	// (a renderer will not render an empty scene: one small black ball behind the camera)
	if (content) f.content = attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(uvSphere(0.1, 8, 4), translate(0.0, 0.0, 5.0)));
}

inline const std::vector<ShotDef> &shotList() {
	static const std::vector<ShotDef> shots = {
		{0.0, "Black, a bell, the sea of clouds", shotClouds},
		{20.0, "The river comes out of the mountains", shotRiverEmerges},
		{28.0, "The Gan River and Hongzhou", shotRiverExpands},
		{37.0, "The Pavilion revealed", shotReveal},
		{45.0, "The procession at the gate", shotGate},
		{53.0, "Hoof, wheel, jade, scrolls, wine, strings", shotMontageFeast},
		{65.0, "The hall made ready", shotHallPrepared},
		{75.0, "The guests arrive", shotGuests},
		{85.0, "The young traveller", shotTraveller},
		{95.0, "To the stair", shotToStair},
		{101.0, "The climb", shotClimb},
		{113.0, "The highest terrace", shotTerrace},
		{121.0, "Looking down", shotLookDown},
		{127.0, "The land opens", shotCircle},
		{140.0, "The market", shotMarket},
		{147.0, "Boats below the Pavilion", shotBoatsBelow},
		{153.0, "The shepherd", shotShepherd},
		{159.0, "Egrets rise", shotEgrets},
		{165.0, "The boats come home", shotBoatsHome},
		{174.0, "The geese", shotGeese},
		{185.0, "A ginkgo leaf falls", shotLeaf},
		{196.0, "The sun goes down", shotSunGoesDown},
		{210.0, "The setting clouds and the solitary duck", shotDuck},
		{228.0, "The water and the sky one colour", shotWaterLevel},
		{245.0, "Wine", shotWine},
		{250.0, "The pipa", shotPipa},
		{256.0, "Poems", shotPoems},
		{263.0, "The paper goes round", shotPaperGoesRound},
		{281.0, "The blank paper", shotBlankPaper},
		{295.0, "Ink becomes the river", shotInkRiver},
		{305.0, "A stroke becomes the ridge", shotInkRidge},
		{311.0, "A dot becomes a bird; a wash, a storm", shotInkBird},
		{318.0, "Through the painted clouds", shotThroughClouds},
		{335.0, "Far away", shotPullAway},
		{350.0, "Alone", shotAlone},
		{360.0, "Ridges without end", shotRidges},
		{372.0, "The pass in the rain", shotPass},
		{390.0, "The crossroads", shotCrossroads},
		{405.0, "The palace doors", shotPalace},
		{419.0, "The general in the snow", shotGeneral},
		{431.0, "The hand tightens", shotHandTightens},
		{441.0, "The ink drop and the stone", shotStone},
		{455.0, "He raises his head", shotRaisesHead},
		{465.0, "The wind", shotWind},
		{473.0, "The terrace and the eagle", shotStandsUp},
		{490.0, "Against the whole sky", shotWholeSky},
		{497.0, "Everything at once", shotEverything},
		{505.0, "The last lines", shotWritesLast},
		{517.0, "The faces", shotFaces},
		{529.0, "The brush laid down", shotBrushDown},
		{545.0, "The lanterns go out", shotLanternsOut},
		{553.0, "Down the stair", shotDownStairs},
		{559.0, "The horse", shotMount},
		{565.0, "The ride away", shotRideAway},
		{575.0, "Rising over the night", shotRiseNight},
		{583.0, "The stars over the river", shotStars},
		{591.0, "The manuscript", shotManuscript},
		{601.0, "Dawn", shotLastDawn},
		{609.0, "The title", shotTitle},
	};
	return shots;
}
inline std::vector<double> shotStarts() {
	std::vector<double> v;
	for (const ShotDef &s : shotList()) v.push_back(s.start);
	return v;
}
inline std::string shotNames() {
	std::string s;
	for (const ShotDef &d : shotList()) s += (s.empty() ? "" : "|") + std::string(d.name);
	return s;
}
// (the same as a lasting C string, for the film table)
inline const char *shotNamesText() {
	static const std::string s = shotNames();
	return s.c_str();
}
inline Frame frameAt(double t, bool content) {
	const std::vector<ShotDef> &shots = shotList();
	std::size_t k = 0;
	for (std::size_t i = 0; i < shots.size(); ++i)
		if (t >= shots[i].start) k = i;
	Frame f;
	shots[k].fill(t, f, content);
	f.view.shot = static_cast<int>(k);
	return f;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The scene
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string headerOf(const Frame &f) {
	char look[220];
	std::snprintf(look, sizeof look, "# @rt-look bloom %.3f vignette %.3f warmth %.3f saturation %.3f\n", f.bloom, f.vignette, f.warmth, f.saturation);
	std::string h = sceneHeader(cameraText(f.view), kSamples, kWidth, kHeight);
	char film[160];
	// (an iso of 0 is a black frame: a cut to black)
	std::snprintf(film, sizeof film, "Film \"rgb\" \"float iso\" [ %.6g ] \"float maxcomponentvalue\" [ 40 ] ", (std::max)(1e-6, f.iso));
	const std::size_t at = h.find("Film \"rgb\" ");
	if (at != std::string::npos) h.replace(at, 11, film);
	const std::string depth = "\"integer maxdepth\" [ " + std::to_string(f.depth) + " ]";
	const std::size_t d = h.find("\"integer maxdepth\" [ 8 ]");
	if (d != std::string::npos) h.replace(d, 24, depth);
	return std::string(look) + (f.mist ? nature::mistBeforeCamera(f.haze) : std::string()) + h;
}
inline std::string headerAt(double t) {
	Frame f = frameAt(t, false);
	if (f.iso <= 1.0) {   // (a black frame: as sceneAt makes it)
		f.iso = 100.0;
		f.mist = false;
		f.bloom = 0.0;
	}
	return headerOf(f);
}
inline std::string sceneAt(double t) {
	Frame f = frameAt(t, true);
	// A black frame (a cut to black, the title's card): a near-black scene at an ordinary exposure. (An exposure of almost nothing was taken as it came, and a frame of it
	// took the renderer some minutes.)
	if (f.iso <= 1.0) {
		f.iso = 100.0;
		f.mist = false;
		f.sun = false;
		f.bloom = 0.0;
		f.skyOverride = "LightSource \"infinite\" \"rgb L\" [ 0.00001 0.00001 0.00001 ]\n";
		f.content = attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(uvSphere(0.1, 8, 4), translate(f.view.eye + Vec3{0.0, 0.0, 5.0})));
	}
	std::string out = headerOf(f);
	if (f.mist) out += nature::mistBoundary(f.haze);
	if (!f.skyOverride.empty()) out += f.skyOverride;
	else out += skyText(f.sky, f.skyScale, f.skyTurn);
	if (f.sun && f.skyOverride.empty()) out += sunText(sunOf(f.sky), f.sunStrength, f.view.eye);
	out += f.content;
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The words
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::vector<Caption> captions() {
	const auto c = [](const char *key, double t0, double t1, double fade = 1.0) { return Caption{t0, t1, film_cache::asset(std::string("tengwang/captions/") + key + ".png"), fade, 0.86, 0.625}; };
	std::vector<Caption> v = {
		c("c01", 9.0, 15.5), c("c02", 38.5, 44.6), c("c03", 86.0, 93.5), c("c04", 128.0, 139.0), c("c05", 166.0, 173.5), c("c06", 176.0, 184.0), c("c07", 212.0, 219.0),
		c("c08", 220.0, 227.5), c("c09", 322.0, 330.0), c("c10", 338.0, 345.0), c("c11", 362.0, 370.5), c("c12", 396.0, 403.5), c("c13", 407.0, 413.5), c("c14", 421.0, 428.0),
		c("c15", 456.0, 463.5), c("c16", 475.0, 481.5), c("c17", 483.0, 490.0), c("c18", 593.0, 600.0),
	};
	Caption title = c("t01", 611.0, 619.6, 1.2);
	title.centreY = 0.40;
	Caption author = c("t02", 612.5, 619.6, 1.2);
	author.centreY = 0.55;
	Caption line = c("t03", 614.0, 619.6, 1.2);
	line.centreY = 0.74;
	v.push_back(title);
	v.push_back(author);
	v.push_back(line);
	return v;
}

}  // namespace tengwang
}  // namespace cartoon
