#pragma once
// scene_showcase.h -- the Scene Builder's showcase scene: a small photo studio that uses what the Builder can do together, written as an ordinary Scene Builder document so it opens for
// editing there. `pbrt_scenes/studio-showcase.pbrt` is exactly showcaseFileText(); a unit test (tests/unit/scene_showcase_tests.cpp) keeps the file and this code in step, and
// `RT_SHOWCASE_OUT=<file> ray_tracer_tests --gtest_filter=SceneShowcaseTest.WriteTheFile` writes it again after a change. Qt-free and std-only.
//
// What is in it: a cyclorama (seamless studio backdrop) lit by the Studio three-light rig and a window-pattern projector; a rounded box of red car paint worn through to copper (a mixed
// material); a block of mossy stone (a ready-made mix); a candle-wax ball (subsurface scattering); a clear glass ball on a tile with a tiled picture (texture repeats); a gold ball.

#include "scene_document.h"

#include <string>

namespace scene_doc {

inline Document makeShowcaseScene() {
	Document d;
	d.title = "Studio Showcase";

	Object backdrop = makeObject(ShapeKind::Cyclorama, "Backdrop");
	backdrop.size = {30.0, 9.0, 18.0};   // wide and deep enough that the camera sees only backdrop: the wall at -6, the floor running on under the lens
	backdrop.radius = 3.0;
	backdrop.position = {0.0, 4.5, 3.0};
	backdrop.material.color = {0.80, 0.80, 0.78};
	d.objects.push_back(backdrop);

	Object box = makeObject(ShapeKind::RoundedBox, "Worn paint box");
	box.size = {1.6, 1.6, 1.6};
	box.radius = 0.22;
	box.position = {-2.4, 0.8, 0.4};
	box.rotation = {0.0, 22.0, 0.0};
	applyMaterialPreset(box.material, "car-paint");
	Material copper;
	copper.kind = MaterialKind::Conductor;
	copper.color = {0.95, 0.64, 0.54};
	copper.roughness = 0.3;
	box.material.mixWith.assign(1, copper);
	box.material.mixAmount = 0.3;
	d.objects.push_back(box);

	Object wax = makeObject(ShapeKind::Sphere, "Candle wax ball");
	wax.radius = 0.9;
	wax.position = {-0.1, 0.9, -0.8};
	applyMaterialPreset(wax.material, "candle-wax");
	d.objects.push_back(wax);

	Object tile = makeObject(ShapeKind::Box, "Tile");
	tile.size = {2.6, 0.12, 2.6};
	tile.position = {2.5, 0.06, 1.1};
	tile.material.imageFile = "uv-checker.bmp";
	tile.material.uvScaleU = 2.0;
	tile.material.uvScaleV = 2.0;
	d.objects.push_back(tile);

	Object glass = makeObject(ShapeKind::Sphere, "Glass ball");
	glass.radius = 0.75;
	glass.position = {2.5, 0.87, 1.1};
	applyMaterialPreset(glass.material, "glass");
	d.objects.push_back(glass);

	Object gold = makeObject(ShapeKind::Sphere, "Gold ball");
	gold.radius = 0.4;
	gold.position = {-0.7, 0.4, 2.3};
	applyMaterialPreset(gold.material, "gold");
	d.objects.push_back(gold);

	Object moss = makeObject(ShapeKind::RoundedBox, "Mossy block");   // a ready-made mix (the Mossy stone preset): stone with moss growing on it
	moss.size = {0.9, 0.9, 0.9};
	moss.radius = 0.2;
	moss.position = {-3.4, 0.45, 2.7};
	moss.rotation = {0.0, 35.0, 0.0};
	applyMixPreset(moss.material, "mossy-stone");
	d.objects.push_back(moss);

	d.camera.position = {0.0, 2.5, 9.6};
	d.camera.target = {0.0, 1.0, 0.0};
	d.camera.fov = 38.0;
	d.render.width = 800;
	d.render.height = 450;
	d.render.samples = 256;
	d.render.maxDepth = 8;

	lighting::apply(d, lighting::Preset::Studio);
	for (Light& l : d.lights)
		if (l.name == "Studio backdrop") l.intensity = 0.4;   // (a brighter room than the rig's dark backdrop, so the floor round the spotlights is not black)
	Light window;   // a window's panes thrown on the backdrop from the camera's left
	window.name = "Window light";
	window.kind = LightKind::Projector;
	window.position = {-5.2, 3.4, 4.6};
	window.target = {1.0, 2.2, -5.0};
	window.fov = 34.0;
	window.intensity = 170.0;
	window.imageFile = "window-slide.bmp";
	d.lights.push_back(window);
	return d;
}

// The text of pbrt_scenes/studio-showcase.pbrt: the Scene Builder's own file with the scene's tags after its title, which is how the scene lists say what it is.
inline std::string showcaseFileText() {
	const std::string text = toPbrt(makeShowcaseScene());
	const std::size_t firstLine = text.find('\n');
	const std::string tags =
	    "# @rt-category Materials\n"
	    "# @rt-description A small photo studio built in the Scene Builder: a cyclorama backdrop lit by a three-light studio rig and a window-pattern projector, a rounded box of red car\n"
	    "# @rt-description paint worn through to copper (a mixed material), a block of mossy stone (a ready-made mix), a candle-wax ball (subsurface scattering), a glass ball on a tiled picture and a gold ball. Open it in the Scene Builder to change anything.\n"
	    "# @rt-performance Medium\n";
	return text.substr(0, firstLine + 1) + tags + text.substr(firstLine + 1);
}

}  // namespace scene_doc
