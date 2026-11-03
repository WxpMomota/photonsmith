// Rooms and houses in the films, and what happens in them (indoor round 1: film_rooms.h, film_furniture.h, film_roomlook.h, film_roomlife.h, film_parse_rooms.h,
// film_roomworld.cpp).
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "../../src/shared/cartoon/cartoon_films.h"
#include "../../src/shared/cartoon/film_parse_rooms.h"
#include "../../src/shared/cartoon/film_roomlife.h"
#include "../../src/shared/cartoon/film_roomlook.h"
#include "../../src/shared/cartoon/sfx.h"

namespace ff = cartoon::film_file;
namespace rm = cartoon::rooms;

namespace {

const char *kSet = R"(
LookAt 0 1.6 3  0 1 0  0 1 0
Camera "perspective" "float fov" [ 50 ]
Film "rgb" "integer xresolution" [ 320 ] "integer yresolution" [ 180 ]
Sampler "halton" "integer pixelsamples" [ 16 ]
WorldBegin
LightSource "infinite" "rgb L" [ 0.4 0.5 0.6 ]
)";

std::filesystem::path filmOf(const std::string &name, const std::string &body) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("rt_rooms_" + name);
	std::filesystem::create_directories(dir);
	std::ofstream(dir / "set.pbrt") << kSet;
	std::ofstream(dir / (name + ".film")) << "film " << name << "\nname Test\nabout A test\nscene set.pbrt\nlength 20\nstill 2\n" << body;
	return dir / (name + ".film");
}
std::size_t count(const std::string &s, const std::string &what) {
	std::size_t n = 0;
	for (std::size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + what.size())) ++n;
	return n;
}
const rm::Item *itemNamed(const rm::Room &r, const std::string &kind) {
	for (const rm::Item &it : r.items)
		if (it.kind == kind) return &it;
	return nullptr;
}
// `cast NAME as pip` just inside a room's front door (a room on its own, at x z: its door is a quarter along its front wall, and nothing stands in front of it).
std::string castAtDoor(const std::string &name, const std::string &kind, double x, double z) {
	rm::Room r;
	r.kind = kind, r.x = x, r.z = z;
	rm::sizeOf(kind, r.w, r.d, r.h);
	const cartoon::Vec3 p = rm::toPbrt(r, rm::wallPoint(r, 0, 0.25) + cartoon::Vec3{0.0, 0.0, 0.7});
	char buf[160];
	std::snprintf(buf, sizeof buf, "cast %s as pip at %.3f 0 %.3f\n", name.c_str(), p.x, p.z);
	return buf;
}
const std::string kShot ="shot 0 a\n  key 0 1.6 2  0 1 -1  50\n";

}  // namespace

TEST(FilmRoomsLines, EachKindIsFurnishedAndProblemsAreSaid) {
	for (const std::string &kind : rm::kinds()) {
		ff::DataFilm film;
		ASSERT_EQ(film.load(filmOf("kind-" + kind, "room " + kind + " r at 0 0\n" + kShot).string()), "") << kind;
		ASSERT_EQ(film.spec.indoors.rooms.size(), 1u);
		const rm::Room &r = film.spec.indoors.rooms[0];
		EXPECT_GE(r.items.size(), 2u) << kind << " has its furniture";
		EXPECT_FALSE(r.lamps.empty()) << kind << " has a light";
		bool door = false, window = false;
		for (const rm::Opening &o : r.openings) door |= o.door, window |= !o.door;
		EXPECT_TRUE(door && window) << kind;
		for (const rm::Item &it : r.items)   // (all of it inside the walls)
			if (!it.flat) {
				const rm::detail::Rect f = rm::detail::footprint(it);
				EXPECT_GE(f.x0, -0.5 * r.w) << kind << " " << it.kind;
				EXPECT_LE(f.x1, 0.5 * r.w) << kind << " " << it.kind;
				EXPECT_GE(f.z0, -0.5 * r.d) << kind << " " << it.kind;
				EXPECT_LE(f.z1, 0.5 * r.d) << kind << " " << it.kind;
			}
	}
	ff::DataFilm a;
	EXPECT_NE(a.load(filmOf("bad1", "room garage g at 0 0\n" + kShot).string()).find("KIND: kitchen"), std::string::npos);
	ff::DataFilm b;
	EXPECT_NE(b.load(filmOf("bad2", "room kitchen k at 0 0\nsit ada on sofa in k at 2\n" + kShot).string()), "") << "no one called ada, no sofa in a kitchen";
	ff::DataFilm c;
	EXPECT_NE(c.load(filmOf("bad3", "room kitchen k at 0 0\nwalls nowhere paint 1 0 0\n" + kShot).string()), "") << "no such room";
	ff::DataFilm d;
	EXPECT_NE(d.load(filmOf("bad4", "room living k at 0 0\nshot 0 a\n  peek k\n  follow ada\n").string()), "") << "a peek and a follow in one shot";
}

