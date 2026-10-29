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

#include "../../src/shared/cartoon/cartoon_films.h"   // (the films themselves, which these tests reach into)
#include "../../src/shared/cartoon/cartoons_registry.h"
#include "../../src/shared/cartoon/fountain.h"
#include "../../src/shared/cartoon/film_score.h"
#include "../../src/shared/cartoon/film_edit.h"
#include "../../src/shared/gltf_skin.h"
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
	ASSERT_EQ(r.all.size(), cartoon::kFilms.size() + 1u) << "only the good one was added";
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
	// (GitHub's hosted macOS runner lists `say`'s voices but cannot make a recording with some of them, and the film picks a character's voice among them all: there,
	// and only there, a voice that makes no recording skips the test. Anywhere else it is what this test is for, and fails.)
	const std::vector<std::string> listed = cartoon::tts::voices();
	for (unsigned k = 0; k < listed.size(); ++k) {   // ("system" with seed k is voice k: tts::pick)
		std::string why;
		if (!cartoon::tts::wavFor("Testing the speech engine.", "system", k, 0.0, 1.0, &why).empty()) continue;
#ifdef __APPLE__
		if (const char *ci = std::getenv("GITHUB_ACTIONS"); ci && std::string(ci) == "true") {
			cartoon::tts::runner() = kept;
			GTEST_SKIP() << "the hosted runner's voice " << listed[k] << " makes no recording: " << why;
		}
#endif
		ADD_FAILURE() << "the voice " << listed[k] << " makes no recording: " << why;
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

// Shot lighting (shot_lighting.h): a key to one side and up, a fill to the other, a rim behind, each giving the subject the light it says however far the camera is; in a
// film file's shot (on what the camera looks at, or a character), in an auto shot (on its speaker), and in a cameras file.
TEST(ShotLighting, AKeyAFillAndARimLightTheSubjectFromTheCamera) {
	namespace sl = cartoon::shot_lighting;
	sl::Lighting l;
	EXPECT_EQ(sl::parse("key 2 fill 0.5 rim 1.5 side right", l), "");
	EXPECT_TRUE(l.on);
	EXPECT_DOUBLE_EQ(l.key, 2.0);
	EXPECT_DOUBLE_EQ(l.side, 1.0);
	EXPECT_NE(sl::parse("key lots", l).find("strength"), std::string::npos);
	EXPECT_NE(sl::parse("glow 1", l).find("takes key, fill, rim and side"), std::string::npos);
	// the lights round a subject at the origin, the camera 4 in front (story space +z), the key on the picture's left (story space -x)
	sl::Lighting usual;
	ASSERT_EQ(sl::parse("", usual), "");
	for (const double distance : {4.0, 12.0}) {
		const std::string text = sl::lightsText({0.0, 1.0, distance}, {0.0, 1.0, 0.0}, usual);
		std::vector<std::vector<double>> spots;   // from x y z, to x y z, I r g b (pbrt's x)
		for (std::size_t at = text.find("\"point3 from\""); at != std::string::npos; at = text.find("\"point3 from\"", at + 1)) {
			std::vector<double> v;
			std::istringstream in(text.substr(at));
			std::string word;
			while (v.size() < 9 && in >> word) {
				char *end = nullptr;
				const double x = std::strtod(word.c_str(), &end);
				if (end && *end == '\0' && !word.empty()) v.push_back(x);
			}
			spots.push_back(v);
		}
		ASSERT_EQ(spots.size(), 3u);
		const auto irradiance = [&](const std::vector<double> &s) {
			const double d2 = (s[0] - s[3]) * (s[0] - s[3]) + (s[1] - s[4]) * (s[1] - s[4]) + (s[2] - s[5]) * (s[2] - s[5]);
			return (0.2126 * s[6] + 0.7152 * s[7] + 0.0722 * s[8]) / d2;
		};
		EXPECT_NEAR(irradiance(spots[0]), 1.2 * 0.9206, 0.02) << "the key gives the subject its strength (times its warm colour's brightness), at any distance";
		EXPECT_GT(spots[0][0], 0.5) << "the key on the picture's left: story space -x is pbrt's +x";
		EXPECT_GT(spots[0][1], 1.5) << "and up";
		EXPECT_LT(spots[1][0], 0.0) << "the fill on the other side";
		EXPECT_LT(spots[1][1], spots[0][1]) << "lower";
		EXPECT_LT(spots[2][2], 0.0) << "the rim behind the subject";
		EXPECT_LT(irradiance(spots[1]), irradiance(spots[0]));
	}
	// a film file's shot: only in that shot, round the character it names
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 8\ncharacter ann person at 2 0 0\nshot 0 A\n key 0 1 5  0 1 0\nshot 4 B\n key 0 1 5  0 1 0\n lighting key 1.5 on ann\n", film), "");
	EXPECT_EQ(film.sceneAt(2.0).find("\"spot\""), std::string::npos);
	const std::string lit = film.sceneAt(6.0);
	ASSERT_NE(lit.find("\"spot\""), std::string::npos);
	EXPECT_GT(lit.find("\"spot\""), lit.find("WorldBegin")) << "in the world";
	EXPECT_EQ(film.headerAt(6.0).find("\"spot\""), std::string::npos) << "the header stays the header";
	const std::size_t to = lit.find("\"point3 to\" [ ");
	ASSERT_NE(to, std::string::npos);
	EXPECT_NEAR(std::atof(lit.c_str() + to + 14), 2.0, 0.3) << "aimed at ann (pbrt's x 2), not at what the camera looks at";
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 8\nlighting\n", bad).find("belongs to a shot"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 8\nshot 0 A\n key 0 1 5  0 1 0\n lighting on nobody\n", bad).find("nobody"), std::string::npos);
	// an auto shot: on each speaker
	ff::DataFilm talk;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 8\ncharacter ann person at -1 0 0 facing 90\ncharacter bo person at 1 0 0 facing -90\nsay ann 1 \"Hello, Bo.\"\nsay bo 3.5 \"Hello, Ann.\"\n"
					   "shot 0 Talk\n auto\n lighting\n", talk), "");
	bool litBo = false;
	for (const ff::Shot &s : talk.spec.shots) {
		EXPECT_TRUE(s.lighting.on) << s.name;
		litBo = litBo || s.lightOn == "bo";
	}
	EXPECT_TRUE(litBo);
	// a cameras file's section
	const cartoon::shot_cameras::File cams = cartoon::shot_cameras::parse("[2]\nlighting rim 2\n[3]\nlighting side up\n");
	ASSERT_EQ(cams.problems.size(), 1u);
	const cartoon::shot_cameras::Change c = cartoon::shot_cameras::changeFor(cams, 1, "B");
	EXPECT_TRUE(c.lighting.on);
	EXPECT_DOUBLE_EQ(c.lighting.rim, 2.0);
	const std::string scene = "LookAt 0 1 5  0 1 0  0 1 0\nCamera \"perspective\"\nWorldBegin\nShape \"sphere\"\n";
	const std::string changed = cartoon::shot_cameras::apply(scene, c);
	EXPECT_NE(changed.find("\"spot\""), std::string::npos);
	EXPECT_EQ(cartoon::shot_cameras::apply("LookAt 0 1 5  0 1 0  0 1 0\nCamera \"perspective\"\nWorldBegin\n", c).find("\"spot\""), std::string::npos) << "a header alone";
}

