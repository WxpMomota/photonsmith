// film_file_tests.cpp - films made from files (src/shared/cartoon/film_file.h): the film file's grammar and its error messages, the cutting of a pbrt scene into plain text and animated
// blocks, what each animation line does to the scene text at a given time, and the registry that puts such films beside the built-in ones.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

#include "../../src/shared/cartoon/cartoons.h"

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
