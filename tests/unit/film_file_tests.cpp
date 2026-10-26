// film_file_tests.cpp - films made from files (src/shared/cartoon/film_file.h): the film file's grammar and its error messages, the cutting of a pbrt scene into plain text and animated
// blocks, what each animation line does to the scene text at a given time, and the registry that puts such films beside the built-in ones.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>

#include "../../src/shared/cartoon/cartoons.h"
#include "../../src/shared/cartoon/fountain.h"
#include "../../src/shared/cartoon/film_score.h"
#include "../../src/shared/pbrt_discover.h"
#include "../../launcher/process.h"   // (a speech engine for the real voices)

namespace ff = cartoon::film_file;

namespace {

const char *kScene = R"(# a scene
LookAt 0 3 10  0 1 0  0 1 0
Camera "perspective" "float fov" [ 40 ]
Film "rgb" "integer xresolution" [ 320 ] "integer yresolution" [ 180 ]
Sampler "halton" "integer pixelsamples" [ 77 ]
WorldBegin
LightSource "infinite" "rgb L" [ 0.1 0.1 0.1 ]
Shape "sphere" "float radius" [ 1 ]

# @anim fan
AttributeBegin
  Translate 0 2 0
  AttributeBegin
    Shape "sphere" "float radius" [ 0.5 ]
  AttributeEnd
AttributeEnd

# @anim lamp
AttributeBegin
  AreaLightSource "diffuse" "rgb L" [ 10 10 10 ]
  Shape "sphere" "float radius" [ 0.2 ]
AttributeEnd
Texture "t" "spectrum" "imagemap" "string filename" "pics/wood.png"
)";

std::filesystem::path tempDir(const std::string &name) {
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_film_test_" + name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	return d;
}
void write(const std::filesystem::path &p, const std::string &text) { std::ofstream(p, std::ios::binary) << text; }

// A film in a temporary folder with the test scene; returns the loaded film (and the problem, if any).
std::string loadFilm(const std::string &filmText, ff::DataFilm &film, const std::string &sceneText = kScene) {
	static int counter = 0;
	const std::filesystem::path d = tempDir("load" + std::to_string(counter++));
	write(d / "s.pbrt", sceneText);
	write(d / "t.film", filmText);
	return film.load((d / "t.film").string());
}

const std::string kGoodFilm = "scene s.pbrt\nlength 10\n";

}  // namespace

// ---- the film file --------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmFile, AFilmFileIsReadWithItsShotsAndAnimations) {
	const ff::Parsed p = ff::parse("film my-film\nname My Film\nabout A story.\nscene s.pbrt\nlength 12\nfps 30\nstill 3\ntags emissive,dusk\nease 0\n"
								   "shot 0 First\n key 0 0 5  0 0 0\n key 1 0 5  0 0 0\nshot 6 Second\n key 0 2 5  0 0 0\n"
								   "move a 0 1 2 3  5 4 5 6\nturn a about 1 1 1 axis 0 0 1  0 0  10 90\ngrow b about 0 0 0  0 1  5 2\nlight c 0 0  1 1\nshow d 2 4  6 8\n",
								   "t.film");
	ASSERT_TRUE(p.ok) << p.error;
	EXPECT_EQ(p.spec.key, "my-film");
	EXPECT_EQ(p.spec.name, "My Film");
	EXPECT_EQ(p.spec.fps, 30);
	EXPECT_DOUBLE_EQ(p.spec.still, 3.0);
	EXPECT_FALSE(p.spec.ease);
	ASSERT_EQ(p.spec.shots.size(), 2u);
	EXPECT_EQ(p.spec.shots[1].name, "Second");
	EXPECT_EQ(p.spec.shots[0].path.keys.size(), 2u);
	ASSERT_EQ(p.spec.anims.size(), 5u);
	EXPECT_EQ(p.spec.anims[0].keys.size(), 2u);
	EXPECT_DOUBLE_EQ(p.spec.anims[1].about[0], 1.0);
	EXPECT_DOUBLE_EQ(p.spec.anims[1].axis[2], 1.0);
	EXPECT_EQ(p.spec.anims[4].intervals.size(), 2u);
}

TEST(FilmFile, AProblemIsReportedWithTheFileAndTheLine) {
	struct Case {
		const char *text;
		const char *line;
		const char *about;
	};
	const Case cases[] = {
		{"scene s.pbrt\nlength 10\nflavour 3\n", "t.film:3:", "unknown word"},
		{"scene s.pbrt\nlength -1\n", "t.film:2:", "length"},
		{"scene s.pbrt\nlength 10\nfps 2.5\n", "t.film:3:", "fps"},
		{"scene s.pbrt\nlength 10\nmove a 0 1 2\n", "t.film:3:", "groups of time x y z"},
		{"scene s.pbrt\nlength 10\nturn a axis 0 0 0  0 0\n", "t.film:3:", "axis"},
		{"scene s.pbrt\nlength 10\nmove a 5 0 0 0  1 1 1 1\n", "t.film:3:", "go back"},
		{"scene s.pbrt\nlength 10\nkey 0 0 5 0 0 0\n", "t.film:3:", "shot"},
		{"scene s.pbrt\nlength 10\nshot 2 A\nkey 0 0 5 0 0 0\n", "t.film:3:", "first shot starts at 0"},
		{"scene s.pbrt\nlength 10\nshot 0 A\nkey 0 0 5 0 0 0\nshot 0 B\nkey 0 0 5 0 0 0\n", "t.film:5:", "later"},
		{"scene s.pbrt\nlength 10\nshot 0 A\n", "t.film:", "no `key` lines"},
		{"scene s.pbrt\nlength 10\nshot 0 A\nkey 1 2\n", "t.film:", "key"},
		{"length 10\n", "t.film:0:", "no `scene"},
		{"scene s.pbrt\n", "t.film:0:", "no `length"},
		{"film Bad_Key\nscene s.pbrt\nlength 3\n", "t.film:1:", "lower-case"},
		{"scene s.pbrt\nlength 10\nshot 0 A\nkey 0 0 5 0 0 0\nshot 12 B\nkey 0 0 5 0 0 0\n", "t.film:", "after the film's end"},
	};
	for (const Case &c : cases) {
		const ff::Parsed p = ff::parse(c.text, "t.film");
		EXPECT_FALSE(p.ok) << c.text;
		EXPECT_NE(p.error.find(c.line), std::string::npos) << p.error << " for " << c.text;
		EXPECT_NE(p.error.find(c.about), std::string::npos) << p.error << " for " << c.text;
	}
}

// ---- the scene ------------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmFile, ASceneIsCutIntoItsHeaderItsPlainTextAndItsAnimatedBlocks) {
	ff::Scene s;
	ASSERT_EQ(ff::cutScene(kScene, s), "");
	EXPECT_TRUE(s.hasLookAt);
	EXPECT_TRUE(s.hasCamera);
	EXPECT_EQ(s.samples, 77);
	ASSERT_EQ(s.blocks.size(), 2u);
	EXPECT_EQ(s.blocks[0].name, "fan");
	EXPECT_NE(s.blocks[0].head.find("AttributeBegin"), std::string::npos);
	EXPECT_NE(s.blocks[0].body.find("AttributeEnd"), std::string::npos);
	EXPECT_NE(s.blocks[0].body.find("radius\" [ 0.5"), std::string::npos) << "the block's inner AttributeEnd does not end it";
	ASSERT_EQ(s.names.size(), 2u);
	EXPECT_EQ(s.header.find("WorldBegin") != std::string::npos, true);

	ff::Scene bad;
	EXPECT_NE(ff::cutScene("WorldBegin\n# @anim x\nShape \"sphere\"\n", bad).find("not followed by an AttributeBegin"), std::string::npos);
	ff::Scene open;
	EXPECT_NE(ff::cutScene("WorldBegin\n# @anim x\nAttributeBegin\nAttributeBegin\nAttributeEnd\n", open).find("never closed"), std::string::npos);
	ff::Scene none;
	EXPECT_NE(ff::cutScene("Shape \"sphere\"\n", none).find("WorldBegin"), std::string::npos);
}

TEST(FilmFile, AFilmThatNamesABlockTheSceneDoesNotHaveIsRefusedWithTheNamesItHas) {
	ff::DataFilm film;
	const std::string problem = loadFilm(kGoodFilm + "move wheel 0 0 0 0  1 1 1 1\n", film);
	EXPECT_NE(problem.find("t.film:3:"), std::string::npos) << problem;
	EXPECT_NE(problem.find("wheel"), std::string::npos);
	EXPECT_NE(problem.find("fan, lamp"), std::string::npos) << "it lists what the scene has: " << problem;
	ff::DataFilm missing;
	EXPECT_NE(loadFilm("scene nothing.pbrt\nlength 4\n", missing).find("cannot be read"), std::string::npos);
}

// ---- the animation --------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmFile, AMoveTurnAndGrowAreInsertedAfterTheBlocksAttributeBeginWhereTheyAreAtThatTime) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm(kGoodFilm + "ease 0\nmove fan 0 0 0 0  10 10 20 30\nturn fan about 0 1 0 axis 0 0 1  0 0  10 100\ngrow fan about 0 0 0  0 1  10 3\n", film), "");
	const std::string at5 = film.sceneAt(5.0);
	// halfway: moved 5 10 15, turned 50 degrees about the point, scaled 2
	EXPECT_NE(at5.find("Translate 5 10 15"), std::string::npos) << at5;
	EXPECT_NE(at5.find("Rotate 50 0 0 1"), std::string::npos);
	EXPECT_NE(at5.find("Scale 2 2 2"), std::string::npos);
	// after the AttributeBegin of the block, before its own Translate
	const std::size_t begin = at5.find("# @anim fan"), moved = at5.find("Translate 5 10 15"), own = at5.find("Translate 0 2 0");
	ASSERT_NE(begin, std::string::npos);
	EXPECT_LT(begin, moved);
	EXPECT_LT(moved, own);
	// before the first key and after the last, the values hold
	EXPECT_NE(film.sceneAt(-3.0).find("Translate 0 0 0"), std::string::npos);
	EXPECT_NE(film.sceneAt(99.0).find("Translate 10 20 30"), std::string::npos);
	// the plain parts and the other block are untouched
	EXPECT_NE(at5.find("LightSource \"infinite\""), std::string::npos);
	EXPECT_EQ(at5.find("Translate 5 10 15"), at5.rfind("Translate 5 10 15"));
}

TEST(FilmFile, EasingMakesAMotionStartAndStopGently) {
	ff::DataFilm eased, even;
	ASSERT_EQ(loadFilm(kGoodFilm + "move fan 0 0 0 0  10 10 0 0\n", eased), "");
	ASSERT_EQ(loadFilm(kGoodFilm + "ease 0\nmove fan 0 0 0 0  10 10 0 0\n", even), "");
	auto x = [](const ff::DataFilm &f, double t) {
		const std::string s = f.sceneAt(t);
		std::smatch m;
		std::regex_search(s, m, std::regex("Translate ([-0-9.e+]+) 0 0"));
		return std::stod(m[1]);
	};
	EXPECT_NEAR(x(even, 2.5), 2.5, 1e-4);
	EXPECT_LT(x(eased, 2.5), 1.8) << "slow at the start";
	EXPECT_NEAR(x(eased, 5.0), 5.0, 1e-4);
}

TEST(FilmFile, ALightFactorScalesTheLightsOfItsBlockAndShowLeavesABlockOutOutsideItsTimes) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm(kGoodFilm + "ease 0\nlight lamp 0 0  10 1\nshow fan 2 4\n", film), "");
	const std::string at5 = film.sceneAt(5.0);
	EXPECT_NE(at5.find("AreaLightSource \"diffuse\" \"rgb L\" [ 10 10 10 ] \"float scale\" [ 0.5 ]"), std::string::npos) << at5;
	EXPECT_EQ(at5.find("radius\" [ 0.5"), std::string::npos) << "the fan is not shown at 5 s";
	EXPECT_NE(film.sceneAt(3.0).find("radius\" [ 0.5"), std::string::npos) << "and is at 3 s";
	EXPECT_NE(film.sceneAt(10.0).find("\"float scale\" [ 1 ]"), std::string::npos);
	// a light that already has a scale has it multiplied
	std::string sceneWithScale = kScene;
	sceneWithScale.replace(sceneWithScale.find("[ 10 10 10 ]"), 12, "[ 10 10 10 ] \"float scale\" [ 3 ]");
	ff::DataFilm film2;
	ASSERT_EQ(loadFilm(kGoodFilm + "ease 0\nlight lamp 0 2  10 2\n", film2, sceneWithScale), "");
	EXPECT_NE(film2.sceneAt(5.0).find("\"float scale\" [ 6 ]"), std::string::npos) << film2.sceneAt(5.0);
}

TEST(FilmFile, TheCameraFollowsTheShotsAndAFilmWithoutShotsKeepsTheScenesOwn) {
	ff::DataFilm plain;
	ASSERT_EQ(loadFilm(kGoodFilm, plain), "");
	EXPECT_NE(plain.sceneAt(1.0).find("LookAt 0 3 10  0 1 0  0 1 0"), std::string::npos) << "the scene's own camera";
	EXPECT_EQ(plain.shotStarts(), std::vector<double>{0.0});

	ff::DataFilm film;
	ASSERT_EQ(loadFilm(kGoodFilm + "shot 0 One\n key 0 0 10  0 0 0  30\n key 10 0 10  0 0 0  30\nshot 5 Two\n key 0 5 20  0 1 0  50\n", film), "");
	EXPECT_EQ(film.shotStarts(), (std::vector<double>{0.0, 5.0}));
	EXPECT_EQ(film.shotNames(), "One|Two");
	const std::string a = film.headerAt(0.0), b = film.headerAt(6.0);
	EXPECT_NE(a.find("LookAt 0 0 10  0 0 0  0 1 0"), std::string::npos) << a;
	EXPECT_NE(a.find("\"float fov\" [ 30 ]"), std::string::npos);
	EXPECT_NE(b.find("LookAt 0 5 20  0 1 0  0 1 0"), std::string::npos) << "a hard cut to the second shot";
	EXPECT_NE(b.find("\"float fov\" [ 50 ]"), std::string::npos);
	EXPECT_EQ(a.find("[ 40 ]"), std::string::npos) << "the scene's old camera line is replaced";
	EXPECT_NE(a.find("pixelsamples\" [ 77 ]"), std::string::npos) << "the rest of the header stays";
	EXPECT_EQ(a.substr(a.size() - 11), "WorldBegin\n");
	// the first shot moves between its keys
	EXPECT_NE(film.headerAt(2.5).find("LookAt 5 0 10"), std::string::npos) << film.headerAt(2.5);
}

TEST(FilmFile, ASceneWithoutACameraGetsOneFromTheShots) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm(kGoodFilm + "shot 0 Only\n key 1 2 3  0 0 0\n", film, "Film \"rgb\" \"integer xresolution\" [ 8 ]\nWorldBegin\nShape \"sphere\"\n"), "");
	const std::string h = film.headerAt(0.0);
	EXPECT_NE(h.find("LookAt 1 2 3"), std::string::npos) << h;
	EXPECT_LT(h.find("LookAt"), h.find("Camera \"perspective\""));
	EXPECT_LT(h.find("Camera \"perspective\""), h.find("WorldBegin"));
}

TEST(FilmFile, RelativeFileNamesInTheSceneAreMadeAbsoluteBesideItsFile) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm(kGoodFilm, film), "");
	const std::string s = film.sceneAt(1.0);
	const std::size_t at = s.find("\"string filename\" \"");
	ASSERT_NE(at, std::string::npos);
	const std::string name = s.substr(at + 19, s.find('"', at + 19) - at - 19);
	EXPECT_TRUE(std::filesystem::path(name).is_absolute()) << name;
	EXPECT_NE(name.find("pics/wood.png"), std::string::npos);
}

// ---- the registry ---------------------------------------------------------------------------------------------------------------------------------------------------------

