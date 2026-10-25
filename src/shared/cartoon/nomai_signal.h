#pragma once
// nomai_signal.h -- "The Last Nomai Signal", an unofficial fan film after Outer Wilds (Mobius Digital): a lone Nomai explorer follows a faint repeating signal to a small
// moon of a gas giant, finds the ruins of an expedition that came there generations before, wakes their observatory deep under the ground, and learns that the signal comes
// from a star that no longer exists - the last message of something long gone. The explorer answers it anyway. Five minutes, 1920 x 1080, 24 fps, with a score.
//
//   I   Arrival        0:00  darkness and a pulse; the stars; the giant and its tiny moon; the shuttle's descent; three eyes in the cockpit; the signal; the landing
//   II  The signal     0:50  footsteps in the dust; the ridge; the canyon and its ruins; the third eye; the floating steps; the corridor wakes; the writing; the recording
//   III Observatory    2:00  the elevator; into the dark; the hall; the staff answers; the hall wakes; the star map; the flashing star; it is gone
//   IV  The message    3:10  the transmission rebuilt; the remote memories (orange, as the game shows a remote message); silence; stillness; the answer
//   V   Ending         4:10  dawn; the campfire; the long pull back to the stars; the title
//
// Made here from scratch (no asset of the game is used): the world (nomai_world.h), the ruins and the observatory (nomai_ruins.h), the explorer (nomai_explorer.h), the
// light-work (nomai_fx.h) and the music and sound (nomai_score.h). The colours follow the game: the green bands of Giant's Deep on the giant, the gold-orange sun, the
// orange sand and banded rock of the Hourglass Twins, the Nomai's ghostly blue light and purple gravity crystals.

#include <functional>

#include "caption_file.h"
#include "film_kit.h"
#include "nomai_explorer.h"
#include "nomai_fx.h"
#include "nomai_ruins.h"
#include "nomai_world.h"
#include "film_events.h"
#include "rig.h"

