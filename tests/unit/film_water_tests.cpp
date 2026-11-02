// Life on the water and by it (film_stir.h and the films' water lines).
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "../../src/shared/cartoon/cartoon_films.h"   // (the films themselves, which these tests reach into)

namespace ff = cartoon::film_file;
namespace st = cartoon::stir;

TEST(FilmWaterRings, LongWavesRunAhead) {
	// the waves further out are longer: count zero crossings of the ring in its outer and inner halves
	const st::Ring q = st::ringOf(0.0, 0.0, 0.0, 0.05, 0.012);
	const double t = 2.5;
	const double outer = st::reachOf(q, t), inner = st::insideOf(q, t);
	ASSERT_GT(outer, inner + 0.5);
	std::vector<double> crossings;
	double last = 0.0;
	for (double r = inner + 0.002; r < outer; r += 0.001) {
		double h = 0.0, sx = 0.0, sz = 0.0;
		st::addRing(q, r, 0.0, t, h, sx, sz);
		if (last * h < 0.0) crossings.push_back(r);
		last = h;
	}
	ASSERT_GE(crossings.size(), 5u);
	EXPECT_GT(crossings.back() - crossings[crossings.size() - 2], 1.5 * (crossings[1] - crossings[0])) << "the outer waves longer than the inner ones";
}

TEST(FilmWaterRings, NothingBeyondItsReachAndSlopeIsTheHeightsSlope) {
	const st::Ring q = st::ringOf(1.0, -2.0, 3.0, 0.08, 0.02);
	const double t = 5.0, age = t - q.t0;
	double h = 0.0, sx = 0.0, sz = 0.0;
	st::addRing(q, q.x + 1.3 * st::reachOf(q, age), q.z, t, h, sx, sz);
	EXPECT_EQ(h, 0.0) << "past the front";
	// numerically: the slope given is the height's, on a crest's flank where the ring is strong (the fast term: phase a whole number of turns)
	const double r = st::kG * age * age / (4.0 * 4.0 * cartoon::kPi), e = 1e-4;
	double a = 0.0, b = 0.0, n = 0.0;
	st::addRing(q, q.x + r - e, q.z, t, a, n, n);
	st::addRing(q, q.x + r + e, q.z, t, b, n, n);
	double h2 = 0.0, s2 = 0.0, unused = 0.0;
	st::addRing(q, q.x + r, q.z, t, h2, s2, unused);
	EXPECT_NEAR(s2, (b - a) / (2.0 * e), 0.35 * std::abs(s2) + 2e-4);
}

TEST(FilmWaterRings, AWakeIsAV) {
	// a thing moving along +x at 2 m/s for 16 s: its waves lie within about 19.5 degrees of its track, and far outside that the water is still
	st::Field f;
	const double t = 16.0;
	st::trail(f, [](double u) { return cartoon::Vec3{2.0 * u, 0.0, 0.0}; }, 0.0, 16.0, t, 2.0, 0.4, 0.02);
	const double behind = 16.0;
	const auto energyAt = [&](double z) {
		double e = 0.0;
		for (double x = -0.6; x < 0.6; x += 0.2) {
			double h = 0.0, a = 0.0, b = 0.0;
			f.at(2.0 * t - behind + x, z, t, h, a, b);
			e += h * h;
		}
		return e;
	};
	const double arm = behind * std::tan(19.5 * cartoon::kPi / 180.0);
	EXPECT_GT(energyAt(0.75 * arm), 6.0 * energyAt(2.2 * arm)) << "inside the V against well outside it";
}

TEST(FilmWaterRings, RainIsTheSameEveryTimeAndOnlyNearTheCamera) {
	st::Field f;
	f.rain = 1.0;
	double h1 = 0.0, h2 = 0.0, a = 0.0, b = 0.0;
	f.at({}, 3.2, 4.1, 7.7, h1, a, b);
	f.at({}, 3.2, 4.1, 7.7, h2, a, b);
	EXPECT_EQ(h1, h2);
	double seen = 0.0;
	for (double x = 0.0; x < 2.0; x += 0.013) {
		double h = 0.0;
		f.at({}, x, 0.0, 7.7, h, a, b);
		seen = std::max(seen, std::abs(h));
	}
	EXPECT_GT(seen, 1e-4);
	double far_ = 0.0;
	f.at({}, 500.0, 0.0, 7.7, far_, a, b);
	EXPECT_EQ(far_, 0.0);
}

