// What is done at home in the films (indoor round 2: film_parse_home.h, film_homelife.h, film_homefun.h, film_pets.h, film_homeworld.cpp, film_ways.cpp): the stairs, things
// carried, cooking, bath time, the night, shadow play, pets, slapstick, parties, and a camera that trails a walker through the house.
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "../../src/shared/cartoon/cartoon_films.h"
#include "../../src/shared/cartoon/film_parse_rooms.h"

namespace ff = cartoon::film_file;
namespace rm = cartoon::rooms;
using V3 = cartoon::Vec3;

namespace {

const char *kSet = R"(
LookAt 0 1.6 3  0 1 0  0 1 0
Camera "perspective" "float fov" [ 50 ]
Film "rgb" "integer xresolution" [ 320 ] "integer yresolution" [ 180 ]
Sampler "halton" "integer pixelsamples" [ 16 ]
WorldBegin
LightSource "infinite" "rgb L" [ 0.4 0.5 0.6 ]
)";

void kinds() {
	cartoon::film_kinds::install();
	cartoon::film_kinds::pets::install();
}
std::filesystem::path filmOf(const std::string &name, const std::string &body, double length = 40.0) {
	kinds();
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("rt_home_" + name);
	std::filesystem::create_directories(dir);
	std::ofstream(dir / "set.pbrt") << kSet;
	std::ofstream(dir / (name + ".film")) << "film " << name << "\nname Test\nabout A test\nscene set.pbrt\nlength " << length << "\nstill 2\n" << body;
	return dir / (name + ".film");
}
std::size_t count(const std::string &s, const std::string &what) {
	std::size_t n = 0;
	for (std::size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + what.size())) ++n;
	return n;
}
// A film's rooms as its lines lay them out (to put characters in them before the real film is read).
std::vector<rm::Room> roomsOf(const std::string &lines) {
	ff::DataFilm f;
	EXPECT_EQ(f.load(filmOf("layout", lines + "shot 0 a\n  key 0 1.6 2  0 1 -1  50\n").string()), "");
	return f.spec.indoors.rooms;
}
// `cast NAME as KIND` at a clear place in room r (near its local x z).
std::string castIn(const std::string &name, const std::string &kind, const rm::Room &r, double x = 0.0, double z = 0.0) {
	const V3 p = rm::clearSpot(r, rm::toPbrt(r, {x, 0.0, z}));
	char buf[200];
	std::snprintf(buf, sizeof buf, "character %s %s at %.3f %.3f %.3f\n", name.c_str(), kind.c_str(), p.x, p.y, p.z);
	return buf;
}
const std::string kShot = "shot 0 a\n  key 0 1.6 2  0 1 -1  50\n";
const std::string kHouse = "house home at 0 0 rooms living kitchen upstairs bedroom\n";

}  // namespace

TEST(FilmHomeStairs, AWalkGoesUpTheStairsAndDownAgain) {
	const std::vector<rm::Room> rs = roomsOf(kHouse);
	ASSERT_EQ(rs.size(), 3u);
	const rm::Room &up = rs[2];
	for (const rm::Item &it : up.items) {   // (round 1 stood the bed over the stairs' hole)
		if (it.flat) continue;
		const rm::detail::Rect f = rm::detail::footprint(it);
		EXPECT_FALSE(f.x0 < up.holeX1 && up.holeX0 < f.x1 && f.z0 < up.holeZ1 && up.holeZ0 < f.z1) << it.kind << " over the stairs";
	}
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("stairs", kHouse + castIn("ada", "person", rs[1], 0.5, 0.0) + "go ada to home.bedroom at 1 speed 1.2\ngo ada to home.kitchen at 25 speed 1.2\n" + kShot).string()), "");
	const ff::Character &ada = film.spec.characters[0];
	ASSERT_GE(ada.walks.size(), 2u);
	double top = -1.0;
	for (const double h : ada.walks[0].heights) top = std::max(top, h);
	EXPECT_NEAR(top, up.y, 0.05) << "up to the floor above";
	EXPECT_NEAR(ada.walks[0].heights.back(), up.y, 0.05) << "and it ends there";
	int climbing = 0;   // (a tread at a time)
	for (std::size_t i = 1; i < ada.walks[0].heights.size(); ++i) climbing += ada.walks[0].heights[i] > ada.walks[0].heights[i - 1] + 0.1;
	EXPECT_GE(climbing, 10);
	V3 at;
	film.momentOf(ada, 24.0, &at);
	EXPECT_EQ(rm::roomAt(film.spec.indoors, {-at.x, at.y, at.z}), 2) << "in the bedroom";
	film.momentOf(ada, 39.0, &at);
	EXPECT_EQ(rm::roomAt(film.spec.indoors, {-at.x, at.y, at.z}), 1) << "down again, in the kitchen";
	// a room up in the air with no stairs to it is said
	ff::DataFilm none;
	EXPECT_NE(none.load(filmOf("nostairs", "room living a at 0 0\nroom bedroom b at 8 0 y 3\n" + castIn("ada", "person", rs[1]) + "go ada to b at 1\n" + kShot).string()).find("no stairs"), std::string::npos);
}

