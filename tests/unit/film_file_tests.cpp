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