TEST(FilmWaterRings, ACutCellMeetsItsNeighbour) {
	// where a ring has not reached, the cut surface lies on the plain one; a cell it touches is cut small, and only that one
	st::Field f;
	f.rings.push_back(st::ringOf(0.5, 0.5, 0.0, 0.03, 0.005));
	cartoon::Mesh m;
	const std::vector<char> cut = st::refineGrid(
		f, 0.5, 4, 4, [](int j, int i) { return st::Cell{i + 0.5, j + 0.5, 0.75, 1.0, 1.0, 2.0}; },
		[](int j, int i) {
			st::Quad q;
			q.p[0] = {-1.0 * i, 0.0, 1.0 * j}, q.p[1] = {-(i + 1.0), 0.0, 1.0 * j}, q.p[2] = {-(i + 1.0), 0.0, j + 1.0}, q.p[3] = {-1.0 * i, 0.0, j + 1.0};
			for (cartoon::Vec3 &n : q.n) n = {0.0, 1.0, 0.0};
			return q;
		},
		[](int j, int i, double u, double v, double &x, double &z) { x = i + u, z = j + v; }, [&m](int, int) -> cartoon::Mesh & { return m; });
	EXPECT_EQ(cut[0], 1);
	EXPECT_EQ(cut[3 * 4 + 3], 0);
	EXPECT_GT(m.verts.size(), 500u);
}

TEST(FilmWaterRings, AStoneSkips) {
	st::Skip k;
	k.bounces = 6;
	const std::vector<st::Hop> hops = st::hopsOf(k);
	ASSERT_EQ(hops.size(), 6u);
	for (std::size_t n = 2; n < hops.size(); ++n) {
		EXPECT_LT(std::abs(hops[n].z - hops[n - 1].z), std::abs(hops[n - 1].z - hops[n - 2].z)) << "each hop shorter";
		EXPECT_GT(hops[n].t, hops[n - 1].t);
	}
	const std::string stone = st::skipText(k, 0.5 * (hops[1].t + hops[2].t), 0.0);
	EXPECT_NE(stone.find("skipping stone"), std::string::npos);
	EXPECT_EQ(st::skipText(k, hops.back().t + 0.1, 0.0), "") << "gone in";
}

namespace {

const char *kSet = R"(# an empty set
LookAt 0 3 12  0 0 0  0 1 0
Camera "perspective" "float fov" [ 40 ]
Film "rgb" "integer xresolution" [ 320 ] "integer yresolution" [ 180 ]
Sampler "halton" "integer pixelsamples" [ 16 ]
WorldBegin
LightSource "infinite" "rgb L" [ 0.4 0.5 0.6 ]
)";

// A film file's text with `body` for its own lines, in a folder of its own.
std::filesystem::path filmOf(const std::string &name, const std::string &body) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("rt_water_" + name);
	std::filesystem::create_directories(dir);
	std::ofstream(dir / "set.pbrt") << kSet;
	std::ofstream(dir / (name + ".film")) << "film " << name << "\nname Test\nabout A test\nscene set.pbrt\nlength 20\nstill 2\n" << body;
	return dir / (name + ".film");
}

}  // namespace

TEST(FilmWaterLines, ProblemsAreSaid) {
	ff::DataFilm film;
	EXPECT_NE(film.load(filmOf("noa", "ripple 0 0 at 1\nshot 0 a\n  key 0 2 6  0 0 0  40\n").string()), "") << "no water to ripple";
	ff::DataFilm film2;
	EXPECT_NE(film2.load(filmOf("nob", "water lake level 0\naboard ada wren\nshot 0 a\n  key 0 2 6  0 0 0  40\n").string()), "") << "no such boat";
	ff::DataFilm film3;
	EXPECT_NE(film3.load(filmOf("noc", "water lake level 0\nbankside reeds 2\nshot 0 a\n  key 0 2 6  0 0 0  40\n").string()), "") << "a lake with no land has no shore";
	ff::DataFilm film4;
	EXPECT_NE(film4.load(filmOf("nod", "water lake level 0\nboat submarine x at 0 0\nshot 0 a\n  key 0 2 6  0 0 0  40\n").string()), "") << "no such kind";
}

