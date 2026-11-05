// film_whole_tests.cpp - every film as a whole, through the program as it is run: each film's shots against the recorded baseline (tests/data/film_baseline.txt), each film's still
// rendered to a picture that is there (not black, not flat, no NaN), each film file's soundtrack, and a short video made end to end with its sound and its log.
//
// The suite is FilmRenderTest: it renders and runs ray_tracer, so it is in the Render tier (scripts/run_tests_parallel.ps1). The tests that run the program skip when it is not
// built (RT_LAUNCHER=<path> names it; RT_REQUIRE_LAUNCHER=1 makes that a failure).
#include <gtest/gtest.h>
#include "fixture_skip.h"
#include "test_parts.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "agreement_test_helpers.h"
#include "../../src/shared/cartoon/cartoons.h"
#include "../../src/external/stb_image.h"

namespace {

std::string slurpFile(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream all;
	all << in.rdbuf();
	return all.str();
}

// Runs the launcher with `args` (as one command line, its output into `log`); its exit code. In a shard of scripts/run_tests_parallel.ps1 (its own TEMP), the program is given
// the machine's temporary folder (RT_MACHINE_TEMP): its film cache is made to be shared between processes, and a cold one in every shard tripled the Render tier's time.
int runLauncher(const std::string &launcher, const std::string &args, const std::filesystem::path &log) {
	std::string command = "\"" + launcher + "\" " + args + " > \"" + log.string() + "\" 2>&1";
#ifdef _WIN32
	if (const char *temp = std::getenv("RT_MACHINE_TEMP"); temp && temp[0]) command = "set \"TEMP=" + std::string(temp) + "\" && set \"TMP=" + std::string(temp) + "\" && " + command;
	command = "\"" + command + "\"";   // (cmd.exe strips one pair of outer quotes)
#endif
	return std::system(command.c_str());
}

std::filesystem::path scratch(const std::string &name) {
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("film_whole_" + name);
	std::error_code ec;
	std::filesystem::remove_all(d, ec);
	std::filesystem::create_directories(d);
	return d;
}

bool isFilmFile(const cartoon::Film &film) {
	return std::none_of(std::begin(cartoon::kFilms), std::end(cartoon::kFilms), [&](const cartoon::Film &f) { return std::string(f.key) == film.key; });
}

}  // namespace

