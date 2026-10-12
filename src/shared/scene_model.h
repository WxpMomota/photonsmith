#pragma once
// scene_model.h -- the document's data types (camera, materials, objects, lights, render settings) and their names in files.
//
// Part of the Scene Builder's document (scene_document.h includes all of it); header-only, standard library only.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "scene_shapes.h"

namespace scene_doc {

struct Float3 {
	double x = 0, y = 0, z = 0;
};

struct Rgb {
	double r = 0.8, g = 0.8, b = 0.8;
};

// Subsurface: light enters, scatters about inside and leaves somewhere else (skin, wax, milk, marble, jade). `color` is the surface's overall colour (pbrt's reflectance: what
// a thick slab of it reflects) and `scatterDistance` how far light travels inside before it scatters, per colour channel in scene units: longer for a colour means that colour
// glows further from where it entered (skin's red does). The same size object wants a different distance: about a tenth to a half of the object's size looks translucent.
enum class MaterialKind { Diffuse, Conductor, Dielectric, CoatedDiffuse, DiffuseTransmission, Subsurface };

// A procedural pattern for the colour of a Diffuse or CoatedDiffuse surface (with no picture and no checker): the renderer's own marble (pale stone with veins) and fbm noise
// (soft grey clouds); both give their own colours.
enum class Pattern { None, Marble, Noise };

// What a bump slot's picture is: a grayscale height map (light is high; the surface is shaded as if it had that relief), or a tangent-space normal map (RGB: the shading normal).
enum class BumpKind { Height, Normal };

struct Material {
	MaterialKind kind = MaterialKind::Diffuse;
	Rgb color{0.8, 0.8, 0.8};          // diffuse / coated reflectance, conductor reflectance; unused by Dielectric
	bool checker = false;              // Diffuse only: a checkerboard of `color` and `color2`
	Rgb color2{0.2, 0.2, 0.2};
	double checkerCount = 8.0;         // checks across the object (its width), for Quad / Disk / Sphere
	double roughness = 0.0;            // Conductor, Dielectric, CoatedDiffuse: 0 = smooth
	double ior = 1.5;                  // Dielectric and CoatedDiffuse
	Rgb transmittance{0.5, 0.5, 0.5};  // DiffuseTransmission (its reflectance is `color`)
	std::string imageFile;             // Diffuse / CoatedDiffuse: a picture used as the surface colour (replaces `color` and the checker)
	Pattern pattern = Pattern::None;   // Diffuse / CoatedDiffuse: marble or noise instead of `color` (not with a picture or a checker)
	double patternScale = 4.0;         // Marble: how many veins (the renderer's marble "scale")
	double patternVariation = 0.3;     // Marble: how much the veins wander
	Rgb scatterDistance{0.10, 0.07, 0.05};   // Subsurface: mean free path per channel, in scene units
	std::string bumpFile;              // Diffuse / CoatedDiffuse / Conductor: a picture that bumps the shading (empty: none)
	BumpKind bumpKind = BumpKind::Height;
	double bumpStrength = 0.05;        // Height map: how tall the relief is (the picture's 0..1 times this, in scene units)
	// Where the surface's texture coordinates put a picture, a checker and a bump map: the texture is repeated `uvScale` times across (U) and up (V) the surface and moved by
	// `uvOffset` repeats, so a picture of bricks can tile a wall and be shifted to start on a brick. (u, v) become (u * scaleU - offsetU, v * scaleV - offsetV); the texture's
	// edges repeat. Only the shapes that write their own texture coordinates (a box, a quad, the ready-made shapes) take it; a sphere, disk, cylinder, cone or mesh file has the
	// renderer's own.
	double uvScaleU = 1.0, uvScaleV = 1.0;
	double uvOffsetU = 0.0, uvOffsetV = 0.0;
	// A mix (pbrt's "mix" material): when `mixWith` holds a material, the surface is this material at some points and that one at the others, `mixAmount` being the fraction of points
	// that are the second (0 is all this one, 1 all the other), chosen afresh at each point of the surface so the two grade into one another. The second material is a plain one:
	// its picture, pattern, checker and bump fields are not used, and it is not itself a mix. (A vector so that Material can hold a Material; it never has more than one.)
	std::vector<Material> mixWith;
	double mixAmount = 0.5;
};

inline bool isMix(const Material& m) { return !m.mixWith.empty(); }

// Whether the texture coordinates are placed in any way other than the plain ones, and whether the material has a texture (picture, checker or bump map) to place.
inline bool uvPlaced(const Material& m) { return m.uvScaleU != 1.0 || m.uvScaleV != 1.0 || m.uvOffsetU != 0.0 || m.uvOffsetV != 0.0; }
inline bool hasUvTexture(const Material& m) { return !m.imageFile.empty() || m.checker || !m.bumpFile.empty(); }

// The colour a material looks like on average, for drawing it in the views: a mix is the blend of its two.
inline Rgb displayColor(const Material& m) {
	if (!isMix(m)) return m.color;
	const double t = std::clamp(m.mixAmount, 0.0, 1.0);
	const Rgb& o = m.mixWith[0].color;
	return {m.color.r * (1.0 - t) + o.r * t, m.color.g * (1.0 - t) + o.g * t, m.color.b * (1.0 - t) + o.b * t};
}

// A volume is an object that is not a surface but a region of participating medium: fog, smoke or a cloud, inside a sphere (or, for fog and smoke, a cylinder). A surface's material,
// picture and light do not apply to it; the renderer's media do (pbrt `MakeNamedMedium` with a `MediumInterface` on the shape). The strength of the medium is `density`, how much
// of the light passing through one unit of it is scattered or absorbed; `color` is the fraction of that which is scattered, per colour channel, so white fog only scatters, a grey
// smoke also absorbs (and looks dark), and a coloured one tints what it scatters.
enum class VolumeKind { None, Fog, Smoke, Cloud };

struct Volume {
	VolumeKind kind = VolumeKind::None;
	double density = 0.4;              // extinction per unit length (a cloud: the strength of its own lumpy density, which is 0 to 1 inside it)
	Rgb color{1.0, 1.0, 1.0};          // Fog, Smoke: scattering albedo per channel: 1 scatters everything, 0 absorbs everything (a Cloud only scatters, and has no tint)
	double anisotropy = 0.0;           // the phase function's g: 0 scatters evenly, towards 1 forwards (-1: backwards)
	double wispiness = 1.0;            // Cloud: how torn and feathery its edges are, 0 to 2
	double frequency = 4.0;            // Cloud: how many lumps across (higher is finer detail)
};

enum class ShapeKind { Sphere, Box, Quad, Disk, Cylinder, Cone, Mesh, Pyramid, Wedge, Stairs, Torus, Capsule, Dome, Tube, RoundedBox, Cyclorama, Terrain };

// Every shape, in the order the GUI lists them.
inline const std::vector<ShapeKind>& allShapeKinds() {
	static const std::vector<ShapeKind> all = {ShapeKind::Sphere,  ShapeKind::Box,   ShapeKind::Quad,    ShapeKind::Disk,  ShapeKind::Cylinder,
	                                           ShapeKind::Cone,    ShapeKind::Pyramid, ShapeKind::Wedge, ShapeKind::Stairs, ShapeKind::Torus,
	                                           ShapeKind::Capsule, ShapeKind::Dome,  ShapeKind::Tube,    ShapeKind::RoundedBox, ShapeKind::Cyclorama, ShapeKind::Terrain, ShapeKind::Mesh};
	return all;
}
// A shape that is written as a generated triangle mesh (scene_shapes.h) rather than one of pbrt's own shapes.
inline bool isGeneratedShape(ShapeKind k) {
	return k == ShapeKind::Pyramid || k == ShapeKind::Wedge || k == ShapeKind::Stairs || k == ShapeKind::Torus || k == ShapeKind::Capsule ||
	       k == ShapeKind::Dome || k == ShapeKind::Tube || k == ShapeKind::RoundedBox || k == ShapeKind::Cyclorama || k == ShapeKind::Terrain;
}

struct Object {
	std::string name = "Object";
	ShapeKind shape = ShapeKind::Sphere;
	Float3 position{};
	Float3 rotation{};                   // degrees, about world X, Y, Z (applied in that order)
	double radius = 1.0;               // Sphere, Disk, Cylinder, Cone, Capsule, Dome; Torus: the ring; Tube: outside; RoundedBox: the roundness of the edges; Cyclorama: the radius of the curve;
	                                   // Terrain: the roughness, 0 to 1
	double radius2 = 0.25;             // Torus: the tube's radius; Tube: the hole's radius; Terrain: how many hills the biggest features have across
	double height = 1.0;               // Cylinder, Cone, Pyramid, Capsule, Tube
	Float3 size{1.0, 1.0, 1.0};          // Box, Wedge, Stairs, RoundedBox, Cyclorama, Terrain (x, y, z); Quad (x and z); Pyramid (x and z, the base)
	int steps = 5;                     // Stairs; Terrain: the grid cells across each side
	int seed = 1;                      // Terrain: which ground the noise makes
	double meshScale = 1.0;            // Mesh
	std::string meshFile;              // Mesh: a .ply file
	Material material;
	Volume volume;                     // kind != None: this object is fog, smoke or a cloud (a Sphere, or a Cylinder for fog and smoke) and has no surface material or light
	bool emissive = false;             // an area light
	Rgb emission{1.0, 1.0, 1.0};
	double emissionStrength = 10.0;    // radiance = emission * strength
	bool twoSided = false;             // emits from both sides
	std::string group;                 // objects with the same non-empty group are picked, moved, copied and deleted together in the Scene Builder (scene_selection.h)
	bool hidden = false;               // not drawn in the Scene Builder's views (it still renders): to see what is behind it while editing (scene_arrange.h)
	bool locked = false;               // cannot be picked or moved by clicking and dragging in the views, so it is not moved by accident (it can still be picked in the list)
};

// The radius of a sphere around the object's position that holds all of it, whatever its rotation (a mesh, whose size the document does not know, counts as its
// scale). Used for the scene's size (scene_pbrt_writer.h documentSize) and for spacing copies (scene_array.h).
inline double boundingRadius(const Object& o) {
	double r = 0.5;
	switch (o.shape) {
		case ShapeKind::Sphere: r = o.radius; break;
		case ShapeKind::Box:
		case ShapeKind::Wedge:
		case ShapeKind::RoundedBox:
		case ShapeKind::Cyclorama:
		case ShapeKind::Terrain:
		case ShapeKind::Stairs: r = 0.5 * std::sqrt(o.size.x * o.size.x + o.size.y * o.size.y + o.size.z * o.size.z); break;
		case ShapeKind::Quad: r = 0.5 * std::sqrt(o.size.x * o.size.x + o.size.z * o.size.z); break;
		case ShapeKind::Pyramid: r = 0.5 * std::sqrt(o.size.x * o.size.x + o.height * o.height + o.size.z * o.size.z); break;
		case ShapeKind::Mesh: r = std::fabs(o.meshScale); break;
		default: r = std::sqrt((o.radius + o.radius2) * (o.radius + o.radius2) + o.height * o.height); break;   // disk, cylinder, cone, capsule, dome, torus, tube
	}
	return std::fabs(r);
}

enum class LightKind { Point, Spot, Distant, Infinite, Projector };   // a Projector is a slide projector (pbrt's "projection" light): the picture in `imageFile` shines out through a frustum of `fov` degrees

// The Sun & sky option of an Infinite light (scene_sky.h): when `physicalSky` is on, the light's picture is a generated clear sky for this sun, and the scene's
// Sun (a Distant light) follows it. Angles in degrees; azimuth 0 is +X, 90 is -Z, 180 is -X, 270 is +Z.
struct SkyParams {
	double sunElevation = 35.0;   // above the horizon
	double sunAzimuth = 250.0;
	double turbidity = 3.0;       // haze: 1.7 very clear, 3 ordinary, 6+ hazy
	double groundAlbedo = 0.3;    // the ground the sky light bounces off
};

struct Light {
	std::string name = "Light";
	LightKind kind = LightKind::Point;
	Float3 position{0.0, 5.0, 0.0};      // Point, Spot (and Distant: where it shines from)
	Float3 target{0.0, 0.0, 0.0};        // Spot, Distant: what it points at
	Rgb color{1.0, 1.0, 1.0};
	double intensity = 50.0;           // Point/Spot: radiant intensity; Distant/Infinite: radiance
	double coneAngle = 30.0;           // Spot, degrees
	double coneDelta = 5.0;            // Spot, degrees of soft edge
	std::string imageFile;             // Infinite: an equirectangular image (empty: a constant colour)
	bool physicalSky = false;          // Infinite: imageFile is the generated sky of `sky` (scene_sky.h)
	double fov = 40.0;                 // Projector: the angle, in degrees, the slide spans along its shorter side
	double skySpin = 0.0;              // Infinite with an image: degrees the picture is turned about the vertical axis (counter-clockwise seen from above)
	SkyParams sky;
	bool hidden = false;               // as Object::hidden
	bool locked = false;               // as Object::locked
};

struct Camera {
	Float3 position{0.0, 2.5, 8.0};
	Float3 target{0.0, 1.0, 0.0};
	Float3 up{0.0, 1.0, 0.0};
	double fov = 40.0;                 // vertical field of view, degrees
	double lensRadius = 0.0;           // 0 = a pinhole; > 0 blurs what is not at focusDistance
	double focusDistance = 8.0;
	double sensorWidthMm = 36.0;       // the film's width in millimetres, for talking about the lens as a focal length (scene_camera_lens.h); the renderer reads only `fov`
};

// A camera the user saved under a name, to come back to (scene_camera_views.h).
struct CameraView {
	std::string name = "View";
	Camera camera;
	double hold = 0.0;   // seconds a flythrough stays at this view (a pause); 0: it flies on
};

struct RenderSettings {
	int width = 800;
	int height = 600;
	int samples = 64;
	int maxDepth = 8;
};

struct Document {
	std::string title = "Untitled scene";
	Camera camera;
	RenderSettings render;
	std::vector<Object> objects;
	std::vector<Light> lights;
	std::vector<CameraView> cameraViews;   // saved cameras, in the order they were saved
};

struct Problem {
	enum class Severity { Warning, Error } severity = Severity::Warning;
	std::string message;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// Names <-> enums (also what the JSON stores)
// ---------------------------------------------------------------------------------------------------------------------------------
inline const char* toString(MaterialKind k) {
	switch (k) {
		case MaterialKind::Diffuse: return "diffuse";
		case MaterialKind::Conductor: return "conductor";
		case MaterialKind::Dielectric: return "dielectric";
		case MaterialKind::CoatedDiffuse: return "coateddiffuse";
		case MaterialKind::DiffuseTransmission: return "diffusetransmission";
		case MaterialKind::Subsurface: return "subsurface";
	}
	return "diffuse";
}
inline const char* toString(Pattern p) {
	switch (p) {
		case Pattern::None: return "none";
		case Pattern::Marble: return "marble";
		case Pattern::Noise: return "noise";
	}
	return "none";
}
inline bool patternFromString(const std::string& s, Pattern& out) {
	for (Pattern p : {Pattern::None, Pattern::Marble, Pattern::Noise})
		if (s == toString(p)) { out = p; return true; }
	return false;
}
inline const char* toString(BumpKind k) { return k == BumpKind::Normal ? "normal" : "height"; }
inline bool bumpKindFromString(const std::string& s, BumpKind& out) {
	if (s == "height") { out = BumpKind::Height; return true; }
	if (s == "normal") { out = BumpKind::Normal; return true; }
	return false;
}
inline bool materialKindFromString(const std::string& s, MaterialKind& out) {
	for (MaterialKind k : {MaterialKind::Diffuse, MaterialKind::Conductor, MaterialKind::Dielectric, MaterialKind::CoatedDiffuse,
	                       MaterialKind::DiffuseTransmission, MaterialKind::Subsurface})
		if (s == toString(k)) { out = k; return true; }
	return false;
}
inline const char* toString(VolumeKind k) {
	switch (k) {
		case VolumeKind::None: return "none";
		case VolumeKind::Fog: return "fog";
		case VolumeKind::Smoke: return "smoke";
		case VolumeKind::Cloud: return "cloud";
	}
	return "none";
}
inline bool volumeKindFromString(const std::string& s, VolumeKind& out) {
	for (VolumeKind k : {VolumeKind::None, VolumeKind::Fog, VolumeKind::Smoke, VolumeKind::Cloud})
		if (s == toString(k)) { out = k; return true; }
	return false;
}
inline const char* toString(ShapeKind k) {
	switch (k) {
		case ShapeKind::Sphere: return "sphere";
		case ShapeKind::Box: return "box";
		case ShapeKind::Quad: return "quad";
		case ShapeKind::Disk: return "disk";
		case ShapeKind::Cylinder: return "cylinder";
		case ShapeKind::Cone: return "cone";
		case ShapeKind::Mesh: return "mesh";
		case ShapeKind::Pyramid: return "pyramid";
		case ShapeKind::Wedge: return "wedge";
		case ShapeKind::Stairs: return "stairs";
		case ShapeKind::Torus: return "torus";
		case ShapeKind::Capsule: return "capsule";
		case ShapeKind::Dome: return "dome";
		case ShapeKind::Tube: return "tube";
		case ShapeKind::RoundedBox: return "roundedbox";
		case ShapeKind::Cyclorama: return "cyclorama";
		case ShapeKind::Terrain: return "terrain";
	}
	return "sphere";
}
inline bool shapeKindFromString(const std::string& s, ShapeKind& out) {
	for (ShapeKind k : allShapeKinds())
		if (s == toString(k)) { out = k; return true; }
	return false;
}
inline const char* toString(LightKind k) {
	switch (k) {
		case LightKind::Point: return "point";
		case LightKind::Spot: return "spot";
		case LightKind::Distant: return "distant";
		case LightKind::Infinite: return "infinite";
		case LightKind::Projector: return "projector";
	}
	return "point";
}
inline bool lightKindFromString(const std::string& s, LightKind& out) {
	for (LightKind k : {LightKind::Point, LightKind::Spot, LightKind::Distant, LightKind::Infinite, LightKind::Projector})
		if (s == toString(k)) { out = k; return true; }
	return false;
}

// Whether the light shines towards a point of its own (`target`): a spotlight, a projector, and a sun (which shines from `position` towards `target`).
inline bool aimsAtTarget(const Light& l) { return l.kind == LightKind::Spot || l.kind == LightKind::Distant || l.kind == LightKind::Projector; }

}  // namespace scene_doc