TEST(FilmHomeThings, CarryAMugFromTheCounterToTheTable) {
	const std::vector<rm::Room> rs = roomsOf("room kitchen k at 0 0\n");
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("carry", "room kitchen k at 0 0\n" + castIn("ada", "person", rs[0], -1.0, -0.5) + "thing cup mug on counter\ncarry ada cup to table at 1\n" + kShot).string()), "");
	const rm::Carry &c = film.spec.indoors.home.carries[0];
	EXPECT_GT(c.picked, 1.0);
	EXPECT_GT(c.put, c.picked + 1.0);
	const V3 start = film.propAt("cup", 0.5), held = film.propAt("cup", 0.5 * (c.picked + c.put)), after = film.propAt("cup", c.put + 2.0);
	EXPECT_LT(cartoon::length(start - ff::DataFilm::story(film.spec.props[0].home.x, film.spec.props[0].home.y, film.spec.props[0].home.z)), 1e-6) << "on the counter";
	EXPECT_GT(cartoon::length(held - start), 0.2) << "in its hand";
	EXPECT_LT(cartoon::length(after - ff::DataFilm::story(c.putAt.x, c.putAt.y, c.putAt.z)), 1e-6) << "on the table";
	EXPECT_NE(film.homeAt(film.headerAt(0.5), 0.5, 0.5), "") << "drawn";
	ff::DataFilm bad;
	EXPECT_NE(bad.load(filmOf("carry2", "room kitchen k at 0 0\n" + castIn("ada", "person", rs[0]) + "thing cup teapot on counter\n" + kShot).string()), "") << "no such kind of thing";
}

TEST(FilmHomeCooking, PotKettleToastOvenAndMeal) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("cook", "room kitchen k at 0 0\ncook k pot at 1 until 20\ncook k kettle at 2\ncook k toast at 3\ncook k oven at 4 until 10\ncook k meal at 5 until 30\n" + kShot).string()), "");
	const rm::Room &r = film.spec.indoors.rooms[0];
	const auto &d = film.spec.indoors.home.doings;
	ASSERT_EQ(d.size(), 5u);
	EXPECT_EQ(rm::cookingText(r, d[0], 0.5), "") << "not yet";
	EXPECT_GT(count(rm::cookingText(r, d[0], 8.0), "AreaLightSource"), 4u) << "its flame";
	EXPECT_GT(rm::cookingText(r, d[0], 8.0).size(), rm::cookingText(r, d[0], 2.0).size()) << "steam once it boils";
	EXPECT_GT(rm::cookingText(r, d[1], 9.0).size(), rm::cookingText(r, d[1], 4.0).size()) << "the kettle's jet of steam";
	EXPECT_NE(rm::cookingText(r, d[2], 4.0), rm::cookingText(r, d[2], 8.6)) << "the toast pops up";
	EXPECT_NE(rm::cookingText(r, d[3], 6.0).find("AreaLightSource"), std::string::npos) << "the oven's window glows";
	EXPECT_EQ(rm::cookingText(r, d[3], 12.0), "");
	EXPECT_GT(rm::cookingText(r, d[4], 6.0).size(), rm::cookingText(r, d[4], 31.0).size()) << "eaten up";
	ff::DataFilm bad;
	EXPECT_NE(bad.load(filmOf("cook2", "room bedroom b at 0 0\ncook b pot at 1\n" + kShot).string()).find("no stove"), std::string::npos);
}