namespace cartoon {
namespace nomai {

constexpr double kDuration = 300.0;
constexpr int kFps = 24;
constexpr int kSamples = 32;
constexpr int kWidth = 1920, kHeight = 1080;

using namespace kit;
struct Frame : kit::Moment {
	double dawn = 0.0;          // where the sun is (nomai_world.h sunDir)
	double starLight = 1.0;
	double giantGlow = 0.06;    // the giant's own brightness (its face): low for a night exposure of the ground, 1 in space
	double giantshine = 1.0;    // the cool fill it casts at night
	bool space = false;         // seen from far: the whole moon, no rocks or ruins
	bool ruins = true;          // the canyon's ruins (when near them)
	bool interior = false;      // inside the rock: no sky, no sun
	Frame() {
		iso = 320.0;
		bloom = 0.14;
		vignette = 0.30;
		warmth = -0.14;
		saturation = 1.08;
		depth = 5;
	}
};
using ShotDef = kit::Shot<Frame>;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Helpers: lenses, headings, the world as a moment sees it, the explorer's walks and blinks
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline double lens(double mm) { return 2.0 * std::atan(10.125 / mm) * 180.0 / kPi; }   // the vertical field of a 35 mm lens on a 16:9 frame
inline double yawOf(Vec3 dir) { return std::atan2(dir.x, dir.z) * 180.0 / kPi; }
inline double giantYaw() { return yawOf({giantDir().x, 0.0, giantDir().z}); }

inline std::string worldText(const Frame &f, double t, double rockRange = 160.0) {
	std::string out = groundText();
	if (!f.space) out += rocksText(f.view.eye.x, f.view.eye.z, rockRange);
	if (f.ruins && !f.space) out += ruinsStatic() + ruinsGlow(t, 0.35) + groveText(t) + wreckText();
	return out + giantText(f.dawn, f.giantGlow) + neighbourText();
}
// A night moment on the surface, or one in space.
inline void night(Frame &f) {
	f.dawn = 0.0;
	f.giantGlow = 0.06;
}
inline void inSpace(Frame &f, double iso = 90.0) {
	f.space = true;
	f.ruins = false;
	f.giantGlow = 1.0;
	f.giantshine = 0.0;
	f.iso = iso;
	f.warmth = -0.04;
}
inline void inside(Frame &f, double iso) {
	f.interior = true;
	f.ruins = false;
	f.iso = iso;
	f.warmth = -0.06;
}

// The explorer's blinks (rig::blink): the two lower eyes together every few seconds, the third on its own - and now and then a little after them.
inline void blink(ExplorerPose &p, double t) {
	p.lidL = p.lidR = std::max(p.lidL, rig::blink(t, 7));
	p.lidTop = std::max(p.lidTop, std::max(rig::blink(t - 0.14, 7), rig::blink(t, 8)));   // (the third eye: late, or on its own)
}
// The explorer's acting beyond walking: gestures from its pose library (explorerPoses, rig::act), at film times. A gesture with a contact time is a hand meeting stone: the
// film's events hear it (a Touch).
inline const std::vector<rig::Gesture> &acting() {
	static const std::vector<rig::Gesture> g = {
		{"reach to the wall", 99.4, 100.6, 106.0, 106.0, 100.4},   // the carved spiral wakes from the hand (to the shot's end)
		{"kneel and touch", 123.2, 124.2, 125.4, 126.2, 124.0},    // the ring of glyphs on the platform
		{"point up", 180.0, 181.5, 184.0, 184.0},                  // at the flashing star
		{"hand on the lectern", 244.0, 245.5, 250.0, 250.0},
		{"look up", 246.5, 248.5, 250.0, 250.0},                   // the answer goes up
	};
	return g;
}
inline ExplorerPose acted(ExplorerPose p, double t) { return rig::act(p, acting(), t, explorerPoses()); }
// A walking pose: long, slow strides, the staff in the right hand, the left arm swinging a little.
inline ExplorerPose walkPose(double walk, double t, double stride = 1.0) {
	ExplorerPose p;
	p.walk = walk;
	p.stride = stride;
	p.time = t;
	const double sw = std::sin(walk * kPi);
	p.handL = {0.21, 0.90, 0.06 - 0.10 * sw * stride};
	p.handR = {-0.25, 1.18, 0.16 + 0.06 * sw * stride};
	p.headPitch = 4.0;
	p.wind = {0.03, 0.0, -0.025};
	p.staffGlow = 0.22;
	blink(p, t);
	return p;
}
inline ExplorerPose standPose(double t) {
	ExplorerPose p = walkPose(0.0, t, 0.0);
	p.handR = {-0.25, 1.15, 0.14};
	return p;
}
// A walk on the ground along a path (arc coordinates in x and z; rig::PathWalk, 0.84 m a step, a foot coming down half a step in - walkPose's cycle): where the body is,
// how far the feet have gone, and its frame.
struct Walk {
	Vec3 at;
	double yaw, walk, dist;
	bool moving;
	Mat4 frame;
	rig::PathWalk path;
};
inline rig::PathWalk walkPath(Vec3 a, Vec3 b, double t0, double speed = 0.72) { return rig::PathWalk{{a, b}, t0, speed, 0.84, 0.5}; }
inline Walk walkOver(const rig::PathWalk &path, double t) {
	Walk w;
	w.path = path;
	const rig::PathWalk::At at = w.path.at(t);
	w.at = at.at;
	w.yaw = at.yaw;
	w.dist = at.dist;
	w.walk = at.steps;
	w.moving = at.moving;
	w.frame = groundFrame(w.at.x, w.at.z, w.yaw);
	return w;
}
inline Walk walkOver(Vec3 a, Vec3 b, double t0, double t, double speed = 0.72) { return walkOver(walkPath(a, b, t0, speed), t); }
// The footstep puffs of a walk: one at each of its last footfalls, where the foot came down (half a stride ahead of the body as it lands).
inline std::string footDust(const Walk &w, Vec3 a, double t, int maxSteps = 6) {
	std::string out;
	const std::vector<rig::PathWalk::Footfall> steps = w.path.footfalls(-1e9, t);
	for (std::size_t i = steps.size() > static_cast<std::size_t>(maxSteps) ? steps.size() - static_cast<std::size_t>(maxSteps) - 1 : 0; i < steps.size(); ++i) {
		const rig::PathWalk::Footfall &s = steps[i];
		const Vec3 d = normalize(Vec3{w.at.x - a.x, 0.0, w.at.z - a.z} + Vec3{1e-9, 0.0, 0.0});
		const Vec3 side = normalize(cross(Vec3{0.0, 1.0, 0.0}, d));
		const Vec3 foot = s.at + d * 0.52 + side * (s.step % 2 == 0 ? 0.11 : -0.11);
		out += dustPuffText(groundPoint(foot.x, foot.z), upAt(foot.x, foot.z), s.t, t, 40, 0.40, 600 + s.step, 0.005);
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The shuttle's flight, landing and the cockpit
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr double kLandX = 4.0, kLandZ = 8.0, kLandYaw = 170.0;   // where it lands, its nose towards the canyon and the giant
constexpr double kTouchdown = 39.6;
inline Mat4 shuttleOnGround(double lift) { return groundFrame(kLandX, kLandZ, kLandYaw, lift); }
// In flight: a long, slowing glide down towards the landing site; its height above the site and its place.
inline Mat4 shuttleInFlight(double t) {
	const double u = clamp01((t - 17.0) / (kTouchdown - 17.0));
	const double e = 1.0 - std::pow(1.0 - u, 2.2);
	const Vec3 from{180.0, 0.0, 230.0}, to{kLandX, 0.0, kLandZ};
	const Vec3 at = lerp(from, to, e);
	const double h = lerp(900.0, 0.0, 1.0 - std::pow(1.0 - u, 1.6)) * (u < 1.0 ? 1.0 : 0.0);
	const Vec3 fwd = normalize(Vec3{to.x - from.x, 0.0, to.z - from.z});
	const Vec3 up = upAt(at.x, at.z);
	return alignY(groundPoint(at.x, at.z, h), up, fwd * -1.0);
}
inline std::string cockpitText(const Mat4 &at, double t, double screen) {
	const Mat4 c = cockpitFrame(at);
	std::string out;
	// the console: a curved bronze-edged desk before the seat, its stone face set with glyph keys
	out += attr(darkStoneMaterial(), meshText(ringWall(0.95, 1.10, [](double) { return 0.10; }, 1.0, 40, kPi * 0.15, kPi * 0.85), mul(c, translate(0.0, 0.0, -0.55), rotate({1.0, 0.0, 0.0}, -25.0))));
	std::string keys;
	for (int k = 0; k < 14; ++k) {
		const double a = kPi * (0.22 + 0.56 * k / 13.0);
		keys += meshText(box(0.04, 0.012, 0.03), mul(c, translate(0.0, 0.0, -0.55), rotate({1.0, 0.0, 0.0}, -25.0), translate(1.02 * std::cos(a), 0.11, 1.02 * std::sin(a))));
	}
	const Vec3 teal{0.40, 0.92, 0.95};
	out += glowText(keys, teal, 2.5 + 1.5 * std::sin(t * 2.0));
	// the screen: a slab of dark glass above the console, the waveform on it
	const Mat4 scr = mul(c, translate(0.0, 0.38, 0.62), rotate({1.0, 0.0, 0.0}, -12.0));
	out += glowText(meshText(box(0.62, 0.30, 0.02), scr), teal, 0.02);   // (the screen's own faint glow lights the face)
	out += glowText(meshText(box(0.60, 0.004, 0.004), mul(scr, translate(0.0, -0.10, -0.012))), teal, 0.6);
	out += waveformText(mul(scr, translate(0.0, 0.0, -0.014)), 0.56, 0.24, t, teal, 3.0 * screen);
	{
		const Vec3 lp = transformPoint(scr, {0.0, 0.10, -0.40});
		const Vec3 I = teal * 0.25;
		char buf[200];
		std::snprintf(buf, sizeof buf, "LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ %.4f %.4f %.4f ]\n", -lp.x, lp.y, lp.z, I.x, I.y, I.z);
		out += buf;
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The shots
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// I Arrival -------------------------------------------------------------------------------------------------------------------------------------------------------
inline void shotDarkness(double t, Frame &f, bool content) {
	// complete darkness; then the stars come, one by one
	f.view = cam(moonCentre() + Vec3{-20000.0, 6000.0, 60000.0}, moonCentre() + Vec3{-60000.0, 26000.0, 120000.0}, lens(35.0));
	inSpace(f, 160.0);
	f.iso = t < 3.0 ? 0.0 : 160.0;
	f.starLight = smoothstep(3.0, 9.0, t);
	// (a renderer will not render an empty scene: one small black ball behind the camera)
	if (content) f.content = attr(diffuseMaterial({0.0, 0.0, 0.0}), meshText(uvSphere(0.1, 8, 4), translate(f.view.eye - normalize(f.view.target - f.view.eye) * 5.0)));
}
inline Vec3 wideEye() { return moonCentre() - giantDir() * 62000.0 + normalize(cross(giantDir(), Vec3{0.0, 1.0, 0.0})) * 14000.0 + Vec3{0.0, 5000.0, 0.0}; }
inline void shotGiantAndMoon(double t, Frame &f, bool content) {
	// the giant fills the frame; before it the tiny moon; a blue spark - the shuttle - crosses towards it
	const Vec3 eye = wideEye();
	const double u = smoothstep(9.0, 19.0, t);
	f.view = cam(eye, lerp(moonCentre() + giantDir() * 22000.0, moonCentre() + giantDir() * 16000.0, u), lens(50.0));
	f.subjects = {moonFacing(eye)};   // (what the shot is about: the film doctor's framing checks)
	inSpace(f, 70.0);
	if (content) {
		f.content = worldText(f, t);
		const Vec3 from = eye + (moonCentre() - eye) * 0.25 + Vec3{-3000.0, 2500.0, 0.0}, to = moonCentre() + Vec3{1500.0, 2600.0, 600.0};
		const Vec3 ship = lerp(from, to, smoothstep(8.0, 20.0, t));
		f.content += emissiveSphere(ship, 28.0, nomaiBlue() * 40.0);
	}
}
inline void shotDescent(double t, Frame &f, bool content) {
	// behind the shuttle as it glides down towards the night side; the giant ahead
	const Mat4 s = shuttleInFlight(t);
	const Vec3 at = transformPoint(s, {0.0, 2.0, 0.0}), fwd = transformPoint(s, {0.0, 0.0, 1.0}) - transformPoint(s, {0.0, 0.0, 0.0});
	const Vec3 up = upAt(at.x, at.z);
	const Vec3 away = normalize(giantDir() * -1.0 + up * 0.15);
	(void)fwd;
	f.view = cam(at + away * 16.0 + handheld(t, 0.3, 3), at + giantDir() * 6.0, lens(30.0));
	f.subjects = {at};   // (what the shot is about: the film doctor's framing checks)
	night(f);
	f.space = true;
	f.giantGlow = 0.14;
	f.iso = 380.0;
	if (content) f.content = worldText(f, t) + shuttleText(s, 0.7, 0.0);
}
inline ExplorerPose seatedPose(double t) {
	ExplorerPose p;
	p.time = t;
	p.sit = 0.55;
	p.staff = 0;
	p.handL = {0.16, 0.86, 0.34};
	p.handR = {-0.16, 0.86, 0.34};
	p.headPitch = 6.0;
	blink(p, t);
	return p;
}
inline Mat4 seatFrame(const Mat4 &ship) { return mul(cockpitFrame(ship), translate(0.0, -0.55, -0.55)); }
inline void shotEyes(double t, Frame &f, bool content) {
	// close: the three eyes behind the mask's openings, the instruments' teal light in them; a blink, the third eye late
	const Mat4 ship = shuttleInFlight(24.0);
	ExplorerPose p = seatedPose(t);
	p.lidL = p.lidR = 1.0 - smoothstep(0.0, 0.10, std::abs(t - 28.3));
	p.lidTop = std::max(0.25, 1.0 - smoothstep(0.0, 0.10, std::abs(t - 28.55)));
	p.gazePitch = 8.0;
	const Mat4 body = seatFrame(ship);
	const ExplorerBody b = explorerBody(p);
	const Mat4 head = mul(body, b.head);
	f.view = cam(transformPoint(head, {0.0, 0.08, 1.15 - 0.08 * smoothstep(25.0, 31.0, t)}), transformPoint(head, {0.0, 0.11, 0.10}), lens(85.0), 0.003);
	f.subjects = {transformPoint(head, {0.0, 0.11, 0.10})};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 900.0);
	if (content) f.content = shuttleText(ship, 0.6, 0.0) + cockpitText(ship, t, 0.4) + explorerText(p, body, 0.008);
}
inline void shotSignal(double t, Frame &f, bool content) {
	// over the shoulder: the hands on the glyph keys, and on the screen the signal - a pulse, a long silence, a pulse
	const Mat4 ship = shuttleInFlight(25.0);
	ExplorerPose p = seatedPose(t);
	p.handR = {-0.13, 0.84 + 0.02 * std::sin(t * 2.3), 0.42 + 0.03 * std::sin(t * 1.1)};
	p.handL = {0.14, 0.82, 0.40 + 0.04 * smoothstep(32.5, 33.5, t)};
	p.headPitch = 14.0;
	const Mat4 body = seatFrame(ship);
	const Mat4 c = cockpitFrame(ship);
	const ExplorerBody b = explorerBody(p);
	const Vec3 head = transformPoint(body, b.headPivot);
	(void)head;
	f.view = cam(transformPoint(c, {-0.95, 1.25, -0.95}), transformPoint(c, {0.0, 0.32, 0.55}), lens(32.0), 0.004);
	f.subjects = {transformPoint(c, {0.0, 0.38, 0.62})};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 900.0);
	if (content) f.content = shuttleText(ship, 0.6, 0.0) + cockpitText(ship, t, 1.0) + explorerText(p, body, 0.010);
}
inline void shotLanding(double t, Frame &f, bool content) {
	// low on the ground: the shuttle settles on its three legs; the dust rolls out slowly in the weak gravity
	const double lift = 18.0 * std::pow(1.0 - smoothstep(36.0, kTouchdown, t), 1.3);
	const Mat4 s = shuttleOnGround(lift);
	const Mat4 site = groundFrame(kLandX, kLandZ, kLandYaw);
	f.view = cam(transformPoint(site, {16.0, 0.9, 20.0}), transformPoint(site, {0.0, 2.6 + lift * 0.6, 0.0}), lens(32.0));
	f.subjects = {transformPoint(site, {0.0, 2.2 + lift, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	night(f);
	f.ruins = false;
	if (content) {
		f.content = worldText(f, t, 90.0) + shuttleText(s, 1.0 - smoothstep(kTouchdown, kTouchdown + 1.5, t), smoothstep(36.0, 38.5, t));
		f.content += dustPuffText(groundPoint(kLandX, kLandZ), upAt(kLandX, kLandZ), kTouchdown - 0.6, t, 520, 6.5, 700, 0.09);
	}
}
inline Vec3 exitStart() { return {kLandX + 2.8, 0.0, kLandZ - 1.5}; }
inline Vec3 exitEnd() { return exitStart() + Vec3{giantDir().x, 0.0, giantDir().z} * 30.0; }
inline rig::PathWalk exitWalk() { return walkPath(exitStart(), exitEnd(), 43.5, 0.62); }
inline void shotUnderGiant(double t, Frame &f, bool content) {
	// far behind: the explorer steps from the shuttle and walks out under the giant, very small
	const Walk w = walkOver(exitWalk(), t);
	const Vec3 back = exitStart() - Vec3{giantDir().x, 0.0, giantDir().z} * 34.0 + Vec3{12.0, 0.0, 0.0};
	const Vec3 eye = groundPoint(back.x, back.z, 1.1);
	f.view = cam(eye, eye + giantDir() * 100.0 + Vec3{0.0, -26.0, 0.0}, lens(24.0));
	f.subjects = {transformPoint(w.frame, {0.0, 1.25, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	f.feet = explorerFeet(w.moving ? walkPose(w.walk, t) : standPose(t), w.frame);   // (the sliding-feet check: Film::feetAt)
	night(f);
	f.ruins = false;
	if (content) {
		ExplorerPose p = w.moving ? walkPose(w.walk, t) : standPose(t);
		f.content = worldText(f, t, 120.0) + shuttleText(shuttleOnGround(0.0), 0.0, 1.0) + explorerText(p, w.frame, 0.016) + footDust(w, exitStart(), t);
	}
}
// II The signal ---------------------------------------------------------------------------------------------------------------------------------------------------
inline Vec3 stepsStart() { return {1.0, 0.0, -12.0}; }
inline Vec3 stepsEnd() { return {-5.0, 0.0, -40.0}; }
inline rig::PathWalk stepsWalk() { return walkPath(stepsStart(), stepsEnd(), 49.0, 0.70); }
inline void shotFootsteps(double t, Frame &f, bool content) {
	// low behind the feet: each long step lifts a little dust that drifts down slowly
	const Walk w = walkOver(stepsWalk(), t);
	const Vec3 d = normalize(Vec3{stepsEnd().x - stepsStart().x, 0.0, stepsEnd().z - stepsStart().z});
	const Vec3 eyeArc = w.at - d * 1.7 + Vec3{0.35, 0.0, 0.0};
	const Vec3 eye = groundPoint(eyeArc.x, eyeArc.z, 0.28);
	f.view = cam(eye, groundPoint(w.at.x + d.x * 2.0, w.at.z + d.z * 2.0, 0.25), lens(30.0), 0.01);
	f.subjects = {transformPoint(w.frame, {0.0, 0.25, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	f.feet = explorerFeet(walkPose(w.walk, t), w.frame);   // (the sliding-feet check: Film::feetAt)
	night(f);
	f.ruins = false;
	if (content) f.content = worldText(f, t, 60.0) + explorerText(walkPose(w.walk, t), w.frame, 0.012) + footDust(w, stepsStart(), t, 8);
}
inline rig::PathWalk ridgeWalk() { return walkPath({-14.0, 0.0, ridgeZ(-14.0)}, {6.0, 0.0, ridgeZ(6.0)}, 55.0, 0.70); }
inline void shotRidge(double t, Frame &f, bool content) {
	// a wide silhouette: the explorer crosses the ridge's crest against the giant's face
	const Walk w = walkOver(ridgeWalk(), t);
	const Vec3 g = normalize(Vec3{giantDir().x, 0.0, giantDir().z});
	const Vec3 camArc = Vec3{-4.0, 0.0, ridgeZ(-4.0)} - g * 27.0;
	const Vec3 eye = groundPoint(camArc.x, camArc.z, 0.5);
	f.view = cam(eye, groundPoint(w.at.x, w.at.z, 2.6), lens(70.0));
	f.subjects = {transformPoint(w.frame, {0.0, 1.25, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	f.feet = explorerFeet(walkPose(w.walk, t), w.frame);   // (the sliding-feet check: Film::feetAt)
	f.giantGlow = 0.5;
	night(f);
	f.ruins = false;
	if (content) f.content = worldText(f, t, 120.0) + explorerText(walkPose(w.walk, t), w.frame, 0.016);
}
inline Vec3 rimStart() { return {-2.5, 0.0, -112.0}; }
inline Vec3 rimEnd() { return {0.0, 0.0, -130.0}; }
inline rig::PathWalk rimWalk() { return walkPath(rimStart(), rimEnd(), 63.0, 0.62); }
inline void shotCanyonReveal(double t, Frame &f, bool content) {
	// behind the explorer to the rim; the camera rises over the shoulder and the canyon opens below - the ruins, the floating steps, the broken bridge
	const Walk w = walkOver(rimWalk(), t);
	const double u = smoothstep(66.0, 74.5, t);
	const Vec3 d = normalize(Vec3{rimEnd().x - rimStart().x, 0.0, rimEnd().z - rimStart().z});
	const Vec3 eyeArc = w.at + d * lerp(-3.2, 9.0, u) + Vec3{lerp(0.8, -3.0, u), 0.0, 0.0};
	const Vec3 eye = groundPoint(w.at.x, w.at.z, 0.0) + upAt(eyeArc.x, eyeArc.z) * lerp(1.75, 13.0, u) + Vec3{eyeArc.x - w.at.x, 0.0, eyeArc.z - w.at.z};
	// (the aim goes from the ground ahead to the far side of the canyon while the camera rises, and only then down to the ruins: aimed at the ruins too soon, it looked
	// at the ground before the rim)
	const Vec3 farSide = ruinsPoint({-6.0, 4.0 + 40.0 * (1.0 - smoothstep(0.35, 1.0, u)), -8.0});
	f.view = cam(eye, lerp(groundPoint(w.at.x + d.x * 10.0, w.at.z + d.z * 10.0, 1.0), farSide, smoothstep(0.05, 0.45, u)), lens(lerp(35.0, 20.0, u)));
	f.subjects = {u < 0.2 ? transformPoint(w.frame, {0.0, 1.25, 0.0}) : farSide};   // (what the shot is about: the explorer, then the canyon, then - over the rim - the ruins)
	f.feet = explorerFeet(w.moving ? walkPose(w.walk, t) : standPose(t), w.frame);   // (the sliding-feet check: Film::feetAt)
	night(f);
	if (content) {
		ExplorerPose p = w.moving ? walkPose(w.walk, t) : standPose(t);
		f.content = worldText(f, t) + explorerText(p, w.frame, 0.014);
	}
}
inline void shotThirdEye(double t, Frame &f, bool content) {
	// close on the mask: the two lower eyes find the ruins first; then the third opens wider
	const Mat4 body = groundFrame(rimEnd().x, rimEnd().z, yawOf(rimEnd() - rimStart()));
	ExplorerPose p = standPose(t);
	p.headPitch = 18.0;
	p.lidL = p.lidR = 0.30 * (1.0 - smoothstep(75.6, 76.2, t));
	p.lidTop = 0.55 * (1.0 - smoothstep(77.0, 77.6, t));
	p.gazePitch = 10.0;
	p.ears = 0.4 * smoothstep(77.0, 78.0, t);
	const ExplorerBody b = explorerBody(p);
	const Vec3 face = transformPoint(mul(body, b.head), {0.0, 0.11, 0.10});
	const Vec3 fwd = normalize(transformPoint(body, {0.0, 0.0, 1.0}) - transformPoint(body, {0.0, 0.0, 0.0}));
	const Vec3 side = normalize(cross(upAt(rimEnd().x, rimEnd().z), fwd));
	f.view = cam(face + fwd * 1.05 + side * 0.25 + Vec3{0.0, -0.18, 0.0}, face, lens(85.0), 0.004);
	f.subjects = {face};   // (what the shot is about: the film doctor's framing checks)
	night(f);
	if (content) f.content = worldText(f, t, 40.0) + explorerText(p, body, 0.008);
}
inline double spiralU(double t) { return lerp(0.48, 0.565, clamp01((t - 80.0) / 8.0)); }   // how far round the floating steps the explorer is
inline double spiralWalked(double t) { return spiralU(t) * kSpiralTurns * 2.0 * kPi * kSpiralRadius; }
inline ExplorerPose spiralPose(double t) {   // walking down the floating steps: the feet on the spiral's slope
	ExplorerPose p = walkPose(spiralWalked(t) / 0.84, t);
	p.slope = -kSpiralDrop / (kSpiralTurns * 2.0 * kPi * kSpiralRadius);
	return p;
}
inline void shotSpiralSteps(double t, Frame &f, bool content) {
	// from the canyon floor, wide: the explorer walks down the floating steps round the spire, the giant over the canyon's rim
	const double u = spiralU(t);
	const Vec3 p0 = spiralStep(u), p1 = spiralStep(u + 0.004);
	const double yaw = yawOf(p1 - p0);
	const Mat4 body = mul(ruinsFrame(), translate(p0), rotate({0.0, 1.0, 0.0}, yaw));
	const Vec3 radial = normalize(Vec3{p0.x - spiralCentre().x, 0.0, p0.z - spiralCentre().z});
	const Vec3 tangent = normalize(Vec3{p1.x - p0.x, 0.0, p1.z - p0.z});
	(void)radial;
	(void)tangent;
	f.view = cam(ruinsPoint(Vec3{p0.x + 3.0, p0.y - 1.2, p0.z - 8.0}), ruinsPoint(p0 + Vec3{0.0, 1.0, 0.0}), lens(28.0));
	f.subjects = {transformPoint(body, {0.0, 1.25, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	f.feet = explorerFeet(spiralPose(t), body);   // (the sliding-feet check: Film::feetAt)
	night(f);
	if (content) f.content = worldText(f, t) + explorerText(spiralPose(t), body, 0.014);
}
inline double corridorS(double t) { return lerp(0.02, 0.30, clamp01((t - 88.0) / 10.0)); }
inline double lampsLit(double s) {   // the lamps wake ahead of the explorer, one by one
	double n = 0.0;
	for (int k = 0; k < kCorridorLamps; ++k) n += clamp01((s + 0.20 - corridorLampAt(k)) / 0.03);
	return n;
}
inline Mat4 corridorBody(double s) { return mul(ruinsFrame(), translate(corridorPoint(s)), rotate({0.0, 1.0, 0.0}, yawOf(corridorHeading(s)))); }
inline void shotCorridor(double t, Frame &f, bool content) {
	// inside the rock: behind the explorer, the old lamps lighting one by one along the curving corridor ahead
	const double s = corridorS(t);
	const Vec3 eye = corridorPoint(std::max(0.0, s - 0.07)) + Vec3{0.25, 1.55, 0.0};
	f.view = cam(ruinsPoint(eye), ruinsPoint(corridorPoint(std::min(1.0, s + 0.18)) + Vec3{0.0, 1.4, 0.0}), lens(32.0));
	f.subjects = {transformPoint(corridorBody(s), {0.0, 1.25, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	f.feet = explorerFeet(walkPose(s * kCorridorLength / 0.84, t), corridorBody(s));   // (the sliding-feet check: Film::feetAt)
	inside(f, 520.0);
	f.mist = true;
	f.haze = nature::Mist{ruinsPoint(corridorPoint(s)), 30.0, 0.012, {0.9, 0.97, 1.1}, 0.003, 0.5};
	if (content) {
		ExplorerPose p = walkPose(s * kCorridorLength / 0.84, t);
		p.staffGlow = 0.35;
		f.content = corridorText(lampsLit(s), t) + ruinsStatic() + explorerText(p, corridorBody(s), 0.012);
	}
}
constexpr double kWritingS = 0.32;
inline void writingPanel(Vec3 &centre, Vec3 &right, Vec3 &up) {
	const Vec3 c = corridorPoint(kWritingS), h = corridorHeading(kWritingS);
	const Vec3 side = normalize(cross(Vec3{0.0, 1.0, 0.0}, h));
	centre = ruinsPoint(c + side * 1.47 + Vec3{0.0, 1.55, 0.0});
	right = ruinsDir(h);
	up = ruinsDir({0.0, 1.0, 0.0});
}
inline void shotWriting(double t, Frame &f, bool content) {
	// the explorer turns to the wall and lays a hand on it: the carved spiral wakes, glowing outwards from the hand
	Vec3 c, r, up;
	writingPanel(c, r, up);
	const Mat4 body = mul(corridorBody(kWritingS), rotate({0.0, 1.0, 0.0}, -80.0 * smoothstep(98.0, 99.5, t)));
	ExplorerPose p = acted(standPose(t), t);   // (reaching to the wall)
	p.headPitch = -4.0;
	p.headYaw = 6.0;
	p.lidTop = 0.3 * (1.0 - smoothstep(101.0, 101.6, t));
	f.view = cam(c + r * 1.9 + ruinsDir(cross(corridorHeading(kWritingS), Vec3{0.0, 1.0, 0.0})) * 2.0 + up * -0.15, c + r * 0.15 + up * -0.25, lens(40.0));
	f.subjects = {c};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 520.0);
	if (content) {
		const double reveal = smoothstep(100.4, 105.0, t);
		f.content = corridorText(kCorridorLamps, t) + explorerText(p, body, 0.010) + writingText(c, r, up, 1.1, 3, 1.0, 0.0, nomaiBlue()) + writingText(c + ruinsDir(cross(corridorHeading(kWritingS), Vec3{0.0, 1.0, 0.0})) * 0.004, r, up, 1.1, 3, reveal, 2.2, nomaiBlue());
	}
}
inline void shotRecording(double t, Frame &f, bool content) {
	// the staff reads it: spirals of light in the air, then a broken image of the expedition that came before - three of them at a shaft, the signal rising from below -
	// and it stops
	const Mat4 body = corridorBody(kWritingS + 0.03);
	ExplorerPose p = standPose(t);
	p.handR = lerp(Vec3{-0.25, 1.15, 0.14}, Vec3{-0.18, 1.32, 0.40}, smoothstep(106.0, 107.5, t));
	p.staffTip = p.handR + Vec3{0.03, 0.62, 0.22};
	const bool on = t > 107.5 && t < 117.6;
	p.staffGlow = on ? 0.9 : 0.35;
	p.headPitch = -2.0;
	const Vec3 holo = transformPoint(body, {0.0, 1.25, 1.15});
	f.view = cam(transformPoint(body, {1.6, 1.45, 2.6}), transformPoint(body, {-0.1, 1.25, 0.7}), lens(35.0));
	f.subjects = {holo};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 520.0);
	if (content) {
		std::string h;
		if (on) {
			const Vec3 r = normalize(transformPoint(body, {1.0, 0.0, 0.0}) - transformPoint(body, {0.0, 0.0, 0.0})), up = upAt(kRuinsX, canyonZ(kRuinsX));
			const double glyphs = 1.0 - smoothstep(110.0, 111.0, t);
			if (glyphs > 0.0) {
				for (int k = 0; k < 3; ++k)
					h += writingText(holo + r * (0.35 * (k - 1)) + up * (0.12 * std::sin(t + k)), r, up, 0.30, 10 + k, smoothstep(107.5, 109.0, t), 1.6 * glyphs, nomaiBlue());
			}
			const double memory = smoothstep(110.5, 111.2, t);
			if (memory > 0.0) {
				for (int k = 0; k < 3; ++k) {
					ExplorerPose q = standPose(t);
					q.staff = k == 1 ? -1 : 0;
					const double a = -kPi / 2.0 + 2.0 * kPi * k / 3.0;
					const Mat4 m = mul(translate(holo + r * (0.22 * std::cos(a)) + Vec3{0.0, -0.32, 0.0} + normalize(cross(r, up)) * (0.22 * std::sin(a))), alignY({0.0, 0.0, 0.0}, up, r * -1.0), rotate({0.0, 1.0, 0.0}, 90.0 - a * 180.0 / kPi), scale(0.22));
					h += holoFigureText(q, m, nomaiBlue(), 1.4 * memory, t, 20 + k, 0.30 + 0.4 * smoothstep(116.0, 117.5, t));
				}
				// the shaft and the signal rising from it: rings of light climbing out of the ground
				for (int k = 0; k < 4; ++k) {
					const double ph = std::fmod(t * 0.6 + k * 0.25, 1.0);
					h += glowText(meshText(torus(0.05 + 0.12 * ph, 0.004, 24, 3), alignY(holo + Vec3{0.0, -0.32 + 0.35 * ph, 0.0}, up)), nomaiBlue(), 1.5 * (1.0 - ph) * memory);
				}
			}
			h += motesText(holo, {0.45, 0.6, 0.3}, 40, nomaiBlue(), 2.0, t, 31, 0.006);
		}
		f.content = corridorText(kCorridorLamps, t) + explorerText(p, body, 0.012) + h;
	}
}
// III Observatory -------------------------------------------------------------------------------------------------------------------------------------------------
inline Mat4 platformBody(double drop, double yaw) { return mul(ruinsFrame(), translate(rotundaCentre() + Vec3{0.0, -drop, 0.0}), rotate({0.0, 1.0, 0.0}, yaw)); }
inline double elevatorDrop(double t) {
	if (t < 126.0) return 0.0;
	if (t < 136.0) return 60.0 * std::pow(smoothstep(126.0, 136.0, t), 1.6);
	return lerp(kShaftDepth - 6.0, landingDrop(), smoothstep(136.0, 145.5, t));
}
inline double elevatorWalkU(double t) { return smoothstep(120.0, 123.0, t); }   // from the corridor's end to the platform's middle
inline void shotElevator(double t, Frame &f, bool content) {
	// the round room: onto the platform; the explorer kneels and touches the ring of glyphs, which wakes; the floor begins to sink
	const Vec3 start = corridorPoint(0.96), centre = rotundaCentre();
	const double u = elevatorWalkU(t);
	const Vec3 at = lerp(start, centre, u);
	const double yaw = yawOf(centre - start);
	const double drop = elevatorDrop(t);
	const Mat4 body = mul(ruinsFrame(), translate(at + Vec3{0.0, -drop, 0.0}), rotate({0.0, 1.0, 0.0}, yaw));
	ExplorerPose p = walkPose(length(at - start) / 0.84, t);   // (and when it stops, the feet stay where they came down: a stand pose snapped them together)
	p = acted(p, t);   // (kneeling to touch the ring of glyphs)
	const double glow = smoothstep(124.0, 124.8, t);
	f.view = cam(ruinsPoint(centre + Vec3{-3.6, 2.3, 3.0}), ruinsPoint(at + Vec3{0.0, 0.9 - drop, 0.0}), lens(24.0));
	f.subjects = {transformPoint(body, {0.0, 1.25, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	f.feet = explorerFeet(p, mul(ruinsFrame(), translate(at), rotate({0.0, 1.0, 0.0}, yaw)));   // (the sliding-feet check: on the platform, which sinks - so in its own space)
	inside(f, 520.0);
	if (content) f.content = corridorText(kCorridorLamps, t) + rotundaText(drop, glow, t) + explorerText(p, body, 0.012);
}
inline void shotIntoDark(double t, Frame &f, bool content) {
	// from the dome straight down the shaft: the platform sinks, the explorer on it smaller and smaller, into the dark
	const double drop = elevatorDrop(t);
	const Vec3 c = rotundaCentre();
	f.view = cam(ruinsPoint(c + Vec3{0.25, 6.2, 0.0}), ruinsPoint(c + Vec3{0.0, -drop - 10.0, 0.0}), lens(32.0));
	f.subjects = {ruinsPoint(c + Vec3{0.0, -drop, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 600.0);
	if (content) {
		ExplorerPose p = standPose(t);
		p.headPitch = 25.0;
		p.staffGlow = 0.5;
		f.content = rotundaText(drop, 1.0, t) + explorerText(p, platformBody(drop, 30.0), 0.016);
	}
}
inline void shotHall(double t, Frame &f, bool content) {
	// the platform comes out of the roof of a vast dark hall: a small light descending past floating stone rings to the floor far below
	const double drop = elevatorDrop(t);
	const Vec3 fl = observatoryFloor();
	f.view = cam(ruinsPoint(fl + Vec3{18.0, 1.6, -6.0}), ruinsPoint(rotundaCentre() + Vec3{0.0, -drop - 4.0, 0.0} + Vec3{0.0, -40.0 * (1.0 - smoothstep(136.0, 146.0, t)), 0.0}), lens(16.0));
	f.subjects = {ruinsPoint(rotundaCentre() + Vec3{0.0, -drop, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 1200.0);
	f.mist = true;
	f.haze = nature::Mist{ruinsPoint(fl + Vec3{0.0, 30.0, 0.0}), 60.0, 0.0025, {0.9, 0.97, 1.1}, 0.001, 0.6};
	if (content) {
		ExplorerPose p = standPose(t);
		p.staffGlow = 1.0;
		f.content = observatoryText(t, 0.04, 0.0, 0.30) + rotundaText(drop, 1.0, t) + explorerText(p, platformBody(drop, 30.0), 0.016);
	}
}
inline Vec3 landing() { return observatoryFloor() + Vec3{0.0, 0.0, kObservatoryOffset}; }
inline Vec3 towardMachine(double dist) { return landing() + normalize(Vec3{0.0, 0.0, -1.0}) * dist; }
inline double staffWalkDist(double t) { return clamp01((t - 146.5) / 7.5) * 5.4 + 1.8; }   // off the platform towards the machine
inline void shotStaffReacts(double t, Frame &f, bool content) {
	// off the platform towards the machine; the staff brightens, and light runs out through the floor from the middle
	const double dist = staffWalkDist(t);
	const Vec3 at = towardMachine(dist);
	const Mat4 body = mul(ruinsFrame(), translate(at), rotate({0.0, 1.0, 0.0}, 180.0));
	ExplorerPose p = walkPose(dist / 0.84, t, t < 154.0 ? 1.0 : 0.0);
	p.staffGlow = 0.3 + 0.7 * smoothstep(147.0, 151.0, t);
	p.handR = {-0.24, 1.25, 0.24};
	f.view = cam(ruinsPoint(at + Vec3{3.6, 1.2, 1.5}), ruinsPoint(at + Vec3{0.0, 1.1, -1.5}), lens(35.0));
	f.subjects = {transformPoint(body, {0.0, 1.25, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	f.feet = explorerFeet(p, body);   // (the sliding-feet check: Film::feetAt)
	inside(f, 950.0);
	f.mist = true;
	f.haze = nature::Mist{ruinsPoint(at + Vec3{0.0, 20.0, 0.0}), 55.0, 0.003, {0.9, 0.97, 1.1}, 0.001, 0.6};
	if (content) f.content = observatoryText(t, 0.04 + 0.46 * smoothstep(149.0, 154.0, t), 0.0, 0.15) + rotundaText(landingDrop(), 1.0, t) + explorerText(p, body, 0.012);
}
inline Vec3 standPlace() { return towardMachine(7.2); }
inline Mat4 standBody(double yaw = 180.0) { return mul(ruinsFrame(), translate(standPlace()), rotate({0.0, 1.0, 0.0}, yaw)); }
inline void shotAwakening(double t, Frame &f, bool content) {
	// one ring turns, then another, then dozens; dust sifts down through the light; the camera rises
	const double u = smoothstep(154.0, 166.0, t);
	ExplorerPose p = standPose(t);
	p.headPitch = -32.0 * smoothstep(155.0, 157.0, t);
	p.staffGlow = 1.0;
	p.ears = 0.5;
	const Vec3 sp = standPlace();
	f.view = cam(ruinsPoint(sp + Vec3{5.0 + 4.0 * u, 1.4 + 22.0 * u, 7.0 + 6.0 * u}), ruinsPoint(lerp(sp + Vec3{0.0, 3.0, 0.0}, machineCentre(), 0.55 + 0.25 * u)), lens(24.0));
	f.subjects = {ruinsPoint(machineCentre())};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 520.0);
	f.mist = true;
	f.haze = nature::Mist{ruinsPoint(sp + Vec3{0.0, 25.0, 0.0}), 60.0, 0.004, {0.9, 0.97, 1.1}, 0.001, 0.6};
	if (content) {
		const double rings = smoothstep(155.0, 165.0, t);
		f.content = observatoryText(t, 0.5 + 0.5 * u, rings, 0.1 + 0.5 * u) + rotundaText(landingDrop(), 1.0, t) + explorerText(p, standBody(), 0.014);
		// dust falling from the roof, slowly
		std::string dust;
		for (int k = 0; k < 160; ++k) {
			const double fall = std::fmod(hash01(k, 801) + (t - 154.0) * 0.05 * (0.6 + hash01(k, 802)), 1.0);
			const Vec3 q = sp + Vec3{30.0 * (hash01(k, 803) - 0.5), 60.0 * (1.0 - fall), 30.0 * (hash01(k, 804) - 0.5)};
			dust += meshText(uvSphere(0.03, 5, 3), translate(ruinsPoint(q)));
		}
		f.content += attr(diffuseMaterial({0.70, 0.66, 0.58}), dust);
	}
}
inline void shotStarMap(double t, Frame &f, bool content) {
	// the star map: thousands of points of light round the explorer; the camera circles slowly
	const Vec3 sp = standPlace();
	const double a = 0.6 + 0.9 * smoothstep(166.0, 178.0, t);
	ExplorerPose p = standPose(t);
	p.headYaw = 30.0 * std::sin((t - 166.0) * 0.35);
	p.headPitch = -14.0 + 6.0 * std::sin((t - 166.0) * 0.27);
	p.staffGlow = 0.9;
	p.ears = 0.3;
	f.view = cam(ruinsPoint(sp + Vec3{4.2 * std::sin(a), 1.7, 4.2 * std::cos(a)}), ruinsPoint(sp + Vec3{0.0, 1.5, 0.0}), lens(35.0));
	f.subjects = {transformPoint(standBody(), {0.0, 1.25, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 520.0);
	if (content) {
		const Vec3 centre = ruinsPoint(sp + Vec3{0.0, 1.6, 0.0});
		f.content = observatoryText(t, 0.6, 1.0, 0.4) + explorerText(p, standBody(), 0.012) + starMapText(centre, smoothstep(166.0, 171.0, t), 0.0, false, t);
	}
}
inline void shotFlashingStar(double t, Frame &f, bool content) {
	// over the shoulder: among them, one star flashing, in time with the signal
	const Vec3 sp = standPlace();
	const Vec3 centre = ruinsPoint(sp + Vec3{0.0, 1.6, 0.0});
	const Vec3 star = centre + signalStarLocal();
	ExplorerPose p = standPose(t);
	p.headYaw = 22.0;
	p.headPitch = -24.0;
	p = acted(p, t);   // (pointing up at it)
	p.staffGlow = 0.6 + 0.4 * pulseAt(t);
	const Vec3 head = transformPoint(standBody(), {0.0, 1.75, 0.0});
	const Vec3 upv = upAt(kRuinsX, canyonZ(kRuinsX));
	const Vec3 side = normalize(cross(star - head, upv));
	const Vec3 back = normalize((head - star) - upv * dot(head - star, upv));   // (level: behind the head, not down through the floor)
	f.view = cam(head + back * 2.6 + side * 0.7 + upv * 0.05, lerp(head, star, 0.55), lens(35.0), 0.004);
	f.subjects = {star};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 520.0);
	if (content) f.content = observatoryText(t, 0.6, 1.0, 0.4) + explorerText(p, standBody(), 0.012) + starMapText(centre, 1.0, pulseAt(t), false, t);
}
inline void shotGone(double t, Frame &f, bool content) {
	// the star goes out; the signal goes on. The lower eyes widen first, then the third
	const Vec3 sp = standPlace();
	const Vec3 centre = ruinsPoint(sp + Vec3{0.0, 1.6, 0.0});
	ExplorerPose p = standPose(t);
	p.headYaw = 22.0;
	p.headPitch = -12.0;
	p.lidL = p.lidR = 0.25 * (1.0 - smoothstep(186.0, 186.4, t));
	p.lidTop = 0.45 * (1.0 - smoothstep(186.7, 187.2, t));
	p.gazePitch = -6.0;
	p.ears = 0.4 - 0.7 * smoothstep(187.0, 189.0, t);
	p.staffGlow = 0.4 + 0.5 * pulseAt(t);
	const ExplorerBody b = explorerBody(p);
	const Mat4 head = mul(standBody(), b.head);
	const Vec3 face = transformPoint(head, {0.0, 0.11, 0.10});
	const Vec3 out = normalize(transformPoint(standBody(), {0.0, 0.0, 1.0}) - transformPoint(standBody(), {0.0, 0.0, 0.0}));
	f.view = cam(face + out * 1.0 + upAt(kRuinsX, canyonZ(kRuinsX)) * 0.05, face, lens(85.0), 0.004);
	f.subjects = {face};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 600.0);
	if (content) f.content = observatoryText(t, 0.6, 1.0, 0.4) + explorerText(p, standBody(), 0.008) + starMapText(centre, 1.0, t < 185.5 ? pulseAt(t) : 0.0, t >= 185.5, t);
}
// IV The message --------------------------------------------------------------------------------------------------------------------------------------------------
inline Vec3 consolePlace() { return observatoryFloor() + Vec3{0.0, 0.0, 19.0}; }   // a stone lectern on the floor, before the machine
inline std::string consoleText(double glow, double t) {
	const Mat4 F = ruinsFrame();
	const Vec3 c = consolePlace();
	std::string out = attr(paintedMaterial(15), meshText(columnMesh(0.30, 1.05, 190), mul(F, translate(c))));
	out += attr(darkStoneMaterial(), meshText(roundedBox(0.7, 0.08, 0.5, 0.03, 2), mul(F, translate(c + Vec3{0.0, 1.08, 0.0}), rotate({1.0, 0.0, 0.0}, 20.0))));
	out += glowText(meshText(torus(0.14, 0.008, 32, 3), mul(F, translate(c + Vec3{0.0, 1.125, 0.0}), rotate({1.0, 0.0, 0.0}, 20.0))), nomaiBlue(), 0.5 + 3.0 * glow * (0.9 + 0.1 * std::sin(t * 4.0)));
	return out;
}
inline Mat4 consoleBody() { return mul(ruinsFrame(), translate(consolePlace() + Vec3{0.0, 0.0, 0.85}), rotate({0.0, 1.0, 0.0}, 180.0)); }
inline void shotReconstruction(double t, Frame &f, bool content) {
	// to the lectern at the machine's foot; a hand on it; the hall dims, and an orange glow gathers in the air
	const double walkU = smoothstep(190.0, 193.5, t);
	const Vec3 from = standPlace(), to = consolePlace() + Vec3{0.0, 0.0, 0.85};
	const Vec3 at = lerp(from, to, walkU);
	const Mat4 body = mul(ruinsFrame(), translate(at), rotate({0.0, 1.0, 0.0}, 180.0));
	ExplorerPose p = walkU < 1.0 ? walkPose(length(at - from) / 0.84, t) : standPose(t);
	const double palm = smoothstep(194.0, 195.0, t);
	p.handL = lerp(p.handL, Vec3{0.06, 1.10, 0.52}, palm);
	p.headPitch = 10.0;
	const double dim = smoothstep(195.0, 198.0, t);
	f.view = cam(ruinsPoint(to + Vec3{-4.5, 2.0, 4.0}), ruinsPoint(to + Vec3{0.0, 1.3, -1.6}), lens(35.0));
	f.subjects = {transformPoint(body, {0.0, 1.25, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 560.0);
	if (content) {
		f.content = observatoryText(t, lerp(1.0, 0.25, dim), lerp(1.0, 0.35, dim), lerp(0.5, 0.15, dim)) + consoleText(palm, t) + explorerText(p, body, 0.012);
		f.content += motesText(ruinsPoint(to + Vec3{0.0, 1.6, -2.0}), {1.2, 1.4, 0.6}, static_cast<int>(60 * dim), remoteOrange(), 1.5 * dim, t, 41, 0.008);
	}
}
// The remote memories, one after another: (from, to, which)
inline int memoryAt(double t) {
	static const double starts[] = {198.0, 203.0, 208.0, 212.0, 216.0, 220.0, 224.0};
	for (int k = 0; k < 6; ++k)
		if (t >= starts[k] && t < starts[k + 1]) return k;
	return -1;
}
inline std::string memoryText(double t, const Mat4 &stage, Vec3 up) {
	const int k = memoryAt(t);
	if (k < 0) return std::string();
	static const double starts[] = {198.0, 203.0, 208.0, 212.0, 216.0, 220.0, 224.0};
	const double local = t - starts[k], len = starts[k + 1] - starts[k];
	const double in = smoothstep(0.0, 0.5, local) * (1.0 - smoothstep(len - 0.4, len, local));
	const double s = 1.6 * in;
	const Vec3 o = transformPoint(stage, {0.0, 0.0, 0.0});
	std::string out;
	const auto figure = [&](const ExplorerPose &q, Vec3 at, double yaw, double size, int seed) {
		return holoFigureText(q, mul(stage, translate(at), rotate({0.0, 1.0, 0.0}, yaw), scale(size)), remoteOrange(), s, t, seed, 0.3);
	};
	switch (k) {
		case 0: {   // another Nomai under a strange sky: two suns, a far horizon
			ExplorerPose q = standPose(t);
			q.headPitch = -20.0;
			out += figure(q, {0.0, 0.0, 0.0}, 20.0, 0.75, 50);
			out += emissiveSphere(transformPoint(stage, {-0.9, 2.2, -1.4}), 0.16, remoteOrange() * (4.0 * in)) + emissiveSphere(transformPoint(stage, {-0.5, 1.9, -1.4}), 0.08, remoteOrange() * (4.0 * in));
			out += motesText(transformPoint(stage, {0.0, 0.0, -1.2}), {1.6, 0.05, 0.1}, 50, remoteOrange(), 1.2 * in, t, 51, 0.008);
			break;
		}
		case 1: {   // a campfire, three seated round it
			for (int i = 0; i < 3; ++i) {
				ExplorerPose q = standPose(t);
				q.sit = 1.0;
				q.staff = 0;
				q.handL = {0.2, 0.55, 0.3};
				q.handR = {-0.2, 0.55, 0.3};
				const double a = 2.0 * kPi * i / 3.0 + 0.6;
				out += figure(q, {0.75 * std::cos(a), 0.0, 0.75 * std::sin(a)}, -90.0 - a * 180.0 / kPi, 0.6, 52 + i);
			}
			for (int i = 0; i < 4; ++i) {
				const double h = 0.28 + 0.1 * std::sin(t * 7.0 + i * 1.7);
				out += glowText(meshText(lathe({{0.0, 0.0}, {0.06, 0.05}, {0.02, h * 0.8}, {0.0, h}}, 8), mul(stage, translate(0.05 * std::cos(i * 1.6), 0.0, 0.05 * std::sin(i * 1.6)))), remoteOrange(), 3.0 * in);
			}
			break;
		}
		case 2: {   // children, walking beside a grown one
			const double w = local * 1.2;
			out += figure(walkPose(w, t), {-0.3 + 0.1 * local, 0.0, 0.0}, 90.0, 0.7, 55);
			out += figure(walkPose(w * 1.6, t), {0.25 + 0.1 * local, 0.0, 0.2}, 90.0, 0.36, 56);
			out += figure(walkPose(w * 1.7 + 0.5, t), {0.55 + 0.1 * local, 0.0, -0.15}, 90.0, 0.32, 57);
			break;
		}
		case 3: {   // a star map, and a ship among the stars
			out += starMapText(transformPoint(stage, {0.0, 1.2, 0.0}), 1.0, 0.0, true, t, 0.16);
			const Mat4 ship = mul(stage, translate(0.4 * std::sin(local * 0.5), 1.0, 0.2), rotate({0.0, 1.0, 0.0}, 60.0 + 10.0 * local), scale(0.08));
			std::string ribs;
			for (int i = 1; i < 8; ++i) ribs += meshText(torus(1.9 * std::pow(std::sin(kPi * (0.08 + 0.84 * i / 8.0)), 0.55), 0.06, 24, 3), mul(ship, translate(0.0, 0.0, -3.6 + 7.2 * i / 8.0), rotate({1.0, 0.0, 0.0}, 90.0)));
			out += glowText(ribs, remoteOrange(), 2.5 * in);
			break;
		}
		case 4: {   // three eyes, looking up at the sky
			ExplorerPose q = standPose(t);
			q.headPitch = -35.0;
			out += holoFigureText(q, mul(stage, translate(0.0, -2.5, -0.4), scale(2.2)), remoteOrange(), s, t, 58, 0.22);
			// (the three eyes glowing in its mask's windows)
			const ExplorerBody qb = explorerBody(q);
			for (int e = 0; e < 3; ++e) out += emissiveSphere(transformPoint(mul(stage, translate(0.0, -2.5, -0.4), scale(2.2), qb.head), eyePlace(e).c), 0.035, remoteOrange() * (5.0 * in));
			break;
		}
		case 5: {   // a hand reaching for the recorder, close - and the transmission ends
			ExplorerPose q = standPose(t);
			q.handL = {0.10, 1.45, 0.62 + 0.10 * smoothstep(0.0, 3.0, local)};
			out += holoFigureText(q, mul(stage, translate(0.2, -2.6, -1.0), scale(1.9)), remoteOrange(), s * (local > 3.6 ? 0.0 : 1.0), t, 59, 0.35);
			break;
		}
	}
	out += motesText(o + up * 1.0, {1.0, 1.4, 0.5}, 50, remoteOrange(), 1.2 * in, t, 60 + k, 0.006);
	return out;
}
inline void shotTransmission(double t, Frame &f, bool content) {
	// the old transmission, rebuilt in broken orange light before the explorer
	const Mat4 body = consoleBody();
	ExplorerPose p = standPose(t);
	p.handL = {0.06, 1.10, 0.52};
	p.headPitch = -6.0;
	p.lidTop = 0.15 * std::sin(t * 0.3);
	const Mat4 stage = mul(ruinsFrame(), translate(consolePlace() + Vec3{0.0, 0.0, -2.4}), rotate({0.0, 1.0, 0.0}, 180.0));
	const double u = smoothstep(198.0, 224.0, t);
	const Vec3 sp = consolePlace() + Vec3{0.0, 0.0, 0.85};
	f.view = cam(ruinsPoint(sp + Vec3{2.3 - 0.6 * u, 1.75, 1.3 + 0.5 * u}), ruinsPoint(consolePlace() + Vec3{-0.4, 1.2, -2.4}), lens(32.0), 0.01);
	f.subjects = {transformPoint(stage, {0.0, 1.0, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 600.0);
	if (content) f.content = observatoryText(t, 0.2, 0.3, 0.12) + consoleText(1.0, t) + explorerText(p, body, 0.012) + memoryText(t, stage, upAt(kRuinsX, canyonZ(kRuinsX)));
}
inline void shotSilence(double t, Frame &f, bool content) {
	// silence: far off, the explorer alone in the dark hall
	const Mat4 body = consoleBody();
	ExplorerPose p = standPose(t);
	p.handL = lerp(Vec3{0.06, 1.10, 0.52}, Vec3{0.21, 0.90, 0.06}, smoothstep(225.0, 227.0, t));
	p.headPitch = 12.0;
	p.ears = -0.5;
	f.view = cam(ruinsPoint(consolePlace() + Vec3{-26.0, 6.0, 24.0}), ruinsPoint(consolePlace() + Vec3{0.0, 3.0, 0.0}), lens(24.0));
	f.subjects = {transformPoint(body, {0.0, 1.25, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 900.0);
	f.mist = true;
	f.haze = nature::Mist{ruinsPoint(consolePlace() + Vec3{0.0, 20.0, 0.0}), 60.0, 0.003, {0.9, 0.97, 1.1}, 0.001, 0.6};
	if (content) f.content = observatoryText(t, 0.2, 0.3, 0.12) + consoleText(0.3, t) + explorerText(p, body, 0.016);
}
inline void shotStillness(double t, Frame &f, bool content) {
	// close on the mask, very slowly closer: behind its windows the three eyes hold the place where the star was. Stillness: one slow blink, the third eye last
	const Mat4 body = consoleBody();
	ExplorerPose p = standPose(t);
	p.headPitch = -5.0 - 3.0 * smoothstep(232.0, 242.0, t);
	p.headYaw = 8.0;
	p.lidL = p.lidR = 0.12 + 0.88 * (1.0 - smoothstep(0.0, 0.14, std::abs(t - 238.5)));
	p.lidTop = std::max(0.30, 1.0 - smoothstep(0.0, 0.14, std::abs(t - 238.85)));
	p.gazePitch = -8.0;
	p.ears = -0.45;
	const ExplorerBody b = explorerBody(p);
	const Mat4 head = mul(body, b.head);
	const Vec3 face = transformPoint(head, {0.0, 0.10, 0.10});
	const Vec3 out = normalize(transformPoint(body, {0.0, 0.0, 1.0}) - transformPoint(body, {0.0, 0.0, 0.0}));
	f.view = cam(face + out * (2.0 - 0.45 * smoothstep(230.0, 244.0, t)) + upAt(kRuinsX, canyonZ(kRuinsX)) * -0.05, face + upAt(kRuinsX, canyonZ(kRuinsX)) * 0.03, lens(85.0), 0.004);
	f.subjects = {face};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 650.0);
	if (content) {
		const Vec3 centre = ruinsPoint(standPlace() + Vec3{0.0, 1.6, 0.0});
		f.content = observatoryText(t, 0.25, 0.35, 0.15) + consoleText(0.4, t) + explorerText(p, body, 0.008) + starMapText(centre, 0.35, 0.0, true, t);
		// (the faint glow of the map before the face: enough to find the eyes in the mask's windows)
		const Vec3 lp = face + out * 0.6 + upAt(kRuinsX, canyonZ(kRuinsX)) * 0.15;
		char buf[200];
		std::snprintf(buf, sizeof buf, "LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ 0.060 0.085 0.120 ]\n", -lp.x, lp.y, lp.z);
		f.content += buf;
	}
}
inline void shotAnswer(double t, Frame &f, bool content) {
	// a hand on the lectern again; the explorer records an answer, and the machine sends it: a column of light up through the hall, and a ring going out
	const Mat4 body = consoleBody();
	ExplorerPose p = standPose(t);
	p.headPitch = 10.0;
	p = acted(p, t);   // (the hand on the lectern, then looking up)
	p.lidTop = 0.1;
	const double send = smoothstep(246.0, 247.0, t);
	// (low and wide, from behind the lectern's side: the explorer at the foot of the frame, the column going up through the rest of it)
	f.view = cam(ruinsPoint(consolePlace() + Vec3{4.5, 1.1, 10.0}), ruinsPoint(consolePlace() + Vec3{0.0, 8.0, -4.0}), lens(16.0));
	f.subjects = {transformPoint(body, {0.0, 1.25, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	inside(f, 600.0);
	if (content) {
		f.content = observatoryText(t, 0.4 + 0.6 * send, 0.35 + 0.65 * send, 0.15 + 0.85 * send) + consoleText(1.0, t) + explorerText(p, body, 0.012);
		if (send > 0.0) {
			const Mat4 F = ruinsFrame();
			const double h = 80.0 * smoothstep(246.0, 248.5, t);
			f.content += glowText(meshText(capsule(0.35, std::max(0.1, h), 16, 3), mul(F, translate(machineCentre()))), nomaiBlue(), 6.0 * send);
			const double ring = std::fmod(t - 246.0, 2.5) / 2.5;
			f.content += glowText(meshText(torus(4.0 + 50.0 * ring, 0.12, 128, 4), mul(F, translate(machineCentre()))), nomaiBlue(), 4.0 * (1.0 - ring) * send);
		}
	}
}
// V Ending --------------------------------------------------------------------------------------------------------------------------------------------------------
inline rig::PathWalk dawnWalk() { return walkPath({15.6, 0.0, -121.0}, {8.0, 0.0, -112.0}, 250.5, 0.62); }
inline void shotDawn(double t, Frame &f, bool content) {
	// outside: the night ending; the explorer steps off the floating bridge onto the rim in the first warm light
	const Walk w = walkOver(dawnWalk(), t);
	f.dawn = lerp(0.95, 0.97, smoothstep(250.0, 257.0, t));
	f.giantGlow = 0.10;
	f.iso = 420.0;
	f.warmth = 0.10;
	f.view = cam(groundPoint(-2.0, -104.0, 1.2), groundPoint(14.0, -122.0, 2.2), lens(28.0));
	f.subjects = {transformPoint(w.frame, {0.0, 1.25, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	f.feet = explorerFeet(walkPose(w.walk, t, w.moving ? 1.0 : 0.0), w.frame);   // (the sliding-feet check: Film::feetAt)
	if (content) {
		ExplorerPose p = walkPose(w.walk, t, w.moving ? 1.0 : 0.0);
		f.content = worldText(f, t) + explorerText(p, w.frame, 0.014);
	}
}
inline Mat4 campfireBody() { return mul(campfireFrame(), translate(0.0, 0.0, 1.05), rotate({0.0, 1.0, 0.0}, 180.0)); }
inline ExplorerPose sittingPose(double t) {
	ExplorerPose p;
	p.time = t;
	p.sit = 1.0;
	p.staff = 0;
	p.handL = {0.20, 0.55, 0.30};
	p.handR = {-0.20, 0.55, 0.30};
	p.headPitch = 10.0;
	p.wind = {0.02, 0.0, -0.01};
	blink(p, t);
	return p;
}
// The sceptre laid on the ground beside the explorer (campfire body space: its grip there, the shaft lying along the ground).
inline Mat4 sceptreRest() { return mul(campfireBody(), translate(0.55, 0.03, 0.45), rotate({0.0, 1.0, 0.0}, -20.0), rotate({0.0, 0.0, 1.0}, -88.0)); }
inline void shotCampfire(double t, Frame &f, bool content) {
	// close behind: the explorer sits at the small fire, the sceptre laid beside it, and looks into the flames, then up at the sky
	const Mat4 F = campfireFrame();
	ExplorerPose p = sittingPose(t);
	p.headPitch = lerp(18.0, -12.0, smoothstep(261.0, 264.5, t));
	f.dawn = lerp(0.97, 0.99, smoothstep(257.0, 266.0, t));
	f.giantGlow = 0.10;
	f.iso = 420.0;
	f.warmth = 0.06;
	// (behind and to the left, so that the flames show past the explorer's shoulder: straight behind, the body hid them)
	f.view = cam(transformPoint(F, {-2.0, 1.30, 3.0}), transformPoint(F, {0.15, 0.55, -0.3}), lens(40.0), 0.01);
	f.subjects = {transformPoint(F, {0.0, 0.4, 0.0})};   // (what the shot is about: the film doctor's framing checks)
	if (content) {
		f.content = worldText(f, t, 60.0) + campfireText(t, 1.0) + explorerText(p, campfireBody(), 0.007) + sceptreText(sceptreRest(), 0.15);
	}
}
inline void shotPullBack(double t, Frame &f, bool content) {
	// the long pull back: the campfire, the ruins, the canyon, the moon, the giant, the stars - the fire a single orange point, then nothing but space
	const Mat4 F = campfireFrame();
	const Vec3 fire = transformPoint(F, {0.0, 0.4, 0.0});
	const double u = clamp01((t - 266.0) / 25.0);
	const double d = 4.0 * std::pow(180000.0 / 4.0, std::pow(u, 1.35));
	const Vec3 up = upAt(campfireArc().x, campfireArc().y);
	const Vec3 behind = normalize(transformPoint(F, {-0.55, 0.0, 1.0}) - transformPoint(F, {0.0, 0.0, 0.0}));   // (behind and to the left, as the campfire shot: the fire past the shoulder)
	const double rise = smoothstep(0.0, 0.5, u);
	Vec3 dir = normalize(behind * (1.0 - 0.6 * rise) + up * (0.35 + 0.9 * rise));
	dir = normalize(lerp(dir, normalize(wideEye() - moonCentre()), smoothstep(0.45, 0.9, u)));
	const Vec3 base = lerp(fire, moonCentre(), smoothstep(std::log(3000.0), std::log(60000.0), std::log(d)));   // (from the fire, then from the moon's middle)
	const Vec3 eye = base + dir * d;
	const double away = smoothstep(std::log(300.0), std::log(60000.0), std::log(d));
	const Vec3 target = lerp(fire, lerp(moonCentre(), moonCentre() + giantDir() * 30000.0, smoothstep(0.7, 1.0, u)), away);
	f.view = cam(eye, target, lens(lerp(40.0, 50.0, u)));
	f.subjects = {d < 2000.0 ? fire : moonFacing(eye)};   // (what the shot is about: the film doctor's framing checks)
	f.dawn = 0.99 + 0.01 * u;
	f.giantGlow = lerp(0.10, 1.0, away);
	f.giantshine = 1.0 - away;
	f.iso = lerp(420.0, 70.0, away);
	f.warmth = lerp(0.06, -0.04, away);
	f.space = d > 400.0;
	f.ruins = d < 2500.0;
	if (content) {
		f.content = worldText(f, t, 60.0);
		if (f.space && f.ruins) f.content += ruinsStatic() + ruinsGlow(t, 0.35);
		if (d < 3000.0) {
			ExplorerPose p = sittingPose(t);
			p.headPitch = -12.0;   // (still looking up at the sky, as the campfire shot left it)
			f.content += campfireText(t, 1.0) + explorerText(p, campfireBody(), d < 30.0 ? 0.012 : 0.03);
			f.content += sceptreText(sceptreRest(), 0.15);
		}
		// the fire as a point of light that stays about a pixel and a half across as the camera leaves it
		if (d > 15.0 && d < 90000.0) {
			const double px = d * std::tan(lens(45.0) * kPi / 360.0) * 2.0 / kHeight;
			f.content += emissiveSphere(fire, 0.9 * px, Vec3{1.0, 0.50, 0.18} * (6.0 * (1.0 - smoothstep(30000.0, 90000.0, d))));
		}
	}
}
inline void shotTitle(double t, Frame &f, bool content) {
	// black; the title (laid by the captions)
	f.view = cam({0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, 40.0);
	f.iso = 0.0;
	(void)t;
	if (content) f.content = attr(diffuseMaterial({0.0, 0.0, 0.0}), meshText(uvSphere(0.1, 8, 4), translate(0.0, 0.0, 5.0)));
}

inline const std::vector<ShotDef> &shotList() {
	static const std::vector<ShotDef> shots = {
		{0.0, "Darkness and a pulse", shotDarkness},
		{9.0, "The giant and its moon", shotGiantAndMoon},
		{19.0, "The descent", shotDescent},
		{25.0, "Three eyes", shotEyes},
		{31.0, "The signal", shotSignal},
		{36.0, "The landing", shotLanding},
		{42.0, "Under the giant", shotUnderGiant},
		{50.0, "Footsteps", shotFootsteps},
		{57.0, "The ridge", shotRidge},
		{65.0, "The canyon", shotCanyonReveal},
		{75.0, "The third eye", shotThirdEye},
		{80.0, "The floating steps", shotSpiralSteps},
		{88.0, "The corridor wakes", shotCorridor},
		{98.0, "Writing on the wall", shotWriting},
		{106.0, "The recording", shotRecording},
		{120.0, "The elevator", shotElevator},
		{128.0, "Into the dark", shotIntoDark},
		{136.0, "The observatory", shotHall},
		{146.0, "The staff answers", shotStaffReacts},
		{154.0, "The hall wakes", shotAwakening},
		{166.0, "The star map", shotStarMap},
		{178.0, "The flashing star", shotFlashingStar},
		{184.0, "Gone", shotGone},
		{190.0, "The transmission rebuilt", shotReconstruction},
		{198.0, "The remote memories", shotTransmission},
		{224.0, "Silence", shotSilence},
		{230.0, "Stillness", shotStillness},
		{244.0, "The answer", shotAnswer},
		{250.0, "Dawn", shotDawn},
		{257.0, "The campfire", shotCampfire},
		{266.0, "Pulling away", shotPullBack},
		{292.0, "The title", shotTitle},
	};
	return shots;
}
inline std::vector<double> shotStarts() { return startsOf(shotList()); }
inline std::string shotNames() { return kit::shotNamesOf(shotList()); }
inline const char *shotNamesText() {
	static const std::string s = shotNames();
	return s.c_str();
}
inline Frame frameAt(double t, bool content) { return kit::momentAt(shotList(), t, content); }
// What each moment is about (Film::subjectsAt): the shots' own subjects.
inline std::vector<Vec3> subjectsAt(double t) { return frameAt(t, false).subjects; }
// The explorer's feet at each moment of the shots it walks in (Film::feetAt).
inline std::vector<Foot> feetAt(double t) { return frameAt(t, false).feet; }

// When a shot starts and ends (the next one's start), by its name.
inline void shotSpan(const std::string &name, double &from, double &to) {
	const auto &s = shotList();
	from = to = 0.0;
	for (std::size_t i = 0; i < s.size(); ++i)
		if (name == s[i].name) {
			from = s[i].start;
			to = i + 1 < s.size() ? s[i + 1].start : kDuration;
		}
}
// What the film's motion does that its soundtrack hears (Film::events): the explorer's footfalls in every shot it walks in, from the shots' own walks (so a retimed walk
// retimes its steps), panned and quietened from each moment's camera; the shuttle's touchdown; the hand on the carved wall and on the ring of glyphs.
inline std::vector<FilmEvent> events() {
	std::vector<FilmEvent> out;
	const film_events::ViewAt view = [](double t) { return frameAt(t, false).view; };
	const auto ground = [](Vec3 p) { return groundPoint(p.x, p.z); };
	double a = 0.0, b = 0.0;
	const std::pair<const char *, rig::PathWalk> walks[] = {{"Under the giant", exitWalk()}, {"Footsteps", stepsWalk()}, {"The ridge", ridgeWalk()}, {"The canyon", rimWalk()},
															 {"Dawn", dawnWalk()}};
	for (const auto &[shot, walk] : walks) {
		shotSpan(shot, a, b);
		film_events::footfalls(out, walk, a, b, 1.0, view, ground);
	}
	shotSpan("The floating steps", a, b);
	film_events::stepsAlong(out, spiralWalked, [](double t) { return ruinsPoint(spiralStep(spiralU(t))); }, a, b, 0.84, 0.5, 1.0, view);
	shotSpan("The corridor wakes", a, b);
	film_events::stepsAlong(out, [](double t) { return corridorS(t) * kCorridorLength; }, [](double t) { return ruinsPoint(corridorPoint(corridorS(t))); }, a, b, 0.84, 0.5, 1.0, view);
	shotSpan("The elevator", a, b);
	const Vec3 corridorEnd = corridorPoint(0.96), platform = rotundaCentre();
	film_events::stepsAlong(out, [=](double t) { return elevatorWalkU(t) * length(platform - corridorEnd); },
							[=](double t) { return ruinsPoint(lerp(corridorEnd, platform, elevatorWalkU(t))); }, a, b, 0.84, 0.5, 1.0, view);
	shotSpan("The staff answers", a, b);
	film_events::stepsAlong(out, staffWalkDist, [](double t) { return ruinsPoint(towardMachine(staffWalkDist(t))); }, a, b, 0.84, 0.5, 1.0, view);
	film_events::at(out, kTouchdown, EventKind::Impact, 1.0, groundPoint(kLandX, kLandZ), view, 82, 40.0);
	// the hands on stone: the gestures' contacts, heard where the explorer is
	int seed = 85;
	for (const double t : rig::contactsOf(acting())) film_events::at(out, t, EventKind::Touch, 1.0, subjectsAt(t).empty() ? Vec3{} : subjectsAt(t).front(), view, seed++);
	return film_events::sorted(out);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The scene (film_kit.h makes it from a moment)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr kit::FilmFormat kFormat{kSamples, kWidth, kHeight};
inline std::string lightOf(const Frame &f) {
	if (f.interior) return "LightSource \"infinite\" \"rgb L\" [ 0.0002 0.0002 0.0003 ]\n";
	return starsText(f.starLight) + sunText(f.dawn) + giantshineText(f.giantshine * (1.0 - smoothstep(0.5, 0.9, f.dawn)));
}
inline std::string headerAt(double t) { return kit::headerText(frameAt(t, false), kFormat); }
inline std::string sceneAt(double t) {
	Frame f = frameAt(t, true);
	if (f.iso > 1.0) f.light = lightOf(f);
	return kit::sceneText(f, kFormat);
}

// The title and the credit: pbrt_scenes/film_assets/nomai-signal/captions.txt (caption_file.h).
inline std::vector<Caption> captions() { return caption_file::read(film_cache::asset("nomai-signal/captions.txt")); }

}  // namespace nomai
}  // namespace cartoon
