// cartoon_films_tests.cpp - what each cartoon film says about itself (the table in src/shared/cartoon/cartoons.h) and the story each one tells: its motion, its lights, its timing.
// (The toolkit, the launcher, the presets and the stills are in cartoon_tests.cpp; this file is separate because the films are code, and a test file that instantiates all of them
// would be too big for one object file.)

#include <gtest/gtest.h>
#include "fixture_skip.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "pbrt_flatten_test_helpers.h"
#include "../../src/shared/ply_mesh.h"
#include "../../src/shared/cartoon/cartoons.h"
#include "../../src/shared/video_preset.h"

// ---- what each film says about itself (the Film table in cartoons.h) -------------------------------------------------------------------------------------------------

TEST(CartoonMeta, EveryFilmHasAStableNumberAndTheVideoPresetUsesIt) {
	std::set<int> ids;
	for (const cartoon::Film &film : cartoon::kFilms) {
		EXPECT_GE(film.meta.id, 7) << film.key << ": V1 to V6 are the camera flythroughs";
		EXPECT_TRUE(ids.insert(film.meta.id).second) << film.key << ": the number " << film.meta.id << " is used twice";
		const video_preset::VideoPreset *p = video_preset::find(film.key);
		ASSERT_NE(p, nullptr) << film.key;
		EXPECT_STREQ(p->id, cartoon::presetIdOf(film).c_str());
		EXPECT_EQ(p->frames, cartoon::frameCountOf(film));
	}
}

TEST(CartoonMeta, TagsComeFromTheVocabularyAndShotsAreNamedOnceEach) {
	const std::set<std::string> vocabulary(std::begin(cartoon::kTagVocabulary), std::end(cartoon::kTagVocabulary));
	for (const cartoon::Film &film : cartoon::kFilms) {
		const std::vector<std::string> tags = cartoon::tagsOf(film);
		EXPECT_GE(tags.size(), 2u) << film.key << " says too little about itself";
		EXPECT_LE(tags.size(), 5u) << film.key << " says too much (a tag that is on every film tells nothing)";
		std::set<std::string> seen;
		for (const std::string &tag : tags) {
			EXPECT_TRUE(vocabulary.count(tag)) << film.key << ": \"" << tag << "\" is not in the tag vocabulary";
			EXPECT_TRUE(seen.insert(tag).second) << film.key << ": tag " << tag << " twice";
		}
		const std::vector<double> starts = film.shotStarts();
		const std::vector<std::string> names = cartoon::shotNamesOf(film);
		ASSERT_FALSE(starts.empty()) << film.key;
		EXPECT_EQ(names.size(), starts.size()) << film.key << ": one name for each shot";
		EXPECT_EQ(starts.front(), 0.0) << film.key;
		for (std::size_t i = 1; i < starts.size(); ++i) EXPECT_GT(starts[i], starts[i - 1]) << film.key << ": shot " << i << " begins after the one before it";
		EXPECT_LT(starts.back(), film.duration - 1.0) << film.key << ": the last shot has time to be seen";
		EXPECT_GE(film.stillTime, 0.0);
		EXPECT_LE(film.stillTime, film.duration);
	}
}

TEST(CartoonMeta, TheRecommendedSamplesAreTheOnesTheFilmsOwnHeaderAsksFor) {
	for (const cartoon::Film &film : cartoon::kFilms) {
		const std::string header = film.headerAt(0.0);
		const std::string needle = "\"integer pixelsamples\" [ " + std::to_string(film.meta.samples) + " ]";
		EXPECT_NE(header.find(needle), std::string::npos) << film.key << " says " << film.meta.samples << " samples but its Sampler line says otherwise";
		EXPECT_GT(film.meta.secondsPerFrame, 0.0) << film.key << " has not been measured: python scripts/measure_cartoons.py --write";
	}
}

TEST(CartoonMeta, DescriptionsDoNotRestateWhatIsWorkedOutFromTheData) {
	// "Three shots, 24 seconds" typed into a description goes stale when the film changes; the length and the shot count are worked out (summaryOf) instead.
	const std::regex restated(R"((\b(one|two|three|four|five|six|\d+) shots?\b)|(\b\d+ seconds\b))", std::regex::icase);
	for (const cartoon::Film &film : cartoon::kFilms) {
		EXPECT_FALSE(std::regex_search(std::string(film.description), restated)) << film.key << ": the description restates the length or the shots";
		const std::string summary = cartoon::summaryOf(film);
		EXPECT_NE(summary.find(cartoon::lengthText(film)), std::string::npos) << film.key;
		EXPECT_NE(summary.find(std::to_string(cartoon::shotCountOf(film)) + (cartoon::shotCountOf(film) == 1 ? " shot" : " shots")), std::string::npos) << film.key;
		for (const std::string &shot : cartoon::shotNamesOf(film)) EXPECT_NE(summary.find(shot), std::string::npos) << film.key << ": " << shot;
	}
}