TEST(FilmRoomsHouses, RoomsInARowWithDoorwaysAndStairs) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("house", "house home at 0 0 rooms kitchen living upstairs bedroom\n" + kShot).string()), "");
	const auto &rs = film.spec.indoors.rooms;
	ASSERT_EQ(rs.size(), 3u);
	int between = 0;
	for (const rm::Room &r : rs)
		for (const rm::Opening &o : r.openings) between += o.door && !o.to.empty();
	EXPECT_GE(between, 2) << "the kitchen and the living room open into each other";
	bool stairs = false, hole = false;
	for (const rm::Room &r : rs) stairs |= r.stairs, hole |= r.ceilingHole || r.floorHole;
	EXPECT_TRUE(stairs && hole) << "a way up";
	EXPECT_GT(rs[2].y, rs[0].y + 2.0) << "the bedroom is upstairs";
	EXPECT_EQ(rs[2].x, rs[0].x) << "over the room under it";
	EXPECT_EQ(rs[2].z, rs[0].z);
	// the stairs against the first room's left wall, the hole over them in its ceiling and the bedroom's floor (they were once set from a room the upstairs had moved)
	EXPECT_NEAR(rs[0].holeX0, -0.5 * rs[0].w + rm::kWall, 1e-9);
	EXPECT_EQ(rs[0].holeX0, rs[2].holeX0);
	EXPECT_NEAR(rs[0].holeZ1, 0.5 * rs[0].d - rm::kWall, 1e-9);
	for (const rm::Room &r : rs) {   // (every room has a window: one under the stairs too)
		int windows = 0;
		for (const rm::Opening &o : r.openings) windows += !o.door;
		EXPECT_GE(windows, 1) << r.name;
	}
	// the house is in what the way-finding reads: a walk from one room to the next goes through the doorway
	const rm::Room &k = rs[0], &l = rs[1];
	const cartoon::Vec3 from = ff::detail::clearSpot(k, rm::toPbrt(k, {0.0, 0.0, 0.0})), to = ff::detail::clearSpot(l, rm::toPbrt(l, {0.0, 0.0, 0.0}));
	ff::DataFilm walker;
	char line[256];
	std::snprintf(line, sizeof line, "cast ada as pip at %.3f 0 %.3f\nwalk ada 1 0.9 to %.3f %.3f\n", from.x, from.z, to.x, to.z);
	ASSERT_EQ(walker.load(filmOf("walk", "house home at 0 0 rooms kitchen living upstairs bedroom\n" + std::string(line) + kShot).string()), "");
	const ff::WalkLine &w = walker.spec.characters[0].walks[0];
	ASSERT_GE(w.points.size(), 2u);
	bool throughDoor = false;
	for (const rm::Opening &o : k.openings) {
		if (!o.door || o.to.empty()) continue;
		const cartoon::Vec3 d = rm::toPbrt(k, rm::wallPoint(k, o.wall, o.along));
		for (const auto &p : w.points) throughDoor |= std::hypot(p.first - d.x, p.second - d.z) < 0.6 * o.width + 0.3;
		for (std::size_t i = 0; i + 1 < w.points.size(); ++i)
			for (double u = 0.0; u <= 1.0; u += 0.05)
				throughDoor |= std::hypot(w.points[i].first + u * (w.points[i + 1].first - w.points[i].first) - d.x, w.points[i].second + u * (w.points[i + 1].second - w.points[i].second) - d.z) < 0.5 * o.width + 0.1;
	}
	EXPECT_TRUE(throughDoor) << "the way goes through the doorway, not the wall";
}