TEST(FilmFile, FilmsFromFilesAreFilmsLikeTheOthersWithAStableNumberFilteredTagsAndNoCollisions) {
	const std::filesystem::path d = tempDir("registry");
	write(d / "s.pbrt", kScene);
	write(d / "alpha.film", "name Alpha\nscene s.pbrt\nlength 8\ntags emissive,made-up,dusk\nshot 0 A\n key 0 0 5  0 0 0\nshot 4 B\n key 0 0 6  0 0 0\n");
	write(d / "dup.film", "film fireflies\nscene s.pbrt\nlength 8\n");   // a built-in film's key
	write(d / "bad.film", "scene s.pbrt\nlength x\n");
	cartoon::detail::Registry r;
	for (const cartoon::Film &f : cartoon::kFilms) r.all.push_back(&f);
	for (const char *file : {"alpha.film", "dup.film", "bad.film"}) cartoon::detail::addFilmFile(r, (d / file).string());
	ASSERT_EQ(r.all.size(), static_cast<std::size_t>(cartoon::kFilmCount) + 1u) << "only the good one was added";
	EXPECT_EQ(r.problems.size(), 2u);
	const cartoon::Film &f = *r.all.back();
	EXPECT_STREQ(f.key, "alpha");
	EXPECT_STREQ(f.name, "Alpha");
	EXPECT_GE(f.meta.id, 1000);
	EXPECT_EQ(cartoon::tagsOf(f), (std::vector<std::string>{"emissive", "dusk"})) << "the words of the vocabulary only";
	EXPECT_EQ(cartoon::shotCountOf(f), 2);
	EXPECT_EQ(cartoon::frameCountOf(f), 192);
	EXPECT_NE(f.sceneAt(1.0).find("WorldBegin"), std::string::npos);
	EXPECT_EQ(cartoon::presetIdOf(f), "V" + std::to_string(f.meta.id));
	// the same key gives the same number in another registry, whatever else is in it
	cartoon::detail::Registry other;
	cartoon::detail::addFilmFile(other, (d / "alpha.film").string());
	ASSERT_EQ(other.all.size(), 1u);
	EXPECT_EQ(other.all[0]->meta.id, f.meta.id);
	EXPECT_NE(cartoon::summaryOf(f).find("A film made from"), std::string::npos) << "a film with no `about` still says something";
}

// Every film has a soundtrack: a film file with nothing to hear plays the score drafted from it (no cues file), `music none` leaves the score out (a soft wind then, if nothing else
// is heard), and its set's sound stays under the score. Each is heard over its length and is the same every time it is made.
TEST(FilmFile, EveryFilmFileHasASoundtrackEvenWithNothingInItToHear) {
	const std::filesystem::path d = tempDir("soundtracks");
	write(d / "s.pbrt", kScene);
	const std::string shots = "scene s.pbrt\nlength 8\nshot 0 A\n key 0 0 5  0 0 0\nshot 4 B\n key 0 0 6  0 0 0\n";
	write(d / "plain.film", shots);
	write(d / "hushed.film", shots + "music none\n");
	write(d / "brook.film", shots + "music none\nambience water 0.8\n");
	write(d / "both.film", shots + "music draft\nambience water 0.8\n");
	write(d / "loud.film", shots + "music loud\n");
	cartoon::detail::Registry r;
	for (const char *file : {"plain.film", "hushed.film", "brook.film", "both.film", "loud.film"}) cartoon::detail::addFilmFile(r, (d / file).string());
	ASSERT_EQ(r.all.size(), 4u);
	ASSERT_EQ(r.problems.size(), 1u);
	EXPECT_NE(r.problems[0].find("`music none`"), std::string::npos) << r.problems[0];
	std::map<std::string, std::vector<std::int16_t>> heard;
	for (const cartoon::Film *f : r.all) {
		ASSERT_TRUE(static_cast<bool>(f->soundtrack)) << f->key << " has no soundtrack";
		cartoon::audio::Mix a(f->duration), b(f->duration);
		f->soundtrack(a);
		f->soundtrack(b);
		const std::vector<std::int16_t> pa = a.finish();
		EXPECT_EQ(pa, b.finish()) << f->key << ": the same every time";
		for (double t = 0.5; t + 2.0 < f->duration - 1.0; t += 1.0) {
			double e = 0.0;
			const std::size_t i0 = static_cast<std::size_t>(t * cartoon::audio::kRate) * 2, i1 = static_cast<std::size_t>((t + 2.0) * cartoon::audio::kRate) * 2;
			for (std::size_t i = i0; i < i1; ++i) e += static_cast<double>(pa[i]) * pa[i];
			EXPECT_GT(std::sqrt(e / static_cast<double>(i1 - i0)) / 32768.0, 0.0008) << f->key << ": silence from " << t << " s";
		}
		heard[f->key] = pa;
	}
	EXPECT_NE(heard["plain"], heard["hushed"]) << "the drafted score is played";
	EXPECT_NE(heard["hushed"], heard["brook"]) << "the film's own sound, not the wind, when it has one";
	EXPECT_NE(heard["brook"], heard["both"]) << "the score over the set's sound";
}

// A video of a film at another length gets its soundtrack stretched to fit at the same pitch: exactly the video's length, a tone still the same tone, as loud, and a click at a
// moment of the film still at that moment of the video.
TEST(CartoonAudioStretch, ASoundtrackStretchedToAnotherLengthKeepsItsPitchLoudnessAndTiming) {
	const double seconds = 4.0;
	const std::size_t n = static_cast<std::size_t>(seconds * cartoon::audio::kRate);
	std::vector<std::int16_t> pcm(n * 2);
	for (std::size_t i = 0; i < n; ++i) {
		const double t = static_cast<double>(i) / cartoon::audio::kRate;
		const double v = 8000.0 * std::sin(cartoon::audio::kTau * 440.0 * t) + (std::abs(t - 2.0) < 0.002 ? 20000.0 : 0.0);
		pcm[2 * i] = pcm[2 * i + 1] = static_cast<std::int16_t>(std::lround(std::max(-32767.0, std::min(32767.0, v))));
	}
	for (const double factor : {1.5, 0.6}) {
		const std::size_t frames = static_cast<std::size_t>(static_cast<double>(n) * factor);
		const std::vector<std::int16_t> out = cartoon::audio::stretched(pcm, frames);
		ASSERT_EQ(out.size(), frames * 2) << factor;
		// the tone: its rising zero crossings over the middle second, and its loudness (away from the click)
		const std::size_t m0 = frames / 8, m1 = m0 + static_cast<std::size_t>(cartoon::audio::kRate) / 2;
		int rises = 0;
		double e = 0.0;
		for (std::size_t i = m0; i < m1; ++i) {
			rises += out[2 * (i - 1)] < 0 && out[2 * i] >= 0;
			e += static_cast<double>(out[2 * i]) * out[2 * i];
		}
		EXPECT_NEAR(rises * 2.0, 440.0, 440.0 * 0.03) << factor << ": the same pitch";
		EXPECT_NEAR(std::sqrt(e / static_cast<double>(m1 - m0)), 8000.0 / std::sqrt(2.0), 8000.0 / std::sqrt(2.0) * 0.12) << factor << ": as loud";
		// the click: the loudest sample, where the film's 2 s falls in the video
		std::size_t loudest = 0;
		for (std::size_t i = 0; i < frames; ++i)
			if (std::abs(out[2 * i]) > std::abs(out[2 * loudest])) loudest = i;
		EXPECT_NEAR(static_cast<double>(loudest) / cartoon::audio::kRate, 2.0 * factor, 0.03) << factor << ": in step with the pictures";
	}
	// within 0.1% of its length the sound is only cut or padded
	const std::vector<std::int16_t> same = cartoon::audio::stretched(pcm, n + 10);
	EXPECT_TRUE(std::equal(pcm.begin(), pcm.end(), same.begin()));
	EXPECT_EQ(same.back(), 0);
}

// Every film the program has - built in or read from a film file in the scene folders - has a soundtrack.
TEST(FilmFile, EveryFilmInTheProgramHasASoundtrack) {
	for (const cartoon::Film *f : cartoon::films()) EXPECT_TRUE(static_cast<bool>(f->soundtrack)) << f->key << " has no soundtrack";
}

// A film in parts: each part a film of its own - its set, its length, its shots, its lines - played one after another as one film, the lines before the first part shared
// by every part (here the cast and a set) and the film's own (its key, name, tags, still) the whole film's.
TEST(FilmParts, AFilmInPartsPlaysItsPartsOneAfterAnotherEachOnItsOwnSet) {
	const std::filesystem::path d = tempDir("parts");
	write(d / "s.pbrt", kScene);
	write(d / "b.pbrt", "LookAt 0 3 10  0 1 0  0 1 0\nCamera \"perspective\" \"float fov\" [ 40 ]\nWorldBegin\nShape \"cylinder\" \"float radius\" [ 3 ]\n");
	write(d / "journey.film", "film journey\nname The Journey\ntags dusk,made-up\nstill 9\nscene s.pbrt\ncharacter ann person\n"
							  "part Home\nlength 6\nshot 0 Door\n key 0 1 5  0 1 0\nshot 3 Hall\n key 0 2 6  0 1 0\n"
							  "part \"The Lake\" dissolve 1   # a part comes in as a shot does\nscene b.pbrt\nlength 8\nsay ann 1 \"Hello there.\"\nshot 0 Shore\n key 0 1 9  0 1 0\n");
	cartoon::detail::Registry r;
	cartoon::detail::addFilmFile(r, (d / "journey.film").string());
	ASSERT_TRUE(r.problems.empty()) << r.problems[0];
	ASSERT_EQ(r.all.size(), 1u) << "one film, not one for each part";
	const cartoon::Film &f = *r.all[0];
	EXPECT_STREQ(f.key, "journey");
	EXPECT_STREQ(f.name, "The Journey");
	EXPECT_DOUBLE_EQ(f.duration, 14.0);
	EXPECT_DOUBLE_EQ(f.stillTime, 9.0) << "a time in the whole film";
	EXPECT_EQ(cartoon::tagsOf(f), (std::vector<std::string>{"dusk"}));
	EXPECT_GE(f.meta.id, 1000);
	EXPECT_EQ(f.shotStarts(), (std::vector<double>{0.0, 3.0, 6.0}));
	EXPECT_STREQ(f.meta.shotNames, "Door|Hall|Shore");
	// each part on its own set, at its own time
	EXPECT_EQ(f.sceneAt(2.0).find("cylinder"), std::string::npos);
	EXPECT_NE(f.sceneAt(2.0).find("\"sphere\""), std::string::npos) << "the first part's set";
	EXPECT_NE(f.sceneAt(7.0).find("cylinder"), std::string::npos) << "the second part's set";
	EXPECT_NE(f.headerAt(7.0).find("LookAt"), std::string::npos);
	// the way in to the second part: a dissolve at its start, whose other side is the other part's scene
	ASSERT_TRUE(static_cast<bool>(f.transitions));
	const std::vector<cartoon::Transition> tr = f.transitions();
	ASSERT_EQ(tr.size(), 1u);
	EXPECT_EQ(tr[0].kind, cartoon::Transition::Dissolve);
	EXPECT_DOUBLE_EQ(tr[0].at, 6.0);
	EXPECT_DOUBLE_EQ(tr[0].length, 1.0);
	EXPECT_NE(f.sceneAcross(5.8).find("cylinder"), std::string::npos) << "before the cut, the part coming in";
	EXPECT_EQ(f.sceneAcross(6.2).find("cylinder"), std::string::npos) << "after it, the part going out";
	EXPECT_NE(f.sceneAcross(9.0).find("cylinder"), std::string::npos) << "away from the cut, the scene itself";
	// the shared character speaks in the second part: its caption and its voice at the second part's time
	ASSERT_TRUE(static_cast<bool>(f.captions));
	const std::vector<cartoon::Caption> words = f.captions();
	ASSERT_EQ(words.size(), 1u);
	EXPECT_GE(words[0].t0, 7.0);
	ASSERT_TRUE(static_cast<bool>(f.speechAt));
	EXPECT_EQ(f.speechAt(1.5), 0.0);
	EXPECT_GT(f.speechAt(words[0].t0 + 0.1), 0.0);
	// its sound: both parts', heard from start to end
	ASSERT_TRUE(static_cast<bool>(f.soundtrack));
	cartoon::audio::Mix m(f.duration);
	f.soundtrack(m);
	const std::vector<std::int16_t> pcm = m.finish();
	for (double t = 0.5; t + 2.0 < f.duration - 1.0; t += 1.0) {
		double e = 0.0;
		const std::size_t i0 = static_cast<std::size_t>(t * cartoon::audio::kRate) * 2, i1 = static_cast<std::size_t>((t + 2.0) * cartoon::audio::kRate) * 2;
		for (std::size_t i = i0; i < i1; ++i) e += static_cast<double>(pcm[i]) * pcm[i];
		EXPECT_GT(std::sqrt(e / static_cast<double>(i1 - i0)) / 32768.0, 0.0008) << "silence from " << t << " s";
	}
	// a part read on its own: the whole film's key and its number, the part's name
	ff::DataFilm second;
	ASSERT_EQ(second.load((d / "journey.film").string(), 1), "");
	EXPECT_EQ(second.spec.key, "journey--2");
	EXPECT_EQ(second.spec.name, "The Lake");
	EXPECT_EQ(second.spec.characters.size(), 1u) << "the shared cast";
	ff::DataFilm whole;
	EXPECT_NE(whole.load((d / "journey.film").string()).find("a film in parts"), std::string::npos) << "the file as one film is not a film";
}

TEST(FilmParts, WhatIsWrongWithAFilmInPartsIsSaidWithItsLine) {
	const auto problem = [](const std::string &text) { return ff::partsOf(text, "f.film").error; };
	EXPECT_EQ(problem("scene s.pbrt\nlength 4\n"), "") << "not in parts";
	EXPECT_TRUE(ff::partsOf("scene s.pbrt\nlength 4\n", "f.film").parts.empty());
	EXPECT_NE(problem("scene s.pbrt\nshot 0 A\npart B\n").find("f.film:2: a shot belongs to a part"), std::string::npos);
	EXPECT_NE(problem("part A\npart A\n").find("f.film:2: there is already a part called \"A\""), std::string::npos);
	EXPECT_NE(problem("part A spin 2\n").find("f.film:1: a part comes in with"), std::string::npos);
	EXPECT_NE(problem("part\n").find("a part has a name"), std::string::npos);
	EXPECT_NE(problem("music loud\npart A\n").find("`music none`"), std::string::npos);
	// a part's lines are the file's own lines (the others blank): a problem in one names the file's line
	const std::filesystem::path d = tempDir("partsbad");
	write(d / "s.pbrt", kScene);
	write(d / "bad.film", "scene s.pbrt\npart A\nlength 4\npart B\nlength x\n");
	ff::DataFilm b;
	EXPECT_NE(b.load((d / "bad.film").string(), 1).find("bad.film:5:"), std::string::npos);
	cartoon::detail::Registry r;
	cartoon::detail::addFilmFile(r, (d / "bad.film").string());
	EXPECT_TRUE(r.all.empty());
	ASSERT_EQ(r.problems.size(), 1u);
	EXPECT_NE(r.problems[0].find("bad.film:5:"), std::string::npos) << r.problems[0];
	EXPECT_TRUE(r.data.empty()) << "nothing of it is kept";
}