TEST(FilmHomeBath, FillsFogsTheMirrorAndLeavesPrints) {
	const std::string lines = "house h at 0 0 rooms bathroom living\n";
	const std::vector<rm::Room> rs = roomsOf(lines);
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("bath", lines + castIn("ada", "person", rs[0], 0.4, -0.6) + "bath h.bathroom at 2 until 12\nshower h.bathroom at 14 until 24\ngo ada to h.living at 26\n" + kShot).string()), "");
	const rm::Room &r = film.spec.indoors.rooms[0];
	const auto &d = film.spec.indoors.home.doings;
	EXPECT_LT(rm::bathLevel(d, 0, 1.0), 0.01);
	EXPECT_GT(rm::bathLevel(d, 0, 11.5), 0.95) << "full";
	EXPECT_LT(rm::bathLevel(d, 0, 22.0), 0.01) << "let out";
	bool mirror = false, dry = false;
	for (const rm::Item &it : r.items) mirror |= it.kind == "mirror", dry |= it.kind == "bath" && !it.water;
	EXPECT_TRUE(mirror) << "over the basin";
	EXPECT_TRUE(dry) << "the bath's own water left out: it is filled";
	EXPECT_GT(rm::steamOf(d, 0, 24.0), rm::steamOf(d, 0, 1.0) + 0.25) << "steam builds";
	EXPECT_NE(rm::materialOf("bath", r, "mirror", "t", 0.8), rm::materialOf("bath", r, "mirror", "t", 0.0)) << "the mirror fogs";
	EXPECT_NE(rm::bathText(r, d, 0, 20.0).find("dielectric"), std::string::npos) << "the shower's spray";
	EXPECT_EQ(count(film.homeAt(film.headerAt(25.5), 25.5, 25.5), "dielectric"), 0u) << "nothing wet out before it walks";
	EXPECT_GT(count(film.homeAt(film.headerAt(29.0), 29.0, 29.0), "dielectric"), 0u) << "wet footprints";
	EXPECT_GT(film.roomSendAt({-rm::toPbrt(r, {0.0, 1.0, 0.0}).x, 1.0, rm::toPbrt(r, {0.0, 1.0, 0.0}).z}), 0.3) << "a tiled bathroom rings";
}

TEST(FilmHomeNight, TorchFlickerLightningAndShadowPlay) {
	const std::vector<rm::Room> rs = roomsOf("room living l at 0 0\n");
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("night", "room living l at 0 0\n" + castIn("ada", "person", rs[0]) + "torch ada at 2 until 8\nflicker l at 2 until 8\nshadowplay ada animal rabbit at 10 until 16\nlightning 6 during 18 30\n" + kShot).string()), "");
	const ff::Character &ada = film.spec.characters[0];
	EXPECT_GT(film.momentOf(ada, 5.0).hands[1].weight, 0.9) << "the torch held up";
	EXPECT_NE(film.homeAt(film.headerAt(5.0), 5.0, 5.0).find("\"spot\""), std::string::npos) << "its beam";
	const rm::Indoors &in = film.spec.indoors;
	const rm::Room &r = in.rooms[0];
	double lo = 1.0, hi = 0.0;
	for (double t = 2.0; t < 8.0; t += 0.05) {
		const double on = rm::lampOn(in, r, r.lamps[0], t, 1.0);
		lo = std::min(lo, on), hi = std::max(hi, on);
	}
	EXPECT_LT(lo, 0.3) << "nearly out now and then";
	EXPECT_GT(hi, 0.6);
	EXPECT_LT(rm::lampOn(in, r, r.lamps[0], 13.0, 1.0), 0.2) << "turned down for shadow play";
	const std::string play = film.homeAt(film.headerAt(13.0), 13.0, 13.0);
	EXPECT_NE(play.find("\"point\""), std::string::npos) << "the bulb behind it";
	EXPECT_GT(film.momentOf(ada, 13.0).hands[0].weight, 0.9) << "its hands making the rabbit";
	double flashes = 0.0;
	for (double t = 18.0; t < 30.0; t += 1.0 / 24.0) flashes += film.homeAt(film.headerAt(t), t, t).find("coneangle\" [ 60") != std::string::npos;
	EXPECT_GT(flashes, 0.0) << "lightning in at the window";
}

