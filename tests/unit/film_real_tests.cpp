// film_real_tests.cpp -- what makes plants and animals look real: the land's cracks, trees that stay trees into the distance, leaves.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/cartoon_films.h"   // (the films themselves, which these tests reach into)

namespace forest = cartoon::forest;
namespace terrain = cartoon::terrain;
using cartoon::Mesh;
using V3 = cartoon::Vec3;

namespace {

// A grid of `n` x `n` cells a metre across, its height at each corner from `height`, as terrain::surfaceMesh lays one out.
template <class Height>
Mesh grid(int n, Height height) {
	Mesh m;
	for (int j = 0; j <= n; ++j)
		for (int i = 0; i <= n; ++i) {
			m.verts.push_back({static_cast<double>(i), height(i, j), static_cast<double>(j)});
			m.uvs.push_back({0.0, 0.0});
		}
	const int nx = n + 1;
	for (int j = 0; j < n; ++j)
		for (int i = 0; i < n; ++i) {
			const int a = j * nx + i;
			m.tris.push_back({a, a + nx, a + 1});
			m.tris.push_back({a + 1, a + nx, a + nx + 1});
		}
	terrain::landNormals(m);
	return m;
}

double leafArea(const Mesh &m, std::size_t fromTri = 0) {
	double area = 0.0;
	for (std::size_t k = fromTri; k < m.tris.size(); ++k) {
		const auto &t = m.tris[k];
		area += 0.5 * cartoon::length(cartoon::cross(m.verts[t[1]] - m.verts[t[0]], m.verts[t[2]] - m.verts[t[0]]));
	}
	return area;
}

}  // namespace

// A gully one cell wide is no cliff: as one it was a dark crack across the green hills. A real cliff still is one.
TEST(FilmReal, AGullyOneCellWideIsNoCliff) {
	const Mesh gully = grid(40, [](int i, int) { return i == 20 ? -3.0 : 0.0; });
	EXPECT_TRUE(terrain::surfacePart(gully, 1, 0.13).tris.empty());
	EXPECT_TRUE(terrain::surfacePart(gully, 2, 0.13).tris.empty());
	EXPECT_EQ(terrain::surfacePart(gully, 0, 0.13).tris.size(), gully.tris.size()) << "all of it ground";
	const Mesh wall = grid(40, [](int i, int) { return i < 10 ? 0.0 : i < 30 ? 2.0 * (i - 10) : 40.0; });
	EXPECT_GT(terrain::surfacePart(wall, 1, 0.13).tris.size(), 1000u) << "a real cliff is still a cliff";
}

// The land's normals come from its own faces: one taken from finer ground than the mesh has leaned away from the viewer on a face that looks at them, and shaded black.
TEST(FilmReal, TheLandsNormalsAgreeWithItsFaces) {
	const Mesh ridge = grid(30, [](int i, int j) { return 4.0 * std::sin(i * 0.7) * std::cos(j * 0.5); });
	for (const auto &t : ridge.tris) {
		const V3 face = cartoon::normalize(cartoon::cross(ridge.verts[t[1]] - ridge.verts[t[0]], ridge.verts[t[2]] - ridge.verts[t[0]])) * 1.0;
		const V3 up = face.y < 0.0 ? face * -1.0 : face;
		for (int c = 0; c < 3; ++c) EXPECT_GT(cartoon::dot(up, ridge.normals[static_cast<std::size_t>(t[c])]), 0.15);
	}
}