// The library: a cast (what a `character` line says but where it stands) and a set (film-file lines: a scene from the set's folder and what dresses it) brought in by name,
// beside the film file; a cast's voice is under each of its lines; what is wrong in a library file is said with that file's own line.
TEST(FilmLibrary, CastsAndSetsAreBroughtInByNameFromTheLibrary) {
	const std::filesystem::path d = tempDir("library");
	std::filesystem::create_directories(d / "library" / "rooms");
	write(d / "library" / "rooms" / "den.pbrt", kScene);
	write(d / "library" / "den.set", "# a den\nscene rooms/den.pbrt\nambience night 0.4\nplace rock at 1 0 -2\n");
	write(d / "library" / "bea.cast", "# Bea\nkind person\nsweater #c0392b   # a colour, not a comment\nhair blond\npitch -3\nrate 0.9\n");
	write(d / "f.film", "set den\nlength 6\ncast bea at 0.5 0 1 facing 90\ncast twin as bea at -0.5 0 1 hair black\nsay bea 1 \"Hello.\" pitch 1\nshot 0 A\n key 0 1 5  0 1 0\n");
	ff::DataFilm film;
	ASSERT_EQ(film.load((d / "f.film").string()), "");
	EXPECT_EQ(std::filesystem::path(film.spec.scene).lexically_normal(), (d / "library" / "rooms" / "den.pbrt").lexically_normal()) << "the set's scene is from the set's folder";
	EXPECT_NE(film.sceneAt(1.0).find("@anim"), std::string::npos) << "the set's scene is the film's";
	ASSERT_EQ(film.spec.ambience.size(), 1u);
	EXPECT_EQ(film.spec.ambience[0].kind, "night");
	EXPECT_EQ(film.spec.placements.size(), 1u);
	ASSERT_EQ(film.spec.characters.size(), 2u);
	const ff::Character &bea = film.spec.characters[0], &twin = film.spec.characters[1];
	EXPECT_EQ(bea.name, "bea");
	EXPECT_EQ(bea.kind, "person");
	EXPECT_DOUBLE_EQ(bea.start[0], 0.5);
	EXPECT_DOUBLE_EQ(bea.facing, 90.0);
	EXPECT_EQ(bea.line, 3) << "the film's line that brought it in";
	ASSERT_TRUE(bea.colours.count("sweater"));
	cartoon::Vec3 red;
	ASSERT_TRUE(ff::colourOf("#c0392b", red));
	EXPECT_DOUBLE_EQ(bea.colours.at("sweater").x, red.x) << "#c0392b is a colour";
	EXPECT_DOUBLE_EQ(bea.pitch, -3.0);
	EXPECT_EQ(twin.name, "twin");
	EXPECT_EQ(twin.kind, "person");
	EXPECT_NE(twin.colours.at("hair").x, bea.colours.at("hair").x) << "this film's look on top of the cast's";
	ASSERT_EQ(film.spec.lines.size(), 1u);
	EXPECT_DOUBLE_EQ(film.spec.lines[0].pitch, -2.0) << "the voice's pitch under the line's own";
	EXPECT_DOUBLE_EQ(film.spec.lines[0].rate, 0.9);
	// what is wrong: the film's line, or the library file's own
	const auto problem = [&](const std::string &text) {
		write(d / "g.film", text);
		ff::DataFilm g;
		return g.load((d / "g.film").string());
	};
	EXPECT_NE(problem("set nowhere\nlength 4\n").find("g.film:1: no set called \"nowhere\""), std::string::npos);
	EXPECT_NE(problem("set ../x\nlength 4\n").find("g.film:1: `set NAME`"), std::string::npos);
	EXPECT_NE(problem("set den\nlength 4\ncast nobody\n").find("g.film:3: no cast called \"nobody\""), std::string::npos);
	write(d / "library" / "bad.set", "scene rooms/den.pbrt\n\nlength 5\n");
	EXPECT_NE(problem("set bad\nlength 4\n").find("bad.set:3: `length` is the film's"), std::string::npos) << problem("set bad\nlength 4\n");
	write(d / "library" / "kindless.cast", "sweater red\n");
	EXPECT_NE(problem("set den\nlength 4\ncast x as kindless\n").find("has no `kind KIND` line"), std::string::npos);
	write(d / "library" / "odd.cast", "kind person\nwings blue\n");
	const std::string odd = problem("set den\nlength 4\ncast x as odd\n");
	EXPECT_NE(odd.find("g.film"), std::string::npos) << odd;
	EXPECT_NE(odd.find("wings"), std::string::npos) << odd;
	// a set in a part of a film in parts
	write(d / "parted.film", "part One\nset den\nlength 3\nshot 0 A\n key 0 1 5  0 1 0\npart Two\nset den\nlength 3\ncast bea\nshot 0 B\n key 0 1 5  0 1 0\n");
	ff::DataFilm two;
	ASSERT_EQ(two.load((d / "parted.film").string(), 1), "");
	EXPECT_EQ(two.spec.characters.size(), 1u);
}

// Real voices (film_tts.h): a film's `voices` and a character's `voice` choose what its lines are said in; with a speech engine a line is said into a recording that the
// mouth, the loudness, the line's end and the soundtrack all follow; without one (and no recording yet) it keeps the babble voice.
TEST(FilmVoices, ALineSaidInAComputersVoiceIsItsRecording) {
	(void)cartoon::films();
	ff::DataFilm parsed;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 6\nvoices system\ncharacter ann person voice Zira\ncharacter bo person voice babble\ncharacter cy person\n", parsed), "");
	EXPECT_EQ(parsed.spec.voices, "system");
	EXPECT_EQ(parsed.voiceAskedOf(parsed.spec.characters[0]), "zira");
	EXPECT_EQ(parsed.voiceAskedOf(parsed.spec.characters[1]), "") << "babble";
	EXPECT_EQ(parsed.voiceAskedOf(parsed.spec.characters[2]), "system") << "the film's";
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 6\nvoices\n", bad).find("`voices babble|system|NAME`"), std::string::npos);
	// a line whose recording is not made, in a program without a speech engine: the babble voice
	const cartoon::tts::Runner kept = cartoon::tts::runner();
	cartoon::tts::runner() = nullptr;
	const std::string unique = "Never said before " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".";
	ff::DataFilm quiet;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 6\nvoices system\ncharacter ann person\nsay ann 1 \"" + unique + "\"\n", quiet), "");
	EXPECT_EQ(quiet.recordingOf(0), nullptr);
	EXPECT_DOUBLE_EQ(quiet.lineEnd(0), cartoon::speech::endOf(quiet.spoken[0], 1.0));
	// with a speech engine
	cartoon::tts::runner() = [](const std::vector<std::string> &command) { return run_subprocess(command, true); };
	if (cartoon::tts::voices().empty()) {
		cartoon::tts::runner() = kept;
		GTEST_SKIP() << "this computer has no text-to-speech voice";
	}
	ff::DataFilm said;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 6\nvoices system\ncharacter ann person\nsay ann 1 \"Hello there, how are you today?\"\n", said), "");
	const cartoon::music_cues::Recording *r = said.recordingOf(0);
	ASSERT_NE(r, nullptr);
	const double end = said.lineEnd(0);
	EXPECT_GT(end, 2.0) << "a sentence takes a second or more";
	EXPECT_LT(end, 5.0);
	double loudest = 0.0, widest = 0.0;
	for (double t = 1.0; t < end; t += 0.01) {
		loudest = std::max(loudest, said.speechAt(t));
		cartoon::film_file::CharacterMoment m;
		said.speakingAt(said.spec.characters[0], t, m);
		widest = std::max(widest, m.mouth.open);
	}
	EXPECT_GT(loudest, 0.5);
	EXPECT_GT(widest, 0.3) << "the mouth opens with the recording";
	EXPECT_EQ(said.speechAt(0.5), 0.0);
	// the same line in another process (another DataFilm) is the same recording
	ff::DataFilm again;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 6\nvoices system\ncharacter ann person\nsay ann 1 \"Hello there, how are you today?\"\n", again), "");
	ASSERT_NE(again.recordingOf(0), nullptr);
	EXPECT_EQ(again.recordingOf(0), r);
	EXPECT_DOUBLE_EQ(again.lineEnd(0), end);
	// the soundtrack has it
	cartoon::audio::Mix with(6.0), without(6.0);
	said.speak(with);
	const std::vector<std::int16_t> a = with.finish(), b = without.finish();
	double energy = 0.0;
	for (std::size_t i = static_cast<std::size_t>(1.2 * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(end * cartoon::audio::kRate) * 2; ++i) energy += std::abs(a[i] - b[i]);
	EXPECT_GT(energy, 1e5);
	cartoon::tts::runner() = kept;
}

// Acting from the words (film_acting.h): a line's words bring a gesture its speaker's kind has and an expression, at the word that brings them.
TEST(FilmActing, ALineIsActedFromItsWords) {
	const std::vector<std::string> person = {"bow", "cheer", "nod", "point", "shake head", "shrug", "think", "wave"};
	const auto cues = [&](const std::string &line, const std::vector<std::string> &poses = {"bow", "cheer", "nod", "point", "shake head", "shrug", "think", "wave"}, bool face = true) {
		std::string out;
		for (const cartoon::acting::Cue &c : cartoon::acting::cuesOf(line, 10.0, 12.0, poses, face)) out += (out.empty() ? "" : ", ") + c.what;
		return out;
	};
	EXPECT_EQ(cues("Hello there!"), "wave");
	EXPECT_EQ(cues("No, I don't think so."), "shake head, thinking");
	EXPECT_EQ(cues("Yes, that's wonderful!"), "cheer, happy");
	EXPECT_EQ(cues("Yes, let's go."), "nod");
	EXPECT_EQ(cues("Look over there."), "point");
	EXPECT_EQ(cues("Where did it go?"), "shrug");
	EXPECT_EQ(cues("Hmm, maybe."), "think, thinking");
	EXPECT_EQ(cues("I'm so sorry."), "sad");
	EXPECT_EQ(cues("Thank you, friend."), "bow, happy");
	EXPECT_EQ(cues("What?!"), "surprised");
	EXPECT_EQ(cues("Where did it go?!"), "surprised") << "an exclaimed question is a surprise, not a shrug";
	EXPECT_EQ(cues("Where did it go?", {"wave", "nod"}), "") << "a kind without the pose does without";
	EXPECT_EQ(cues("I'm so sorry.", person, false), "") << "no face, no expression";
	// at the word that brings it
	const std::vector<cartoon::acting::Cue> c = cartoon::acting::cuesOf("Well, look at that.", 10.0, 12.0, person, true);
	ASSERT_EQ(c.size(), 1u);
	EXPECT_NEAR(c[0].at, 10.0 + 2.0 * 6.0 / 19.0, 1e-9) << "\"look\" starts at the 7th of 19 characters";
	// in a film: `acting words` adds them where the film gives none of its own
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 12\nacting words\ncharacter ann person\ncharacter bo person\nsay ann 1 \"Hello, Bo!\"\nsay bo 3 \"No, I'm sorry.\"\n"
					   "say ann 6 \"Look over there.\"\ngesture ann \"cheer\" 5.5 6 8 8.5\n", film), "");
	const ff::Character &ann = film.spec.characters[0], &bo = film.spec.characters[1];
	ASSERT_EQ(ann.gestures.size(), 2u) << "the wave; the point is not added where ann cheers already";
	EXPECT_EQ(ann.gestures[1].pose, "wave");
	EXPECT_LT(ann.gestures[1].in0, 1.2);
	ASSERT_EQ(bo.gestures.size(), 1u);
	EXPECT_EQ(bo.gestures[0].pose, "shake head");
	EXPECT_LT(bo.gestures[0].out1, 4.5) << "a shake of the head is quick";
	ASSERT_EQ(bo.feelings.size(), 1u);
	EXPECT_EQ(bo.feelings[0].when.pose, "sad");
	ff::DataFilm plain;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 12\ncharacter ann person\nsay ann 1 \"Hello, Bo!\"\n", plain), "");
	EXPECT_TRUE(plain.spec.characters[0].gestures.empty()) << "only with `acting words`";
}

// Every cast and set of the library that comes with the program can be brought into a film.
TEST(FilmLibrary, TheLibraryThatComesWithTheProgramLoads) {
	(void)cartoon::films();   // (the kinds of character)
	const std::string libraryDir = cartoon::film_cache::asset("library");
	if (libraryDir.empty()) GTEST_SKIP() << "run where the scenes are";
	const std::filesystem::path d = tempDir("bundled_library");
	int casts = 0, sets = 0;
	for (const auto &entry : std::filesystem::directory_iterator(libraryDir)) {
		const std::string name = entry.path().stem().string(), ext = entry.path().extension().string();
		if (ext != ".cast" && ext != ".set") continue;
		const std::string text = ext == ".set" ? "set " + name + "\nlength 2\n" : "set meadow\nlength 2\ncast " + name + " at 0 0 0\n";
		write(d / (name + ext + ".film"), text);
		ff::DataFilm film;
		EXPECT_EQ(film.load((d / (name + ext + ".film")).string()), "") << entry.path();
		(ext == ".cast" ? casts : sets) += 1;
	}
	EXPECT_GE(casts, 5);
	EXPECT_GE(sets, 3);
}

TEST(FilmFile, TheExampleFilmIsGoodAndAnimatesWhatItSays) {
	std::string file;
	for (const char *candidate : {"examples/animated/windmill.film", "../examples/animated/windmill.film", "../../examples/animated/windmill.film"})
		if (std::filesystem::exists(candidate)) file = candidate;
	if (file.empty()) GTEST_SKIP() << "run from the repository";
	ff::DataFilm film;
	ASSERT_EQ(film.load(file), "");
	EXPECT_EQ(film.spec.key, "windmill");
	EXPECT_EQ(film.shotStarts().size(), 2u);
	// the sails turn, the ball moves on, the lamp comes up
	const std::string a = film.sceneAt(1.0), b = film.sceneAt(11.0);
	EXPECT_NE(a, b);
	EXPECT_NE(b.find("\"float scale\" [ 1 ]"), std::string::npos);
	EXPECT_EQ(b.find("nan"), std::string::npos);
}

// ---- characters: a film blocked out as a screenplay ------------------------------------------------------------------------------------------------------

// The character lines: a character of a kind from the library, its walks, its gestures (a quoted pose, out times, a contact), a shot that follows it and one that names its
// subject; and the mistakes: an unknown kind, a walk for no one, a gesture without its times, a shot naming someone not brought in.
TEST(FilmFile, CharactersWalksGesturesAndFollowingShotsAreReadAndTheirMistakesSaid) {
	(void)cartoon::films();   // (the library of character kinds)
	const std::string good =
		"scene s.pbrt\nlength 10\n"
		"character kai nomai-explorer at 0 0 4 facing 180 detail 0.05\n"
		"walk kai 1.0 0.8 0 0 0 -3\n"
		"gesture kai \"look up\" 6 7 8 9 contact 6.5\n"
		"shot 0 Walk\n  follow kai behind 3 up 1.8 side 0.5 fov 35\n"
		"shot 5 Wide\n  key 5 2 5 0 1 0\n  subject kai\n";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm(good, film), "");
	ASSERT_EQ(film.spec.characters.size(), 1u);
	const ff::Character &kai = film.spec.characters[0];
	EXPECT_EQ(kai.kind, "nomai-explorer");
	EXPECT_DOUBLE_EQ(kai.start[2], 4.0);
	EXPECT_DOUBLE_EQ(kai.facing, 180.0);
	ASSERT_EQ(kai.walks.size(), 1u);
	EXPECT_EQ(kai.walks[0].points.size(), 2u);
	ASSERT_EQ(kai.gestures.size(), 1u);
	EXPECT_EQ(kai.gestures[0].pose, "look up") << "a quoted pose is one word";
	EXPECT_DOUBLE_EQ(kai.gestures[0].out1, 9.0);
	EXPECT_DOUBLE_EQ(kai.gestures[0].contact, 6.5);
	EXPECT_TRUE(film.spec.shots[0].following);
	EXPECT_DOUBLE_EQ(film.spec.shots[0].follow.side, 0.5);
	EXPECT_EQ(film.spec.shots[1].about, std::vector<std::string>{"kai"});

	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\ncharacter kai robot\n", bad).find("no kind of character called \"robot\""), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nwalk kai 1 1 0 0\n", bad).find("brought in before it"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\ncharacter kai nomai-explorer\ngesture kai \"look up\"\n", bad).find(":4:"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nshot 0 A\n  follow kim\n", bad).find("kim, who is not brought in"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\ncharacter kai nomai-explorer\nshot 0 A\n  follow kai\n  key 0 0 0 0 0 -1\n", bad).find("both `key` lines and `follow`"), std::string::npos);
}