TEST(CartoonMeta, TheDocumentTablesSayWhatTheTableSays) {
	std::ifstream in("docs/VIDEO_GENERATION.md", std::ios::binary);
	if (!in) RT_SKIP_MISSING_FIXTURE("docs/VIDEO_GENERATION.md is not next to the working directory");
	std::stringstream all;
	all << in.rdbuf();
	std::string doc = all.str();
	doc.erase(std::remove(doc.begin(), doc.end(), '\r'), doc.end());
	for (const cartoon::Film &film : cartoon::kFilms) {
		EXPECT_NE(doc.find(cartoon::markdownRowOf(film) + "\n"), std::string::npos) << film.key << ": the film table of docs/VIDEO_GENERATION.md is out of date; paste the output of `ray_tracer --film-table`";
		const std::string preset = "| " + cartoon::presetIdOf(film) + " | " + film.key + " | (its own scenes) | " + cartoon::cameraPathOf(film) + " | " + std::to_string(cartoon::frameCountOf(film)) + " | " +
								   std::to_string(film.fps) + " | 1.0 |\n";
		EXPECT_NE(doc.find(preset), std::string::npos) << film.key << ": the preset table of docs/VIDEO_GENERATION.md has no row \"" << preset << "\"";
	}
}

TEST(CartoonNewtonsCradle, TheBallsHangTouchingAndTheSwingPassesFromEndToEndWhileLosingEnergy) {
	namespace nc = cartoon::newtons_cradle;
	for (int i = 0; i + 1 < nc::kBalls; ++i)
		EXPECT_NEAR(cartoon::length(nc::ballCentre(i + 1, 0.0) - nc::ballCentre(i, 0.0)), 2.0 * nc::kRadius, 1e-9) << "at rest the balls touch";
	const nc::Swing lifted = nc::swingAt(nc::kReleaseAt - 0.01);
	EXPECT_NEAR(lifted.left, -nc::kAmplitude, 0.5) << "the first ball is held at its highest point";
	EXPECT_EQ(lifted.right, 0.0);
	// every half period the swing is at the other end, and each time a little lower
	double last = nc::kAmplitude;
	for (int k = 0; k < 8; ++k) {
		const double mid = nc::kReleaseAt + nc::kQuarter + (k + 0.5) * 2.0 * nc::kQuarter;
		const nc::Swing s = nc::swingAt(mid);
		const double a = k % 2 == 0 ? s.right : -s.left;
		EXPECT_GT(a, 0.0) << "swing " << k << " is at the " << (k % 2 == 0 ? "right" : "left") << " end";
		EXPECT_LT(a, last) << "each swing keeps less of the energy";
		EXPECT_EQ(k % 2 == 0 ? s.left : s.right, 0.0) << "the other end ball hangs still";
		last = a;
	}
}

TEST(CartoonDominoRun, TheWaveRunsAlongTheWholePathAndTheLampComesOnAtTheEnd) {
	namespace dr = cartoon::domino_run;
	const int n = dr::dominoCount();
	EXPECT_GT(n, 100);
	for (int i = 1; i < n; ++i) EXPECT_GT(dr::startOf(i), dr::startOf(i - 1)) << "domino " << i << " falls after the one before it";
	EXPECT_EQ(dr::leanOf(0, dr::kStart - 0.01), 0.0) << "nothing moves before the marble arrives";
	EXPECT_NEAR(dr::leanOf(n / 2, dr::startOf(n / 2) + dr::kFall + 0.1), dr::kLean, 1e-9) << "a fallen domino rests on its neighbour";
	EXPECT_GT(dr::leanOf(n - 1, dr::impactAt()), 80.0) << "the last one lies against the lamp";
	EXPECT_LT(dr::impactAt() + 3.0, dr::kDuration) << "the lamp is lit with time left to enjoy it";
	// the dominoes are evenly spaced along the path
	for (int i = 1; i < n; ++i) EXPECT_NEAR(cartoon::length(dr::pointAtArc(dr::arcOf(i)) - dr::pointAtArc(dr::arcOf(i - 1))), dr::kSpacing, 0.01);
	// the marble starts well before the first domino and has reached it when it is struck
	EXPECT_GT(cartoon::length(dr::marbleAt(0.0) - dr::marbleAt(dr::kStart)), 3.0);
	// the lamp is dark first and lit after
	EXPECT_EQ(cartoon::domino_run::sceneAt(1.0).find("LightSource \"point\""), std::string::npos);
	EXPECT_NE(cartoon::domino_run::sceneAt(dr::impactAt() + 2.0).find("LightSource \"point\""), std::string::npos);
}