TEST(FilmRoomsUse, SitOnTheSofaAndLieInBed) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("sit", "room living l at 0 0\nroom bedroom b at 8 0\n" + castAtDoor("ada", "living", 0, 0) + castAtDoor("bob", "bedroom", 8, 0) +
									  "sit ada on sofa at 1 until 12\nlie bob in bed at 1\n" + kShot).string()), "");
	const auto &uses = film.spec.indoors.uses;
	ASSERT_EQ(uses.size(), 2u);
	EXPECT_GT(uses[0].arrive, 1.0) << "it walked there";
	EXPECT_LT(uses[0].arrive, 8.0);
	const ff::Character &ada = film.spec.characters[0];
	cartoon::Vec3 at;
	const auto seated =film.momentOf(ada, uses[0].arrive + 2.0, &at);
	EXPECT_GT(seated.sit, 0.9) << "sitting";
	EXPECT_NEAR(seated.seat, uses[0].seatHeight, 0.02);
	EXPECT_LT(std::hypot(-at.x - uses[0].seat.x, at.z - uses[0].seat.z), 0.1) << "on the seat";
	EXPECT_LT(film.momentOf(ada, 16.0).sit, 0.1) << "up again";
	// lying back on the bed
	const ff::Character &bob = film.spec.characters[1];
	const auto lying = film.momentOf(bob, uses[1].arrive + 2.0);
	EXPECT_GT(lying.lie, 0.9) << "lying back (castAt tips the whole body: the actor kinds draw from a place and a heading)";
	EXPECT_GT(cartoon::transformPoint(lying.frame, {0.0, 0.0, 0.0}).y, uses[1].seatHeight) << "up on the bed";
	EXPECT_EQ(film.momentOf(bob, 0.5).lie, 0.0) << "upright before";
	// and drawn so: its body in a turn of its own
	const std::string drawn = film.castAt(uses[1].arrive + 2.0);
	EXPECT_GT(count(drawn, "ConcatTransform"), count(film.castAt(0.5), "ConcatTransform")) << "its body turned";
}

TEST(FilmRoomsUse, OpenTheFridgeAndLookOut) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("open", "room kitchen k at 0 0\n" + castAtDoor("ada", "kitchen", 0, 0) + "open ada fridge at 1\nlookout ada window 1 at 8\n" + kShot).string()), "");
	const rm::Use &u = film.spec.indoors.uses[0];
	const std::string shut = film.sceneAt(0.5), open = film.sceneAt(u.arrive + 1.2);
	EXPECT_NE(shut, open) << "the fridge's door swings";
	bool reach = false;
	for (const auto &g : film.spec.characters[0].gestures) reach |= g.pose == "reach";
	EXPECT_TRUE(reach) << "a hand to its handle";
	const rm::Use &l = film.spec.indoors.uses[1];
	const rm::Room &k = film.spec.indoors.rooms[0];
	const cartoon::Vec3 local = rm::toLocal(k, l.stand);
	EXPECT_GT(local.z, 0.5 * k.d - 1.2) << "by the window, on the back wall";
}

TEST(FilmRoomsLight, LampsAtNightSwitchesAndAPowerCut) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("lamps", "room living l at 0 0\nroom living m at 8 0\nswitch m on 2 off 6\npowercut 10 until 14\n" + kShot).string()), "");
	const rm::Indoors &in = film.spec.indoors;
	const auto ceilingOf = [](const rm::Room &r) {
		for (const rm::Lamp &l : r.lamps)
			if (l.kind == "ceiling") return &l;
		return static_cast<const rm::Lamp *>(nullptr);
	};
	const rm::Room &l = in.rooms[0], &m = in.rooms[1];
	ASSERT_TRUE(ceilingOf(l) && ceilingOf(m));
	EXPECT_LT(rm::lampOn(in, m, *ceilingOf(m), 1.0, 0.0), 0.1) << "off by day before the switch";
	EXPECT_GT(rm::lampOn(in, m, *ceilingOf(m), 4.0, 0.0), 0.9) << "switched on";
	EXPECT_LT(rm::lampOn(in, m, *ceilingOf(m), 8.0, 1.0), 0.1) << "switched off, even at night";
	EXPECT_LT(rm::lampOn(in, l, *ceilingOf(l), 4.0, 0.0), 0.1) << "off by day when no one says";
	EXPECT_GT(rm::lampOn(in, l, *ceilingOf(l), 8.0, 1.0), 0.9) << "on at night when no one says";
	EXPECT_LT(rm::lampOn(in, l, *ceilingOf(l), 12.0, 1.0), 0.1) << "the power is off";
	EXPECT_GT(rm::lampOn(in, l, *ceilingOf(l), 16.0, 1.0), 0.9) << "and back";
	EXPECT_NE(rm::lampsText(in, m, 4.0, 0.0).find("AreaLightSource"), std::string::npos);
	// a camera in the room opens up for its light (no sky: a night in): by the lamps, more by candlelight in a power cut
	EXPECT_GT(film.indoorExposure(4.0), 1.0);
	EXPECT_GT(film.indoorExposure(12.0), film.indoorExposure(4.0));
}

TEST(FilmRoomsLight, CurtainsRainAndDust) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("curt", "room bedroom b at 0 0\ncurtains b open\ncurtains b closed at 5\n" + kShot).string()), "");
	const rm::Room &r = film.spec.indoors.rooms[0];
	double closed = 0.0, blow = 0.0;
	rm::curtainsAt(r, 1.0, closed, blow);
	EXPECT_LT(closed, 0.1);
	rm::curtainsAt(r, 9.0, closed, blow);
	EXPECT_GT(closed, 0.9);
	EXPECT_NE(rm::curtainsText(r, 1.0), rm::curtainsText(r, 9.0));
	EXPECT_NE(rm::rainOnGlassText(r, 2.0, 1.0), "") << "drops on the panes";
	EXPECT_EQ(rm::rainOnGlassText(r, 2.0, 0.0), "");
	EXPECT_NE(rm::motesText(r, 2.0, cartoon::normalize(cartoon::Vec3{0.0, 0.5, 1.0})), "") << "dust in the sun through the window";
}