// A character film: the character is in the scene where its walk has it, the following camera stays on it (its subject in the middle of the frame), its planted feet stay put
// on a straight walk, and its footfalls and its gesture's contact are its events.
TEST(FilmFile, ACharacterWalksActsIsFollowedByTheCameraAndIsHeard) {
	(void)cartoon::films();
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 10\n"
					   "character kai nomai-explorer at 0 0 4 facing 180 detail 0.05\n"
					   "walk kai 1.0 1.0 0 -2\n"
					   "gesture kai \"reach to the wall\" 8 8.5 contact 8.4\n"
					   "shot 0 Walk\n  follow kai behind 3 up 1.8\n",
					   film),
			  "");
	// at 3 s it has walked 2 m of its 6, from z 4 towards -2 (pbrt's): its scene text is in the frame's
	const std::string scene = film.sceneAt(3.0);
	EXPECT_NE(scene.find("# kai (nomai-explorer)"), std::string::npos);
	const std::vector<cartoon::Vec3> subjects = film.subjectsAt(3.0);
	ASSERT_EQ(subjects.size(), 1u);
	EXPECT_NEAR(subjects[0].z, 2.0, 1e-6) << "story space: z as pbrt's";
	cartoon::Vec3 eye, target;
	double fov = 0.0;
	ASSERT_TRUE(film_subjects::cameraOf(film.headerAt(3.0), eye, target, fov));
	const film_subjects::View v = film_subjects::project(eye, target, fov, subjects[0], 320, 180);
	EXPECT_NEAR(v.x, 160.0, 1.0) << "the following camera looks at it";
	EXPECT_NEAR(v.y, 90.0, 1.0);
	EXPECT_NEAR(eye.z, 5.0, 1e-6) << "3 m behind it (it walks towards -z)";
	// its feet, planted where they come down
	double worst = 0.0;
	std::vector<cartoon::Foot> before = film.feetAt(1.0);
	ASSERT_EQ(before.size(), 2u);
	for (double t = 1.0 + 1.0 / 24.0; t < 7.0; t += 1.0 / 24.0) {
		const std::vector<cartoon::Foot> now = film.feetAt(t);
		for (std::size_t i = 0; i < 2; ++i)
			if (before[i].planted && now[i].planted) worst = std::max(worst, cartoon::length(now[i].at - before[i].at) * 24.0);
		before = now;
	}
	EXPECT_LT(worst, 0.02);
	// its events: a footfall every 0.84 m from half a step in (6 m: 7 of them), and the touch
	int steps = 0, touches = 0;
	for (const cartoon::FilmEvent &e : film.events()) {
		steps += e.kind == cartoon::EventKind::Footfall;
		touches += e.kind == cartoon::EventKind::Touch;
	}
	EXPECT_EQ(steps, 7);
	EXPECT_EQ(touches, 1);
}

// Props with physics in a film file: a marked block that bounces (moved to where its physics has it) and one that topples (turned about its edge), each landing an impact on
// the film's events; a block the scene does not mark is refused.
TEST(FilmFile, APropBouncesOrTopplesWithPhysicsAndItsLandingsAreHeard) {
	(void)cartoon::films();
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 10\n"
					   "bounce fan 1.0 from 0 2 0 velocity 0.5 0 0 radius 0.5\n"
					   "topple lamp 2.0 pivot 0 0 0 axis 0 0 1 height 1 depth 0.1 push 50\n",
					   film),
			  "");
	ASSERT_EQ(film.spec.bounces.size(), 1u);
	ASSERT_EQ(film.spec.topples.size(), 1u);
	const std::string before = film.sceneAt(0.5), falling = film.sceneAt(1.3);
	EXPECT_NE(before.find("Translate 0 2 0"), std::string::npos) << "before it starts, where it was: " << before.substr(before.find("# @anim fan"), 120);
	EXPECT_EQ(falling.find("Translate 0 2 0\n  Rotate"), std::string::npos) << "falling, lower";
	EXPECT_NE(film.sceneAt(9.0).find("Rotate 90 0 0 1"), std::string::npos) << "the lamp has toppled";
	int impacts = 0;
	for (const cartoon::FilmEvent &e : film.events()) impacts += e.kind == cartoon::EventKind::Impact;
	EXPECT_GE(impacts, 3) << "the fan's bounces and the lamp's landing";
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nbounce rock 1 from 0 1 0\n", bad).find("`# @anim rock`"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nbounce fan 1 velocity 0 0 0\n", bad).find("needs `from X Y Z`"), std::string::npos);
}

// A moving block is blurred over its shutter: its transform at the frame's time (StartTime) and a share of a frame later (EndTime); a block that is still at that moment
// keeps one transform, and `shutter 0` makes every block sharp.
TEST(FilmFile, AMovingBlockIsBlurredOverTheShutterAndAStillOneIsNot) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 10\nfps 25\nmove fan 0 0 0 0  4 4 0 0\n", film), "");
	EXPECT_DOUBLE_EQ(film.spec.shutter, 0.5) << "a film camera's 180 degrees by default";
	const std::string moving = film.sceneAt(2.0);
	ASSERT_NE(moving.find("ActiveTransform \"StartTime\""), std::string::npos) << moving;
	EXPECT_NE(moving.find("ActiveTransform \"EndTime\""), std::string::npos);
	EXPECT_NE(moving.find("ActiveTransform \"All\""), std::string::npos);
	EXPECT_EQ(film.sceneAt(6.0).find("ActiveTransform"), std::string::npos) << "after its last key it is still: one transform";
	ff::DataFilm sharp;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 10\nshutter 0\nmove fan 0 0 0 0  4 4 0 0\n", sharp), "");
	EXPECT_EQ(sharp.sceneAt(2.0).find("ActiveTransform"), std::string::npos);
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nshutter 2\n", bad).find("shutter is a share of a frame"), std::string::npos);
}

// The kinds of character beside the Nomai explorer (film_kinds.h): Pip, a person and the turtle each walk a path with a corner, are drawn where the walk has them, keep their
// planted feet where they came down (a turtle's four), step once a stride, act with their own poses (a pose of another kind's library is refused, with the kind's own
// listed), and the two that can be scaled are scaled with their stride and chest.
TEST(FilmFile, PipAPersonAndTheTurtleWalkTurnACornerWithFeetThatStayPutAndActWithTheirOwnPoses) {
	(void)cartoon::films();
	struct Case {
		const char *kind, *gesture;
		double speed, scale;
		std::size_t feet;
	};
	for (const Case &k : {Case{"pip", "wave", 0.5, 1.0, 2}, Case{"person", "point", 1.0, 1.0, 2}, Case{"turtle", "nod", 0.3, 6.0, 4}}) {
		SCOPED_TRACE(k.kind);
		ff::DataFilm film;
		const std::string scaled = k.scale != 1.0 ? " scale " + std::to_string(k.scale) : "";
		ASSERT_EQ(loadFilm(std::string("scene s.pbrt\nlength 12\n") + "character c " + k.kind + " at 0 0 4 facing 180" + scaled + "\n" + "walk c 1.0 " + std::to_string(k.speed) +
							   " 0 1  2 1\n" + "gesture c \"" + k.gesture + "\" 9 9.5\n",
						   film),
				  "");
		const ff::CharacterKind &kind = ff::characterKinds().at(k.kind);
		const std::string scene = film.sceneAt(9.8);
		EXPECT_NE(scene.find(std::string("# c (") + k.kind + ")"), std::string::npos);
		EXPECT_GT(scene.size(), 2000u) << "the character's meshes";
		// its feet: planted ones stay put round the corner
		const double end = 1.0 + 5.0 / k.speed;   // (3 m to the corner, 2 m after it)
		std::vector<cartoon::Foot> before = film.feetAt(1.0);
		ASSERT_EQ(before.size(), k.feet);
		double worst = 0.0;
		int planted = 0;
		for (double t = 1.0 + 1.0 / 24.0; t < end + 1.0; t += 1.0 / 24.0) {
			const std::vector<cartoon::Foot> now = film.feetAt(t);
			for (std::size_t i = 0; i < now.size(); ++i)
				if (before[i].planted && now[i].planted) {
					worst = std::max(worst, cartoon::length(now[i].at - before[i].at) * 24.0);
					++planted;
				}
			before = now;
		}
		EXPECT_GT(planted, 50);
		EXPECT_LT(worst, 0.01) << "m/s";
		// a footfall each stride from a step in
		const double stride = kind.stride * k.scale;
		int steps = 0;
		for (const cartoon::FilmEvent &e : film.events()) steps += e.kind == cartoon::EventKind::Footfall;
		EXPECT_EQ(steps, static_cast<int>(std::floor(5.0 / stride - kind.firstStep + 1.0 + 1e-9)));
		// its chest, scaled with it
		std::vector<cartoon::Vec3> chest;
		ff::DataFilm followed;
		ASSERT_EQ(loadFilm(std::string("scene s.pbrt\nlength 12\ncharacter c ") + k.kind + " at 0 0 4" + scaled + "\nshot 0 A\n  key 0 1 0 0 1 4\n  subject c\n", followed), "");
		chest = followed.subjectsAt(1.0);
		ASSERT_EQ(chest.size(), 1u);
		EXPECT_NEAR(chest[0].y, kind.chest.y * k.scale, 1e-9);
		// a pose it does not have
		ff::DataFilm bad;
		const std::string problem = loadFilm(std::string("scene s.pbrt\nlength 12\ncharacter c ") + k.kind + "\ngesture c \"reach to the wall\" 1 2\n", bad);
		EXPECT_NE(problem.find("is not a pose of a"), std::string::npos) << problem;
		EXPECT_NE(problem.find(std::string("\"") + k.gesture + "\""), std::string::npos) << "its own poses are listed: " << problem;
	}
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 12\ncharacter c person scale 2\n", bad).find("a person cannot be scaled"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 12\ncharacter c pip scale 0\n", bad).find("above 0"), std::string::npos);
}

// ---- what characters say ------------------------------------------------------------------------------------------------------------------------------------------------------

// A line is cut into syllables at its vowel groups (a silent e is not one; a Chinese character is one of its own), paced with pauses at its punctuation; its pitch falls over
// a statement and rises at the end of a question.
TEST(FilmSpeech, ALineIsTimedBySyllablesWithPausesAndAnIntonationThatFollowsItsPunctuation) {
	namespace sp = cartoon::speech;
	sp::Line l;
	l.t = 2.0;
	l.text = "Hello there, friend.";
	const std::vector<sp::Syllable> y = sp::syllablesOf(l);
	ASSERT_EQ(y.size(), 4u) << "hel-lo there friend";
	EXPECT_EQ(y[0].vowel, 'e');
	EXPECT_EQ(y[1].vowel, 'o');
	EXPECT_EQ(y[0].onset, 'h');
	EXPECT_GT(y[0].t0, 2.0);
	for (std::size_t i = 1; i < y.size(); ++i) EXPECT_GT(y[i].t0, y[i - 1].t1) << "one after another";
	EXPECT_GT(y[3].t0 - y[2].t1, 0.2) << "the comma pauses";
	EXPECT_GT(y[0].pitch, y[3].pitch) << "a statement falls";
	l.text = "Are you there?";
	const std::vector<sp::Syllable> q = sp::syllablesOf(l);
	ASSERT_GE(q.size(), 3u);
	EXPECT_GT(q.back().pitch, q.front().pitch) << "a question rises at its end";
	l.text = "\xE4\xBD\xA0\xE5\xA5\xBD\xEF\xBC\x8C\xE6\x9C\x8B\xE5\x8F\x8B\xE3\x80\x82";   // 你好，朋友。
	const std::vector<sp::Syllable> zh = sp::syllablesOf(l);
	ASSERT_EQ(zh.size(), 4u) << "a syllable a character";
	EXPECT_GT(zh[2].t0 - zh[1].t1, 0.2) << "the full-width comma pauses";
	l.text = "Faster.";
	const double slow = sp::endOf(sp::syllablesOf(l), l.t);
	l.rate = 2.0;
	EXPECT_LT(sp::endOf(sp::syllablesOf(l), l.t) - l.t, 0.6 * (slow - l.t)) << "rate 2 is about twice as quick";
}

// The mouth takes each vowel's shape while it is heard (wide for an "ee", round for an "oo"), the lips meet at the start of an m, and between lines it is shut; the voice is
// loud while a syllable is spoken and silent after the line.
TEST(FilmSpeech, TheMouthTakesEachVowelsShapeAndTheLipsMeetForAnM) {
	namespace sp = cartoon::speech;
	sp::Line l;
	l.text = "me too";
	const std::vector<sp::Syllable> y = sp::syllablesOf(l);
	ASSERT_EQ(y.size(), 2u);
	const double mid0 = 0.5 * (y[0].t0 + y[0].t1), mid1 = 0.5 * (y[1].t0 + y[1].t1);
	const sp::Mouth ee = sp::mouthAt(y, mid0), oo = sp::mouthAt(y, mid1);
	EXPECT_GT(ee.wide, 0.7);
	EXPECT_LT(ee.round, 0.01);
	EXPECT_GT(oo.round, 0.7);
	EXPECT_LT(oo.wide, 0.01);
	EXPECT_LT(sp::mouthAt(y, y[0].t0 - 0.005).open, 0.05) << "the lips meet for the m";
	EXPECT_GT(sp::loudnessAt(y, mid0), 0.9);
	EXPECT_EQ(sp::loudnessAt(y, y[1].t1 + 0.2), 0.0);
	EXPECT_EQ(sp::mouthAt(y, y[1].t1 + 0.2).open, 0.0);
	// the voice: as long as the line, not silent, not clipped, louder where a syllable is
	const std::vector<float> v = sp::speak(y, l.t, sp::Voice{});
	EXPECT_NEAR(v.size() / static_cast<double>(cartoon::audio::kRate), y.back().t1 + 0.12, 0.01);
	const auto rms = [&](double a, double b) {
		double sum = 0.0;
		const std::size_t i0 = static_cast<std::size_t>(a * cartoon::audio::kRate), i1 = std::min(v.size(), static_cast<std::size_t>(b * cartoon::audio::kRate));
		for (std::size_t i = i0; i < i1; ++i) sum += static_cast<double>(v[i]) * v[i];
		return std::sqrt(sum / std::max<std::size_t>(1, i1 - i0));
	};
	float peak = 0.0f;
	for (float s : v) peak = std::max(peak, std::abs(s));
	EXPECT_GT(peak, 0.1f);
	EXPECT_LT(peak, 1.0f);
	EXPECT_GT(rms(mid0 - 0.03, mid0 + 0.03), 4.0 * rms(y[1].t1 + 0.08, y[1].t1 + 0.11));
}