TEST(CartoonPaperBoat, TheRainEasesTheWaterCalmsAndTheBoatSailsAcrossThePond) {
	namespace pb = cartoon::paper_boat;
	EXPECT_NEAR(pb::rainAt(5.0), 1.0, 1e-12);
	EXPECT_NEAR(pb::rainAt(21.0), 0.0, 1e-12);
	// the surface: busy in the rain, nearly flat once it has stopped
	double busy = 0.0, calm = 0.0;
	for (double x = -6.0; x <= 6.0; x += 0.7) {
		for (double z = -4.0; z <= 4.0; z += 0.7) {
			busy = std::max(busy, std::abs(pb::surfaceAt(x, z, 10.0).h));
			calm = std::max(calm, std::abs(pb::surfaceAt(x, z, 23.5).h));
		}
	}
	EXPECT_GT(busy, 0.02);
	EXPECT_LT(calm, 0.03) << "only the faint swell is left";
	EXPECT_LT(pb::boatBase(0.0).x, -7.0);
	EXPECT_GT(pb::boatBase(pb::kDuration).x, 5.0);
	for (double t = 0.0; t <= pb::kDuration; t += 0.5) {
		const pb::BoatPose b = pb::boatAt(t);
		EXPECT_LT(std::abs(b.pitch), 25.0) << "t = " << t << ": rocked, not capsized";
		EXPECT_LT(std::abs(b.roll), 25.0) << "t = " << t;
		EXPECT_LT(std::abs(b.position.y), 0.2) << "t = " << t << ": it floats";
	}
	// the water's slope is the slope of its height: a finite difference agrees with the analytic one
	const double x = 1.3, z = -0.7, e = 1e-4;
	const pb::Surface s = pb::surfaceAt(x, z, 8.0);
	EXPECT_NEAR(s.dhdx, (pb::surfaceAt(x + e, z, 8.0).h - pb::surfaceAt(x - e, z, 8.0).h) / (2 * e), 2e-3);
	EXPECT_NEAR(s.dhdz, (pb::surfaceAt(x, z + e, 8.0).h - pb::surfaceAt(x, z - e, 8.0).h) / (2 * e), 2e-3);
}

TEST(CartoonOrrery, ThePlanetsKeepTheirOrbitsTheMoonKeepsItsPlanetAndTheGearsMesh) {
	namespace orr = cartoon::orrery;
	for (int k = 0; k < orr::kPlanets; ++k) {
		for (double t : {0.0, 3.3, 11.0, 21.0}) {
			const cartoon::Vec3 p = orr::planetAt(k, t);
			EXPECT_NEAR(std::hypot(p.x, p.z), orr::planet(k).orbit, 1e-9) << "planet " << k;
			EXPECT_NEAR(p.y, orr::planet(k).height, 1e-9);
		}
	}
	for (int k = 1; k < orr::kPlanets; ++k) EXPECT_GT(orr::planet(k).orbit, orr::planet(k - 1).orbit);
	for (double t : {0.0, 5.0, 17.0}) EXPECT_NEAR(cartoon::length(orr::moonAt(t) - orr::planetAt(2, t)), 0.55, 0.12) << "the moon stays by its planet";
	// the gears: the teeth ratio gives the speed ratio, in the other direction, and the small one starts half a tooth out
	EXPECT_NEAR((orr::smallGearAngle(10.0) - orr::smallGearAngle(0.0)) / (orr::bigGearAngle(10.0) - orr::bigGearAngle(0.0)), -static_cast<double>(orr::kTeethBig) / orr::kTeethSmall, 1e-9);
	EXPECT_NEAR(cartoon::length(orr::smallGearAt() - orr::bigGearAt()), orr::kBigRadius + orr::kSmallRadius, 1e-9) << "the pitch circles touch";
}

