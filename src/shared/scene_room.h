#pragma once
// scene_room.h -- "Room from a photo": turns the room.json the photo helper writes (tools/photo_to_room/photo_to_room.py, geometry in room_fit.py) into a Scene Builder document.
//
// The helper fits a box-shaped room to one photo: the floor, ceiling and walls as thin axis-aligned slabs with the colour the photo shows, the windows and lamps as emissive
// panels, and a camera where the photo was taken from. This file is the Builder's side of that: it reads the JSON (a plain data file, so the reader and the conversion need no
// Python and are unit-tested with hand-written files) and makes ordinary Builder objects, so the room can be edited like any scene: the surfaces are Boxes (locked, so a stray
// drag does not move a wall; they can still be picked in the list), the lights are emissive Boxes.
//
// With --furniture the helper also makes a mesh of each piece of furniture it finds (TripoSR, the Object from a photo model); room.json lists them and each becomes a Mesh object
// standing where the photo shows it, facing the camera. The mesh files are named relative to the room.json's folder.
//
// Header-only, standard library only; part of the Scene Builder's document (scene_document.h).

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "scene_json.h"
#include "scene_model.h"

namespace scene_room {

struct Import {
	std::string title;
	scene_doc::Camera camera;
	int width = 800, height = 600;                 // the render size: the photo's proportions
	std::vector<scene_doc::Object> surfaces;       // floor, ceiling, walls
	std::vector<scene_doc::Object> lights;         // emissive panels
	std::vector<scene_doc::Object> furniture;      // meshes the helper made of the furniture it found (empty without --furniture)
	std::vector<std::string> notes;                // what the helper had to assume, for the user
	double photoBrightness = 0.0;                  // how bright the photo looks (pictureBrightness of it), 0 when the file does not say: the lights are then left as the helper set them
	scene_doc::Float3 roomMin, roomMax;
};

namespace detail {

using scene_doc::detail::Json;

inline bool finite3(double a, double b, double c) { return std::isfinite(a) && std::isfinite(b) && std::isfinite(c); }

// min / max of a slab or panel: both must be there, finite, with max above min on every axis.
inline bool readBox(const Json& j, scene_doc::Float3& lo, scene_doc::Float3& hi, std::string& err, const std::string& what) {
	if (j.type != Json::Type::Obj) { err = what + " is not an object"; return false; }
	if (!j.find("min") || !j.find("max")) { err = what + " has no min and max"; return false; }
	if (!scene_doc::detail::readVec(j, "min", lo) || !scene_doc::detail::readVec(j, "max", hi)) { err = what + " has a bad min or max"; return false; }
	if (!finite3(lo.x, lo.y, lo.z) || !finite3(hi.x, hi.y, hi.z)) { err = what + " has a number that is not finite"; return false; }
	if (!(hi.x > lo.x && hi.y > lo.y && hi.z > lo.z)) { err = what + " has no volume"; return false; }
	if (std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) > 200.0) { err = what + " is larger than 200 metres"; return false; }
	return true;
}

// A file the room file names (a picture, a mesh), inside the room file's folder: `path` is the folder joined with the name. False for a name that is absolute or climbs out of the folder
// (a room file is data: it must not make the Builder read any file on the disk), and for a name that is not there. With no folder the name is used as it is.
inline bool findFile(const std::string& baseFolder, const std::string& name, std::string& path) {
	namespace fs = std::filesystem;
	if (name.empty()) return false;
	std::error_code ec;
	if (baseFolder.empty()) {
		path = name;
		return fs::exists(path, ec);
	}
	const fs::path rel(name);
	if (rel.is_absolute() || rel.has_root_name() || rel.has_root_directory()) return false;
	for (const fs::path& part : rel)
		if (part == "..") return false;
	path = (fs::path(baseFolder) / rel).string();
	return fs::exists(path, ec);
}

inline scene_doc::Object boxObject(const std::string& name, const scene_doc::Float3& lo, const scene_doc::Float3& hi) {
	scene_doc::Object o = scene_doc::makeObject(scene_doc::ShapeKind::Box, name);
	o.position = {(lo.x + hi.x) / 2.0, (lo.y + hi.y) / 2.0, (lo.z + hi.z) / 2.0};
	o.size = {hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
	return o;
}

}  // namespace detail

// Reads the helper's room.json. Returns false with `err` set when the file is not one the Builder can use; otherwise `out` holds the room. `baseFolder` is the folder the file is in:
// the furniture's mesh and texture files are named relative to it (a piece whose files are not there, or whose numbers are not believable, is left out, and the notes say so).
inline bool fromJson(const std::string& text, Import& out, std::string& err, const std::string& baseFolder = std::string()) {
	using scene_doc::detail::Json;
	using scene_doc::detail::readNum;
	using scene_doc::detail::readRgb;
	using scene_doc::detail::readVec;
	Json root;
	if (!scene_doc::detail::JsonParser(text).parse(root, err)) return false;
	if (root.type != Json::Type::Obj) { err = "the room file is not a JSON object"; return false; }
	out = Import();

	// ---- camera
	const Json* cam = root.find("camera");
	if (!cam || cam->type != Json::Type::Obj) { err = "the room file has no camera"; return false; }
	double vfov = 40.0;
	if (!readVec(*cam, "position", out.camera.position) || !readVec(*cam, "target", out.camera.target) || !readVec(*cam, "up", out.camera.up) || !readNum(*cam, "vfov", vfov)) {
		err = "the camera in the room file is not readable";
		return false;
	}
	if (!(vfov > 5.0 && vfov < 150.0)) { err = "the camera's field of view is not believable"; return false; }
	{
		const scene_doc::Float3 &p = out.camera.position, &t = out.camera.target, &u = out.camera.up;
		const double dx = t.x - p.x, dy = t.y - p.y, dz = t.z - p.z;
		const double view = std::sqrt(dx * dx + dy * dy + dz * dz), upLength = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
		const double along = view > 0.0 && upLength > 0.0 ? std::fabs(dx * u.x + dy * u.y + dz * u.z) / (view * upLength) : 1.0;
		if (!detail::finite3(p.x, p.y, p.z) || !detail::finite3(t.x, t.y, t.z) || !detail::finite3(u.x, u.y, u.z) || std::fabs(p.x) > 1e4 || std::fabs(p.y) > 1e4 || std::fabs(p.z) > 1e4 ||
		    !(view > 1e-6) || !(upLength > 1e-6) || along > 0.999) {
			err = "the camera in the room file does not look anywhere believable";
			return false;
		}
	}
	out.camera.fov = vfov;
	out.camera.lensRadius = 0.0;

	// ---- the photo's proportions (for the render size)
	if (const Json* photo = root.find("photo")) {
		double w = 0, h = 0;
		if (readNum(*photo, "width", w) && readNum(*photo, "height", h) && w > 0 && h > 0) {
			const double aspect = h / w;
			out.width = aspect > 1.0 ? 600 : 800;
			out.height = std::max(120, static_cast<int>(std::lround(out.width * aspect)));
		}
		double brightness = 0.0;
		if (readNum(*photo, "brightness", brightness) && brightness > 0.0 && brightness < 1.0) out.photoBrightness = brightness;
	}

	// ---- the room's extent
	const Json* room = root.find("room");
	if (!room || !detail::readBox(*room, out.roomMin, out.roomMax, err, "the room")) {
		if (err.empty()) err = "the room file has no room";
		return false;
	}

	// ---- surfaces
	const Json* surfaces = root.find("surfaces");
	if (!surfaces || surfaces->type != Json::Type::Arr || surfaces->a.empty()) { err = "the room file has no surfaces"; return false; }
	if (surfaces->a.size() > 200) { err = "the room file has more than 200 surfaces"; return false; }
	for (const Json& s : surfaces->a) {
		scene_doc::Float3 lo, hi;
		std::string name = "Surface";
		scene_doc::detail::readStr(s, "name", name);
		if (!detail::readBox(s, lo, hi, err, "the surface \"" + name + "\"")) return false;
		scene_doc::Object o = detail::boxObject(name, lo, hi);
		scene_doc::Rgb c{0.7, 0.7, 0.7};
		if (!readRgb(s, "color", c)) { err = "the surface \"" + name + "\" has a bad colour"; return false; }
		o.material.color = {std::clamp(c.r, 0.0, 1.0), std::clamp(c.g, 0.0, 1.0), std::clamp(c.b, 0.0, 1.0)};
		o.locked = true;   // a wall is not dragged by accident; it can still be picked in the list
		// The photo's own look on the surface: a picture named relative to the room.json's folder (the flat colour stays under it, and is what shows when the picture is missing)
		std::string picture, path;
		if (scene_doc::detail::readStr(s, "texture", picture) && !picture.empty()) {
			if (detail::findFile(baseFolder, picture, path)) o.material.imageFile = path;
			else out.notes.push_back("The picture of the " + name + " was not found, so it keeps its flat colour.");
		}
		out.surfaces.push_back(o);
	}

	// ---- lights
	if (const Json* lights = root.find("lights")) {
		if (lights->type != Json::Type::Arr) { err = "the lights in the room file are not a list"; return false; }
		if (lights->a.size() > 100) { err = "the room file has more than 100 lights"; return false; }
		for (const Json& l : lights->a) {
			scene_doc::Float3 lo, hi;
			std::string name = "Light";
			scene_doc::detail::readStr(l, "name", name);
			if (!detail::readBox(l, lo, hi, err, "the light \"" + name + "\"")) return false;
			scene_doc::Object o = detail::boxObject(name, lo, hi);
			scene_doc::Rgb c{1.0, 1.0, 1.0};
			double strength = 8.0;
			if (!readRgb(l, "color", c) || !readNum(l, "strength", strength)) { err = "the light \"" + name + "\" has a bad colour or strength"; return false; }
			o.emissive = true;
			o.emission = {std::clamp(c.r, 0.0, 1.0), std::clamp(c.g, 0.0, 1.0), std::clamp(c.b, 0.0, 1.0)};
			// A window's view: the picture the helper cut out of the photo, which the light then takes its colours from (the colour above is what shows without it)
			std::string view, path;
			if (scene_doc::detail::readStr(l, "picture", view) && !view.empty()) {
				if (detail::findFile(baseFolder, view, path)) o.emissionImage = path;
				else out.notes.push_back("The picture of the light \"" + name + "\" was not found, so it is a plain light.");
			}
			o.emissionStrength = std::clamp(strength, 0.0, 1000.0);
			o.material.color = {0.0, 0.0, 0.0};   // a lamp only emits
			out.lights.push_back(o);
		}
	}

	// ---- furniture: a mesh and a texture each, placed and turned
	if (const Json* objects = root.find("objects")) {
		if (objects->type != Json::Type::Arr) { err = "the furniture in the room file is not a list"; return false; }
		if (objects->a.size() > 100) { err = "the room file has more than 100 pieces of furniture"; return false; }
		for (const Json& o : objects->a) {
			// A staircase: the box it fills, the way it climbs and how many steps; the Builder's own stairs shape (it climbs along its local +z, and stands centred on its box)
			std::string kind;
			if (o.type == Json::Type::Obj && scene_doc::detail::readStr(o, "kind", kind) && kind == "stairs") {
				std::string name = "Stairs", why;
				scene_doc::detail::readStr(o, "name", name);
				scene_doc::Float3 lo, hi;
				scene_doc::Rgb colour{0.55, 0.4, 0.25};
				int steps = 10;
				const Json* ascend = o.find("ascend");
				const bool good = detail::readBox(o, lo, hi, why, name) && readRgb(o, "color", colour) && scene_doc::detail::readInt(o, "steps", steps) && ascend && ascend->type == Json::Type::Arr &&
				                  ascend->a.size() == 2 && ascend->a[0].type == Json::Type::Num && ascend->a[1].type == Json::Type::Num;
				if (!good) {
					out.notes.push_back("A staircase in the room file (" + name + ") could not be used and was left out.");
					continue;
				}
				const double ax = ascend->a[0].n, az = ascend->a[1].n;
				scene_doc::Object s = scene_doc::makeObject(scene_doc::ShapeKind::Stairs, name);
				s.position = {(lo.x + hi.x) / 2.0, (lo.y + hi.y) / 2.0, (lo.z + hi.z) / 2.0};
				const bool alongZ = std::fabs(az) >= std::fabs(ax);
				s.size = {alongZ ? hi.x - lo.x : hi.z - lo.z, hi.y - lo.y, alongZ ? hi.z - lo.z : hi.x - lo.x};   // width, rise, run
				s.rotation = {0.0, alongZ ? (az > 0 ? 0.0 : 180.0) : (ax > 0 ? 90.0 : -90.0), 0.0};
				s.steps = std::clamp(steps, 1, 40);
				s.material.color = {std::clamp(colour.r, 0.0, 1.0), std::clamp(colour.g, 0.0, 1.0), std::clamp(colour.b, 0.0, 1.0)};
				out.furniture.push_back(s);
				continue;
			}
			// A piece made of measured boxes (a table: a top and four legs; a cabinet: one block): each part is a Box, all of them one group so the piece is picked and moved as one
			if (const Json* parts = o.type == Json::Type::Obj ? o.find("parts") : nullptr) {
				std::string name = "Furniture";
				scene_doc::detail::readStr(o, "name", name);
				bool all = parts->type == Json::Type::Arr && !parts->a.empty();
				std::vector<scene_doc::Object> made;
				std::string why;
				for (const Json& part : parts->a) {
					scene_doc::Float3 lo, hi;
					std::string partName = name;
					scene_doc::detail::readStr(part, "name", partName);
					scene_doc::Rgb colour{0.6, 0.6, 0.6};
					if (!detail::readBox(part, lo, hi, why, partName) || !readRgb(part, "color", colour)) { all = false; break; }
					scene_doc::Object box = detail::boxObject(partName, lo, hi);
					box.material.color = {std::clamp(colour.r, 0.0, 1.0), std::clamp(colour.g, 0.0, 1.0), std::clamp(colour.b, 0.0, 1.0)};
					// A table's top can carry the photo's own look (its wood, its cloth): a picture named relative to the room file's folder; the flat colour stays when it is missing
					std::string picture, path;
					if (scene_doc::detail::readStr(part, "texture", picture) && !picture.empty()) {
						if (detail::findFile(baseFolder, picture, path)) box.material.imageFile = path;
						else out.notes.push_back("The picture of the " + partName + " was not found, so it keeps its flat colour.");
					}
					if (parts->a.size() > 1) box.group = name;
					made.push_back(box);
				}
				if (!all) {
					out.notes.push_back("A piece of furniture in the room file (" + name + ") could not be used and was left out.");
					continue;
				}
				for (scene_doc::Object& box : made) out.furniture.push_back(box);
				continue;
			}
			std::string name = "Furniture", mesh, texture;
			scene_doc::Float3 pos{};
			double yaw = 0.0, scale = 1.0;
			const bool readable = o.type == Json::Type::Obj && scene_doc::detail::readStr(o, "name", name) && scene_doc::detail::readStr(o, "mesh", mesh) &&
			                      scene_doc::detail::readStr(o, "texture", texture) && readVec(o, "position", pos) && readNum(o, "yaw", yaw) && readNum(o, "scale", scale);
			const bool believable = readable && !mesh.empty() && detail::finite3(pos.x, pos.y, pos.z) && std::fabs(pos.x) < 500 && std::fabs(pos.y) < 500 && std::fabs(pos.z) < 500 &&
			                        std::isfinite(yaw) && scale > 0.001 && scale < 100.0;
			std::string meshPath, texturePath;
			if (!believable || !detail::findFile(baseFolder, mesh, meshPath) || (!texture.empty() && !detail::findFile(baseFolder, texture, texturePath))) {
				out.notes.push_back("A piece of furniture in the room file (" + name + ") could not be used and was left out.");
				continue;
			}
			scene_doc::Object m = scene_doc::makeObject(scene_doc::ShapeKind::Mesh, name);
			m.meshFile = meshPath;
			m.material.imageFile = texturePath;
			m.meshScale = scale;
			m.position = pos;
			m.rotation = {0.0, yaw, 0.0};
			out.furniture.push_back(m);
		}
	}

	if (const Json* notes = root.find("notes")) {
		if (notes->type == Json::Type::Arr)
			for (const Json& n : notes->a)
				if (n.type == Json::Type::Str) out.notes.push_back(n.s);
	}
	if (const Json* photo = root.find("photo")) {
		std::string source;
		if (scene_doc::detail::readStr(*photo, "fov_source", source) && !source.empty() && source.rfind("assumed", 0) == 0)
			out.notes.push_back("The photo's lens is not known, so a phone camera's field of view was assumed; if the room looks stretched, set the camera's field of view in the Camera section.");
	}
	return true;
}

// How bright a picture looks, for matching a room's light to the photo's: the mean of every channel value (0..1, as displayed) with anything above 0.9 held at 0.9, so a window clipped to
// white does not count for more than a bright wall. `rgb` is 8-bit, `count` values (three per pixel). The helper computes the same number for the photo (room_fit.picture_brightness).
inline double pictureBrightness(const unsigned char* rgb, size_t count) {
	if (count == 0) return 0.0;
	double sum = 0.0;
	for (size_t i = 0; i < count; ++i) sum += std::min(rgb[i] / 255.0, 0.9);
	return sum / static_cast<double>(count);
}

// Multiplies the strength of the room's lights (the factor is relative to what the helper set; the result stays between 0.5 and 500).
inline void scaleLights(Import& room, double factor) {
	for (scene_doc::Object& o : room.lights) o.emissionStrength = std::clamp(o.emissionStrength * factor, 0.5, 500.0);
}

// The factor for the lights at which `measure(factor)`, how bright a draft render of the room looks, reaches `target`. The helper's own estimate of the light level is a guess (the
// right level depends on the view, the windows' position and how white the walls are, by a factor of several); a draft render is not. A picture's brightness rises with the light by
// about the square root, so each step jumps by that law from the last measurement and then follows the slope between the last two (kept between 0.15 and 1.2, so a noisy pair
// cannot send it far). `measure` returns <= 0 when it cannot render: the search then stops with 1 (no change). At most `maxEvaluations` renders; stops within `tolerance` of the target.
template <class Measure>
inline double solveLightFactor(Measure&& measure, double target, int maxEvaluations = 7, double tolerance = 0.04) {
	if (!(target > 0.0)) return 1.0;
	double bestFactor = 1.0, bestError = 1e30;
	double f = 1.0, prevF = 0.0, prevM = 0.0, slope = 0.5;
	for (int i = 0; i < maxEvaluations; ++i) {
		const double m = measure(f);
		if (!(m > 0.0)) return i == 0 ? 1.0 : bestFactor;
		const double error = std::fabs(std::log(m / target));
		if (error < bestError) { bestError = error; bestFactor = f; }
		if (error < std::log1p(tolerance)) break;
		if (prevF > 0.0 && std::fabs(std::log(f / prevF)) > 1e-6 && std::fabs(std::log(m / prevM)) > 1e-6)
			slope = std::clamp(std::log(m / prevM) / std::log(f / prevF), 0.15, 1.2);
		prevF = f;
		prevM = m;
		// a step in log space, at most 8 times up or down, and the factor itself stays within 0.02..300
		const double step = std::clamp(std::log(target / m) / slope, std::log(1.0 / 8.0), std::log(8.0));
		f = std::clamp(f * std::exp(step), 0.02, 300.0);
		if (std::fabs(std::log(f / prevF)) < 1e-4) break;   // pinned at a limit: nothing more to find
	}
	return bestFactor;
}

// Replaces the document's scene with the room: its objects, lights and camera (the render size follows the photo; other settings stay). The title is set when the document has the
// untouched default.
inline void applyTo(const Import& room, scene_doc::Document& doc, const std::string& title) {
	doc.objects.clear();
	doc.lights.clear();
	doc.cameraViews.clear();
	for (const scene_doc::Object& o : room.surfaces) doc.objects.push_back(o);
	for (const scene_doc::Object& o : room.lights) doc.objects.push_back(o);
	for (const scene_doc::Object& o : room.furniture) doc.objects.push_back(o);
	doc.camera = room.camera;
	doc.camera.focusDistance = std::max(1.0, std::sqrt((room.roomMax.x - room.roomMin.x) * (room.roomMax.x - room.roomMin.x) + (room.roomMax.z - room.roomMin.z) * (room.roomMax.z - room.roomMin.z)) / 2.0);
	doc.render.width = room.width;
	doc.render.height = room.height;
	if (!title.empty()) doc.title = title;
}

}  // namespace scene_room