TEST(FilmWaterBoats, TheyRideAndStir) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("boats", "water sea level 0 waves 1 wind 30\nboat rowboat wren at 0 0 to 0 -40 speed 1.2\nboat sailboat gull at 20 -10 to 60 -10\ncast ada as pip at 0 0 0\naboard ada wren rows\nshot 0 a\n  key 0 3 12  0 0 -10  40\n").string()), "");
	ASSERT_EQ(film.spec.crafts.size(), 2u);
	const cartoon::craft::Craft &c = film.spec.crafts[0];
	const cartoon::craft::Shape h = cartoon::craft::shapeOf(c.kind, c.length, c.colour);
	const cartoon::craft::Afloat afloat{&film.spec.seas[0], nullptr};
	const double before = cartoon::craft::ridingAt(c, h, afloat, 2.0).at.z, after = cartoon::craft::ridingAt(c, h, afloat, 10.0).at.z;
	EXPECT_LT(after, before - 5.0) << "she goes";
	double highest = -1e9, lowest = 1e9;
	for (double t = 0.0; t < 20.0; t += 0.5) {
		const double y = cartoon::craft::ridingAt(c, h, afloat, t).at.y;
		highest = std::max(highest, y), lowest = std::min(lowest, y);
	}
	EXPECT_GT(highest - lowest, 0.1) << "she rises and falls on the swell";
	cartoon::Vec3 feet, front, chest;
	EXPECT_TRUE(film.wildSubject("wren", 5.0, feet, front, chest)) << "a shot can follow her";
	const cartoon::stir::Field f = film.stirredAt(10.0, 10.0, {0.0, 3.0, 12.0});
	EXPECT_GT(f.rings.size(), 20u) << "a wake";
	// who rows has its hands on the oars
	const cartoon::craft::Oar o = cartoon::craft::oarAt(h, 1, 1.0, true);
	EXPECT_GT(length(o.tip - o.handle), 1.5);
}

TEST(FilmWaterPeople, OutOfTheirDepthTheySwim) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("swim", "water lake level 0 waves 0.02\ncast deep as pip at 0 -3 -6\ncast wade as pip at 3 -0.3 -6\ncast dry as pip at -4 0 -6\nshot 0 a\n  key 0 3 12  0 0 -6  40\n").string()), "");
	cartoon::Vec3 at;
	film.momentOf(film.spec.characters[0], 5.0, &at);
	const auto &kind = ff::characterKinds().at(film.spec.characters[0].kind);
	EXPECT_NEAR(at.y, -0.72 * kind.height * film.spec.characters[0].scale, 0.1) << "treading water, not on the bottom";
	film.momentOf(film.spec.characters[1], 5.0, &at);
	EXPECT_NEAR(at.y, -0.3, 0.01) << "wading: its feet on the bottom";
	EXPECT_GT(film.momentOf(film.spec.characters[0], 5.0).wet, 0.9);
	EXPECT_EQ(film.momentOf(film.spec.characters[2], 5.0).wet, 0.0);
}

TEST(FilmWaterPeople, OverTheSideAndOut) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("fall", "water lake level 0 waves 0.02\nboat rowboat wren at 0 -6\ncast ada as pip at 0 0 0\naboard ada wren\nfallin ada at 3 to 2 -6\nclimbout ada at 9 to 6 0.5 -6\nshot 0 a\n  key 0 3 12  0 0 -6  40\n").string()), "");
	cartoon::Vec3 inBoat, under, up, out;
	film.momentOf(film.spec.characters[0], 2.0, &inBoat);
	film.momentOf(film.spec.characters[0], 4.2, &under);
	film.momentOf(film.spec.characters[0], 7.0, &up);
	film.momentOf(film.spec.characters[0], 12.0, &out);
	EXPECT_GT(inBoat.y, 0.0);
	EXPECT_LT(under.y, up.y) << "down, then up again";
	EXPECT_NEAR(out.y, 0.5, 0.01);
	EXPECT_NEAR(-out.x, 6.0, 0.01);
}

