// film_story_tests.cpp - films made from files, split from film_file_tests.cpp (which keeps the grammar, the scene, the animation, the registry, parts, the library,
// titles and characters' blocking): what characters say, feel and handle, their walks, looks, moves, idle life, lip sync and crowds, the coverage and camera moves,
// transitions, the set's dressing and weather, a film from a screenplay, its music, the film timeline and the film cache.

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
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 10\nweather hail\n", bad).find("rain, snow, leaves or dust"), std::string::npos);
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
	const std::vector<std::int16_t> pcm = a.finish(0.0, 0.0, 0.89, 0.0), whole = full.finish(0.0, 0.0, 0.89, 0.0);   // (as made: two mixes compared)
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

// The film cache keeps, of the other programs' stamp folders, those used within a day and the two used last; older ones go, and every one over a week old (every rebuild made
// a folder: one film had 146, 22 GB, in four days).
TEST(FilmCache, OldStampFoldersArePrunedButTheTwoUsedLastAreKept) {
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("photonsmith-test-prune-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	const auto now = fs::file_time_type::clock::now();
	const std::vector<std::pair<std::string, int>> folders = {{"build_a", 0}, {"build_b", 30}, {"build_c", 48}, {"build_d", 60}, {"build_e", 72}, {"build_f", 200}};   // (hours ago)
	for (const auto &[name, hours] : folders) {
		fs::create_directories(dir / name);
		fs::last_write_time(dir / name, now - std::chrono::hours(hours));
	}
	cartoon::film_cache::prune(dir);
	std::set<std::string> left;
	for (const auto &e : fs::directory_iterator(dir)) left.insert(e.path().filename().string());
	EXPECT_EQ(left, (std::set<std::string>{"build_a", "build_b"})) << "the newest two (and anything within a day); not the week-old one";
	std::error_code ec;
	fs::remove_all(dir, ec);
}

// Numbers a film file gives are kept in range, and a prop is given only to a free hand: a `detail` finer than 4 mm (billions of sculpting samples), a time of day past
// 24:00, a fade out longer than the film, and a give to a hand that already holds something are refused; a screenplay's title makes its own key.
TEST(FilmFile, OutOfRangeNumbersAndAFullHandAreRefused) {
	(void)cartoon::films();
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 8\ncharacter kai person at 0 0 0 detail 0.0005\n", bad).find("`detail`"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 8\nsky 18:00 24:59\n", bad).find("times of day"), std::string::npos);
	EXPECT_NE(loadFilm("scene s.pbrt\nlength 8\nfadeout 9\n", bad).find("longer than the film"), std::string::npos);
	const std::string two = "scene s.pbrt\nlength 14\ncharacter ann person at 0 0 0 facing 0\ncharacter pip pip at 0 0 1.35 facing 180\nprop fan at 0.25 0.95 0.35\nprop lamp at 0.1 0.4 1.5\n";
	const std::string problem = loadFilm(two + "pick pip lamp 1\npick ann fan 3\ngive ann fan 6 to pip\nshot 0 A\n  key 0 1.6 5  0 1 0\n", bad);
	EXPECT_NE(problem.find("already holds lamp"), std::string::npos) << problem;
	cartoon::fountain::Script s;
	s.title = "Dr. No";
	EXPECT_NE(cartoon::fountain::filmFile(s, "s.pbrt", "x").find("film    dr-no\n"), std::string::npos) << "not just \"dr\" (a path's stem)";
}