// Contact actions: a hand on the set (`touch`: a wall to lean on, a door's handle), two characters' hands held (`hold`), a hug (`hug`) - reached for before, held, let go
// after, each heard as a touch.
TEST(FilmContacts, HandsGoOnTheSetAndOnEachOther) {
	(void)cartoon::films();
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 12\ncharacter ann person at 0 0 0 facing 90\ncharacter bo person at 0.9 0 0 facing -90\ncharacter cy person at 5 0 0\n"
					   "character di person at 5.35 0 0 facing -90\n"
					   "touch ann 1 3 -0.45 1.1 0.2 left\nhold ann bo 4 6\nhug cy di 7 9\n", film), "");
	const auto &kinds = ff::characterKinds();
	const ff::Character &ann = film.spec.characters[0], &bo = film.spec.characters[1], &cy = film.spec.characters[2], &di = film.spec.characters[3];
	const auto hand = [&](const ff::Character &c, int h, double t) { return kinds.at(c.kind).handAt(film.actingMoment(c, t), h); };
	// the touch: her left hand on the place (pbrt's x -0.45 is story space's 0.45), and not before or after
	const cartoon::Vec3 place{0.45, 1.1, 0.2};
	EXPECT_LT(cartoon::length(hand(ann, 0, 2.0) - place), 0.08);
	EXPECT_GT(cartoon::length(hand(ann, 0, 0.2) - place), 0.2);
	EXPECT_GT(cartoon::length(hand(ann, 0, 3.8) - place), 0.2);
	// holding hands: her right and his left together
	EXPECT_LT(cartoon::length(hand(ann, 1, 5.0) - hand(bo, 0, 5.0)), 0.08);
	EXPECT_GT(cartoon::length(hand(ann, 1, 2.0) - hand(bo, 0, 2.0)), 0.15);
	// the hug: both of cy's hands behind di (and di's behind cy)
	const cartoon::Vec3 cyChest = film.chestOf(cy, 8.0), diChest = film.chestOf(di, 8.0);
	cartoon::Vec3 toward = diChest - cyChest;
	toward.y = 0.0;
	toward = cartoon::normalize(toward);
	for (int h = 0; h < 2; ++h) {
		EXPECT_GT(cartoon::dot(hand(cy, h, 8.0) - cyChest, toward), 0.75 * cartoon::length(diChest - cyChest)) << "cy's hand " << h << " reaches round di's side";
		EXPECT_GT(cartoon::dot(hand(di, h, 8.0) - diChest, toward * -1.0), 0.75 * cartoon::length(diChest - cyChest)) << "di's hand " << h << " reaches round cy's side";
	}
	// each heard
	int touches = 0;
	for (const cartoon::FilmEvent &e : film.events())
		if (e.kind == cartoon::EventKind::Touch && (e.t == 1.0 || e.t == 4.0 || e.t == 7.0)) ++touches;
	EXPECT_EQ(touches, 3);
	// what is wrong
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 12\ncharacter ann person\ntouch ann 1 3 0 1\n", bad).find("`touch NAME T0 T1 X Y Z"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 12\ncharacter ann person\nhug ann ann 1 3\n", bad).find("someone else"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 12\ncharacter ann person\nhold ann zed 1 3\n", bad).find("zed is not brought in"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 12\ncharacter ann person\nhold ann zed 3 1\n", bad).find("later T1"), std::string::npos);
}

// Titles and credits: a title card over the opening with its episode, cards of credits after the story over black frames (the name, the cast, the film's own lines), and a
// theme under the title; in a film in parts the title is the first part's and the credits the last's.
TEST(FilmTitles, ATitleCardCreditsAfterTheStoryAndATheme) {
	(void)cartoon::films();
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nname The Lost Mitten\nlength 8\ntitle at 0.5 for 3\nepisode 2 \"Winter\"\ncredits\ncredit \"Music by Sam\"\ntheme jingle\n"
					   "character pip pip\ncharacter ann person at 2 0 0\nshot 0 A\n key 0 1 5  0 1 0\n", film), "");
	EXPECT_DOUBLE_EQ(film.spec.storyEnd, 8.0);
	EXPECT_DOUBLE_EQ(film.spec.length, 8.0 + 3.0 * 3) << "the name, the cast (two), the film's own lines: three cards";
	ASSERT_EQ(film.spec.shots.size(), 2u);
	EXPECT_EQ(film.spec.shots[1].name, "Credits");
	EXPECT_DOUBLE_EQ(film.spec.shots[1].start, 8.0);
	// the credits' frames: black (the set and the characters left out)
	EXPECT_NE(film.sceneAt(4.0).find("# pip (pip)"), std::string::npos);
	const std::string black = film.sceneAt(10.0);
	EXPECT_EQ(black.find("# pip (pip)"), std::string::npos);
	EXPECT_EQ(black.find("@anim"), std::string::npos);
	EXPECT_NE(black.find("WorldBegin"), std::string::npos);
	EXPECT_EQ(black.find("AreaLightSource"), std::string::npos);
	// a fade into them
	bool fade = false;
	for (const cartoon::Transition &tr : film.transitions()) fade = fade || (tr.kind == cartoon::Transition::Fade && tr.at == 8.0);
	EXPECT_TRUE(fade);
	// the cards
	const std::vector<cartoon::Caption> cards = film.titleCaptions();
	ASSERT_EQ(cards.size(), 4u) << "the title and three cards of credits";
	EXPECT_EQ(cards[0].lines[0].text, "The Lost Mitten");
	ASSERT_EQ(cards[0].lines.size(), 2u);
	EXPECT_EQ(cards[0].lines[1].text, "Episode 2: Winter");
	EXPECT_DOUBLE_EQ(cards[0].t0, 0.5);
	EXPECT_GE(cards[1].t0, 8.0);
	EXPECT_EQ(cards[2].lines[0].text, "Cast");
	EXPECT_EQ(cards[2].lines[1].text, "Pip  -  a pip");
	EXPECT_EQ(cards[2].lines[2].text, "Ann  -  a person");
	EXPECT_EQ(cards[3].lines[0].text, "Music by Sam");
	EXPECT_LE(cards[3].t1, film.spec.length);
	// the theme: heard under the title, and not without it
	cartoon::audio::Mix with(film.spec.length), without(film.spec.length);
	film.playTheme(with);
	const std::vector<std::int16_t> a = with.finish(), b = without.finish();
	double heard = 0.0;
	for (std::size_t i = static_cast<std::size_t>(0.6 * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(2.5 * cartoon::audio::kRate) * 2; ++i) heard += std::abs(a[i] - b[i]);
	EXPECT_GT(heard, 1e6);
	// what is wrong
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 8\ncredits\n", bad).find("needs a `shot` line"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 8\nepisode two\n", bad).find("`episode N"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 8\ntheme\n", bad).find("`theme jingle"), std::string::npos);
	// a film in parts: the title over the first part, the credits after the last, both with the whole film's name
	const std::filesystem::path d = tempDir("titles");
	write(d / "s.pbrt", kScene);
	write(d / "two.film", "name Two Parts\ntitle\ncredits\nscene s.pbrt\npart One\nlength 4\nshot 0 A\n key 0 1 5  0 1 0\npart Two\nlength 4\nshot 0 B\n key 0 1 5  0 1 0\n");
	ff::DataFilm one, two;
	ASSERT_EQ(one.load((d / "two.film").string(), 0), "");
	ASSERT_EQ(two.load((d / "two.film").string(), 1), "");
	EXPECT_TRUE(one.spec.hasTitle);
	EXPECT_FALSE(one.spec.credits);
	EXPECT_FALSE(two.spec.hasTitle);
	EXPECT_TRUE(two.spec.credits);
	EXPECT_EQ(one.titleCaptions()[0].lines[0].text, "Two Parts");
	EXPECT_EQ(two.titleCaptions()[0].lines[0].text, "Two Parts");
	// ... and the credits name the cast of every part, each once, in the order they come on
	write(d / "cast.film", "name Cast\ncredits\nscene s.pbrt\npart One\nlength 4\ncharacter ann person\ncharacter bo person at 2 0 0\nshot 0 A\n key 0 1 5  0 1 0\n"
						   "part Two\nlength 4\ncharacter bo person\ncharacter pip pip at 2 0 0\nshot 0 B\n key 0 1 5  0 1 0\n");
	ff::DataFilm last;
	ASSERT_EQ(last.load((d / "cast.film").string(), 1), "");
	std::vector<std::string> named;
	for (const cartoon::Caption &c : last.titleCaptions())
		if (c.lines[0].text == "Cast")
			for (std::size_t i = 1; i < c.lines.size(); ++i) named.push_back(c.lines[i].text);
	EXPECT_EQ(named, (std::vector<std::string>{"Ann  -  a person", "Bo  -  a person", "Pip  -  a pip"}));
}