TEST(FilmWaterRivers, RunRoundRocksAndCarry) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("river", "river -40 0  0 0  40 0 width 8 speed 2 clear rocks 8 rapids 20 40 seed 3\nadrift paperboat p1 at -36 0 river 0\nboat canoe reed river 0 from 2\nshot 0 a\n  key 0 3 12  0 0 0  40\n").string()), "");
	const cartoon::water::River &r = film.spec.rivers[0];
	ASSERT_EQ(r.stones.size(), 8u);
	EXPECT_GT(cartoon::stream::speedAt(r, 0.0, 8.0), 2.5 * cartoon::stream::speedAt(r, 3.9, 8.0)) << "fastest in the middle";
	EXPECT_GT(r.path.front().y, r.path.back().y) << "rapids fall";
	EXPECT_NEAR(cartoon::stream::rapidAt(r, 30.0), 1.0, 1e-6);
	EXPECT_EQ(cartoon::stream::rapidAt(r, 5.0), 0.0);
	double white = 0.0;
	const cartoon::water::Rock &q = r.stones[0];
	const auto rocks = cartoon::stream::rocksNear(r, q.s - q.r);
	const double hump = cartoon::stream::shapeAt(r, rocks, q.s - 1.1 * q.r, q.c, 0.0, 0.0, white);
	EXPECT_GT(hump, 0.0) << "the water piles against it";
	const cartoon::adrift::Thing &th = film.spec.adrifts[0];
	ASSERT_EQ(th.ways.size(), 1u);
	EXPECT_GT(th.ways[0].back().s, th.ways[0].front().s + 40.0) << "carried down";
	EXPECT_EQ(th.ways[0].size(), film.spec.adrifts[0].ways[0].size());
	cartoon::Vec3 feet, front, chest;
	EXPECT_TRUE(film.wildSubject("p1", 6.0, feet, front, chest));
	EXPECT_TRUE(film.wildSubject("reed", 6.0, feet, front, chest));
	const std::string text = film.worldAt(film.headerAt(6.0), 6.0);
	EXPECT_NE(text.find("# a river (film_stream.h)"), std::string::npos);
	EXPECT_NE(text.find("carried by the water"), std::string::npos);
}

TEST(FilmWaterBanks, PlantsPartAndLifeTouchesTheWater) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("pond", "water lake level 0 waves 0.01 ripples 0.2 catspaws 2\nbankside reeds 2 lilies 1 duckweed 1 patch 0 -8 3\nwatermist 1\npondlife ducks 1 at 0 -6 radius 2 young 4\npondlife heron at -3 -6 strike 2\npondlife frogs 2 at 2 -6 radius 2\npondlife trout 2 at 1 -8\nshot 0 a\n  key 0 2 4  0 0 -8  40\n").string()), "");
	EXPECT_GT(film.spec.bank.clumps.size(), 10u);
	EXPECT_GT(film.spec.bank.pads.size(), 5u);
	EXPECT_FALSE(film.spec.lifes[1].touches.empty()) << "the heron's strike touches the water";
	EXPECT_FALSE(film.spec.lifes[2].touches.empty());
	const std::string text = film.worldAt(film.headerAt(2.3), 2.3);
	EXPECT_NE(text.find("# by the water"), std::string::npos);
	EXPECT_NE(text.find("ducks (film_pondlife.h)"), std::string::npos);
	EXPECT_NE(text.find("heron (film_pondlife.h)"), std::string::npos);
	// something coming through parts the reeds
	const std::vector<cartoon::bankside::Mover> movers = {{0.0, 0.0, 1.0}};
	EXPECT_GT(cartoon::bankside::detail::pushedBy(movers, 0.5, 0.0).x, 0.1);
	EXPECT_EQ(cartoon::bankside::detail::pushedBy(movers, 2.0, 0.0).x, 0.0);
}

TEST(FilmWaterChanges, TideFloodWhirlAndWave) {
	cartoon::water::Sea s;
	s.tideLow = -1.0, s.tideHigh = 1.0, s.tidePeriod = 40.0;
	EXPECT_NEAR(s.riseAt(0.0), -1.0, 1e-9);
	EXPECT_NEAR(s.riseAt(20.0), 1.0, 1e-9);
	cartoon::water::Sea f;
	f.floods.push_back({0.0, 4.0, 10.0, 20.0});
	EXPECT_NEAR(f.riseAt(15.0), 2.0, 1e-9);
	EXPECT_NEAR(f.riseAt(40.0), 4.0, 1e-9);
	cartoon::water::Sea w;
	w.waves = 0.05;
	cartoon::water::Sea::Bend b;
	b.radius = 20.0, b.depth = 5.0, b.t0 = -100.0;
	w.bends.push_back(b);
	cartoon::Vec3 p, n;
	double fold = 0.0;
	cartoon::water::seaAt(w, 0.5, 0.0, 0.0, 0.0, p, n, fold);
	EXPECT_LT(p.y, -3.0) << "the funnel's throat";
	cartoon::water::seaAt(w, 19.9, 0.0, 0.0, 0.0, p, n, fold);
	EXPECT_GT(p.y, -0.2) << "nothing at its edge";
	cartoon::water::Sea g;
	g.waves = 0.05;
	cartoon::water::Sea::Bend v;
	v.kind = 1, v.radius = 20.0, v.depth = 6.0, v.t0 = 0.0, v.speed = 10.0, v.facing = 0.0;
	g.bends.push_back(v);
	cartoon::water::seaAt(g, 0.0, 100.0, 10.0, 0.0, p, n, fold);
	EXPECT_GT(p.y, 5.0) << "where it has got to";
	cartoon::water::seaAt(g, 0.0, 300.0, 10.0, 0.0, p, n, fold);
	EXPECT_LT(p.y, 0.5) << "ahead of it";
}