// A film file's `say` line: read with its rate, pitch and caption choice; the character's mouth and voice follow it; its soundtrack has it, and its captions show it until a
// little after its end, cut short where the next line begins. A line by someone not brought in is refused.
TEST(FilmSpeech, AFilmFilesCharactersSayTheirLinesWithMouthsVoicesAndCaptions) {
	(void)cartoon::films();
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 10\n"
					   "character pip pip at -1 0 0\ncharacter ann person at 1 0 0\n"
					   "say pip 1.0 \"Look at the moon!\" rate 1.2 pitch 2\n"
					   "say ann 2.0 \"It is very round.\"\n"
					   "say pip 5.0 \"Hmm.\" nocaption\n",
					   film),
			  "");
	ASSERT_EQ(film.spec.lines.size(), 3u);
	EXPECT_DOUBLE_EQ(film.spec.lines[0].rate, 1.2);
	EXPECT_DOUBLE_EQ(film.spec.lines[0].pitch, 2.0);
	EXPECT_FALSE(film.spec.lines[2].caption);
	// pip speaks at its first syllable, ann does not
	const cartoon::speech::Syllable &first = film.spoken[0][0];
	const double mid = 0.5 * (first.t0 + first.t1);
	ff::CharacterMoment pip, ann;
	film.speakingAt(*film.character("pip"), mid, pip);
	film.speakingAt(*film.character("ann"), mid, ann);
	EXPECT_GT(pip.voice, 0.5);
	EXPECT_GT(pip.mouth.open, 0.3);
	EXPECT_EQ(ann.voice, 0.0);
	// the captions: two (the third is not captioned), the first cut short where ann begins
	const std::vector<cartoon::Caption> caps = film.lineCaptions();
	ASSERT_EQ(caps.size(), 2u);
	EXPECT_DOUBLE_EQ(caps[0].t0, 1.0);
	EXPECT_LE(caps[0].t1, 2.0);
	ASSERT_EQ(caps[1].lines.size(), 1u);
	EXPECT_EQ(caps[1].lines[0].text, "It is very round.");
	EXPECT_GT(caps[1].t1, cartoon::speech::endOf(film.spoken[1], 2.0));
	// the soundtrack has the lines: louder while pip speaks than before
	cartoon::audio::Mix mix(10.0);
	film.speak(mix);
	const std::vector<std::int16_t> pcm = mix.finish(0.0, 0.0);
	const auto level = [&](double a, double b) {
		double sum = 0.0;
		for (std::size_t i = static_cast<std::size_t>(a * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(b * cartoon::audio::kRate) * 2; ++i) sum += std::abs(static_cast<double>(pcm[i]));
		return sum;
	};
	EXPECT_GT(level(1.0, 1.8), 10.0 * level(0.2, 0.9) + 1.0);
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nsay kim 1 \"Hi.\"\n", bad).find("kim says a line but is not brought in"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\ncharacter kim pip\nsay kim 1 \"Hi.\" loudly\n", bad).find("a line takes rate"), std::string::npos);
}

// ---- what characters feel -----------------------------------------------------------------------------------------------------------------------------------------------------

// A `feel` line eases an expression in and out like a gesture, at its amount; two at once add up; an expression that does not exist is refused with the list, and the
// masked explorer, who has no face, is told to use a gesture.
TEST(FilmFaces, AFeelingEasesAnExpressionInAndOutAndFeelingsAddUp) {
	(void)cartoon::films();
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 10\ncharacter pip pip\n"
					   "feel pip happy 1 2 5 6\n"
					   "feel pip surprised 3 3.2 4 4.5 amount 0.5\n",
					   film),
			  "");
	const ff::Character &pip = *film.character("pip");
	ASSERT_EQ(pip.feelings.size(), 2u);
	const cartoon::faces::Face &happy = cartoon::faces::expressions().at("happy"), &surprised = cartoon::faces::expressions().at("surprised");
	EXPECT_EQ(cartoon::faces::faceAt(pip.feelings, 0.5).smile, 0.0) << "before it";
	EXPECT_NEAR(cartoon::faces::faceAt(pip.feelings, 2.5).smile, happy.smile, 1e-12) << "held";
	EXPECT_NEAR(cartoon::faces::faceAt(pip.feelings, 1.5).smile, 0.5 * happy.smile, 1e-12) << "half way in (smoothly)";
	const cartoon::faces::Face both = cartoon::faces::faceAt(pip.feelings, 3.5);
	EXPECT_NEAR(both.browRaise, happy.browRaise + 0.5 * surprised.browRaise, 1e-12) << "two at once add up, at their amounts";
	EXPECT_EQ(cartoon::faces::faceAt(pip.feelings, 7.0).smile, 0.0) << "after it";
	// it is on the face drawn: Pip's scene text differs between a happy and a sad moment
	ff::DataFilm sad;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 10\ncharacter pip pip\nfeel pip sad 1 2 5 6\n", sad), "");
	const auto pipText = [](const std::string &scene) { return scene.substr(scene.find("# pip (pip)")); };   // (the scenes are in folders of their own)
	EXPECT_NE(pipText(film.sceneAt(2.5)), pipText(sad.sceneAt(2.5)));
	EXPECT_TRUE(pipText(film.sceneAt(8.0)) == pipText(sad.sceneAt(8.0))) << "at rest the same";
	ff::DataFilm bad;
	const std::string unknown = loadFilm("scene s.pbrt\nlength 10\ncharacter pip pip\nfeel pip giddy 1 2\n", bad);
	EXPECT_NE(unknown.find("no expression called \"giddy\""), std::string::npos) << unknown;
	EXPECT_NE(unknown.find("sleepy"), std::string::npos) << "the list";
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\ncharacter kai nomai-explorer\nfeel kai sad 1 2\n", bad).find("has no face"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\ncharacter pip pip\nfeel pip sad 1 2 amount 3\n", bad).find("`amount` is from 0 to 2"), std::string::npos);
}

// ---- props characters handle, and where they look ------------------------------------------------------------------------------------------------------------------------------

// A prop is picked up (the hand reaches it as it is taken), carried (it goes where its holder goes), handed over (the two hands meet; it is the other's from then on) and put
// down (it stays there); each is a touch on the soundtrack. Handling that cannot happen is refused.
TEST(FilmProps, APropIsPickedUpCarriedHandedOverAndPutDownInTheHandThatHoldsIt) {
	(void)cartoon::films();
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 20\n"
					   "character ann person at 0 0 0 facing 0\ncharacter pip pip at 0 0 1.35 facing 180\n"
					   "prop fan at 0.25 0.95 0.35\n"
					   "pick ann fan 2.0\n"
					   "walk ann 4.0 0.8  0 0.6\n"
					   "give ann fan 8.0 to pip\n"
					   "put pip fan 12.0 at -0.4 0.5 1.9\n",
					   film),
			  "");
	const auto pbrt = [](cartoon::Vec3 v) { return cartoon::Vec3{-v.x, v.y, v.z}; };   // (story space to the film file's)
	const cartoon::Vec3 home{0.25, 0.95, 0.35};
	EXPECT_LT(cartoon::length(pbrt(film.propAt("fan", 1.0)) - home), 1e-9) << "where the scene has it";
	const ff::Character &ann = *film.character("ann"), &pip = *film.character("pip");
	const ff::CharacterKind &person = ff::characterKinds().at("person");
	// as it is taken, ann's hand is at it
	EXPECT_LT(cartoon::length(person.handAt(film.actingMoment(ann, 2.0), 1) - film.propAt("fan", 1.99)), 0.03);
	// carried: in front of her, and with her as she walks
	const cartoon::Vec3 carried = film.propAt("fan", 3.5), walked = film.propAt("fan", 4.8);
	EXPECT_GT(walked.z - carried.z, 0.3) << "it went with her";
	EXPECT_LT(cartoon::length(film.propAt("fan", 4.8) - person.handAt(film.actingMoment(ann, 4.8), 1)), 1e-9) << "in her hand";
	// handed over: it is in pip's hand after, and the two hands met (close) as it was given
	const ff::CharacterKind &pipKind = ff::characterKinds().at("pip");
	EXPECT_LT(cartoon::length(person.handAt(film.actingMoment(ann, 8.0), 1) - pipKind.handAt(film.actingMoment(pip, 8.0), 1)), 0.08);
	EXPECT_LT(cartoon::length(film.propAt("fan", 9.0) - pipKind.handAt(film.actingMoment(pip, 9.0), 1)), 1e-9);
	// put down: there, and it stays
	EXPECT_LT(cartoon::length(pbrt(film.propAt("fan", 13.0)) - cartoon::Vec3{-0.4, 0.5, 1.9}), 1e-9);
	EXPECT_LT(cartoon::length(pbrt(film.propAt("fan", 19.0)) - cartoon::Vec3{-0.4, 0.5, 1.9}), 1e-9);
	// the block is moved in the scene text, and the hands are heard
	EXPECT_NE(film.sceneAt(13.0).find("Translate -0.65 -0.45 1.55"), std::string::npos);
	int touches = 0;
	for (const cartoon::FilmEvent &e : film.events()) touches += e.kind == cartoon::EventKind::Touch;
	EXPECT_EQ(touches, 3);
	// what cannot happen
	const std::string base = "scene s.pbrt\nlength 20\ncharacter ann person\ncharacter pip pip\nprop fan at 0 1 0\n";
	ff::DataFilm bad;
	EXPECT_NE(loadFilm(base + "pick ann fan 2\npick pip fan 4\n", bad).find("in ann's hand"), std::string::npos);
	EXPECT_NE(loadFilm(base + "put pip fan 2 at 0 0 0\n", bad).find("pip does not hold fan"), std::string::npos);
	EXPECT_NE(loadFilm(base + "pick ann fan 2\ngive ann fan 2.5 to pip\n", bad).find("too soon"), std::string::npos);
	EXPECT_NE(loadFilm(base + "pick ann cup 2\n", bad).find("cup is not a prop"), std::string::npos);
	EXPECT_NE(loadFilm(base + "prop rock at 0 0 0\n", bad).find("`# @anim rock`"), std::string::npos);
	EXPECT_NE(loadFilm(base + "pick ann fan 2\ngive ann fan 4 to ann\n", bad).find("to itself"), std::string::npos);
	const std::string apart = loadFilm("scene s.pbrt\nlength 20\ncharacter ann person at 0 0 0\ncharacter pip pip at 0 0 3 facing 180\nprop fan at 0.25 0.95 0.35\npick ann fan 2\ngive ann fan 4 to pip\n", bad);
	EXPECT_NE(apart.find("cannot reach each other"), std::string::npos) << apart;
}

// `look` turns a character's head and eyes to someone (their head), a prop or a point, eased in and out; what it looks at is in its moment and changes how it is drawn.
TEST(FilmProps, ACharacterLooksAtSomeoneAPropOrAPoint) {
	(void)cartoon::films();
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 10\ncharacter ann person at 0 0 0 facing 0\ncharacter pip pip at 1.5 0 1 facing -90\nprop fan at 0 2 0\n"
					   "look pip at ann 1 1.5 4 4.5\nlook ann at fan 6 6.5\nlook pip at 0 3 5 7 7.5\n",
					   film),
			  "");
	const ff::Character &ann = *film.character("ann"), &pip = *film.character("pip");
	EXPECT_EQ(film.actingMoment(pip, 0.5).look.weight, 0.0);
	const ff::CharacterMoment at = film.actingMoment(pip, 2.0);
	EXPECT_DOUBLE_EQ(at.look.weight, 1.0);
	EXPECT_LT(cartoon::length(at.look.at - film.bodyPoint(ann, 2.0, ff::characterKinds().at("person").head)), 1e-9) << "at ann's head";
	EXPECT_LT(cartoon::length(film.actingMoment(ann, 7.0).look.at - film.propAt("fan", 7.0)), 1e-9) << "at the prop";
	EXPECT_NEAR(film.actingMoment(pip, 8.0).look.at.z, 5.0, 1e-9) << "at a point";
	const auto pipText = [](const std::string &scene) { return scene.substr(scene.find("# pip (pip)")); };
	EXPECT_NE(pipText(film.sceneAt(2.0)), pipText(film.sceneAt(5.0))) << "its head turned";
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\ncharacter pip pip\nlook pip at ann 1 2\n", bad).find("neither a character nor a prop"), std::string::npos);
}

// ---- walks that find their way ------------------------------------------------------------------------------------------------------------------------------------------------

// A floor map from the set (what stands between ankle and head height, seen from above), grown by the walker's size, and the shortest way across it: straight when nothing is
// in the way, round a wall otherwise, never through it; no way into a closed room.
TEST(FilmPaths, TheWayRoundAWallIsFoundOnAMapOfTheSetAndNeverCrossesIt) {
	namespace fp = cartoon::film_paths;
	const std::string wall = "WorldBegin\nShape \"trianglemesh\" \"integer indices\" [ 0 1 2 0 2 3 ] \"point3 P\" [ -2 0 0  2 0 0  2 2 0  -2 2 0 ]\n"   // a wall across x -2..2 at z 0
							 "Shape \"sphere\" \"float radius\" [ 0.5 ]\n";   // (a ball half sunk in the floor at the origin: in the wall anyway)
	const pbrt_flatten::FlatScene s = fp::flattenText(wall, {});
	ASSERT_EQ(s.triangles.size(), 2u);
	fp::Grid g = fp::gridAround(0.0, 0.0, 6.0, 0.08);
	fp::addScene(g, s, 0.0, 0.15, 1.8);
	EXPECT_TRUE(g.at(g.col(1.0), g.row(0.0))) << "the wall is on the map";
	EXPECT_FALSE(g.at(g.col(1.0), g.row(1.0)));
	const fp::Grid ways = fp::grown(g, 0.3);
	std::vector<std::pair<double, double>> way;
	std::string why;
	ASSERT_TRUE(fp::findWay(ways, 3.0, -3.0, 3.0, 3.0, way, why)) << why;
	EXPECT_EQ(way.size(), 1u) << "beside the wall: straight";
	ASSERT_TRUE(fp::findWay(ways, 0.0, -3.0, 0.0, 3.0, way, why)) << why;
	ASSERT_GE(way.size(), 2u) << "round it";
	double length = 0.0, px = 0.0, pz = -3.0;
	for (const auto &[x, z] : way) {
		EXPECT_TRUE(fp::clear(ways, px, pz, x, z)) << "no leg crosses the wall";
		length += std::hypot(x - px, z - pz);
		px = x, pz = z;
	}
	EXPECT_GT(length, 6.0);
	EXPECT_LT(length, 2.0 * std::hypot(2.3, 3.0) + 0.3) << "the short way round an end";
	// shut in: no way
	const std::string room = "WorldBegin\nShape \"trianglemesh\" \"integer indices\" [ 0 1 2 0 2 3  4 5 6 4 6 7  8 9 10 8 10 11  12 13 14 12 14 15 ] \"point3 P\" [ "
							 "-1 0 -1  1 0 -1  1 2 -1  -1 2 -1   -1 0 1  1 0 1  1 2 1  -1 2 1   -1 0 -1  -1 0 1  -1 2 1  -1 2 -1   1 0 -1  1 0 1  1 2 1  1 2 -1 ]\n";
	fp::Grid shut = fp::gridAround(0.0, 0.0, 6.0, 0.08);
	fp::addScene(shut, fp::flattenText(room, {}), 0.0, 0.15, 1.8);
	EXPECT_FALSE(fp::findWay(fp::grown(shut, 0.25), 3.0, 0.0, 0.0, 0.0, way, why));
}

// In a film file a walk `to` a place goes round what is in the way, the set's and a character standing there; a way that cannot be found is refused, and two characters whose
// walks run into each other are told of.
TEST(FilmPaths, AWalkToAPlaceGoesRoundTheSetAndWhoeverStandsThere) {
	(void)cartoon::films();
	// the test scene has a ball of radius 1 at the origin: a walk from one side to the other goes round it
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 20\ncharacter ann person at 0 0 -3\ncharacter pip pip at 2.5 0 0\nwalk ann 1 1.0 to 0 3\n", film), "");
	const ff::WalkLine &w = film.spec.characters[0].walks[0];
	ASSERT_GE(w.points.size(), 2u) << "round the ball";
	double closest = 1e9, closestPip = 1e9;
	for (double t = 1.0; t < 12.0; t += 0.02) {
		cartoon::Vec3 at;
		film.momentOf(film.spec.characters[0], t, &at);
		closest = std::min(closest, std::hypot(at.x, at.z));
		closestPip = std::min(closestPip, std::hypot(-at.x - 2.5, at.z));
	}
	EXPECT_GT(closest, 1.0 + 0.25 - 0.1) << "it keeps its size away from the ball";
	EXPECT_GT(closestPip, 0.25 + 0.35 - 0.1) << "and from pip";
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 20\ncharacter ann person at 0 0 -3\nwalk ann 1 1.0 to 0 0\n", bad).find("ann cannot find a way to 0 0: something is in the way there"), std::string::npos);
	const std::string bump = loadFilm("scene s.pbrt\nlength 20\ncharacter ann person at -3 0 3\ncharacter pip pip at 3 0 3 facing -90\nwalk ann 1 1 3 3\n", bad);
	EXPECT_NE(bump.find("ann and pip walk into each other"), std::string::npos) << bump;
}

// ---- automatic coverage -------------------------------------------------------------------------------------------------------------------------------------------------------