// `ease` is the film's motions wherever its line is - after the shots too - and a shot's camera has `ease camera`; a shot that is not of `key` lines has none.
TEST(FilmFile, EaseIsTheFilmsWhereverItIsAndEaseCameraAShots) {
	const std::string shots = "scene s.pbrt\nlength 10\nshot 0 A\n key 0 1 5  0 1 0\n key 4 1 5  0 1 0\n";
	const ff::Parsed after = ff::parse(shots + "ease 0\n", "t.film");
	ASSERT_TRUE(after.ok) << after.error;
	EXPECT_FALSE(after.spec.ease) << "after a shot, still the film's";
	EXPECT_TRUE(after.spec.shots[0].path.ease) << "and not the shot's camera";
	const ff::Parsed camera = ff::parse(shots + "  ease camera 0\n", "t.film");
	ASSERT_TRUE(camera.ok) << camera.error;
	EXPECT_TRUE(camera.spec.ease);
	EXPECT_FALSE(camera.spec.shots[0].path.ease);
	EXPECT_NE(ff::parse("scene s.pbrt\nlength 10\nease camera 0\n", "t.film").error.find("belongs to a shot"), std::string::npos);
	EXPECT_NE(ff::parse("scene s.pbrt\nlength 10\nease 0.5\n", "t.film").error.find("`ease 1` or `ease 0`"), std::string::npos);
	EXPECT_NE(ff::parse("scene s.pbrt\nlength 10\ncharacter ann person\nshot 0 A\n orbit ann radius 3 degrees 120 height 1.6\n ease camera 0\n", "t.film").error.find("for a shot of `key` lines"), std::string::npos);
}

