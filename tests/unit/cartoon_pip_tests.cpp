// cartoon_pip_tests.cpp - the character rig (cartoon_actor.h), the sound synthesiser (cartoon_audio.h) and the story "Pip and the Moon": that its picture has no bad numbers anywhere in
// its 96 seconds, that Pip's hands are where the story says they are, that the moon is where the camera's mirror geometry puts it, and that its soundtrack is deterministic, in range and
// on the beats of the story's own timeline.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/pip_and_the_moon.h"

namespace pm = cartoon::pip_moon;
using cartoon::Vec3;

// ---- the rig ------------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(CartoonActor, AHandIsWhereItsTargetIsWhenTheTargetIsWithinReachAndOnTheArmWhenItIsNot) {
	cartoon::actor::Character c;
	cartoon::actor::Pose p;
	p.pos = {1.0, 0.0, 2.0};
	p.handWeightR = 1.0;
	const cartoon::actor::Arm rest = cartoon::actor::armOf(c, p, 1.0);
	p.handR = rest.shoulder + Vec3{0.2, -0.2, 0.1};
	const cartoon::actor::Arm near = cartoon::actor::armOf(c, p, 1.0);
	EXPECT_LT(cartoon::length(near.hand - p.handR), 1e-6);
	p.handR = rest.shoulder + Vec3{5.0, 0.0, 0.0};
	const cartoon::actor::Arm far = cartoon::actor::armOf(c, p, 1.0);
	EXPECT_LT(cartoon::length(far.hand - far.shoulder), 0.6 + 1e-6) << "an arm is only so long";
	EXPECT_GT(far.hand.x, far.shoulder.x) << "and it points at the target";
}

TEST(CartoonActor, ThePoseOfTheWholeFilmNeverMakesABadNumber) {
	for (double t = 0.0; t < pm::kDuration; t += 0.125) {
		const cartoon::actor::Pose p = pm::poseAt(t);
		for (double v : {p.pos.x, p.pos.y, p.pos.z, p.yaw, p.squash, p.lean, p.lift, p.headPitch, p.headYaw, p.look.x, p.look.y, p.look.z, p.handR.x, p.handR.y, p.handL.x, p.mouthOpen})
			ASSERT_TRUE(std::isfinite(v)) << "t = " << t;
	}
}

// ---- the picture --------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(CartoonPipAndTheMoon, NoSceneOfTheFilmHasABadNumber) {
	for (double t = 0.0; t < pm::kDuration; t += 1.0) {
		const std::string s = pm::sceneAt(t);
		ASSERT_EQ(s.find("nan"), std::string::npos) << "t = " << t;
		for (std::size_t at = s.find("inf"); at != std::string::npos; at = s.find("inf", at + 1))
			ASSERT_EQ(s.compare(at, 8, "infinite"), 0) << "an infinity in the scene text at t = " << t;   // (the one light of that name)
	}
}

TEST(CartoonPipAndTheMoon, EveryCameraOnTheReflectionLineSeesTheMoonWhereTheStoryPutsIt) {
	// the line from a camera to the moon's mirror image crosses the water at the point the camera was placed to look at
	const Vec3 m = pm::moonCentre(), image = {m.x, -m.y, m.z};
	for (double s : {3.0, 5.0, 8.0}) {
		const Vec3 cam = pm::reflectionCamera(pm::kTouch, s);
		EXPECT_GT(cam.y, 0.5);
		const double u = cam.y / (cam.y - image.y);   // where y = 0 along cam -> image
		const Vec3 hit = cam + (image - cam) * u;
		EXPECT_NEAR(hit.x, pm::kTouch.x, 1e-9);
		EXPECT_NEAR(hit.z, pm::kTouch.z, 1e-9);
	}
}

TEST(CartoonPipAndTheMoon, PipsHandsAreWhereTheStoryNeedsThem) {
	cartoon::actor::Character c;
	// the finger on the moon
	EXPECT_LT(cartoon::length(cartoon::actor::armOf(c, pm::poseAt(pm::kTouched), 1.0).hand - pm::kTouch), 0.12) << "the hand reaches the water where the moon is";
	// the hand on the bucket's handle, and the bucket in it
	EXPECT_LT(cartoon::length(cartoon::actor::armOf(c, pm::poseAt(pm::kPick - 0.05), 1.0).hand - (pm::kBucketRest + Vec3{0.0, 0.70, 0.0})), 0.15);
	EXPECT_LT(cartoon::length(pm::bucketAt(pm::kPick + 0.01).pos - pm::kBucketRest), 0.06) << "the bucket does not jump when it is picked up";
	// the bucket goes under the water when dipped, and the moon appears in it afterwards
	EXPECT_LT(pm::bucketAt(pm::kDip).pos.y, 0.05);
	EXPECT_DOUBLE_EQ(pm::glowAt(pm::kGlow - 0.1).power, 0.0);
	EXPECT_GT(pm::glowAt(pm::kGlow + 1.0).power, 0.99);
}