// A scene that asks for two halves side by side (`# @rt-sidebyside depth 1`, src/shared/side_by_side.h) is rendered by the launcher as two half-width renders joined:
// the picture is as wide as asked, its left half (rays that stop at the first surface) darker than its right (light that bounces off the floor onto the wall), the
// line between them pale, and no half's file left beside it. On the CPU, small: the Fast tier and CI run it.
TEST(SideBySideRender, TheLauncherRendersTwoHalvesAndJoinsThem) {
	const std::string launcher = findRayTracerLauncher();
	if (launcher.empty()) GTEST_SKIP() << "ray_tracer was not found";
	const std::filesystem::path d = scratch("sidebyside");
	{
		std::ofstream f(d / "s.pbrt");
		f << "# @rt-sidebyside depth 1\nLookAt 0 1 3  0 0.8 0  0 1 0\nCamera \"perspective\" \"float fov\" [ 70 ]\n"
			 "Film \"rgb\" \"integer xresolution\" [ 40 ] \"integer yresolution\" [ 20 ]\nSampler \"halton\" \"integer pixelsamples\" [ 16 ]\nWorldBegin\n"
			 "AttributeBegin\n  AreaLightSource \"diffuse\" \"rgb L\" [ 30 30 30 ] \"bool twosided\" [ true ]\n"
			 "  Shape \"trianglemesh\" \"integer indices\" [ 0 2 1  0 3 2 ] \"point3 P\" [ -0.2 1.6 0.6  0.2 1.6 0.6  0.2 1.6 1.0  -0.2 1.6 1.0 ]\nAttributeEnd\n"
			 "Material \"diffuse\" \"rgb reflectance\" [ 0.8 0.8 0.8 ]\n"
			 "Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ] \"point3 P\" [ -3 0 -1  3 0 -1  3 0 3  -3 0 3 ]\n"
			 "Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ] \"point3 P\" [ -3 0 -1  -3 3 -1  3 3 -1  3 0 -1 ]\n";
	}
	const std::filesystem::path out = d / "out.png";
	const int rc = runLauncher(launcher, "--cpu --seed 1 --height 20 --output \"" + out.string() + "\" 40 16 6 \"" + (d / "s.pbrt").string() + "\"", d / "log.txt");
	ASSERT_EQ(rc, 0) << slurpFile(d / "log.txt");
	int w = 0, h = 0, n = 0;
	unsigned char *px = stbi_load(out.string().c_str(), &w, &h, &n, 3);
	ASSERT_NE(px, nullptr) << "a picture: " << slurpFile(d / "log.txt");
	const auto mean = [&](int x0, int x1) {
		double sum = 0.0;
		for (int y = 0; y < h; ++y)
			for (int x = x0; x < x1; ++x)
				for (int c = 0; c < 3; ++c) sum += px[(y * w + x) * 3 + c];
		return sum / ((x1 - x0) * h * 3.0);
	};
	const double left = mean(0, w / 2 - 2), right = mean(w / 2 + 2, w), line = mean(w / 2 - 1, w / 2 + 1);
	stbi_image_free(px);
	EXPECT_EQ(w, 40);
	EXPECT_EQ(h, 20);
	EXPECT_LT(left * 1.1, right) << "the left half (depth 1) is darker than the right (bounced light): " << left << " vs " << right;
	EXPECT_GT(line, 180.0) << "the pale line between the halves";
	EXPECT_FALSE(std::filesystem::exists(d / "out.left.png") || std::filesystem::exists(d / "out.right.png") || std::filesystem::exists(d / "out.left.ppm"))
		<< "no half's file is left";
	EXPECT_NE(slurpFile(d / "log.txt").find("Side by side"), std::string::npos) << "the run says so";
}

// The films in test_parts.h's parts, so the Render tier's shards share them (one test of every film's baseline took 141 s of a shard, a quarter of the stills 126 s). The suite
// is in the Render tier by its name (scripts/run_tests_parallel.ps1 $renderSuites). A part without a film passes at once.
class FilmRenderTestParts : public ::testing::TestWithParam<int> {
  protected:
	// This part's films: every film the program has (the built-in ones and the film files), each in one part.
	std::vector<const cartoon::Film *> films() const {
		std::vector<const cartoon::Film *> out;
		const std::vector<const cartoon::Film *> &all = cartoon::films();
		for (std::size_t i = 0; i < all.size(); ++i)
			if (test_parts::inPart(i, GetParam())) out.push_back(all[i]);
		return out;
	}
};

// Every film, shot by shot, is what the baseline recorded: a change to code the films share (the rig, the kit, a sky) that moves a film's scenes fails here, naming the film and
// the shots. When a film is changed ON PURPOSE, record it again and look at what changed: `python scripts/anim/film.py baseline --write --exe x64/Release/ray_tracer.exe`.
TEST_P(FilmRenderTestParts, EveryFilmsShotsAreWhatTheBaselineRecorded) {
#ifndef _WIN32
	GTEST_SKIP() << "the baseline is recorded on Windows: another platform's maths library differs in the last digits a scene's text keeps";
#endif
	std::vector<const cartoon::Film *> mine = films();
	mine.erase(std::remove_if(mine.begin(), mine.end(), [](const cartoon::Film *f) { return std::string(f->key).rfind("my-", 0) == 0; }), mine.end());   // (a film of your own, in no baseline)
	if (mine.empty()) return;
	const std::string launcher = findRayTracerLauncher();
	if (launcher.empty()) GTEST_SKIP() << "ray_tracer was not found";
	const std::filesystem::path baseline = std::filesystem::absolute("tests/data/film_baseline.txt");
	if (!std::filesystem::exists(baseline)) RT_SKIP_MISSING_FIXTURE(baseline.string() << " is not next to the working directory");
	const std::filesystem::path d = scratch("baseline" + std::to_string(GetParam()));
	std::string keys;
	for (const cartoon::Film *film : mine) keys += std::string(" ") + film->key;
	const int rc = runLauncher(launcher, "--film-baseline \"" + baseline.string() + "\"" + keys, d / "out.txt");
	const std::string out = slurpFile(d / "out.txt");
	EXPECT_EQ(rc, 0) << "films that changed (kind, film, shot, name):\n" << out;
	EXPECT_NE(out.find("the same ("), std::string::npos) << out;
	std::size_t looked = 0;
	if (const std::size_t at = out.find("("); at != std::string::npos) looked = std::strtoul(out.c_str() + at + 1, nullptr, 10);
	EXPECT_GE(looked, mine.size()) << "every film's shots were looked at:" << keys;
	for (const cartoon::Film *film : mine)   // (the program says each film it looks at: "<key>...")
		EXPECT_NE(out.find(std::string(film->key) + "..."), std::string::npos) << film->key << ": the program did not look at it:\n" << out;
}