// Changes to a film file from the program (film_edit.h): a listed shot found as its file's line, its camera set to a view or given one more key, its start moved - the rest
// of the file kept as it was, and the file still a film that does what was asked.
TEST(FilmEdit, AShotsCameraAndStartAreChangedInTheFilmFile) {
	namespace fe = cartoon::film_edit;
	const std::string text =
		"name Edits   # a film to change\nscene s.pbrt\ncharacter ann person at -1 0 0 facing 90\ncharacter bo person at 1 0 0 facing -90\n"
		"part One\nlength 10\nshot 0 Wide   # the opening\n  key 0 1 5  0 1 0  40\n  key 1 1 5  0 1 0  40\nsay ann 2 \"Hello, Bo.\"\nshot 4 Talk\n  auto\nsay bo 6 \"Hello.\"\n"
		"part Two\nlength 6\nshot 0 Follow\n  follow ann\nshot 3 End\n  key 0 2 6  0 1 0\n";
	const std::vector<fe::ShotLine> lines = fe::shotLines(text);
	ASSERT_EQ(lines.size(), 4u);
	EXPECT_EQ(lines[0].name, "Wide") << "without its comment";
	EXPECT_DOUBLE_EQ(lines[1].start, 4.0);
	// the film's listed shots to the file's lines
	const std::vector<std::string> names = {"Wide", "Talk: ann", "Talk: bo", "Follow", "End", "Credits"};
	bool first = false;
	EXPECT_EQ(fe::shotLineFor(text, names, 0, &first), 0);
	EXPECT_EQ(fe::shotLineFor(text, names, 1, &first), 1);
	EXPECT_TRUE(first);
	EXPECT_EQ(fe::shotLineFor(text, names, 2, &first), 1);
	EXPECT_FALSE(first) << "the auto shot's second listed shot";
	EXPECT_EQ(fe::shotLineFor(text, names, 3), 2);
	EXPECT_EQ(fe::shotLineFor(text, names, 4), 3);
	EXPECT_EQ(fe::shotLineFor(text, names, 5), -1) << "the credits have no line";
	// a view for the shot: its camera (keys, follow, auto) replaced by one key, everything else kept
	const double eye[3] = {2, 1.5, 4}, target[3] = {0, 1, 0};
	const std::string key = fe::keyLine(eye, target, 35);
	EXPECT_EQ(key, "key   2 1.5 4   0 1 0   35");
	std::string why;
	const std::string wide = fe::setShotView(text, 0, key, false, why);
	EXPECT_EQ(why, "");
	EXPECT_NE(wide.find("shot 0 Wide   # the opening\n  key   2 1.5 4   0 1 0   35\nsay ann"), std::string::npos) << wide;
	EXPECT_EQ(wide.find("key 1 1 5"), std::string::npos);
	const std::string keepsFov = fe::setShotView(text, 0, fe::keyLine(eye, target), false, why);
	EXPECT_NE(keepsFov.find("  key   2 1.5 4   0 1 0   40\n"), std::string::npos) << "a view with no fov keeps the shot's (40)";
	const std::string follow = fe::setShotView(text, 2, key, false, why);
	EXPECT_EQ(follow.find("follow ann"), std::string::npos);
	EXPECT_NE(follow.find("shot 0 Follow\n  key   2 1.5 4"), std::string::npos);
	// one more key: after its keys; not for a shot whose camera is not keys
	const std::string more = fe::setShotView(text, 0, key, true, why);
	EXPECT_NE(more.find("  key 1 1 5  0 1 0  40\n  key   2 1.5 4   0 1 0   35\nsay ann"), std::string::npos) << more;
	EXPECT_EQ(fe::setShotView(text, 2, key, true, why), text);
	EXPECT_NE(why.find("not keys"), std::string::npos);
	// a shot's start moved; not the first of a part, not past its neighbours
	const std::string moved = fe::moveShotStart(text, 1, 5.5, why);
	EXPECT_EQ(why, "");
	EXPECT_NE(moved.find("shot 5.5 Talk\n"), std::string::npos);
	EXPECT_EQ(fe::moveShotStart(text, 2, 1.0, why), text);
	EXPECT_NE(why.find("starts at 0"), std::string::npos);
	EXPECT_EQ(fe::moveShotStart(text, 3, 0.01, why), text);
	EXPECT_NE(why.find("after the one before"), std::string::npos);
	// Windows line ends stay
	std::string crlf;
	for (char c : text) crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
	const std::string movedCrlf = fe::moveShotStart(crlf, 1, 5.5, why);
	EXPECT_NE(movedCrlf.find("shot 5.5 Talk\r\n"), std::string::npos);
	EXPECT_EQ(movedCrlf.find("\r\r"), std::string::npos);
	// the changed file is still the film, with the change
	const std::filesystem::path d = tempDir("edits");
	write(d / "s.pbrt", kScene);
	write(d / "e.film", fe::moveShotStart(fe::setShotView(text, 0, key, false, why), 1, 5.5, why));
	ff::DataFilm one;
	ASSERT_EQ(one.load((d / "e.film").string(), 0), "");
	EXPECT_NE(one.headerAt(1.0).find("LookAt 2 1.5 4"), std::string::npos) << one.headerAt(1.0);
	EXPECT_DOUBLE_EQ(one.spec.shots[1].start, 5.5);
}