TEST(FilmHomePets, CatNapsDogWaitsAndHeels) {
	kinds();
	ASSERT_TRUE(ff::characterKinds().count("cat") && ff::characterKinds().count("dog"));
	const std::vector<rm::Room> rs = roomsOf("house home at 0 0 rooms living kitchen\n");
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("pets", "house home at 0 0 rooms living kitchen\n" + castIn("tom", "cat", rs[0], 1.0, -0.6) + castIn("rex", "dog", rs[0], -1.4, -1.2) + castIn("ada", "person", rs[1]) +
										  "nap tom on sofa at 1 until 30\nwait rex in home.living at 1 until 12\nheel rex ada at 14\ngo ada to home.living at 15\ndoor home.living open 8\n" + kShot).string()), "");
	const ff::Character &tom = film.spec.characters[0], &rex = film.spec.characters[1], &ada = film.spec.characters[2];
	const rm::Use *nap = nullptr, *wait = nullptr;
	for (const rm::Use &u : film.spec.indoors.uses) {
		if (u.verb == "nap") nap = &u;
		if (u.verb == "wait") wait = &u;
	}
	ASSERT_TRUE(nap && wait);
	const ff::CharacterMoment curled = film.momentOf(tom, nap->arrive + 2.0);
	EXPECT_GT(curled.lie, 0.9) << "curled up";
	EXPECT_GT(cartoon::transformPoint(curled.frame, {0.0, 0.0, 0.0}).y, 0.3) << "up on the sofa";
	EXPECT_GT(film.momentOf(rex, wait->arrive + 2.0).sit, 0.9) << "sitting by the door";
	ASSERT_GE(rex.walks.size(), 2u) << "after ada";
	V3 a, b;
	film.momentOf(ada, 39.0, &a), film.momentOf(rex, 39.0, &b);
	EXPECT_LT(std::hypot(a.x - b.x, a.z - b.z), 1.6) << "close behind her";
	EXPECT_GT(std::hypot(a.x - b.x, a.z - b.z), 0.4);
	const std::string drawn = film.castAt(nap->arrive + 2.0);
	EXPECT_NE(drawn, "");
}

// A pet at heel follows a walk along points as it follows one to a place (it was left behind: only walks with `to` places were followed).
TEST(FilmHomePets, AHeelingPetFollowsAWalkAlongPoints) {
	kinds();
	const std::vector<rm::Room> rs = roomsOf("house home at 0 0 rooms living\n");
	const V3 there = rm::clearSpot(rs[0], rm::toPbrt(rs[0], {1.2, 0.0, 0.9}));
	char walk[120];
	std::snprintf(walk, sizeof walk, "walk ada 3 0.8  %.3f %.3f\n", there.x, there.z);
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("heelpoints", "house home at 0 0 rooms living\n" + castIn("rex", "dog", rs[0], -1.2, -1.0) + castIn("ada", "person", rs[0], -0.8, -1.2) + "heel rex ada at 1\n" + walk + kShot).string()), "");
	const ff::Character &rex = film.spec.characters[0], &ada = film.spec.characters[1];
	ASSERT_GE(rex.walks.size(), 1u) << "the dog has a walk after her";
	V3 a, b;
	film.momentOf(ada, 30.0, &a), film.momentOf(rex, 30.0, &b);
	EXPECT_LT(std::hypot(a.x - b.x, a.z - b.z), 1.6) << "close behind her where she stopped";
}