TEST(CartoonDandelion, TheWindwardSeedsGoFirstAndAReleasedSeedTravelsDownwind) {
	namespace dd = cartoon::dandelion;
	double firstToLast = 0.0, earliest = 1e9, latest = 0.0;
	for (int i = 0; i < dd::kSeeds; ++i) {
		earliest = std::min(earliest, dd::releaseOf(i));
		latest = std::max(latest, dd::releaseOf(i));
	}
	firstToLast = latest - earliest;
	EXPECT_GT(firstToLast, 4.0) << "the gust takes seconds to strip the head";
	EXPECT_GE(earliest, dd::kGust - 1e-9);
	EXPECT_LT(latest, dd::kDuration - 6.0) << "every seed has gone with time to follow it";
	// a seed facing the wind lets go before one facing away
	int toward = 0, away = 0;
	for (int i = 0; i < dd::kSeeds; ++i) {
		if (cartoon::dot(dd::seedDirection(i), dd::wind()) > cartoon::dot(dd::seedDirection(toward), dd::wind())) toward = i;
		if (cartoon::dot(dd::seedDirection(i), dd::wind()) < cartoon::dot(dd::seedDirection(away), dd::wind())) away = i;
	}
	EXPECT_LT(dd::releaseOf(toward), dd::releaseOf(away));
	const cartoon::Vec3 before = dd::seedAt(toward, dd::releaseOf(toward) - 0.1).position, later = dd::seedAt(toward, dd::releaseOf(toward) + 6.0).position;
	EXPECT_FALSE(dd::seedAt(toward, dd::releaseOf(toward) - 0.1).free);
	EXPECT_TRUE(dd::seedAt(toward, dd::releaseOf(toward) + 6.0).free);
	EXPECT_GT(cartoon::dot(later - before, dd::wind()), 5.0) << "it has been blown away";
	// the seeds are on the head before the gust: all within the seed head's reach
	for (int i = 0; i < dd::kSeeds; ++i) EXPECT_NEAR(cartoon::length(dd::seedAt(i, 0.0).position - cartoon::Vec3{0.0, dd::kHeadHeight, 0.0}), 0.12 + dd::kStalk, 0.02);
}

TEST(CartoonCandle, TheCandleBurnsDownAndTheFlameDiesWithSmokeAfterwards) {
	namespace cd = cartoon::candle;
	EXPECT_GT(cd::heightAt(0.0), cd::heightAt(cd::kDuration));
	EXPECT_NEAR(cd::burn(5.0), 1.0, 1e-12);
	EXPECT_LT(cd::burn(17.5), 1.0);
	EXPECT_NEAR(cd::burn(cd::kDark), 0.0, 1e-9);
	const std::string lit = cd::sceneAt(5.0), out = cd::sceneAt(cd::kDark + 0.5);
	EXPECT_NE(lit.find("rgb L\" [ 15.0"), std::string::npos) << "the flame is an emitter while it burns";
	EXPECT_EQ(out.find("rgb L\" [ 15.0"), std::string::npos) << "and gone once it has died";
	// the smoke is only there once the flame has begun to die
	EXPECT_TRUE(cd::buildSmoke(10.0).empty());
	EXPECT_FALSE(cd::buildSmoke(cd::kOut + 3.0).empty());
}

TEST(CartoonFilms, EveryFilmFlattensAtSeveralTimesWithoutWarnings) {
	// (a film may keep its heavy meshes in PLY files of its cache, film_cache.h: read them as the scene loader does)
	const MeshResolver plyFiles = [](const std::string &path, std::vector<float> &positions, std::vector<int> &indices, std::vector<float> &uvs, std::vector<float> &normals) {
		ply_mesh::LoadResult m = ply_mesh::loadFile(path);
		if (!m.ok) return false;
		positions = std::move(m.mesh.positions);
		indices = std::move(m.mesh.indices);
		uvs = std::move(m.mesh.uvs);
		normals = std::move(m.mesh.normals);
		return true;
	};
	for (const cartoon::Film &film : cartoon::kFilms) {
		for (double f : {0.0, 0.1, 0.3, 0.5, 0.7, 0.9, 0.99}) {   // (0.99: the last second may be a cut to black)
			const double t = f * film.duration;
			const FlatScene s = flattenSource(film.sceneAt(t), plyFiles);
			// (a black card -- a title over black: no light at all, a black sky -- is a picture too; and a frame lit by its sky alone, an ink painting on paper, needs no lamp)
			const bool skyLit = s.infiniteLight.present && (s.infiniteLight.L[0] + s.infiniteLight.L[1] + s.infiniteLight.L[2] > 0.0 || !s.infiniteLight.imageFile.empty());
			if (s.areaLights.empty() && s.infiniteLight.present && !skyLit) {
				for (const auto &w : s.warnings) ADD_FAILURE() << film.key << " t = " << t << ": " << w.message;
				continue;
			}
			if (!(s.areaLights.empty() && skyLit)) EXPECT_GT(s.triangles.size(), 1000u) << film.key << " t = " << t;
			EXPECT_TRUE(!s.areaLights.empty() || skyLit) << film.key << " t = " << t;
			for (const auto &w : s.warnings) ADD_FAILURE() << film.key << " t = " << t << ": " << w.message;
		}
	}
}