// A rigged glTF character (gltf_skin.h): its skeleton, skin and clips read, its vertices moved by their joints - the library's robot raises its right arm in its wave, swings
// its legs in its walk, half as far at half the weight - and as a kind of a film file (film_rigged.h: `rigged`, or a cast that names its model).
TEST(RiggedCharacters, ARiggedGltfModelIsPosedByItsClipsAndIsAKindOfCharacter) {
	(void)cartoon::films();
	const std::string file = cartoon::film_cache::asset("library/robot.glb");
	if (file.empty()) GTEST_SKIP() << "run where the scenes are";
	std::ifstream in(file, std::ios::binary);
	const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	const gltf_skin::Result r = gltf_skin::load(bytes);
	ASSERT_TRUE(r.ok) << r.error;
	const gltf_skin::Rig &rig = r.rig;
	ASSERT_EQ(rig.skins.size(), 1u);
	EXPECT_EQ(rig.skins[0].joints.size(), 7u);
	EXPECT_EQ(rig.clips.size(), 5u);
	for (const char *name : {"walk", "idle", "wave", "cheer", "nod"}) EXPECT_GE(rig.clipNamed(name), 0) << name;
	const auto box = [&](const gltf_skin::Pose &pose, double lo[3], double hi[3]) { gltf_skin::boundsOf(gltf_skin::posed(rig, pose), lo, hi); };
	double lo[3], hi[3];
	box(gltf_skin::restPose(rig), lo, hi);
	EXPECT_NEAR(lo[1], 0.0, 1e-6) << "its feet on the ground";
	EXPECT_NEAR(hi[1], 1.86, 1e-3) << "the top of its antenna";
	EXPECT_NEAR(lo[0], -0.345, 1e-3) << "its right arm hangs (its right is -x)";
	// the wave: the right arm up and out (150 degrees about z at 0.4 s)
	gltf_skin::Pose wave = gltf_skin::restPose(rig);
	gltf_skin::apply(rig, rig.clips[static_cast<std::size_t>(rig.clipNamed("wave"))], 0.4, wave, 1.0, false);
	box(wave, lo, hi);
	EXPECT_NEAR(lo[0], -0.558, 0.005) << "the arm swung out: its far corner (0.015, -0.55) from the shoulder turned by -150 degrees";
	EXPECT_GT(hi[1], 1.88) << "and above the antenna";
	// half the weight: half way (the arm out by about 75 degrees)
	gltf_skin::Pose half = gltf_skin::restPose(rig);
	gltf_skin::apply(rig, rig.clips[static_cast<std::size_t>(rig.clipNamed("wave"))], 0.4, half, 0.5, false);
	box(half, lo, hi);
	EXPECT_LT(lo[0], -0.6) << "75 degrees out reaches further to the side than 150";
	// the walk: the legs swing forward and back
	gltf_skin::Pose walk = gltf_skin::restPose(rig);
	gltf_skin::apply(rig, rig.clips[static_cast<std::size_t>(rig.clipNamed("walk"))], 0.25, walk);
	box(walk, lo, hi);
	EXPECT_GT(hi[2] - lo[2], 0.5) << "a stride";
	// as a kind of character: from the library's cast (its model named there), walking and waving
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 8\ncast robo as robot at 0 0 0 facing 0\nwalk robo 1 0.6  0 3\ngesture robo \"wave\" 5 5.4 6.6 7\nshot 0 A\n key 0 1 5  0 1 0\n", film), "");
	const auto &kinds = ff::characterKinds();
	const std::string robot = film.spec.characters[0].kind;   // (the film's own kind: "<film>/robot", not shared with another film's robot)
	EXPECT_EQ(robot, film.spec.key + "/robot");
	ASSERT_TRUE(kinds.count(robot));
	EXPECT_FALSE(kinds.count("robot"));
	const ff::CharacterKind &kind = kinds.at(robot);
	EXPECT_NEAR(kind.height, 1.2, 1e-9) << "sized by its cast (height 1.2)";
	EXPECT_EQ(kind.poses, (std::vector<std::string>{"wave", "cheer", "nod"})) << "its clips but walk and idle, as the file has them";
	const std::string standing = film.sceneAt(0.5), walking = film.sceneAt(2.0), waving = film.sceneAt(5.8);
	EXPECT_NE(standing.find("# robo (" + robot + ")"), std::string::npos);
	EXPECT_NE(standing.find("\"trianglemesh\""), std::string::npos);
	EXPECT_NE(standing, film.sceneAt(1.2)) << "it moves when it walks";
	EXPECT_NE(walking.find("conductor"), std::string::npos) << "its metal limbs";
	EXPECT_NE(waving, film.sceneAt(4.5));
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 8\ncast robo as robot\ngesture robo \"juggle\" 1 2\n", bad).find("\"wave\""), std::string::npos) << "its poses are its clips";
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 8\nrigged bot nowhere.glb\n", bad).find("cannot be read"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 8\nrigged bot " + file + " walk moonwalk\n", bad).find("no clip called \"moonwalk\""), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 8\nrigged person " + file + "\n", bad).find("is a kind of character already"), std::string::npos) << "it would have replaced every film's person";
}