TEST(FilmHomeSlapstick, KnockFallBreakSpillSlipBumpAndPile) {
	const std::vector<rm::Room> rs = roomsOf("room living l at 0 0\n");
	const rm::Room &r = rs[0];
	const V3 a = rm::clearSpot(r, rm::toPbrt(r, {-1.3, 0.0, -0.2})), b = rm::clearSpot(r, rm::toPbrt(r, {1.3, 0.0, -0.2}));
	char lines[400];
	std::snprintf(lines, sizeof lines, "character ada person at %.3f 0 %.3f\nwalk ada 3 0.9 to %.3f %.3f\n", a.x, a.z, b.x, b.z);
	ff::DataFilm way;   // (her way round the furniture, and a puddle half way along it)
	ASSERT_EQ(way.load(filmOf("slapway", "room living l at 0 0\n" + std::string(lines) + kShot).string()), "");
	const auto &pts = way.spec.characters[0].walks[0].points;
	ASSERT_GE(pts.size(), 2u);
	char puddle[120];
	std::snprintf(puddle, sizeof puddle, "puddle l %.3f %.3f at 1 size 0.5\n", pts[pts.size() / 2].first, pts[pts.size() / 2].second);
	std::strcat(lines, puddle);
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("slap", "room living l at 0 0\n" + std::string(lines) + castIn("tom", "cat", r, 0.0, 1.0) + "thing pot vase on coffeetable\nknock tom pot at 9\npile l books falls 5\n" + kShot).string()), "");
	const rm::Home &h = film.spec.indoors.home;
	const rm::Bump *slip = nullptr;
	for (const rm::Bump &k : h.bumps) slip = k.slip && k.who == "ada" ? &k : slip;
	ASSERT_TRUE(slip) << "she walks over the puddle";
	EXPECT_GT(film.momentOf(film.spec.characters[0], slip->t + 0.8).tilt, 60.0) << "onto her back";
	EXPECT_LT(film.momentOf(film.spec.characters[0], slip->t + 3.2).tilt, 1.0) << "and up again";
	ASSERT_EQ(film.fallen.size(), 1u) << "the cat knocks the vase off";
	EXPECT_TRUE(film.fallen[0].breaks);
	EXPECT_NEAR(film.fallen[0].to.y, r.y, 1e-6) << "onto the floor";
	bool spilt = false;
	for (const rm::Mishap &m : h.mishaps) spilt |= m.verb == "spill";
	EXPECT_TRUE(spilt) << "what was in it";
	const double after = film.fallen[0].t + 1.5;
	EXPECT_NE(film.homeAt(film.headerAt(after), after, after), film.homeAt(film.headerAt(film.fallen[0].t - 0.5), film.fallen[0].t - 0.5, film.fallen[0].t - 0.5)) << "in pieces";
	const rm::Mishap *pile = nullptr;
	for (const rm::Mishap &m : h.mishaps) pile = m.verb == "pile" ? &m : pile;
	ASSERT_TRUE(pile);
	EXPECT_NE(film.homeAt(film.headerAt(4.0), 4.0, 4.0), film.homeAt(film.headerAt(7.0), 7.0, 7.0)) << "the pile topples";
	// a door opened into someone standing just inside it
	const std::vector<rm::Room> hs = roomsOf("house h at 0 0 rooms living kitchen\n");
	const rm::Room &k = hs[1];
	const rm::Opening *door = nullptr;
	for (const rm::Opening &o : k.openings) door = o.door && !o.to.empty() ? &o : door;
	ASSERT_TRUE(door);
	const V3 in = rm::toPbrt(k, rm::wallPoint(k, door->wall, door->along) + rm::wallInward(door->wall) * 0.5);
	char cast[200];
	std::snprintf(cast, sizeof cast, "character bob person at %.3f 0 %.3f\ndoor h.kitchen to %s open 3\n", in.x, in.z, door->to.c_str());
	ff::DataFilm doors;
	ASSERT_EQ(doors.load(filmOf("bump", "house h at 0 0 rooms living kitchen\n" + std::string(cast) + kShot).string()), "");
	ASSERT_FALSE(doors.spec.indoors.home.bumps.empty()) << "knocked back";
	EXPECT_GT(doors.momentOf(doors.spec.characters[0], doors.spec.indoors.home.bumps[0].t + 0.12).tilt, 15.0);
}

TEST(FilmHomeParty, BalloonsCakeBlowoutConfettiGuestsAndDance) {
	const std::vector<rm::Room> rs = roomsOf("room kitchen k at 0 0\n");
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("party", "room kitchen k at 0 0\nparty k birthday\nguests k 3\n" + castIn("ada", "pip", rs[0], 0.0, -1.0) + "blowout ada at 2\nconfetti k at 9\ndance k at 14 until 24\n" + kShot).string()), "");
	const ff::Spec &s = film.spec;
	std::vector<V3> guests;
	for (const ff::Character &c : s.characters)
		if (c.name.rfind("guest-", 0) == 0) guests.push_back({c.start[0], c.start[1], c.start[2]});
	ASSERT_EQ(guests.size(), 3u);
	for (std::size_t i = 0; i < guests.size(); ++i) {
		EXPECT_EQ(rm::roomAt(s.indoors, guests[i]), 0) << "in the kitchen";
		for (std::size_t j = i + 1; j < guests.size(); ++j) EXPECT_GT(std::hypot(guests[i].x - guests[j].x, guests[i].z - guests[j].z), 0.55) << "a step apart";
	}
	const rm::Use *blow = nullptr;
	for (const rm::Use &u : s.indoors.uses) blow = u.verb == "blowout" ? &u : blow;
	ASSERT_TRUE(blow && blow->arrive > 2.0);
	const auto lights = [&](double t) { return count(film.homeAt(film.headerAt(t), t, t), "AreaLightSource"); };
	EXPECT_GT(lights(blow->arrive - 0.2), lights(blow->arrive + 4.0)) << "the candles blown out";
	EXPECT_GT(film.homeAt(film.headerAt(13.0), 13.0, 13.0).size(), film.homeAt(film.headerAt(8.5), 8.5, 8.5).size()) << "confetti lies about";
	std::size_t dancers = 0;
	for (const rm::Act &a : s.indoors.home.acts) dancers += a.verb == "dance";
	EXPECT_GE(dancers, 4u) << "everyone in the room";
	const ff::Character &g = s.characters[0];
	EXPECT_NE(film.momentOf(g, 16.1).frame, film.momentOf(g, 16.35).frame) << "bobbing and swaying";
	const rm::Room &r = s.indoors.rooms[0];
	for (const char *kind : {"christmas", "halloween", "plain"}) {
		rm::Doing p;
		p.verb = "party", p.what = kind, p.t0 = 0.0;
		EXPECT_NE(rm::partyText(r, p, {}, 1e30, 3.0), "") << kind;
	}
}