TEST(CartoonLittleRobot, TheRobotKneelsAtTheSeedlingAndItBloomsAfterTheWatering) {
	namespace lr = cartoon::little_robot;
	const cartoon::Film *film = cartoon::find("little-robot");
	ASSERT_NE(film, nullptr);
	EXPECT_DOUBLE_EQ(film->duration, lr::kDuration);
	// the robot starts left of the seedling and ends beside it
	EXPECT_LT(lr::robotX(0.5), lr::kSeedling.x - 1.0);
	EXPECT_NEAR(lr::robotX(lr::kDuration), lr::kStandX, 0.6);
	// the flower is a good deal more geometry than the bare sprout
	const size_t sprout = flattenSource(film->sceneAt(20.0)).triangles.size();
	const size_t flower = flattenSource(film->sceneAt(30.5)).triangles.size();
	EXPECT_GT(flower, sprout);
}

TEST(CartoonFireflies, TheSwarmWakesAfterThePairHasMetAndEveryLightIsFaded) {
	namespace ff = cartoon::fireflies;
	const cartoon::Film *film = cartoon::find("fireflies");
	ASSERT_NE(film, nullptr);
	const size_t early = flattenSource(film->sceneAt(5.0)).areaLights.size();
	const size_t late = flattenSource(film->sceneAt(23.0)).areaLights.size();
	EXPECT_GT(late, early + 20) << "dozens more fireflies by the end";
	EXPECT_EQ(ff::fade(0.0), 0.0) << "the film opens from black";
	EXPECT_EQ(ff::fade(ff::kDuration), 0.0) << "and closes to black";
	// the two flashes start out of step and end in step
	auto offset = [](double t) { return std::abs(ff::glowA(t) - ff::glowB(t)); };
	double early_gap = 0.0, late_gap = 0.0;
	for (double t = 6.5; t < 8.0; t += 0.01) early_gap = std::max(early_gap, offset(t));
	for (double t = 13.0; t < 14.0; t += 0.01) late_gap = std::max(late_gap, offset(t));
	EXPECT_GT(early_gap, 0.3);
	EXPECT_LT(late_gap, 1e-6);
}

TEST(CartoonFireflies, AFireflyDoesNotJumpBetweenFrames) {
	namespace ff = cartoon::fireflies;
	const double dt = 1.0 / 24.0;
	for (double t = 0.0; t < ff::kDuration - dt; t += dt) {
		EXPECT_LT(cartoon::length(ff::fireflyA(t + dt) - ff::fireflyA(t)), 0.9) << "A jumped at t = " << t;
		if (t >= 6.0) EXPECT_LT(cartoon::length(ff::fireflyB(t + dt) - ff::fireflyB(t)), 0.9) << "B jumped at t = " << t;
	}
}


// Forest Dawn: the sun comes up during the film (dark before, in the sky after), every camera is above the rolling ground, the mist holds every camera (the shafts need the camera
// inside it), and the soundtrack is deterministic and never silent.
TEST(CartoonForestDawn, TheSunRisesTheCamerasStandAboveTheGroundAndInsideTheMist) {
	namespace fd = cartoon::forest_dawn;
	EXPECT_LT(fd::sunUp(2.0), 0.01) << "before dawn the sun is down";
	EXPECT_GT(fd::sunUp(20.0), 0.99) << "after the second shot it is up";
	for (double t = 0.0; t <= fd::kDuration; t += 0.25) {
		const cartoon::CameraView v = cartoon::cameraAt(fd::shots(), t);
		EXPECT_GT(v.eye.y - fd::ground().height(v.eye.x, v.eye.z), 0.15) << "the camera is below the ground at t = " << t;
		const fd::Mist m = fd::mistAt(t);
		EXPECT_LT(cartoon::length(v.eye - m.centre), m.radius - 1.0) << "the camera is outside the mist at t = " << t;
	}
}