// A # inside quoted text is the text's, not a comment (`say kai 3 "Room #4"` lost the rest of its line); a colour like #ff8800 is a colour; a # after a space is a comment.
TEST(FilmFile, AHashInsideQuotesIsTheTextsOwn) {
	EXPECT_EQ(ff::detail::withoutComment("say kai 3 \"Room #4, please\" rate 1.1  # a note"), "say kai 3 \"Room #4, please\" rate 1.1  ");
	EXPECT_EQ(ff::detail::withoutComment("title \"#1\" 0 2"), "title \"#1\" 0 2");
	EXPECT_EQ(ff::detail::withoutComment("walk kai 1 1 0 -2 # then"), "walk kai 1 1 0 -2 ");
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
	// its events: a footfall about every 0.84 m from half a step in, the last as it stops (6 m: 8 of them), and the touch
	int steps = 0, touches = 0;
	for (const cartoon::FilmEvent &e : film.events()) {
		steps += e.kind == cartoon::EventKind::Footfall;
		touches += e.kind == cartoon::EventKind::Touch;
	}
	EXPECT_EQ(steps, 8);
	EXPECT_EQ(touches, 1);
}

// A walk ends as a foot comes down: the walker stands on both feet, not mid-stride with one in the air (a 5 m walk was 8.33 strides); it turns to its way as it sets
// off rather than in one frame; and a walk that starts before the one before it has ended is refused.
TEST(FilmFile, AWalkEndsOnBothFeetTurnsAsItSetsOffAndWalksDoNotOverlap) {
	(void)cartoon::films();
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("scene s.pbrt\nlength 12\ncharacter ann person at 0 0 0 facing 90\nwalk ann 1 1.0 0 -5\nshot 0 A\n  key 0 1.6 6  0 1 0\n", film), "");
	for (double t : {8.0, 10.0}) {
		const std::vector<cartoon::Foot> feet = film.feetAt(t);
		ASSERT_EQ(feet.size(), 2u);
		EXPECT_TRUE(feet[0].planted && feet[1].planted) << "both feet down at " << t << " s";
		EXPECT_LT(std::abs(feet[0].at.y - feet[1].at.y), 0.01);
	}
	const ff::Character &ann = film.spec.characters[0];
	double before = film.momentOf(ann, 0.98).frame[0], worst = 0.0;
	for (double t = 1.0; t < 2.0; t += 1.0 / 48.0) {   // (the frame's turn: its first column, frame to frame)
		const double now = film.momentOf(ann, t).frame[0];
		worst = std::max(worst, std::abs(now - before));
		before = now;
	}
	EXPECT_LT(worst, 0.25) << "it faced +x and walks towards -z: a quarter turn, eased";
	ff::DataFilm overlap;
	const std::string problem = loadFilm("scene s.pbrt\nlength 12\ncharacter ann person at 0 0 0\nwalk ann 1 1.0 0 -8\nwalk ann 4 1.0 4 -8\nshot 0 A\n  key 0 1.6 6  0 1 0\n", overlap);
	EXPECT_NE(problem.find("goes on until"), std::string::npos) << problem;
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
		// a footfall each stride from a step in (the stride fitted to the way: it ends as a foot comes down)
		const double stride = kind.stride * k.scale, frac = kind.firstStep - std::floor(kind.firstStep);
		const double fitted = 5.0 / std::max(frac > 0.0 ? frac : 1.0, std::round(5.0 / stride - frac) + frac);
		EXPECT_NEAR(fitted, stride, 0.5 * stride);
		int steps = 0;
		for (const cartoon::FilmEvent &e : film.events()) steps += e.kind == cartoon::EventKind::Footfall;
		EXPECT_EQ(steps, static_cast<int>(std::floor(5.0 / fitted - kind.firstStep + 1.0 + 1e-9)));
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