// Far off a tree keeps its shape: its own limbs, and fewer, bigger leaves covering as much sky in the same places. It was a ball, or a cone.
TEST(FilmReal, AFarTreeIsTheNearTreeWithFewerBiggerLeaves) {
	for (const std::string sp : {"broadleaf", "birch", "conifer", "acacia", "palm", "kapok", "shrub"}) {
		const forest::detail::Grown near_ = forest::detail::grow(sp, 1, false, false), far_ = forest::detail::growFar(sp, 1, false);
		EXPECT_EQ(near_.wood.tris.size(), far_.wood.tris.size()) << sp << ": its own limbs";
		EXPECT_LT(far_.foliage.tris.size(), near_.foliage.tris.size() / 3) << sp << ": fewer leaves";
		const double nearArea = leafArea(near_.foliage, near_.coreTris), farArea = leafArea(far_.foliage, far_.coreTris);
		EXPECT_NEAR(farArea / nearArea, 1.0, 0.12) << sp << ": covering as much";
		double nearTop = -1e30, farTop = -1e30;
		for (const V3 &p : near_.foliage.verts) nearTop = std::max(nearTop, p.y);
		for (const V3 &p : far_.foliage.verts) farTop = std::max(farTop, p.y);
		EXPECT_NEAR(farTop, nearTop, 0.1 * nearTop + 0.5) << sp << ": as tall";
	}
}

// Each leaf has a cell of the picture of leaves, and the same one near and far: the top of a crown is in the rows of the most exposed.
TEST(FilmReal, EachLeafHasItsPlaceInThePictureOfLeaves) {
	const forest::detail::Grown tree = forest::detail::grow("broadleaf", 0, false, false);
	ASSERT_EQ(tree.foliage.uvs.size(), tree.foliage.verts.size());
	double low = 0.0, high = 0.0;
	int nLow = 0, nHigh = 0;
	double bottom = 1e30, top = -1e30;
	for (const V3 &p : tree.foliage.verts) bottom = std::min(bottom, p.y), top = std::max(top, p.y);
	for (std::size_t i = 0; i < tree.foliage.verts.size(); ++i) {
		const auto &uv = tree.foliage.uvs[i];
		ASSERT_GE(uv.first, 0.0);
		ASSERT_LE(uv.first, 1.0);
		ASSERT_LE(uv.second, 1.0);
		if (tree.foliage.verts[i].y < bottom + 0.2 * (top - bottom)) low += uv.second, ++nLow;
		if (tree.foliage.verts[i].y > top - 0.2 * (top - bottom)) high += uv.second, ++nHigh;
	}
	ASSERT_GT(nLow, 0);
	ASSERT_GT(nHigh, 0);
	EXPECT_GT(high / nHigh, low / nLow + 0.15);
	const forest::detail::Grown far_ = forest::detail::growFar("broadleaf", 0, false);
	EXPECT_EQ(far_.foliage.uvs.size(), far_.foliage.verts.size());
}

// Autumn does not take a crown all at once: the exposed leaves have turned, those deep inside are still green. No two leaves alike.
TEST(FilmReal, AutumnTurnsTheExposedLeavesFirst) {
	const V3 inside = forest::detail::cellColour("broadleaf", "autumn", 0, 0, 3), outside = forest::detail::cellColour("broadleaf", "autumn", 0, forest::detail::kLeafCells - 1, 3);
	EXPECT_GT(outside.x / outside.y, inside.x / inside.y) << "redder outside, greener inside";
	std::map<int, int> seen;
	for (int col = 0; col < forest::detail::kLeafCells; ++col) ++seen[static_cast<int>(forest::detail::cellColour("broadleaf", "summer", 1, 4, col).y * 2000.0)];
	EXPECT_GT(seen.size(), 5u);
}

// Rain stays clear of the lens, where a streak this bright is a wide smear out of focus, and is as thin as a drop.
TEST(FilmReal, RainKeepsClearOfTheLens) {
	cartoon::particles::Weather w;
	w.kind = "rain", w.amount = 1.0;
	const std::string text = cartoon::particles::weatherText(w, 3.0, {0.0, 2.0, 0.0}, 0.02);
	ASSERT_NE(text.find("point3 P"), std::string::npos);
	const std::size_t from = text.find('[', text.find("point3 P"));
	std::istringstream in(text.substr(from + 1, text.find(']', from) - from - 1));
	double x = 0.0, y = 0.0, z = 0.0, nearest = 1e30;
	while (in >> x >> y >> z) nearest = std::min(nearest, std::sqrt(x * x + (y - 2.0) * (y - 2.0) + z * z));
	EXPECT_GT(nearest, 4.7);
	EXPECT_LT(nearest, 8.0) << "but close enough to be seen";
}