TEST(FilmWaterChanges, ABoreRunsDownTheRiverAndADamSendsOne) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("dam", "river 0 -100  0 100 width 8 speed 1\ndam -6 -60 to 6 -60 height 4 burst 3 river 0\nshot 0 a\n  key 8 3 0  0 0 0  40\n").string()), "");
	ASSERT_EQ(film.spec.rivers[0].bores.size(), 1u);
	const cartoon::water::River &r = film.spec.rivers[0];
	double white = 0.0;
	const double from = r.bores[0].from;
	EXPECT_NEAR(from, 40.0, 2.0);
	EXPECT_EQ(cartoon::stream::shapeAt(r, {}, from + 200.0, 0.0, 5.0, 0.0, white), 0.0);
	EXPECT_GT(cartoon::stream::shapeAt(r, {}, from + 5.0, 0.0, 5.0, 0.0, white), 1.0) << "behind its front the water is high";
}

TEST(FilmWaterPlaces, TheyAllDrawAndAMillTurns) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("places", "water lake level 0\npier 0 -8 toward 90 length 12\nharbour 20 -30\nlighthouse -20 -40 height 20\nbridge -10 -20 to 10 -20 arches 3\nwatermill 12 -10\nsteppingstones 0 -6 to 4 -6\nstilthouses 3 at -20 -14\ndam 30 -30 to 40 -30\nsandcastle 2 -3\nrockpool -2 -3\nshot 0 a\n  key 0 3 12  0 0 -10  40\n").string()), "");
	EXPECT_EQ(film.spec.places.size(), 8u);
	const std::string text = film.worldAt(film.headerAt(1.0), 1.0);
	for (const char *word : {"# pier", "# harbour", "# lighthouse", "# bridge", "# watermill", "# steppingstones", "# stilthouses", "# dam", "# a mill wheel", "# a sandcastle", "# a rock pool"}) EXPECT_NE(text.find(word), std::string::npos) << word;
	EXPECT_EQ(cartoon::tideline::slumped(0.0), 0.0);
	EXPECT_GT(cartoon::tideline::slumped(10.0), 0.9);
	EXPECT_NE(cartoon::waterside::detail::millWheel(film.spec.places[4], 0.0, 1.0, 1.2), cartoon::waterside::detail::millWheel(film.spec.places[4], 0.0, 2.0, 1.2));
}

TEST(FilmWaterToon, WaterIsDrawn) {
	ff::DataFilm film;
	ASSERT_EQ(film.load(filmOf("toon", "toon bands 3 lines 2\nwater sea level 0 waves 0.3\nripple 0 -6 at 1 size 0.2\nshot 0 a\n  key 0 3 4  0 0 -8  40\n").string()), "");
	const std::string text = film.worldAt(film.headerAt(1.3), 1.3);
	EXPECT_NE(text.find("# drawn water (film_toonwater.h)"), std::string::npos);
	EXPECT_NE(text.find("fx/bubbles"), std::string::npos) << "no ink round the wave lines";
	EXPECT_EQ(text.find("coateddiffuse\" \"rgb reflectance\" [ 0.01500"), std::string::npos) << "not the real sea's coat";
	ff::DataFilm real;
	ASSERT_EQ(real.load(filmOf("toonoff", "toon bands 3 lines 2\ntoonwater off\nwater sea level 0 waves 0.3\nshot 0 a\n  key 0 3 4  0 0 -8  40\n").string()), "");
	EXPECT_EQ(real.worldAt(real.headerAt(1.3), 1.3).find("# drawn water"), std::string::npos);
}