// An `auto` shot becomes shots of its own when the film is loaded: a two-shot, then each speaker from just before they speak (over the other's shoulder, in focus), back to the
// two-shot in a long pause; every camera stays on one side of the line between the first two characters (`side right` the other side), and each shot is about who it shows.
TEST(FilmCoverage, AnAutoShotCutsToEachSpeakerAndKeepsToOneSideOfTheLine) {
	(void)cartoon::films();
	const std::string film0 = "scene s.pbrt\nlength 16\ncharacter pip pip at -0.9 0 3 facing 90\ncharacter ann person at 0.5 0 3 facing -90\n"
							  "say pip 1.5 \"Look at the moon!\"\nsay ann 4.0 \"It is very round.\"\nsay pip 10.0 \"So it is.\"\n"
							  "shot 0 Talk\n  auto pip ann\n";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm(film0, film), "");
	std::vector<std::string> names;
	for (const ff::Shot &s : film.spec.shots) names.push_back(s.name);
	const std::vector<std::string> want = {"Talk: everyone", "Talk: pip", "Talk: ann", "Talk: everyone", "Talk: pip", "Talk: everyone"};   // (pauses before the third line and after it)
	EXPECT_EQ(names, want);
	ASSERT_EQ(film.spec.shots.size(), want.size());
	EXPECT_NEAR(film.spec.shots[1].start, 1.35, 1e-9) << "a little before the line";
	EXPECT_EQ(film.spec.shots[1].about, std::vector<std::string>{"pip"});
	EXPECT_TRUE(film.spec.shots[1].path.keys[0].hasLens()) << "a shallow focus on the speaker";
	// all on one side: the line runs from pip (story x 0.9) to ann (story x -0.5) at z 3; the cameras are on the +z side (in front of it, seen from where the first is on the left)
	const auto sideOf = [&](const ff::DataFilm &f) {
		int plus = 0, minus = 0;
		for (const ff::Shot &s : f.spec.shots)
			for (const auto &k : s.path.keys) (k.pos[2] > 3.0 ? plus : minus)++;
		return std::make_pair(plus, minus);
	};
	const auto [plus, minus] = sideOf(film);
	EXPECT_TRUE(plus == 0 || minus == 0) << plus << " in front, " << minus << " behind: every camera on one side";
	ff::DataFilm other;
	ASSERT_EQ(loadFilm(film0 + "  side right\n", other), "");
	const auto [plus2, minus2] = sideOf(other);
	EXPECT_EQ(plus2, minus) << "`side right`: the other side";
	EXPECT_EQ(minus2, plus);
	// what cannot be covered
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nshot 0 A\n  auto\n", bad).find("no characters to cover"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\ncharacter pip pip\nshot 0 A\n  auto kim\n", bad).find("covers kim"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\ncharacter pip pip\nshot 0 A\n  auto\n  key 0 1 0 0 1 4\n", bad).find("both `key` lines and `auto`"), std::string::npos);
}

// ---- transitions --------------------------------------------------------------------------------------------------------------------------------------------------------------

// A shot comes in with a dissolve, a fade or a wipe; the film can end with a fade out; the other side of a dissolve is the scene seen by the camera across the cut.
TEST(FilmTransitions, AShotComesInWithADissolveAFadeOrAWipeAndTheOtherSideIsSeenFromAcrossTheCut) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 20\nfadeout 1.5\n"
					   "shot 0 A\n  key 0 1 10 0 1 0\n  fade 1\n"
					   "shot 5 B\n  key 6 1 10 0 1 0\n  dissolve 2\n"
					   "shot 12 C\n  key -6 1 10 0 1 0\n  wipe 1\n",
					   film),
			  "");
	const std::vector<cartoon::Transition> trs = film.transitions();
	ASSERT_EQ(trs.size(), 4u);
	EXPECT_EQ(trs[0].kind, cartoon::Transition::Fade);
	EXPECT_DOUBLE_EQ(trs[0].at, 0.0);
	EXPECT_EQ(trs[1].kind, cartoon::Transition::Dissolve);
	EXPECT_DOUBLE_EQ(trs[1].at, 5.0);
	EXPECT_EQ(trs[2].kind, cartoon::Transition::Wipe);
	EXPECT_EQ(trs[3].kind, cartoon::Transition::Fade);
	EXPECT_DOUBLE_EQ(trs[3].at, 20.0) << "the fade out ends with the film";
	EXPECT_DOUBLE_EQ(trs[3].length, 3.0);
	// across the dissolve's cut: before it, the other side is shot B's camera; after it, shot A's; away from it, the frame itself
	const auto lookAt = [](const std::string &scene) { return scene.substr(scene.find("LookAt"), scene.find('\n', scene.find("LookAt")) - scene.find("LookAt")); };
	EXPECT_EQ(lookAt(film.sceneAcross(4.5)).rfind("LookAt 6 ", 0), 0u) << lookAt(film.sceneAcross(4.5));
	EXPECT_EQ(lookAt(film.sceneAcross(5.5)).rfind("LookAt 0 ", 0), 0u) << lookAt(film.sceneAcross(5.5));
	EXPECT_EQ(film.sceneAcross(8.0), film.sceneAt(8.0));
	EXPECT_EQ(film.sceneAcross(0.2), film.sceneAt(0.2)) << "a fade needs no other side";
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 20\nshot 0 A\n  key 0 1 10 0 1 0\n  dissolve 1\n", bad).find("can only fade in"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 20\ndissolve 1\n", bad).find("belongs to a shot"), std::string::npos);
}

// ---- dressing the set ---------------------------------------------------------------------------------------------------------------------------------------------------------

// The sun of a `sky` line follows the time of day: high at noon in the south, low in the west in the evening, set at night; a dressed film's scene has its things (the pieces
// once, a placement each), its sun and sky instead of the scene's own, and its sound; a placed rock is in a walk's way and grass is not.
// Every object a placed thing uses is one the dressing defines, wherever it stands (a tree left of or behind the middle once named leaves "dress_leaves-1", which did not
// exist, and lost them).
TEST(FilmDressing, EveryPlacedThingUsesObjectsTheDressingDefines) {
	const std::string defined = cartoon::dressing::objects();
	int checked = 0;
	for (const char *kind : {"tree", "rock", "fern", "bush", "grass"})
		for (double x = -9.0; x <= 9.0; x += 3.0)
			for (double z = -9.0; z <= 9.0; z += 3.0) {
				cartoon::dressing::Placement p;
				p.kind = kind;
				p.at = {x, 0.0, z};
				const std::string text = cartoon::dressing::placementText(p, 1.0);
				for (std::size_t at = text.find("ObjectInstance \""); at != std::string::npos; at = text.find("ObjectInstance \"", at + 1)) {
					const std::size_t from = at + 16, to = text.find('"', from);
					const std::string name = text.substr(from, to - from);
					EXPECT_NE(defined.find("ObjectBegin \"" + name + "\""), std::string::npos) << kind << " at " << x << " " << z << " uses " << name;
					++checked;
				}
			}
	EXPECT_GT(checked, 500);
}

TEST(FilmDressing, TheSkyFollowsTheTimeOfDayAndTheSetIsDressedWithThingsAndSound) {
	namespace dr = cartoon::dressing;
	dr::Sky sky;   // (south 180: -z; latitude 40, the longest day)
	double el = 0.0, facing = 0.0;
	dr::sunAt(sky, 12.0, el, facing);
	EXPECT_NEAR(el, 90.0 - 40.0 + 23.44, 0.2) << "noon on the longest day";
	EXPECT_NEAR(facing, 180.0, 0.5) << "in the south";
	dr::sunAt(sky, 18.5, el, facing);
	EXPECT_GT(el, 2.0);
	EXPECT_LT(el, 15.0) << "low in the evening";
	EXPECT_GT(facing, 180.0 + 60.0) << "towards the west (270)";
	dr::sunAt(sky, 23.0, el, facing);
	EXPECT_LT(el, 0.0) << "set";
	(void)cartoon::films();
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 10\nsky 18:00 20:00\nplace rock at 2 0 0 scale 2\nplace tree at -3 0 -3\nscatter grass 50 in -2 3 2 5 seed 4\nambience birds\nambience water 0.5\n"
					   "character ann person at 2 0 -3\nwalk ann 1 1 to 2 3\n",
					   film),
			  "");
	EXPECT_EQ(film.spec.placements.size(), 52u);
	const std::string scene = film.sceneAt(1.0);
	EXPECT_NE(scene.find("ObjectBegin \"dress_rock0\""), std::string::npos) << "the pieces";
	EXPECT_NE(scene.find("ObjectInstance \"dress_soft_tuft"), std::string::npos) << "the grass";
	EXPECT_NE(scene.find("LightSource \"distant\""), std::string::npos) << "the sun";
	EXPECT_EQ(scene.find("\"rgb L\" [ 0.1 0.1 0.1 ]"), std::string::npos) << "the scene's own sky is left out";
	EXPECT_NE(film.sceneAt(1.0), film.sceneAt(9.0)) << "the sun moves";
	// the walk goes round the rock (2 0 0, half a metre or so across) but straight through the grass
	double closest = 1e9;
	for (double t = 1.0; t < 10.0; t += 0.02) {
		cartoon::Vec3 at;
		film.momentOf(film.spec.characters[0], t, &at);
		closest = std::min(closest, std::hypot(-at.x - 2.0, at.z));
	}
	EXPECT_GT(closest, 0.6) << "round the rock";
	// the sound
	cartoon::audio::Mix mix(10.0);
	dr::play(mix, film.spec.ambience);
	const std::vector<std::int16_t> pcm = mix.finish(0.0, 0.0);
	double sum = 0.0;
	for (std::int16_t v : pcm) sum += std::abs(static_cast<double>(v));
	EXPECT_GT(sum / pcm.size(), 20.0) << "birds and water";
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nplace castle at 0 0 0\n", bad).find("tree, rock, fern, bush or grass"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nsky 25:00 26:00\n", bad).find("times of day"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nambience traffic\n", bad).find("wind, birds, crickets, water or night"), std::string::npos);
}

// ---- weather and particles ----------------------------------------------------------------------------------------------------------------------------------------------------

// Weather is a function of time that stands still in the world (the same drops whatever the camera, falling between frames), eased in and out and as dense as its amount;
// particles are thrown from their place when they are due and are gone after their life; a film file's lines are read and its rain is heard.
TEST(FilmParticles, WeatherStandsStillInTheWorldAndParticlesLiveTheirLives) {
	namespace pt = cartoon::particles;
	pt::Weather rain;
	rain.kind = "rain";
	rain.t0 = 2.0;
	rain.t1 = 10.0;
	EXPECT_EQ(pt::weatherText(rain, 1.0, {0, 1.5, 5}, 0.02), "") << "before it";
	EXPECT_NEAR(pt::strength(rain, 2.75), 0.5, 1e-9) << "easing in";
	const std::string a = pt::weatherText(rain, 5.0, {0, 1.5, 5}, 0.02), b = pt::weatherText(rain, 5.0, {0, 1.5, 5}, 0.02);
	EXPECT_EQ(a, b) << "a function of time";
	EXPECT_NE(a, pt::weatherText(rain, 5.04, {0, 1.5, 5}, 0.02)) << "it falls";
	rain.amount = 0.5;
	EXPECT_LT(pt::weatherText(rain, 5.0, {0, 1.5, 5}, 0.02).size(), 0.6 * a.size()) << "half as much rain";
	// a drop's place does not follow the camera: a mesh round a camera moved a metre has the drops of both near both
	pt::Weather snow;
	snow.kind = "snow";
	const std::string here = pt::weatherText(snow, 3.0, {0, 1.5, 0}, 0.02), there = pt::weatherText(snow, 3.0, {1, 1.5, 0}, 0.02);
	std::string flake;   // (one near both cameras: far ones drop out of reach as it moves)
	for (std::size_t at = here.find("Translate"); at != std::string::npos && flake.empty(); at = here.find("Translate", at + 1)) {
		double x = 0, y = 0, z = 0;
		if (std::sscanf(here.c_str() + at, "Translate %lf %lf %lf", &x, &y, &z) == 3 && std::hypot(x, z) < 4.0) flake = here.substr(at, here.find('\n', at) - at);
	}
	ASSERT_FALSE(flake.empty());
	EXPECT_NE(there.find(flake), std::string::npos) << "the same flake in the same place: " << flake;
	// particles: sparks only while they are due (and a little after, as the last ones die), a burst all at once
	pt::Emitter sparks;
	sparks.kind = "sparks";
	sparks.at = {0, 1, 0};
	sparks.t0 = 1.0;
	sparks.t1 = 2.0;
	sparks.rate = 50.0;
	const auto count = [](const std::string &s) { std::size_t n = 0; for (std::size_t at = s.find("AreaLightSource"); at != std::string::npos; at = s.find("AreaLightSource", at + 1)) ++n; return n; };
	EXPECT_EQ(count(pt::emitterText(sparks, 0.9)), 0u);
	EXPECT_GT(count(pt::emitterText(sparks, 1.8)), 20u);
	EXPECT_EQ(count(pt::emitterText(sparks, 4.0)), 0u) << "all gone";
	pt::Emitter splash;
	splash.kind = "splash";
	splash.t0 = 1.0;
	splash.count = 30;
	const auto spheres = [](const std::string &s) { std::size_t n = 0; for (std::size_t at = s.find("\"sphere\""); at != std::string::npos; at = s.find("\"sphere\"", at + 1)) ++n; return n; };
	EXPECT_EQ(spheres(pt::emitterText(splash, 1.05)), 30u) << "a burst: all at once";
	// in a film file
	(void)cartoon::films();
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 10\nweather rain 0.8 from 1 to 8\nparticles sparks at 1 1 0 2 3 rate 40\nburst dust at 0 0 1 4 count 25\n", film), "");
	ASSERT_EQ(film.spec.weather.size(), 1u);
	ASSERT_EQ(film.spec.emitters.size(), 2u);
	EXPECT_EQ(film.spec.emitters[1].count, 25);
	EXPECT_NE(film.sceneAt(5.0).find("Shape \"trianglemesh\""), std::string::npos) << "the rain";
	EXPECT_NE(film.sceneAt(2.5).find("AreaLightSource"), std::string::npos) << "the sparks";
	cartoon::audio::Mix mix(10.0);
	pt::rainSound(mix, film.spec.weather[0]);
	const std::vector<std::int16_t> pcm = mix.finish(0.0, 0.0);
	double during = 0.0, before = 0.0;
	for (std::size_t i = 0; i < pcm.size(); ++i) (i < cartoon::audio::kRate * 2 * 0.9 ? before : during) += std::abs(static_cast<double>(pcm[i]));
	EXPECT_GT(during, 10.0 * before + 1.0) << "the rain is heard while it rains";
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nweather hail\n", bad).find("rain, snow or leaves"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nparticles smoke at 0 0 0 1 2\n", bad).find("sparks, dust or splash"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nburst splash at 0 0 0 1 rate 5\n", bad).find("a burst takes count"), std::string::npos);
}

// ---- a film file from a screenplay ---------------------------------------------------------------------------------------------------------------------------------------------