TEST(CartoonPipAndTheMoon, TheMoonFliesOutOfTheBucketAndLandsInThePondWhereTheSplashIs) {
	const pm::GlowState before = pm::glowAt(pm::kRelease - 0.01), after = pm::glowAt(pm::kRelease + 0.01);
	EXPECT_LT(cartoon::length(before.pos - after.pos), 0.3) << "it leaves the bucket without a jump";
	const pm::GlowState landing = pm::glowAt(pm::kSplashT);
	EXPECT_LT(cartoon::length(landing.pos - (pm::kSplash + Vec3{0.0, 0.12, 0.0})), 0.01);
	EXPECT_GT(landing.pos.z, pm::kDockHalf) << "over the water, not on the dock";
	EXPECT_NEAR(pm::glowAt(pm::kBurnOut).power, 0.0, 0.02);
	// the bucket comes to rest on the dock
	const pm::BucketState rest = pm::bucketAt(pm::kRelease + 3.0);
	EXPECT_NEAR(rest.pos.y, pm::kDock + 0.26, 0.1);
}

TEST(CartoonPipAndTheMoon, PipWalksOnTheDockAndNeverOffIt) {
	for (double t = 0.0; t < pm::kDuration; t += 0.25) {
		const cartoon::actor::Pose p = pm::poseAt(t);
		EXPECT_GE(p.pos.x, pm::kDockX0 - 0.1) << t;
		EXPECT_LE(p.pos.x, pm::kDockX1 + 0.1) << t;
		EXPECT_LE(std::abs(p.pos.z), pm::kDockHalf) << t;
	}
}

// ---- the timeline the sound shares --------------------------------------------------------------------------------------------------------------------------------------

TEST(CartoonPipAndTheMoon, TheFeetLandInStepWithTheWalkingAndOnlyWhileItWalks) {
	const std::vector<double> steps = pm::footfalls();
	EXPECT_GT(steps.size(), 40u);
	for (double t : steps) EXPECT_GT(pm::walkSpeed(t), 0.2) << t;
	for (std::size_t i = 1; i < steps.size(); ++i) EXPECT_GT(steps[i] - steps[i - 1], 0.1) << "two feet never land at once";
}

TEST(CartoonPipAndTheMoon, WhatPipSaysOpensItsMouthWhileItSaysItAndNotOtherwise) {
	for (const pm::Line &l : pm::lines()) {
		EXPECT_GT(pm::mouthOf(l.t + 0.5 * l.dur), 0.1) << l.t;
		EXPECT_LE(pm::mouthOf(l.t + 0.5 * l.dur), 1.0);
	}
	EXPECT_DOUBLE_EQ(pm::mouthOf(0.0), 0.0);
	EXPECT_DOUBLE_EQ(pm::mouthOf(60.0), 0.0);
}

TEST(CartoonPipAndTheMoon, TheWaterIsStruckWhereAndWhenTheStorySays) {
	// a finger on the moon: the water at the touch point moves just after the touch and is calm before it
	const double before = std::abs(pm::surfaceAt(pm::kTouch.x + 0.3, pm::kTouch.z, pm::kTouched - 0.5).h);
	const double after = std::abs(pm::surfaceAt(pm::kTouch.x + 0.3, pm::kTouch.z, pm::kTouched + 0.5).h);
	EXPECT_LT(before, 0.004);
	EXPECT_GT(after, before);
	// the ripples die away, so that the reflection comes back whole
	double slope = 0.0;
	for (double x = 2.0; x < 4.0; x += 0.1) slope = std::max(slope, std::abs(pm::surfaceAt(x, 0.35, pm::kTouched + 12.0).dhdx));
	EXPECT_LT(slope, 0.01);
}

