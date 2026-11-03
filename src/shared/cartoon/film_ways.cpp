// film_ways.cpp -- walks that find their way (film_file.h `walk NAME T SPEED to X Z`, and the walks a film makes itself: to a piece of furniture, after an owner) and the floor
// under every walk: DataFilm's members for them (out of film_file.cpp since indoor round 2, which taught them a house's floors - a walk to a room upstairs goes to the foot of
// the stairs on its own floor, up them a tread at a time, and on from their top over the floor above; down again the same way). Compiled once, in cpu_renderer.

#include "film_file.h"

namespace cartoon {
namespace film_file {

namespace {
pbrt_flatten::MeshResolver plyMeshes() {
	return [](const std::string &path, std::vector<float> &positions, std::vector<int> &indices, std::vector<float> &uvs, std::vector<float> &normals) {
		ply_mesh::LoadResult m = ply_mesh::loadFile(path);
		if (!m.ok) return false;
		positions = std::move(m.mesh.positions);
		indices = std::move(m.mesh.indices);
		uvs = std::move(m.mesh.uvs);
		normals = std::move(m.mesh.normals);
		return true;
	};
}
}  // namespace

std::string DataFilm::findWays() {
	std::vector<std::pair<Character *, std::size_t>> walks;
	for (Character &c : spec.characters)
		for (std::size_t i = 0; i < c.walks.size(); ++i)
			if (!c.walks[i].targets.empty()) walks.push_back({&c, i});
	std::stable_sort(walks.begin(), walks.end(), [](const auto &a, const auto &b) { return a.first->walks[a.second].t0 < b.first->walks[b.second].t0; });
	std::map<std::string, pbrt_flatten::FlatScene> flattened;   // (by the set's text: a set that does not move is flattened once)
	const pbrt_flatten::MeshResolver meshes = plyMeshes();
	const std::string roomsText = roomsStatic();   // (its rooms' walls, stairs and furniture too: the way round them)
	for (const auto &[c, i] : walks) {
		WalkLine &w = c->walks[i];
		const CharacterKind &kind = characterKinds().at(c->kind);
		double fx = c->start[0], fz = c->start[2], fy = c->start[1];   // from where its last walk left it
		if (i > 0) {
			const Vec3 last = endOf(*c, i - 1);
			fx = -last.x, fy = last.y, fz = last.z;
		}
		const std::string set = setAt(w.t0);
		if (!flattened.count(set)) flattened[set] = film_paths::flattenText(set + roomsText, meshes);
		const pbrt_flatten::FlatScene &flat = flattened[set];
		// the stairs it takes on its way to each target on another floor (film_rooms.h), and the map round it all
		std::vector<rooms::Flight> flights(w.targets.size());
		double x0 = fx, x1 = fx, z0 = fz, z1 = fz;
		for (std::size_t k = 0, changed = 0; k < w.targets.size(); ++k) {   // (`changed`: one past the last target on another floor)
			const auto [tx, tz] = w.targets[k];
			x0 = std::min(x0, tx), x1 = std::max(x1, tx), z0 = std::min(z0, tz), z1 = std::max(z1, tz);
			const double to = k < w.floors.size() ? w.floors[k] : std::nan(""), on = changed ? w.floors[changed - 1] : fy;
			if (std::isnan(to) || std::abs(to - on) < 1.0) continue;
			if (!rooms::flightBetween(spec.indoors, on, to, {fx, on, fz}, flights[k])) {
				char buf[240];
				std::snprintf(buf, sizeof buf, "%d: %s cannot get from the floor at %.2f m to the one at %.2f m: there are no stairs between them (a `house` with an `upstairs`)", w.line, c->name.c_str(), on, to);
				return buf;
			}
			for (const Vec3 &p : flights[k].points) x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), z0 = std::min(z0, p.z), z1 = std::max(z1, p.z);
			changed = k + 1;
		}
		const double half = std::min(30.0, 0.5 * std::max(x1 - x0, z1 - z0) + 6.0);
		const double cell = std::max(0.05, std::min(0.12, kind.radius * c->scale * 0.4));
		const double height = std::min(2.0, kind.height * c->scale), step = stepOf(*c);
		const film_paths::Terrain terrain = film_paths::terrainOf(flat, 0.5 * (x0 + x1), 0.5 * (z0 + z1), half, cell, c->start[1]);
		// the map of the floor it is on, from where it stands (the floor it can walk on), whoever else stands there in the way (indoors: on that floor), grown by its size
		std::vector<double> floors;
		film_paths::Grid g, ways;
		const auto mapFrom = [&](double x, double y, double z) {
			g = film_paths::reachable(terrain, x, y, z, step, std::min(0.15, 0.4 * height), height, floors);
			for (const Character &other : spec.characters) {
				if (&other == c) continue;
				Vec3 at;
				momentOf(other, w.t0, &at);
				if (spec.indoors.any() && std::abs(at.y - y) > 1.5) continue;   // (upstairs or down)
				if (std::any_of(spec.indoors.home.acts.begin(), spec.indoors.home.acts.end(), [&](const rooms::Act &a) {
						return a.verb == "heel" && ((w.follows && a.who == c->name && a.what == other.name) || (a.who == other.name && a.what == c->name && w.t0 >= a.t0 && w.t0 < a.t1));
					}))
					continue;   // (a pet at heel and its owner: the owner walks on ahead of it, and it comes along out of the way)
				film_paths::addDisc(g, -at.x, at.z, characterKinds().at(other.kind).radius * other.scale);
			}
			ways = film_paths::grown(g, spec.indoors.any() ? std::min(0.28, kind.radius * c->scale) : kind.radius * c->scale);   // (indoors closer: through a doorway)
		};
		mapFrom(fx, fy, fz);
		w.points.clear();
		w.heights.clear();
		// a way across the floor it is on to (tx, tz), along it every quarter of a metre at the floor's height (over a hill, up the steps)
		const auto walkTo = [&](double tx, double tz) -> std::string {
			std::vector<std::pair<double, double>> leg;
			std::string why;
			if (!film_paths::findWay(ways, fx, fz, tx, tz, leg, why)) {
				if (why == "something is in the way there" && std::isnan(film_paths::floorFrom(g, floors, tx, tz)) &&
					terrain.roomy(terrain.grid.col(tx), terrain.grid.row(tz), terrain.floorAt(tx, tz, fy + 50.0, 100.0), std::min(0.15, 0.4 * height), height))
					why = "it cannot get up or down there (a drop or a rise of more than a step)";   // (the place has room: the way there is the trouble)
				char buf[200];
				std::snprintf(buf, sizeof buf, "%d: %s cannot find a way to %g %g: %s", w.line, c->name.c_str(), tx, tz, why.c_str());
				return buf;
			}
			double px = fx, pz = fz;
			for (const auto &[lx, lz] : leg) {
				const int n = std::max(1, static_cast<int>(std::ceil(std::hypot(lx - px, lz - pz) / 0.25)));
				for (int k = 1; k <= n; ++k) {
					const double u = static_cast<double>(k) / n, x = px + (lx - px) * u, z = pz + (lz - pz) * u;
					const double y = film_paths::floorFrom(g, floors, x, z);
					w.points.push_back({x, z});
					w.heights.push_back(std::isnan(y) ? c->start[1] : (std::abs(y - c->start[1]) < terrain.flat ? c->start[1] : y));
				}
				px = lx, pz = lz;
			}
			fx = tx, fz = tz;
			return std::string();
		};
		for (std::size_t k = 0; k < w.targets.size(); ++k) {
			if (!flights[k].points.empty()) {   // up the stairs (or down them): to the end it starts from, a tread at a time to the other, the map made again from there
				std::vector<Vec3> pts = flights[k].points;
				if (w.floors[k] < fy) std::reverse(pts.begin(), pts.end());
				if (const std::string e = walkTo(pts.front().x, pts.front().z); !e.empty()) return e;
				for (std::size_t q = 1; q < pts.size(); ++q) w.points.push_back({pts[q].x, pts[q].z}), w.heights.push_back(pts[q].y);
				fx = pts.back().x, fy = pts.back().y, fz = pts.back().z;
				mapFrom(fx, fy, fz);
			}
			if (const std::string e = walkTo(w.targets[k].first, w.targets[k].second); !e.empty()) return e;
		}
	}
	return std::string();
}

void DataFilm::heightsOfWalks() {
	terrains.clear();
	const auto grounded = [this](const Character &c) { return motion::amountFor(spec.footing, c.name) > 0.5; };
	const bool anyWalks = std::any_of(spec.characters.begin(), spec.characters.end(), [&](const Character &c) { return !c.walks.empty() || grounded(c); });
	if (!anyWalks) return;
	// (with its rooms: a foot upstairs is on the floor there, not the ground under it)
	const pbrt_flatten::FlatScene flat = film_paths::flattenText(spec.indoors.any() ? setAt(0.0) + roomsStatic() : setAt(0.0), plyMeshes());
	for (Character &c : spec.characters) {
		double x0 = c.start[0], x1 = x0, z0 = c.start[2], z1 = z0;
		for (const WalkLine &w : c.walks)
			for (const auto &[x, z] : w.points) x0 = std::min(x0, x), x1 = std::max(x1, x), z0 = std::min(z0, z), z1 = std::max(z1, z);
		const double half = std::min(30.0, 0.5 * std::max(x1 - x0, z1 - z0) + 2.0);
		auto terrain = std::make_shared<film_paths::Terrain>(film_paths::terrainOf(flat, 0.5 * (x0 + x1), 0.5 * (z0 + z1), half, 0.1, c.start[1]));
		if (grounded(c)) {   // (`footing ground`: it stands on the floor under it - one near where it was put, else the film's land however far above or below)
			const double nearby = terrain->floorAt(c.start[0], c.start[2], c.start[1], std::max(0.6, stepOf(c)));   // (not `near`: a Windows macro)
			if (nearby != c.start[1] || !spec.hasTerrain) {
				c.start[1] = nearby;
			} else if (const std::shared_ptr<const terrain::Field> land = terrain::fieldOf(spec.terrain); land->inside(c.start[0], c.start[2])) {
				c.start[1] = land->heightAt(c.start[0], c.start[2]);
				terrain = std::make_shared<film_paths::Terrain>(film_paths::terrainOf(flat, 0.5 * (x0 + x1), 0.5 * (z0 + z1), half, 0.1, c.start[1]));   // (its floors searched from there)
			}
		}
		double y = c.start[1];
		bool uneven = false;
		const double step = stepOf(c);
		for (std::size_t i = 0; i < c.walks.size(); ++i) {
			WalkLine &w = c.walks[i];
			if (!w.targets.empty()) {   // (found already, on the floor)
				if (!w.heights.empty()) y = w.heights.back();
				continue;
			}
			w.heights.clear();
			for (const auto &[x, z] : w.points) {
				// a jump lands on the highest floor within its height; a walk keeps to a floor within a step of the last
				y = terrain->floorAt(x, z, y, w.style == WalkLine::Jump ? std::max(step, w.height) : step);
				w.heights.push_back(y);
			}
		}
		for (const WalkLine &w : c.walks)
			for (double h : w.heights) uneven = uneven || h != c.start[1];
		if (uneven || grounded(c)) terrains[c.name] = terrain;   // (its feet on the floor too)
	}
}

const film_paths::Terrain * DataFilm::terrainFor(const Character &c) const {
	const auto it = terrains.find(c.name);
	return it == terrains.end() ? nullptr : it->second.get();
}

}  // namespace film_file
}  // namespace cartoon