// Each film's still, rendered small: there is a picture - not black, not one flat colour, no NaN or infinity. (The film doctor looks harder - framing, brightness across cuts
// - but by hand; this catches a film whose frames have gone black or empty, or a scene the renderer refuses, without anyone watching it.)
TEST_P(FilmRenderTestParts, EveryFilmsStillRendersAPictureThatIsThere) {
	const std::vector<const cartoon::Film *> mine = films();
	if (mine.empty()) return;
	const std::string launcher = findRayTracerLauncher();
	if (launcher.empty()) GTEST_SKIP() << "ray_tracer was not found";
	const std::filesystem::path d = scratch("stills" + std::to_string(GetParam()));
	for (const cartoon::Film *film : mine) {
		const std::filesystem::path out = d / (std::string(film->key) + ".exr"), log = d / (std::string(film->key) + ".log");
		// (--preview: characters built coarser, which is most of a frame's making; 64 x 36 at 4 samples)
		const int rc = runLauncher(launcher, "--cpu --preview --output \"" + out.string() + "\" --height 36 64 4 6 \"" + cartoon::cameraPathOf(*film) + "\"", log);
		std::vector<float> rgb;
		int w = 0, h = 0;
		if (!(rc == 0 && loadLinearRgbPixels(out.string(), w, h, rgb))) {
			ADD_FAILURE() << film->key << ": its still did not render (exit " << rc << "):\n" << slurpFile(log);
			continue;
		}
		double lo = 1e30, hi = 0.0, sum = 0.0;
		int bad = 0;
		for (std::size_t i = 0; i + 2 < rgb.size(); i += 3) {
			if (!std::isfinite(rgb[i]) || !std::isfinite(rgb[i + 1]) || !std::isfinite(rgb[i + 2])) {
				++bad;
				continue;
			}
			const double l = 0.2126 * rgb[i] + 0.7152 * rgb[i + 1] + 0.0722 * rgb[i + 2];
			lo = std::min(lo, l), hi = std::max(hi, l), sum += l;
		}
		EXPECT_EQ(bad, 0) << film->key << ": pixels that are NaN or infinite";
		EXPECT_GT(hi, 1e-3) << film->key << ": its still is black";
		EXPECT_GT(hi - lo, 0.05 * hi) << film->key << ": its still is one flat colour (" << lo << " to " << hi << ")";
		EXPECT_GT(sum / (w * h), 1e-4) << film->key << ": its still is all but black";
	}
}
INSTANTIATE_TEST_SUITE_P(Parts, FilmRenderTestParts, test_parts::all());