TEST(FilmRoomsClutter, MessyHasMoreAndAMessGrows) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("mess", "room bedroom a at 0 0\nroom bedroom b at 8 0\nroom bedroom c at 16 0\nclutter a tidy\nclutter b messy\nclutter c messy grows 2 10\n" + kShot).string()), "");
	const auto &rs = film.spec.indoors.rooms;
	EXPECT_LT(rm::clutterOf(rs[0]).size(), rm::clutterOf(rs[1]).size());
	std::set<std::string> w0, w1;
	EXPECT_LT(rm::clutterText(rs[2], rm::clutterOf(rs[2]), 1.0, w0).size(), rm::clutterText(rs[2], rm::clutterOf(rs[2]), 12.0, w1).size()) << "the mess grows";
}

TEST(FilmRoomsLook, WallpaperFloorsAndStyles) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("look", "room living l at 0 0 style old\nwalls l wallpaper roses colour 0.8 0.7 0.6\nfloor l checks\n" + kShot).string()), "");
	const rm::Room &r = film.spec.indoors.rooms[0];
	EXPECT_EQ(r.look.paper, "roses");
	EXPECT_EQ(r.look.floor, "checks");
	EXPECT_NE(rm::materialOf("look", r, "wall0", "t").find("imagemap"), std::string::npos) << "a pictured wallpaper";
	EXPECT_NE(rm::materialOf("look", r, "floor", "t").find("imagemap"), std::string::npos);
	for (const std::string &p : rm::papers()) {
		rm::Room q = r;
		q.look.paper = p;
		EXPECT_NE(rm::materialOf("look", q, "wall1", "t"), "") << p;
	}
	// footsteps on its floor: tiles here, carpet on its rug
	const cartoon::Vec3 middle = rm::toPbrt(r, {0.0, 0.0, 0.0});
	const int s = film.floorSurfaceAt({-middle.x, 0.0, middle.z});
	EXPECT_TRUE(s == static_cast<int>(cartoon::sfx::Surface::Tile) || s == static_cast<int>(cartoon::sfx::Surface::Carpet)) << s;
	EXPECT_EQ(film.floorSurfaceAt({-50.0, 0.0, 50.0}), -1) << "outside";
	EXPECT_GT(film.roomSendAt({-middle.x, 1.0, middle.z}), 0.0);
}

TEST(FilmRoomsCamera, CutawayPeekAndKeptIndoors) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("cam", "room living l at 0 0\ncast ada as pip at 0 0 0\nwalk ada 1 0.8 to 1 1\n"
									  "shot 0 a\n  key 0 4 -8  0 1 0  40\n  cutaway\nshot 5 b\n  peek l\nshot 10 c\n  follow ada\n").string()), "");
	const std::string whole = film.roomsAt(film.headerAt(12.0), 12.0, 12.0), cut = film.roomsAt(film.headerAt(2.0), 2.0, 2.0);
	EXPECT_LT(count(cut, "_wall"), count(whole, "_wall")) << "a dolls' house: the near wall gone";
	const rm::Room &r = film.spec.indoors.rooms[0];
	cartoon::Vec3 eye, target;
	double fov = 0.0;
	ASSERT_TRUE(film_subjects::cameraOf(film.headerAt(7.0), eye, target, fov));
	EXPECT_FALSE(rm::inside(r, {-eye.x, eye.y, eye.z})) << "looking in from outside";
	EXPECT_TRUE(rm::inside(r, {-target.x, target.y, target.z})) << "at the room";
	ASSERT_TRUE(film_subjects::cameraOf(film.headerAt(14.0), eye, target, fov));
	EXPECT_TRUE(rm::inside(r, {-eye.x, eye.y, eye.z})) << "a following camera stays in the room";
}

TEST(FilmRoomsSound, ARoomHumsTicksAndCreaks) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("snd", "house home at 0 0 rooms kitchen living\ndoor home.kitchen to home.living open 2 close 6\n" + kShot).string()), "") << "(the names a house gives)";
	cartoon::audio::Mix m(10.0);
	film.roomSounds(m);
	double e = 0.0;
	for (const auto v : m.finish()) e += static_cast<double>(v) * v;
	EXPECT_GT(e, 0.0);
}