// A Fountain screenplay is read (its title page, scene headings, action, cues, parentheticals and transitions; notes and emphasis taken out) and made into a film file that
// loads: its characters of the cast's kinds, its lines one after another without overlapping, a covered shot of each scene, its dissolve and fades, a parenthetical's
// expression and gesture.
TEST(Fountain, AScreenplayBecomesAFilmFileThatLoads) {
	(void)cartoon::films();
	const std::string script =
		"Title: The *First* Star\nCast: PIP=pip\n\n"
		"FADE IN:\n\n"
		"EXT. THE PATH - DUSK\n\n"
		"Pip looks up at the sky. [[a note]]\n\n"
		"PIP\n(excited)\nLook, the first star!\n\n"
		"ANN (V.O.)\nIt is very bright.\n\n"
		"DISSOLVE TO:\n\n"
		"EXT. THE HILL - NIGHT\n\n"
		"PIP\n(waving)\nGoodnight, star.\n\n"
		"FADE OUT.\n";
	const cartoon::fountain::Script s = cartoon::fountain::parse(script);
	EXPECT_EQ(s.title, "The First Star");
	ASSERT_EQ(s.scenes.size(), 2u);
	EXPECT_EQ(s.scenes[0].heading, "EXT. THE PATH - DUSK");
	EXPECT_EQ(s.scenes[0].transition, "fade");
	EXPECT_EQ(s.scenes[1].transition, "dissolve");
	ASSERT_EQ(s.scenes[0].action.size(), 1u);
	EXPECT_EQ(s.scenes[0].action[0], "Pip looks up at the sky.");
	ASSERT_EQ(s.scenes[0].lines.size(), 2u);
	EXPECT_EQ(s.scenes[0].lines[1].who, "ANN") << "without (V.O.)";
	EXPECT_EQ(s.scenes[0].lines[0].parenthetical, "excited");
	EXPECT_TRUE(s.fadeOut);
	EXPECT_EQ(s.characters, (std::vector<std::string>{"PIP", "ANN"}));
	// the film file
	const std::string text = cartoon::fountain::filmFile(s, "s.pbrt", "first");
	ff::DataFilm film;
	ASSERT_EQ(loadFilm(text, film), "") << text;
	EXPECT_EQ(film.spec.key, "the-first-star");
	ASSERT_EQ(film.spec.characters.size(), 2u);
	EXPECT_EQ(film.spec.characters[0].kind, "pip");
	EXPECT_EQ(film.spec.characters[1].kind, "person");
	ASSERT_EQ(film.spec.lines.size(), 3u);
	for (std::size_t i = 1; i < film.spec.lines.size(); ++i) EXPECT_GT(film.spec.lines[i].t, cartoon::speech::endOf(film.spoken[i - 1], film.spec.lines[i - 1].t)) << "one after another";
	EXPECT_GE(film.spec.length, cartoon::speech::endOf(film.spoken.back(), film.spec.lines.back().t));
	EXPECT_EQ(film.spec.characters[0].feelings.size(), 1u) << "(excited)";
	EXPECT_EQ(film.spec.characters[0].feelings[0].when.pose, "happy");
	ASSERT_EQ(film.spec.characters[0].gestures.size(), 2u) << "(waving), and a point at \"Look\" (acting words: the script gives that line a feeling, no gesture)";
	EXPECT_EQ(film.spec.characters[0].gestures[0].pose, "wave");
	EXPECT_EQ(film.spec.characters[0].gestures[1].pose, "point");
	EXPECT_TRUE(film.spec.actingWords);
	const std::vector<cartoon::Transition> trs = film.transitions();
	ASSERT_EQ(trs.size(), 3u) << "fade in, dissolve, fade out";
	EXPECT_EQ(trs[1].kind, cartoon::Transition::Dissolve);
	EXPECT_GE(film.spec.shots.size(), 2u) << "each scene covered";
	EXPECT_NE(film.spec.shots[0].name.find("EXT. THE PATH"), std::string::npos);
}

// ---- idle life ------------------------------------------------------------------------------------------------------------------------------------------------------------------

// While someone speaks the others turn to them (and the speaker to the nearest listener), the look moving from one speaker to the next; a `look` line wins; standing still a
// character is not still (its head drifts, its eyes dart); `idle off` turns all of it off.
TEST(FilmIdle, ListenersTurnToTheSpeakerAndAStandingCharacterIsNotStill) {
	(void)cartoon::films();
	const std::string base = "scene s.pbrt\nlength 12\ncharacter pip pip at -1 0 0 facing 90\ncharacter ann person at 1 0 0 facing -90\ncharacter tess turtle at 0 0 2 scale 6\n"
							 "say pip 1.0 \"Have you seen my mitten?\"\nsay ann 5.0 \"I think Tess has it.\"\n";
	ff::DataFilm film;
	ASSERT_EQ(loadFilm(base + "look tess at 3 0 5 6 6.5\n", film), "");
	const ff::Character &pip = *film.character("pip"), &ann = *film.character("ann"), &tess = *film.character("tess");
	const auto head = [&](const ff::Character &c, double t) { return film.bodyPoint(c, t, ff::characterKinds().at(c.kind).head); };
	EXPECT_EQ(film.actingMoment(ann, 0.3).look.weight, 0.0) << "before anyone speaks";
	const ff::CharacterMoment listening = film.actingMoment(ann, 2.0);
	EXPECT_GT(listening.look.weight, 0.8);
	EXPECT_LT(cartoon::length(listening.look.at - head(pip, 2.0)), 1e-9) << "ann looks at pip while pip speaks";
	EXPECT_LT(cartoon::length(film.actingMoment(pip, 2.0).look.at - head(ann, 2.0)), 1e-9) << "pip at the nearest listener";
	EXPECT_LT(cartoon::length(film.actingMoment(tess, 6.0).look.at - head(ann, 6.0)), 0.6) << "tess turns to ann when ann speaks";
	EXPECT_NEAR(film.actingMoment(tess, 8.0).look.at.z, 5.0, 1e-9) << "its own look wins";
	// idle: a character standing still looks a little different a second later
	const auto text = [](const std::string &scene, const std::string &who) { const std::size_t at = scene.find("# " + who + " ("); return scene.substr(at, scene.find("\n# ", at + 1) - at); };
	EXPECT_NE(text(film.sceneAt(10.0), "pip"), text(film.sceneAt(10.7), "pip"));
	ff::DataFilm still;
	ASSERT_EQ(loadFilm(base + "idle off\n", still), "");
	EXPECT_EQ(still.actingMoment(ann, 2.0).look.weight, 0.0) << "no turning to the speaker";
	EXPECT_EQ(still.actingMoment(pip, 10.0).idle, false) << "and no idle life";
}

// ---- character looks ------------------------------------------------------------------------------------------------------------------------------------------------------------

// A character line colours its kind's parts - by name, r,g,b or #rrggbb - and the colour is in the scene; a part its kind has not is refused with the kind's parts.
TEST(FilmLooks, ACharacterLineColoursItsKindsParts) {
	(void)cartoon::films();
	cartoon::Vec3 c;
	EXPECT_TRUE(ff::colourOf("red", c));
	EXPECT_TRUE(ff::colourOf("0.2,0.4,0.6", c));
	EXPECT_NEAR(c.y, 0.4, 1e-12);
	EXPECT_TRUE(ff::colourOf("#ffffff", c));
	EXPECT_NEAR(c.x, 1.0, 1e-12);
	EXPECT_FALSE(ff::colourOf("reddish", c));
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 4\ncharacter ann person sweater 0.1,0.7,0.3 hair blond\ncharacter kim pip skin #ff80c0 at 2 0 0\n", film), "");
	EXPECT_EQ(film.spec.characters[0].colours.size(), 2u);
	const std::string scene = film.sceneAt(1.0);
	EXPECT_NE(scene.find("0.10000 0.70000 0.30000"), std::string::npos) << "the green sweater";
	ff::DataFilm plain;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 4\ncharacter ann person\ncharacter kim pip at 2 0 0\n", plain), "");
	EXPECT_EQ(plain.sceneAt(1.0).find("0.10000 0.70000 0.30000"), std::string::npos);
	ff::DataFilm bad;
	const std::string wrong = loadFilm("scene s.pbrt\nlength 4\ncharacter kim pip sweater red\n", bad);
	EXPECT_NE(wrong.find("a pip has no part called \"sweater\""), std::string::npos) << wrong;
	EXPECT_NE(wrong.find("belly"), std::string::npos) << "its parts listed";
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 4\ncharacter kai nomai-explorer robe red\n", bad).find("its colours are its own"), std::string::npos);
}

// ---- more ways to move ---------------------------------------------------------------------------------------------------------------------------------------------------------

// A run steps 1.6 times as far as a walk with its planted feet still held; a jump gathers before it, rises in an arc, lands where it was sent (heard) and its feet are held
// from where it landed; a character sits down on a seat and stands up again.
TEST(FilmMoves, ARunAJumpAndSittingDown) {
	(void)cartoon::films();
	for (const char *kind : {"person", "pip"}) {
		SCOPED_TRACE(kind);
		ff::DataFilm film;
		ASSERT_EQ(loadFilm(std::string("scene s.pbrt\nlength 20\ncharacter c ") + kind + " at 0 0 4 facing 180\n"
										"run c 1 3.0  0 -2\n"
										"jump c 5 to 0 -4 height 0.6\n"
										"walk c 8 0.8  1 -6\n"
										"sit c 13 height 0.45 until 16\n",
						   film),
				  "");
		const ff::Character &c = film.spec.characters[0];
		const ff::CharacterKind &k = ff::characterKinds().at(kind);
		// the run: steps 1.6 times a walk's stride, and its planted feet do not slide
		EXPECT_NEAR(film.pathOf(c, 0).stride, k.stride * 1.6, 1e-12);
		EXPECT_DOUBLE_EQ(film.momentOf(c, 2.0).running, 1.0);
		double worst = 0.0;
		std::vector<cartoon::Foot> before = film.feetAt(1.0);
		for (double t = 1.0 + 1.0 / 48.0; t < 3.0; t += 1.0 / 48.0) {
			const std::vector<cartoon::Foot> now = film.feetAt(t);
			for (std::size_t i = 0; i < now.size(); ++i)
				if (before[i].planted && now[i].planted) worst = std::max(worst, cartoon::length(now[i].at - before[i].at) * 48.0);
			before = now;
		}
		EXPECT_LT(worst, 0.02) << "m/s";
		// the jump: gathering just before it, in the air half way, landed where it was sent
		EXPECT_GT(film.momentOf(c, 4.85).crouch, 0.5) << "gathering itself";
		const double flight = 2.0 * std::sqrt(2.0 * 0.6 / 9.81);
		cartoon::Vec3 mid, landed;
		const ff::CharacterMoment air = film.momentOf(c, 5.0 + 0.5 * flight, &mid);
		EXPECT_TRUE(air.airborne);
		EXPECT_NEAR(mid.y, 0.6, 1e-6) << "the top of the arc";
		film.momentOf(c, 5.0 + flight + 0.5, &landed);
		EXPECT_NEAR(landed.z, -4.0, 1e-9);
		EXPECT_GT(film.momentOf(c, 5.0 + flight + 0.1).crouch, 0.5) << "the landing squashes it";
		int landings = 0;
		for (const cartoon::FilmEvent &e : film.events())
			if (std::abs(e.t - (5.0 + flight)) < 1e-6) ++landings;
		EXPECT_EQ(landings, 1) << "heard";
		// after landing its feet are held where it landed, not where it took off (2 m back)
		for (const cartoon::Foot &f : film.feetAt(5.0 + flight + 0.3)) EXPECT_LT(std::abs(f.at.z - (-4.0)), 0.6);
		// sitting down and standing up
		EXPECT_EQ(film.momentOf(c, 12.5).sit, 0.0);
		EXPECT_DOUBLE_EQ(film.momentOf(c, 14.0).sit, 1.0);
		EXPECT_NEAR(film.momentOf(c, 14.0).seat, 0.45, 1e-12);
		EXPECT_EQ(film.momentOf(c, 17.0).sit, 0.0);
		EXPECT_GT(film.sceneAt(14.0).size(), 1000u);
	}
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\ncharacter c pip\njump c 2 to 1 1 height 9\n", bad).find("up to 5 m"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\ncharacter c pip\nsit c 2 until 2.1\n", bad).find("by 0.7 s at least"), std::string::npos);
}

// ---- uneven ground ---------------------------------------------------------------------------------------------------------------------------------------------------------------

// A platform 0.6 m up with a ramp to it on one side and drops on the others: a walk to its top from below a drop goes round by the ramp and climbs it, a jump lands on it, and
// a walk on the level ground keeps exactly its height.
TEST(FilmGround, AWalkClimbsTheRampToAPlatformAndAJumpLandsOnIt) {
	(void)cartoon::films();
	const std::string set =
		"WorldBegin\n"
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2 0 2 3 ] \"point3 P\" [ -20 0 -20  20 0 -20  20 0 20  -20 0 20 ]\n"   // the ground
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2 0 2 3  4 5 6 4 6 7  8 9 10 8 10 11  12 13 14 12 14 15  16 17 18 16 18 19 ] \"point3 P\" [ "
		"2 0.6 -1  4 0.6 -1  4 0.6 1  2 0.6 1 "   // the platform's top
		"2 0 -1  4 0 -1  4 0.6 -1  2 0.6 -1 "     // its sides (the drops)
		"2 0 1  4 0 1  4 0.6 1  2 0.6 1 "
		"4 0 -1  4 0 1  4 0.6 1  4 0.6 -1 "
		"0 0 -1  2 0.6 -1  2 0.6 1  0 0 1 ]\n";   // the ramp up to it from x 0
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 30\ncharacter ann person at 3 0 -3\nwalk ann 1 1 to 3.2 0\njump ann 15 to 3.2 -2.5 height 0.3\ncharacter bo person at -6 0 6\nwalk bo 1 1 -3 6\n",
					   film, set),
			  "");
	const ff::WalkLine &w = film.spec.characters[0].walks[0];
	ASSERT_EQ(w.heights.size(), w.points.size());
	EXPECT_NEAR(w.heights.back(), 0.6, 0.05) << "on top";
	bool onRamp = false, overDrop = false;
	for (std::size_t i = 0; i < w.points.size(); ++i) {
		const double x = w.points[i].first, z = w.points[i].second, y = w.heights[i];
		onRamp = onRamp || (x > 0.2 && x < 1.8 && std::abs(z) < 1.0 && y > 0.1 && y < 0.5);
		if (y < 0.3 && x > 2.05 && x < 3.95 && std::abs(z) < 0.95) overDrop = true;   // (low, yet over the platform: through its side)
	}
	EXPECT_TRUE(onRamp) << "it went up the ramp";
	EXPECT_FALSE(overDrop) << "not through the platform's side";
	cartoon::Vec3 top;
	film.momentOf(film.spec.characters[0], 14.0, &top);
	EXPECT_NEAR(top.y, 0.6, 0.05) << "standing on it";
	// off it again: a jump down lands on the ground
	cartoon::Vec3 down;
	film.momentOf(film.spec.characters[0], 20.0, &down);
	EXPECT_NEAR(down.y, 0.0, 1e-9) << "jumped down onto the ground";
	// the level walk: exactly its own height
	for (double h : film.spec.characters[1].walks[0].heights) EXPECT_EQ(h, 0.0);
	EXPECT_EQ(film.terrainFor(film.spec.characters[1]), nullptr) << "level ground: its feet as ever";
	EXPECT_NE(film.terrainFor(film.spec.characters[0]), nullptr);
	ff::DataFilm bad;
	const std::string cliff = loadFilm("scene s.pbrt\nlength 30\ncharacter ann person at 3 0 -3\nwalk ann 1 1 to 3 0.5\n", bad,
									   set.substr(0, set.find("0 0 -1  2 0.6 -1  2 0.6 1  0 0 1")) + "0 0 -1  0 0 -1  0 0 1  0 0 1 ]\n");   // (no ramp)
	EXPECT_NE(cliff.find("cannot find a way"), std::string::npos) << cliff;
}

// ---- lip sync from a recording ----------------------------------------------------------------------------------------------------------------------------------------------------