TEST(CartoonForestDawn, TheSoundtrackIsTheSameEveryTimeInRangeAndNeverSilent) {
	namespace fd = cartoon::forest_dawn;
	cartoon::audio::Mix a(fd::kDuration), b(fd::kDuration);
	fd::soundtrack(a);
	fd::soundtrack(b);
	const std::vector<std::int16_t> pa = a.finish(), pb = b.finish();
	EXPECT_EQ(pa, pb) << "a film sounds the same every time it is rendered";
	if (const char *keep = std::getenv("RT_SOUNDTRACK_WAV")) cartoon::audio::writeWav(keep, pa);   // (keeps the mix to listen to)
	auto rms = [&](double t0, double t1) {
		double e = 0.0;
		std::size_t n = 0;
		for (std::size_t i = static_cast<std::size_t>(t0 * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(t1 * cartoon::audio::kRate) * 2 && i < pa.size(); ++i, ++n) e += static_cast<double>(pa[i]) * pa[i];
		return std::sqrt(e / static_cast<double>((std::max)(std::size_t(1), n))) / 32768.0;
	};
	for (double t = 1.0; t < fd::kDuration - 3.0; t += 3.0) EXPECT_GT(rms(t, t + 3.0), 0.003) << "silence at " << t << " s";
	EXPECT_GT(rms(35.0, 39.0), rms(0.5, 4.5)) << "the morning is fuller than the night";
	int peak = 0;
	for (std::int16_t s : pa) peak = (std::max)(peak, std::abs(static_cast<int>(s)));
	EXPECT_GT(peak, 6000) << "not too quiet";
}

// Every film has a soundtrack: the same every time it is made, heard over its whole length (no stretch of 6 s in silence before its last second), and never driven into the
// limiter (its loudest moment is below full scale). RT_SOUNDTRACK_DIR=<folder> keeps each film's mix as <key>.wav to listen to.
TEST(CartoonSound, EveryFilmHasASoundtrackHeardFromStartToEnd) {
	for (const cartoon::Film &film : cartoon::kFilms) {
		ASSERT_TRUE(static_cast<bool>(film.soundtrack)) << film.key << " has no soundtrack";
		if (film.duration > 120.0) continue;   // (the long films' mixes take most of a minute: they are made in their own tests and in a draft)
		cartoon::audio::Mix a(film.duration), b(film.duration);
		film.soundtrack(a);
		film.soundtrack(b);
		const std::vector<std::int16_t> pa = a.finish(), pb = b.finish();
		EXPECT_EQ(pa, pb) << film.key << ": a film sounds the same every time it is rendered";
		if (const char *dir = std::getenv("RT_SOUNDTRACK_DIR")) cartoon::audio::writeWav((std::filesystem::path(dir) / (std::string(film.key) + ".wav")).string(), pa);
		const auto rms = [&](double t0, double t1) {
			double e = 0.0;
			std::size_t n = 0;
			for (std::size_t i = static_cast<std::size_t>(t0 * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(t1 * cartoon::audio::kRate) * 2 && i < pa.size(); ++i, ++n) e += static_cast<double>(pa[i]) * pa[i];
			return std::sqrt(e / static_cast<double>((std::max)(std::size_t(1), n))) / 32768.0;
		};
		// (a pause of a few seconds is the score's own: the long films have them on purpose)
		for (double t = 0.0; t + 6.0 < film.duration - 1.0; t += 2.0) EXPECT_GT(rms(t, t + 6.0), 0.0008) << film.key << ": silence from " << t << " s";
		int peak = 0;
		for (std::int16_t s : pa) peak = (std::max)(peak, std::abs(static_cast<int>(s)));
		EXPECT_GT(peak, 3000) << film.key << ": too quiet";
	}
}

// The Great Turtle Escape: a wide 1920 x 1080 film whose camera stays in the room, whose turtle never jumps between frames, whose night is exposed brighter than its day, and whose
// soundtrack is deterministic and never silent until the cut to black.
TEST(CartoonTurtleEscape, WideFramesCamerasInTheRoomAndATurtleThatDoesNotJump) {
	namespace te = cartoon::turtle_escape;
	const std::string header = te::headerAt(10.0);
	EXPECT_NE(header.find("\"integer xresolution\" [ 1920 ]"), std::string::npos);
	EXPECT_NE(header.find("\"integer yresolution\" [ 1080 ]"), std::string::npos);
	EXPECT_GT(te::exposureAt(250.0), te::exposureAt(10.0)) << "the night is opened up";
	const double dt = 1.0 / te::kFps;
	cartoon::Vec3 last = te::turtleAt(0.0).pos;
	for (double t = 0.0; t < te::kBlack; t += dt) {
		const cartoon::CameraView v = te::viewAt(t);
		EXPECT_TRUE(v.eye.x > te::kRoomX0 && v.eye.x < te::kRoomX1 && v.eye.z > te::kRoomZ0 && v.eye.z < te::kRoomZ1 && v.eye.y > 0.0 && v.eye.y < te::kCeiling)
			<< "the camera is outside the room at t = " << t;
		const cartoon::Vec3 p = te::turtleAt(t).pos;
		EXPECT_LT(cartoon::length(p - last), 0.25) << "the turtle jumped at t = " << t;
		last = p;
	}
}

TEST(CartoonTurtleEscape, TheSoundtrackIsTheSameEveryTimeAndNeverSilent) {
	namespace te = cartoon::turtle_escape;
	cartoon::audio::Mix a(te::kDuration), b(te::kDuration);
	te::soundtrack(a);
	te::soundtrack(b);
	const std::vector<std::int16_t> pa = a.finish(), pb = b.finish();
	EXPECT_EQ(pa, pb);
	if (const char *keep = std::getenv("RT_SOUNDTRACK_WAV")) cartoon::audio::writeWav(keep, pa);
	auto rms = [&](double t0, double t1) {
		double e = 0.0;
		std::size_t n = 0;
		for (std::size_t i = static_cast<std::size_t>(t0 * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(t1 * cartoon::audio::kRate) * 2 && i < pa.size(); ++i, ++n) e += static_cast<double>(pa[i]) * pa[i];
		return std::sqrt(e / static_cast<double>((std::max)(std::size_t(1), n))) / 32768.0;
	};
	for (double t = 0.5; t < te::kBlack - 4.0; t += 4.0) EXPECT_GT(rms(t, t + 4.0), 0.002) << "silence at " << t << " s";
	int peak = 0;
	for (std::int16_t s : pa) peak = (std::max)(peak, std::abs(static_cast<int>(s)));
	EXPECT_GT(peak, 6000) << "not too quiet";
	EXPECT_LT(peak, 32767) << "not clipped";
}

// The sculpting toolkit the characters are made with (sdf_mesh.h): both meshers put every vertex on the surface with the surface's own normal, and leave no
// triangle that the scene text's 10 micrometre rounding would collapse.
TEST(CartoonSdfMesh, MeshersPutEveryVertexOnTheSurfaceWithItsNormal) {
	namespace sdf = cartoon::sdf;
	const auto ball = [](cartoon::Vec3 p) { return sdf::sphere(p, {0.0, 0.0, 0.0}, 0.1); };
	const cartoon::Mesh nets = sdf::surfaceNets(ball, {-0.12, -0.12, -0.12}, {0.12, 0.12, 0.12}, 0.01);
	const cartoon::Mesh rays = sdf::radialMesh(ball, {0.0, 0.0, 0.0}, 0.2, 32, 16);
	for (const cartoon::Mesh *m : {&nets, &rays}) {
		ASSERT_GT(m->tris.size(), 500u);
		ASSERT_EQ(m->verts.size(), m->normals.size());
		for (std::size_t i = 0; i < m->verts.size(); ++i) {
			EXPECT_NEAR(cartoon::length(m->verts[i]), 0.1, 2e-4) << "vertex " << i;
			EXPECT_GT(cartoon::dot(m->normals[i], cartoon::normalize(m->verts[i])), 0.99) << "vertex " << i;
		}
	}
	for (const auto &t : nets.tris)
		for (int k = 0; k < 3; ++k) EXPECT_GT(cartoon::length(nets.verts[static_cast<std::size_t>(t[k])] - nets.verts[static_cast<std::size_t>(t[(k + 1) % 3])]), 1e-5);
	// a smooth union is never above the plain one, and blends within its width
	EXPECT_LE(sdf::smin(0.3, 0.32, 0.1), 0.3);
	EXPECT_DOUBLE_EQ(sdf::smin(0.3, 0.9, 0.1), 0.3);
}

// The Crystal Heart: every camera inside the mist (the shafts and glows need it), above the lake and clear of every crystal; the sun up only after the blue hour; the Heart
// dormant, then ignited; and a score that is the same every time and never silent.
TEST(CartoonCrystalHeart, CamerasStayInTheMistAboveTheWaterAndClearOfTheCrystals) {
	namespace ch = cartoon::crystal_heart;
	EXPECT_LT(ch::sunUp(10.0), 0.01) << "the blue hour has no sun";
	EXPECT_GT(ch::sunUp(30.0), 0.99);
	EXPECT_GT(ch::heartGlow(ch::kPeak), 10.0 * ch::heartGlow(10.0)) << "it ignites";
	ASSERT_GE(ch::spires().size(), 30u);
	for (double t = 0.0; t < ch::kDuration; t += 0.25) {
		const cartoon::CameraView v = ch::viewAt(t);
		const cartoon::nature::Mist m = ch::mistAt(t);
		EXPECT_LT(cartoon::length(v.eye - m.centre), m.radius - 2.0) << "the camera is outside the mist at t = " << t;
		EXPECT_GT(v.eye.y, 0.8) << "the camera is at the water at t = " << t;
		for (const auto &s : ch::spires())
			for (const auto &c : s.crystals) {
				const double horizontal = cartoon::length(cartoon::Vec3{v.eye.x - c.base.x, 0.0, v.eye.z - c.base.z});
				if (v.eye.y < c.base.y + c.height + c.tip) EXPECT_GT(horizontal, c.radius + 1.0) << "the camera is inside a crystal at t = " << t;
			}
	}
}

TEST(CartoonCrystalHeart, TheScoreIsTheSameEveryTimeAndNeverSilent) {
	namespace ch = cartoon::crystal_heart;
	cartoon::audio::Mix a(ch::kDuration), b(ch::kDuration);
	ch::soundtrack(a);
	ch::soundtrack(b);
	const std::vector<std::int16_t> pa = a.finish(), pb = b.finish();
	EXPECT_EQ(pa, pb);
	if (const char *keep = std::getenv("RT_SOUNDTRACK_WAV")) cartoon::audio::writeWav(keep, pa);
	auto rms = [&](double t0, double t1) {
		double e = 0.0;
		std::size_t n = 0;
		for (std::size_t i = static_cast<std::size_t>(t0 * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(t1 * cartoon::audio::kRate) * 2 && i < pa.size(); ++i, ++n) e += static_cast<double>(pa[i]) * pa[i];
		return std::sqrt(e / static_cast<double>((std::max)(std::size_t(1), n))) / 32768.0;
	};
	for (double t = 0.5; t < ch::kDuration - 3.0; t += 3.0) EXPECT_GT(rms(t, t + 3.0), 0.003) << "silence at " << t << " s";
	EXPECT_GT(rms(ch::kIgnite, ch::kIgnite + 3.0), 2.0 * rms(2.0, 5.0)) << "the ignition is the loudest moment, not the lake";
	int peak = 0;
	for (std::int16_t s : pa) peak = (std::max)(peak, std::abs(static_cast<int>(s)));
	EXPECT_GT(peak, 8000) << "not too quiet";
	EXPECT_LT(peak, 32767) << "not clipped";
}

// Preface to the Pavilion of Prince Teng: its shots in order and named, its words (each picture there, inside the film, one at a time on the line), and its score, which is the
// same every time, never clipped, near silent where the film asks for silence (the black before the bell, the stone sinking) and at its loudest when the orchestra opens.
TEST(CartoonTengwang, TheShotsAndTheWordsFitTheFilm) {
	namespace tw = cartoon::tengwang;
	const std::vector<double> starts = tw::shotStarts();
	ASSERT_FALSE(starts.empty());
	EXPECT_DOUBLE_EQ(starts.front(), 0.0);
	for (std::size_t i = 1; i < starts.size(); ++i) EXPECT_GT(starts[i], starts[i - 1]) << "shot " << i;
	EXPECT_LT(starts.back(), tw::kDuration);
	const std::vector<cartoon::Caption> words = tw::captions();
	EXPECT_GE(words.size(), 18u);
	double lastLineEnd = 0.0;
	for (const cartoon::Caption &c : words) {
		const std::string first = c.lines.empty() ? std::string() : c.lines.front().text;
		ASSERT_FALSE(c.lines.empty()) << "a caption with no text (pbrt_scenes/film_assets/tengwang/captions.txt)";
		for (const cartoon::CaptionLine &l : c.lines) {
			EXPECT_FALSE(l.text.empty()) << first;
			EXPECT_FALSE(l.font.empty()) << first;
			EXPECT_GT(l.size, 0.0) << first;
		}
		EXPECT_GE(c.t0, 0.0);
		EXPECT_LE(c.t1, tw::kDuration);
		EXPECT_LT(c.t0, c.t1);
		if (c.centreY > 0.8) {   // (the lines at the foot of the picture never overlap; the title's three parts share the screen)
			EXPECT_GE(c.t0, lastLineEnd) << first;
			lastLineEnd = c.t1;
		}
	}
}

TEST(CartoonTengwang, TheScoreIsTheSameEveryTimeQuietWhereItShouldBeAndNeverClipped) {
	namespace tw = cartoon::tengwang;
	cartoon::audio::Mix a(tw::kDuration), b(tw::kDuration);
	tw::soundtrack(a);
	tw::soundtrack(b);
	const std::vector<std::int16_t> pa = a.finish(), pb = b.finish();
	EXPECT_EQ(pa, pb);
	if (const char *keep = std::getenv("RT_SOUNDTRACK_WAV")) cartoon::audio::writeWav(keep, pa);
	auto rms = [&](double t0, double t1) {
		double e = 0.0;
		std::size_t n = 0;
		for (std::size_t i = static_cast<std::size_t>(t0 * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(t1 * cartoon::audio::kRate) * 2 && i < pa.size(); ++i, ++n) e += static_cast<double>(pa[i]) * pa[i];
		return std::sqrt(e / static_cast<double>((std::max)(std::size_t(1), n))) / 32768.0;
	};
	EXPECT_LT(rms(0.6, 2.8), rms(3.0, 6.0)) << "the bell breaks the quiet of the black";
	EXPECT_GT(rms(491.0, 504.0), 2.0 * rms(447.0, 454.0)) << "the orchestra opens after the stone has sunk";
	int peak = 0;
	for (std::int16_t s : pa) peak = (std::max)(peak, std::abs(static_cast<int>(s)));
	EXPECT_GT(peak, 8000) << "not too quiet";
	EXPECT_LT(peak, 32767) << "not clipped";
}