TEST(FilmHomeCamera, TrailFollowsItsWayAndBesideTheHouse) {
	const std::string lines = "house home at 0 0 rooms living kitchen\n";
	const std::vector<rm::Room> rs = roomsOf(lines);
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("trail", lines + castIn("ada", "person", rs[0], -1.0, 0.0) + "go ada to home.kitchen at 1\nshot 0 a\n  trail ada\nshot 20 b\n  trail ada\n  cutaway\n").string()), "");
	const ff::Character &ada = film.spec.characters[0];
	V3 eye, target, feet;
	double fov = 0.0;
	ASSERT_TRUE(film_subjects::cameraOf(film.headerAt(5.0), eye, target, fov));
	film.momentOf(ada, 5.0, &feet);
	EXPECT_GE(rm::roomAt(film.spec.indoors, {-eye.x, 0.5, eye.z}), 0) << "inside, along its way";
	EXPECT_NEAR(std::hypot(eye.x - feet.x, eye.z - feet.z), 1.8, 0.6) << "behind it";
	ASSERT_TRUE(film_subjects::cameraOf(film.headerAt(25.0), eye, target, fov));
	EXPECT_LT(rm::roomAt(film.spec.indoors, {-eye.x, 0.5, eye.z}), 0) << "beside the house";
	const std::string cut = film.roomsAt(film.headerAt(25.0), 25.0, 25.0), whole = film.roomsAt(film.headerAt(5.0), 5.0, 5.0);
	EXPECT_LT(count(cut, "_wall"), count(whole, "_wall")) << "its front walls left out";
}

TEST(FilmHomeSound, KitchenBathAndMishapsAreHeard) {
	// (how much of the sound is between t0 and t1: the mix is brought to one loudness, so a film with more in it is not louder overall)
	const auto share = [](const std::string &name, const std::string &lines, double t0, double t1) {
		ff::DataFilm film;
		EXPECT_EQ(film.load(filmOf(name, "house h at 0 0 rooms kitchen bathroom\n" + lines + kShot, 20.0).string()), "");
		cartoon::audio::Mix m(20.0);
		film.roomSounds(m);
		const std::vector<std::int16_t> s = m.finish(0.0, 0.0);
		double in = 0.0, all = 1e-9;
		for (std::size_t i = 0; i < s.size(); ++i) {
			const double t = static_cast<double>(i / 2) / cartoon::audio::kRate, e = static_cast<double>(s[i]) * s[i];
			all += e, in += t >= t0 && t < t1 ? e : 0.0;
		}
		return in / all;
	};
	EXPECT_GT(share("snd1", "cook h.kitchen kettle at 1\n", 7.0, 13.0), share("snd0", "", 7.0, 13.0) + 0.1) << "the kettle's whistle";
	EXPECT_GT(share("snd2", "shower h.bathroom at 1 until 10\n", 1.0, 10.0), share("snd0", "", 1.0, 10.0) + 0.1) << "the shower";
	EXPECT_GT(share("snd3", "thing m mug on table\nfall m at 3\n", 3.0, 4.5), share("snd0", "", 3.0, 4.5) + 0.05) << "a crash";
}