// A film file's soundtrack, as the built-in films' are checked (CartoonSound.EveryFilmHasASoundtrackHeardFromStartToEnd): the same every time it is made, heard over its whole
// length, never clipped, and at the loudness every soundtrack is brought to (-16 LUFS, cartoon_audio.h Mix::finish).
TEST(FilmRenderTest, EveryFilmFilesSoundtrackIsHeardFromStartToEndAtItsLoudness) {
	int checked = 0;
	for (const cartoon::Film *film : cartoon::films()) {
		if (!isFilmFile(*film) || film->duration > 180.0) continue;
		ASSERT_TRUE(static_cast<bool>(film->soundtrack)) << film->key << " has no soundtrack";
		++checked;
		cartoon::audio::Mix a(film->duration), b(film->duration);
		film->soundtrack(a);
		film->soundtrack(b);
		const std::vector<std::int16_t> pa = a.finish(), pb = b.finish();
		EXPECT_EQ(pa, pb) << film->key << ": a film sounds the same every time it is made";
		const auto rms = [&](double t0, double t1) {
			double e = 0.0;
			std::size_t n = 0;
			for (std::size_t i = static_cast<std::size_t>(t0 * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(t1 * cartoon::audio::kRate) * 2 && i < pa.size(); ++i, ++n)
				e += static_cast<double>(pa[i]) * pa[i];
			return std::sqrt(e / static_cast<double>(std::max<std::size_t>(1, n))) / 32768.0;
		};
		for (double t = 0.0; t + 6.0 < film->duration - 1.0; t += 2.0) EXPECT_GT(rms(t, t + 6.0), 0.0008) << film->key << ": silence from " << t << " s";
		int peak = 0;
		for (std::int16_t s : pa) peak = std::max(peak, std::abs(static_cast<int>(s)));
		EXPECT_LT(peak, 32767) << film->key << ": clipped";
		std::vector<float> left, right;
		for (std::size_t i = 0; i + 1 < pa.size(); i += 2) left.push_back(pa[i] / 32768.0f), right.push_back(pa[i + 1] / 32768.0f);
		EXPECT_NEAR(cartoon::audio::loudness(left, right), -16.0, 2.0) << film->key << ": its loudness";
	}
	EXPECT_GE(checked, 5) << "the bundled film files were found (they are read from pbrt_scenes/ in the working directory)";
}

// The whole way a video is made, as a person makes one: the program renders a film's frames in runs of their own, turns them into pictures, makes the soundtrack, has ffmpeg
// make the MP4, and keeps its own log beside the video, which ends with what went wrong (here, nothing). Skipped where ffmpeg is not installed (the MP4 needs it; a Mac uses
// its own encoder first, but the test asks for the same file).
TEST(FilmRenderTest, AShortVideoIsMadeWithItsSoundAndItsLog) {
	const std::string launcher = findRayTracerLauncher();
	if (launcher.empty()) GTEST_SKIP() << "ray_tracer was not found";
	const std::filesystem::path d = scratch("video");
	if (runLauncher("ffmpeg", "-version", d / "ffmpeg.txt") != 0) GTEST_SKIP() << "ffmpeg is not on the PATH";
	const std::filesystem::path out = d / "v.ppm";
	const int rc = runLauncher(launcher, "--cpu --video-preset ball-parade --frames 3 --output \"" + out.string() + "\" --height 27 48 2 3", d / "run.txt");
	const std::string run = slurpFile(d / "run.txt");
	ASSERT_EQ(rc, 0) << run;
	const std::filesystem::path video = d / "v_video.mp4", sound = d / "v_video.wav", log = d / "v_video.log";
	ASSERT_TRUE(std::filesystem::exists(video)) << run;
	EXPECT_GT(std::filesystem::file_size(video), 1000u);
	EXPECT_TRUE(std::filesystem::exists(sound)) << "the soundtrack beside it";
	ASSERT_TRUE(std::filesystem::exists(log)) << "the run's own log";
	const std::string text = slurpFile(log);
	EXPECT_NE(text.find("[3/3]"), std::string::npos) << "the frames' lines are in it";
	EXPECT_NE(text.find("No problems were reported"), std::string::npos) << "and it ends saying nothing went wrong:\n" << text;
	EXPECT_NE(run.find("No problems were reported"), std::string::npos) << "as the console does";
	std::error_code ec;
	std::filesystem::remove_all(d, ec);
}