// A recorded voice's mouth shapes from its formants: an "ah" opens the mouth widest, an "ee" is wide, an "oo" round, and the mouth is shut where it is silent. (The recordings
// here are the babble voice, whose vowels' formants are known.)
TEST(FilmLipSync, ARecordingsVowelsGiveTheMouthItsShapes) {
	namespace sp = cartoon::speech;
	const auto shapes = [](const std::string &text, double pitch) {
		sp::Line l;
		l.text = text;
		const std::vector<sp::Syllable> y = sp::syllablesOf(l);
		sp::Voice v;
		v.pitch = pitch;
		const std::vector<float> sound = sp::speak(y, 0.0, v);
		std::vector<float> loud;
		double top = 1e-9;
		for (std::size_t at = 0; at < sound.size(); at += cartoon::audio::kRate / 100) {
			double s = 0.0;
			const std::size_t end = std::min(sound.size(), at + cartoon::audio::kRate / 100);
			for (std::size_t i = at; i < end; ++i) s += static_cast<double>(sound[i]) * sound[i];
			loud.push_back(static_cast<float>(std::sqrt(s / std::max<std::size_t>(1, end - at))));
			top = std::max(top, static_cast<double>(loud.back()));
		}
		for (float &x : loud) x = static_cast<float>(x / top);
		const std::vector<sp::Mouth> m = sp::mouthsOf(sound, loud);
		sp::Mouth mean;
		int n = 0;
		for (std::size_t f = 0; f < m.size(); ++f)
			if (loud[f] > 0.5) mean.open += m[f].open, mean.wide += m[f].wide, mean.round += m[f].round, ++n;
		mean.open /= std::max(1, n), mean.wide /= std::max(1, n), mean.round /= std::max(1, n);
		return std::make_pair(mean, m);
	};
	for (double pitch : {140.0, 290.0}) {
		SCOPED_TRACE(pitch);
		const auto [ah, ahFrames] = shapes("ah ah ah ah", pitch);
		const auto [ee, eeFrames] = shapes("ee ee ee ee", pitch);
		const auto [oo, ooFrames] = shapes("oo oo oo oo", pitch);
		EXPECT_GT(ah.open, ee.open);
		EXPECT_GT(ah.open, oo.open) << "an ah opens widest";
		EXPECT_GT(ee.wide, 0.3) << "an ee is wide";
		EXPECT_GT(ee.wide, oo.wide + 0.2);
		EXPECT_GT(oo.round, 0.3) << "an oo is round";
		EXPECT_GT(oo.round, ee.round + 0.2);
		EXPECT_EQ(ahFrames.back().open, 0.0) << "silent at the end: shut";
	}
}

// ---- camera moves ----------------------------------------------------------------------------------------------------------------------------------------------------------------

// An orbit goes round its character, a crane rises in front of it, a push comes in towards it, a track keeps beside it as it walks; a hand-held camera wanders a little; a focus
// pull moves the focus from one character to another.
TEST(FilmCamera, OrbitCranePushTrackHandheldAndAFocusPull) {
	(void)cartoon::films();
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 20\ncharacter ann person at 0 0 0 facing 0\ncharacter pip pip at 1.5 0 3\nwalk ann 15 1.0  0 6\n"
					   "shot 0 Round\n  orbit ann radius 3 degrees 90\n"
					   "shot 4 Up\n  crane ann from 1 to 4 distance 4\n"
					   "shot 8 In\n  push ann from 6 to 2\n  focus ann 10 pip\n  aperture 0.05\n"
					   "shot 12 Shaky\n  key 0 1.5 6  0 1 0\n  handheld 2\n"
					   "shot 15 Along\n  track ann side 3\n",
					   film),
			  "");
	const auto cam = [&](double t, cartoon::Vec3 &eye, cartoon::Vec3 &target) {
		double fov = 0;
		film_subjects::cameraOf(film.headerAt(t), eye, target, fov);
	};
	cartoon::Vec3 e0, e1, tg;
	// the orbit: 3 m out from her, a quarter of the way round over the shot
	cam(0.0, e0, tg);
	cam(3.999, e1, tg);
	EXPECT_NEAR(std::hypot(e0.x, e0.z), 3.0, 1e-6);
	EXPECT_NEAR(std::hypot(e1.x, e1.z), 3.0, 1e-3);
	const double turned = std::acos(std::clamp((e0.x * e1.x + e0.z * e1.z) / 9.0, -1.0, 1.0)) * 180.0 / 3.14159265358979;
	EXPECT_NEAR(turned, 90.0, 1.0);
	// the crane: from 1 m up to 4
	cam(4.0, e0, tg);
	cam(7.999, e1, tg);
	EXPECT_NEAR(e0.y, 1.0, 1e-6);
	EXPECT_NEAR(e1.y, 4.0, 1e-3);
	// the push: from 6 m to 2
	cam(8.0, e0, tg);
	cam(11.999, e1, tg);
	EXPECT_NEAR(std::hypot(e0.x, e0.z), 6.0, 0.2);
	EXPECT_NEAR(std::hypot(e1.x, e1.z), 2.0, 0.2);
	// the focus pull: on ann, then on pip
	const auto focusAt = [&](double t) {
		const std::string h = film.headerAt(t);
		const std::size_t at = h.find("focaldistance");
		return at == std::string::npos ? -1.0 : std::stod(h.substr(h.find('[', at) + 1));
	};
	cartoon::Vec3 eye, target;
	cam(9.0, eye, target);
	EXPECT_NEAR(focusAt(9.0), cartoon::length(film.bodyPoint(*film.character("ann"), 9.0, ff::characterKinds().at("person").head) - eye), 1e-3);
	cam(11.0, eye, target);
	EXPECT_NEAR(focusAt(11.0), cartoon::length(film.bodyPoint(*film.character("pip"), 11.0, ff::characterKinds().at("pip").head) - eye), 1e-3);
	// hand-held: near where its key puts it, not quite still
	cam(13.0, e0, tg);
	cam(13.5, e1, tg);
	EXPECT_LT(std::abs(e0.x) + std::abs(e0.y - 1.5) + std::abs(e0.z - 6.0), 0.15);
	EXPECT_GT(cartoon::length(e0 - e1), 1e-4);
	// the track: 3 m to her side all along her walk
	for (double t : {15.5, 17.0, 19.0}) {
		cartoon::Vec3 feet;
		film.momentOf(*film.character("ann"), t, &feet);
		cam(t, e0, tg);
		EXPECT_NEAR(std::hypot(e0.x - feet.x, e0.z - feet.z), 3.0, 1e-6);
		EXPECT_NEAR(e0.z, feet.z, 1e-6) << "level with her";
	}
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\ncharacter ann person\nshot 0 A\n  orbit ann\n  key 0 1 0 0 1 4\n", bad).find("has a move and"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\ncharacter ann person\nshot 0 A\n  orbit ann side 3\n", bad).find("an orbit takes"), std::string::npos);
}

// ---- crowds ----------------------------------------------------------------------------------------------------------------------------------------------------------------------

// A crowd is so many extras of varied looks, each wandering in a cell of its own of the rectangle (they never meet), the same every time the film is loaded; they are not in
// an `auto` shot's cast unless named.
TEST(FilmCrowd, ExtrasOfVariedLooksWanderInCellsOfTheirOwn) {
	(void)cartoon::films();
	const std::string text = "scene s.pbrt\nlength 20\ncrowd folk person 6 in 3 -6 9 -2 seed 4\ncharacter ann person at -3 0 2\ncharacter bo person at -3 0 4\n"
							 "say ann 1 \"Look at them all.\"\nshot 0 Talk\n  auto\n";
	ff::DataFilm film, again;
	ASSERT_EQ(loadFilm(text, film), "");
	ASSERT_EQ(loadFilm(text, again), "");
	ASSERT_EQ(film.spec.characters.size(), 8u);
	std::set<std::string> sweaters;
	for (std::size_t i = 0; i < 6; ++i) {
		const ff::Character &c = film.spec.characters[i];
		EXPECT_TRUE(c.extra);
		EXPECT_EQ(c.name, "folk-" + std::to_string(i + 1));
		EXPECT_GT(c.walks.size(), 1u) << "it wanders";
		const cartoon::Vec3 col = c.colours.at("sweater");
		char buf[64];
		std::snprintf(buf, sizeof buf, "%.3f %.3f %.3f", col.x, col.y, col.z);
		sweaters.insert(buf);
		EXPECT_EQ(c.start[0], again.spec.characters[i].start[0]) << "the same every time";
	}
	EXPECT_GE(sweaters.size(), 3u) << "varied looks";
	// over the film: inside the rectangle, and never two closer than their sizes
	for (double t = 0.0; t < 20.0; t += 0.25) {
		std::vector<cartoon::Vec3> at;
		for (std::size_t i = 0; i < 6; ++i) {
			cartoon::Vec3 p;
			film.momentOf(film.spec.characters[i], t, &p);
			EXPECT_GE(-p.x, 3.0 - 1e-6);
			EXPECT_LE(-p.x, 9.0 + 1e-6);
			EXPECT_GE(p.z, -6.0 - 1e-6);
			EXPECT_LE(p.z, -2.0 + 1e-6);
			at.push_back(p);
		}
		for (std::size_t a = 0; a < at.size(); ++a)
			for (std::size_t b = a + 1; b < at.size(); ++b) EXPECT_GT(std::hypot(at[a].x - at[b].x, at[a].z - at[b].z), 0.5) << "t " << t;
	}
	for (const ff::Shot &s : film.spec.shots)
		for (const std::string &who : s.about) EXPECT_EQ(who.rfind("folk", 0), std::string::npos) << s.name << " is about " << who;
	EXPECT_NE(film.sceneAt(5.0).find("# folk-6 (person)"), std::string::npos);
}

// ---- music that follows the film --------------------------------------------------------------------------------------------------------------------------------------------------

// The music is ducked under the lines (down by two thirds while one is heard, back after it); an `oncuts` cue hits every cut; a drafted score gives each shot the mood its
// characters feel (minor and slow for sad, major for happy) and reads back as a cues file with nothing wrong in it.
TEST(FilmMusic, MusicDucksUnderLinesHitsTheCutsAndADraftScoreFollowsTheMood) {
	namespace mc = cartoon::music_cues;
	// ducking: speech from 2 s to 4 s
	const std::vector<float> gain = mc::duckingOf([](double t) { return t >= 2.0 && t < 4.0 ? 1.0 : 0.0; }, 8.0);
	EXPECT_NEAR(gain[100], 1.0, 1e-6) << "before the line";
	EXPECT_NEAR(gain[350], 0.35, 0.01) << "under it: down by two thirds";
	EXPECT_GT(gain[700], 0.95) << "back after it";
	cartoon::audio::Mix a(3.0), music(3.0);
	music.put(0.5, std::vector<float>(cartoon::audio::kRate, 0.5f), 1.0, 0.0, 0.0);
	a.add(music, std::vector<float>(400, 0.5f));
	cartoon::audio::Mix full(3.0);
	full.add(music, {});
	const std::vector<std::int16_t> pcm = a.finish(0.0, 0.0), whole = full.finish(0.0, 0.0);
	EXPECT_NEAR(static_cast<double>(pcm[cartoon::audio::kRate * 2]) / whole[cartoon::audio::kRate * 2], 0.5, 0.03) << "laid on at half its level";
	// cut hits
	const mc::Sheet sheet = mc::parse("oncuts mallet A3 0.5 gain 0.3\n");
	ASSERT_TRUE(sheet.problems.empty());
	cartoon::audio::Mix hits(8.0);
	std::vector<std::string> problems;
	mc::play(hits, sheet, {}, {0.0, 2.0, 5.0}, {"A", "B", "C"}, problems);
	const std::vector<std::int16_t> h = hits.finish(0.0, 0.0);
	const auto level = [&](double t0, double t1) {
		double s = 0.0;
		for (std::size_t i = static_cast<std::size_t>(t0 * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(t1 * cartoon::audio::kRate) * 2; ++i) s += std::abs(static_cast<double>(h[i]));
		return s;
	};
	EXPECT_GT(level(2.0, 2.3), 100.0 * (level(0.0, 0.3) + 1.0)) << "a hit at the second shot's cut, none at the start";
	EXPECT_GT(level(5.0, 5.3), 100.0 * (level(0.0, 0.3) + 1.0));
	// a drafted score
	(void)cartoon::films();
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 16\ncharacter ann person\nfeel ann happy 0 0.5 6 7\nfeel ann sad 8 8.5\nshot 0 Glad\n  key 0 1 5 0 1 0\nshot 8 Sorry\n  key 0 1 5 0 1 0\n", film), "");
	const std::string draft = cartoon::score::draft(film);
	EXPECT_NE(draft.find("[1]   # Glad: happy, major"), std::string::npos) << draft.substr(0, 400);
	EXPECT_NE(draft.find("[2]   # Sorry: sad, minor"), std::string::npos);
	const mc::Sheet drafted = mc::parse(draft);
	EXPECT_TRUE(drafted.problems.empty()) << (drafted.problems.empty() ? "" : drafted.problems[0]);
	cartoon::audio::Mix scored(16.0);
	problems.clear();
	mc::play(scored, drafted, {}, film.shotStarts(), film.shotNameList(), problems);
	EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0]);
	// the film's speech, which ducks it
	ff::DataFilm talk;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 6\ncharacter ann person\nsay ann 1 \"Hello there.\"\n", talk), "");
	EXPECT_EQ(talk.speechAt(0.5), 0.0);
	EXPECT_GT(talk.speechAt(talk.spoken[0][0].t0 + 0.05), 0.5);
}

// ---- the film timeline -----------------------------------------------------------------------------------------------------------------------------------------------------------

// A moment of a film is a scene file of its own (what Live Preview draws for "cartoon:<key>@<seconds>"), its camera the film's camera then; a film made from a file is read
// again when it is saved, and the moment drawn after that is of the new film.
TEST(FilmTimeline, AMomentIsASceneFileAndAFilmFileIsReadAgainWhenSaved) {
	(void)cartoon::films();
	const std::string moment = pbrt_discover::filmMoment("cartoon:lamp-and-ball@3.5");
	ASSERT_FALSE(moment.empty());
	std::string text;
	{
		std::ifstream in(moment, std::ios::binary);
		text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}
	EXPECT_NE(text.find("WorldBegin"), std::string::npos);
	const cartoon::Film *film = cartoon::find("lamp-and-ball");
	ASSERT_NE(film, nullptr);
	EXPECT_NE(text.find(film->sceneAt(3.5)), std::string::npos) << "the film at that moment";
	EXPECT_TRUE(pbrt_discover::filmMoment("cartoon:no-such-film@1").empty());
	const std::string later = pbrt_discover::filmMoment("cartoon:lamp-and-ball@4");
	EXPECT_NE(later, moment);
	EXPECT_TRUE(std::filesystem::exists(later));
	EXPECT_FALSE(std::filesystem::exists(moment)) << "only the latest moment of a film is kept";
	double cam[6];
	ASSERT_TRUE(pbrt_discover::filmCamera("lamp-and-ball", 3.5, cam));
	const std::string header = film->headerAt(3.5);
	char want[64];
	std::snprintf(want, sizeof want, "LookAt %g", cam[0]);
	EXPECT_NE(header.find(want), std::string::npos) << "the film's own camera then: " << header.substr(header.find("LookAt"), 60);
	// read again when saved
	ff::DataFilm film2;
	const std::filesystem::path d = std::filesystem::temp_directory_path() / "photonsmith_film_test_reload";
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	{
		std::ofstream(d / "s.pbrt", std::ios::binary) << kScene;
		std::ofstream(d / "t.film", std::ios::binary) << "scene s.pbrt\nlength 10\n";
	}
	ASSERT_EQ(film2.load((d / "t.film").string()), "");
	EXPECT_FALSE(film2.reloadIfChanged()) << "not saved since";
	std::filesystem::last_write_time(d / "t.film", std::filesystem::last_write_time(d / "t.film") + std::chrono::seconds(2));
	{
		std::ofstream(d / "t.film", std::ios::binary) << "scene s.pbrt\nlength 12\n";
	}
	std::filesystem::last_write_time(d / "t.film", std::filesystem::last_write_time(d / "t.film") + std::chrono::seconds(4));
	EXPECT_TRUE(film2.reloadIfChanged());
	EXPECT_DOUBLE_EQ(film2.spec.length, 12.0) << "the saved film";
	// a saved file with a mistake: said, and the film stays as it was
	{
		std::ofstream(d / "t.film", std::ios::binary) << "scene s.pbrt\nlength 12\nwobble fan\n";
	}
	std::filesystem::last_write_time(d / "t.film", std::filesystem::last_write_time(d / "t.film") + std::chrono::seconds(6));
	std::string problem;
	EXPECT_FALSE(film2.reloadIfChanged(&problem));
	EXPECT_NE(problem.find("wobble"), std::string::npos);
	EXPECT_DOUBLE_EQ(film2.spec.length, 12.0);
}