// ---- the sound ----------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(CartoonAudio, AMixHasTheLengthItWasMadeWithAndANoteInItIsHeardWhereItWasPut) {
	cartoon::audio::Mix m(3.0);
	m.put(1.0, cartoon::audio::mallet(440.0, 0.5), 0.5, 0.0, 0.0);
	const std::vector<std::int16_t> pcm = m.finish(0.0, 0.0);
	EXPECT_EQ(pcm.size(), static_cast<std::size_t>(3.0 * cartoon::audio::kRate) * 2);
	auto energy = [&](double a, double b) {
		double e = 0.0;
		for (std::size_t i = static_cast<std::size_t>(a * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(b * cartoon::audio::kRate) * 2; ++i) e += std::abs(static_cast<double>(pcm[i]));
		return e;
	};
	EXPECT_EQ(energy(0.0, 0.9), 0.0) << "silence before the note";
	EXPECT_GT(energy(1.0, 1.4), 1000.0);
	EXPECT_LT(energy(2.5, 3.0), energy(1.0, 1.4) * 0.05) << "and it has died away";
}

TEST(CartoonAudio, PanningPutsASoundInTheEarItIsPannedTo) {
	cartoon::audio::Mix m(2.0);
	m.put(0.2, cartoon::audio::bell(880.0, 1.0), 0.5, -1.0, 0.0);
	const std::vector<std::int16_t> pcm = m.finish(0.0, 0.0);
	double left = 0.0, right = 0.0;
	for (std::size_t i = 0; i + 1 < pcm.size(); i += 2) {
		left += std::abs(static_cast<double>(pcm[i]));
		right += std::abs(static_cast<double>(pcm[i + 1]));
	}
	EXPECT_GT(left, 100.0 * (right + 1.0));
}

TEST(CartoonAudio, TheLimiterKeepsAnyLoudnessInRangeAndTheWavFileHasTheRightSize) {
	cartoon::audio::Mix m(2.0);
	for (int i = 0; i < 40; ++i) m.put(0.1, cartoon::audio::additive(200.0 + 37.0 * i, {{1.0, 1.0, 0.5}}, 1.5), 1.0, 0.0, 0.5);
	const std::vector<std::int16_t> pcm = m.finish(0.0, 0.0);
	int peak = 0;
	for (std::int16_t s : pcm) peak = std::max(peak, std::abs(static_cast<int>(s)));
	EXPECT_LE(peak, 32767);
	EXPECT_GT(peak, 20000) << "a loud mix is loud";
	const std::string path = (std::filesystem::temp_directory_path() / "photonsmith_audio_test.wav").string();
	ASSERT_TRUE(cartoon::audio::writeWav(path, pcm));
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	EXPECT_EQ(static_cast<std::size_t>(f.tellg()), 44 + pcm.size() * 2);
	f.seekg(0);
	char head[12];
	f.read(head, 12);
	EXPECT_EQ(std::string(head, 4), "RIFF");
	EXPECT_EQ(std::string(head + 8, 4), "WAVE");
	f.close();
	std::filesystem::remove(path);
}

TEST(CartoonPipAndTheMoon, TheSoundtrackIsTheSameEveryTimeInRangeAndHasSomethingToSayThroughoutExceptWhereTheStoryIsSilent) {
	cartoon::audio::Mix a(pm::kDuration), b(pm::kDuration);
	pm::soundtrack(a);
	pm::soundtrack(b);
	const std::vector<std::int16_t> pa = a.finish(), pb = b.finish();
	ASSERT_EQ(pa.size(), pb.size());
	EXPECT_EQ(pa, pb) << "a film sounds the same every time it is rendered";
	// (RT_SOUNDTRACK_WAV=<file> keeps the mix to listen to)
	if (const char *keep = std::getenv("RT_SOUNDTRACK_WAV")) cartoon::audio::writeWav(keep, pa);
	EXPECT_EQ(pa.size(), static_cast<std::size_t>(pm::kDuration * cartoon::audio::kRate) * 2);
	auto rms = [&](double t0, double t1) {
		double e = 0.0;
		std::size_t n = 0;
		for (std::size_t i = static_cast<std::size_t>(t0 * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(t1 * cartoon::audio::kRate) * 2; ++i, ++n) e += static_cast<double>(pa[i]) * pa[i];
		return std::sqrt(e / std::max<std::size_t>(1, n)) / 32768.0;
	};
	for (double t = 2.0; t < 90.0; t += 4.0) EXPECT_GT(rms(t, t + 4.0), 0.004) << "silence at " << t << " s";
	// the dance is louder than the night at the start, and the splash is the loudest moment of the fall
	EXPECT_GT(rms(48.0, 52.0), rms(0.0, 3.0));
	EXPECT_GT(rms(pm::kSplashT, pm::kSplashT + 0.5), rms(70.0, 71.0));
	int peak = 0;
	for (std::int16_t s : pa) peak = std::max(peak, std::abs(static_cast<int>(s)));
	EXPECT_LE(peak, 32767);
	EXPECT_GT(peak, 8000) << "not too quiet";
}
