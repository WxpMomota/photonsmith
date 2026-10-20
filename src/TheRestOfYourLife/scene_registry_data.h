#pragma once
// scene_registry_data.h -- the compiled-in scene data table, split out of
// scene_registry.h to keep that file to the wiring functions
// (build_curated_pbrt_scene_descriptor/wire_pbrt_backed_scene/append/paths)
// around it. This file is a single function, get_builtin_scene_registry(),
// whose body is one large SceneDescriptor vector literal - not control
// flow - so the split is a pure textual move with no logic change.
// #include'd directly from scene_registry.h at the point this content used
// to live; not meant to be included standalone (relies on SceneDescriptor,
// SceneCategories:: and
// pbrt_scene_registry::build_curated_pbrt_scene_descriptor(), all declared
// earlier in scene_registry.h).

// -----------------------------------------------------------------------
// Scenes loaded from .pbrt files (see append_pbrt_scenes below)
// -----------------------------------------------------------------------

// The scenes compiled into this binary. Everything the full registry holds
// beyond these came from a .pbrt file found on disk at startup.
// Part 1 of 7 of the compiled-in scene table (the order of the parts is the order of the registry).
inline std::vector<SceneDescriptor> builtin_scenes_part1() {
    return {
        // CameraMode::UserControlled is passed explicitly: the Cornell-box camera is user-controlled, and the default
        // (Fixed) would silently disable --cam_x/y/z and the GUI's camera controls for this scene.
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "A1", 169, "Cornell Box", "cornell-box", SceneCategories::Basics,
            "Classic Cornell box with glass sphere and aluminum box",
            "Fast", "cornell-box-native.pbrt", CameraMode::UserControlled),
        // A2's grid layout is a deterministic std::mt19937(42) sequence (see pbrt_scenes/bouncing-spheres.pbrt's
        // header for why); the per-sphere motion blur comes from ActiveTransform on each sphere.
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "A2", 1, "Bouncing Spheres", "bouncing-spheres", SceneCategories::Basics,
            "Random spheres with checker ground (In One Weekend final)",
            "Fast", "bouncing-spheres.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "A3", 185, "Checkered Spheres", "checkered-spheres", SceneCategories::Basics,
            "Two spheres with procedural checker texture",
            "Fast", "checkered-spheres.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "A4", 190, "Earth", "earth", SceneCategories::Basics,
            "Globe with earth texture mapping (requires earthmap.jpg)",
            "Fast", "earth-globe.pbrt", CameraMode::Fixed,
            /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "A5", 4, "Perlin Spheres", "perlin-spheres", SceneCategories::Basics,
            "Spheres with Perlin noise marble texture",
            "Fast", "perlin-spheres.pbrt", CameraMode::Fixed),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "A6", 168, "Colored Quads", "colored-quads", SceneCategories::Basics,
            "Five flat coloured panels at different angles, lit by one glowing panel: a check that every orientation catches light correctly.",
            "Fast", "colored-quads.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "A7", 6, "Simple Light", "simple-light", SceneCategories::Basics,
            "Perlin spheres with emissive light sources",
            "Fast", "simple-light.pbrt", CameraMode::Fixed),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "A8", 191, "Cornell Smoke", "cornell-smoke", SceneCategories::Basics,
            "The Cornell room with two boxes of coloured fog instead of solid boxes: fog confined to a shape scatters light rather than blocking it.",
            "Medium", "cornell-smoke.pbrt", CameraMode::UserControlled),
        // A9's noise-textured sphere uses pbrt's "fbm" texture in place of the book's sin+turbulence formula
        // (see pbrt_scenes/final-scene.pbrt's header).
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "A9", 8, "Final Scene", "final-scene", SceneCategories::Basics,
            "The cover scene of Ray Tracing: The Next Week: rolling box terrain, glass, metal, a fog-filled glass ball, an Earth-textured sphere, a marble sphere and a cluster of small white spheres.",
            "Slow", "final-scene.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B1", 9, "Rough Metal Spheres", "rough-metal-spheres", SceneCategories::Materials,
            "Five GGX spheres roughness 0.05 to 0.8 -- showcases microfacet BRDF",
            "Fast", "rough-metal-spheres.pbrt", CameraMode::Fixed),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B2", 10, "Cornell Rough Metal", "cornell-rough-metal", SceneCategories::Materials,
            "Cornell box with rough aluminum box and rough gold sphere",
            "Medium", "cornell-rough-metal.pbrt", CameraMode::UserControlled),
        // CameraMode::UserControlled is passed explicitly: the Cornell-box camera is user-controlled, and the default
        // (Fixed) would silently disable --cam_x/y/z and the GUI's camera controls for this scene.
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B3", 171, "Cornell Rough Glass", "cornell-rough-glass", SceneCategories::Materials,
            "Cornell box with a GGX rough-dielectric sphere (pbrt-v4 RoughDielectricBxDF)",
            "Fast", "cornell-rough-glass.pbrt", CameraMode::UserControlled),
        // CameraMode::UserControlled is passed explicitly: the Cornell-box camera is user-controlled, and the default
        // (Fixed) would silently disable --cam_x/y/z and the GUI's camera controls for this scene.
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B4", 170, "Cornell Conductor", "cornell-conductor", SceneCategories::Materials,
            "Cornell box with polished gold sphere and aluminium box using GGX VNDF + complex Fresnel (pbrt-v4 ConductorBxDF)",
            "Fast", "cornell-conductor.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B5", 172, "Cornell Coated Diffuse", "cornell-coated-diffuse", SceneCategories::Materials,
            "Cornell box with blue coated-diffuse sphere and red coated-diffuse box (pbrt-v4 CoatedDiffuseBxDF)",
            "Fast", "cornell-coated-diffuse.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B6", 173, "Cornell Thin Glass", "cornell-thin-glass", SceneCategories::Materials,
            "Cornell box with a vertical thin-glass panel, analytic multi-bounce Fresnel (pbrt-v4 ThinDielectricBxDF)",
            "Fast", "cornell-thin-glass.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B7", 174, "Cornell Coated Conductor", "cornell-coated-conductor", SceneCategories::Materials,
            "Cornell box with lacquered-gold sphere and lacquered-copper box (pbrt-v4 CoatedConductorBxDF)",
            "Medium", "cornell-coated-conductor.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B8", 175, "Cornell Wax Slab", "cornell-wax-slab", SceneCategories::Materials,
            "Cornell box with a wax sphere that diffusely reflects and transmits light (pbrt-v4 DiffuseTransmissionBxDF)",
            "Fast", "cornell-wax-slab.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B9", 17, "Cornell Crystal", "cornell-crystal", SceneCategories::Materials,
            "Cornell box with a crystal sphere using Fresnel-weighted diffuse reflection (pbrt-v4 NormalizedFresnelBxDF)",
            "Fast", "cornell-crystal.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B10", 18, "Principled Showcase", "principled-showcase", SceneCategories::Materials,
            "Row of spheres from matte plastic to metallic with clearcoat (pbrt-v4 PrincipledBxDF)",
            "Fast", "principled-showcase.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B11", 192, "Hair Fibers", "hair-fibers", SceneCategories::Materials,
            "Sphere cluster with hair/fur fiber scattering (pbrt-v4 HairBxDF)",
            "Fast", "hair-fibers-scene.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B12", 20, "Normal Mapped Cornell", "normal-mapped-cornell", SceneCategories::Materials,
            "Cornell box with procedural bump-mapped back wall and normal-mapped sphere (pbrt-v4 NormalMap/BumpMap)",
            "Fast", "normal-mapped-cornell.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B13", 193, "Subsurface Slab", "subsurface-slab", SceneCategories::Materials,
            "Cornell box with translucent wax slab and jade sphere using subsurface-like scattering",
            "Medium", "subsurface-slab.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "D1", 22, "Depth of Field", "depth-of-field", SceneCategories::Cameras,
            "Row of spheres with defocus blur showing depth-of-field from the thin-lens camera model",
            "Fast", "depth-of-field-spheres.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "F1", 200, "Bilinear Patch", "bilinear-patch", SceneCategories::Geometry,
            "Cornell box with curved bilinear patch saddle surface (pbrt-v4 BilinearPatch shape)",
            "Fast", "bilinear-patch-scene.pbrt", CameraMode::UserControlled),
    };
}

// Part 2 of 7 of the compiled-in scene table (the order of the parts is the order of the registry).
inline std::vector<SceneDescriptor> builtin_scenes_part2() {
    return {
        // ---- pbrt-v4 light / camera / medium showcase ----
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C1", 203, "HDRI Sky", "hdri-sky", SceneCategories::Lights,
            "Open scene lit by an image-based sky: a smooth sky gradient stored as a real .exr environment map.",
            "Fast", "hdri-sky-gradient.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C2", 176, "Spotlight Cornell", "spotlight-cornell", SceneCategories::Lights,
            "Cornell box lit by a spotlight with smooth penumbra (pbrt-v4 SpotLight)",
            "Fast", "cornell-spotlight.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C3", 177, "Distant Light Cornell", "distant-light-cornell", SceneCategories::Lights,
            "Cornell box lit by a parallel sun-like distant light (pbrt-v4 DistantLight)",
            "Fast", "cornell-distant-light.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C4", 178, "Point Light Cornell", "point-light-cornell", SceneCategories::Lights,
            "Cornell box lit by a single overhead point light with 1/r^2 falloff (pbrt-v4 PointLight)",
            "Fast", "cornell-point-light.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C5", 179, "Goniometric Light", "goniometric-light", SceneCategories::Lights,
            "Cornell box lit by a goniometric (IES-profile) point light (pbrt-v4 GoniometricLight)",
            "Fast", "cornell-goniometric.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C6", 180, "Projection Light", "projection-light", SceneCategories::Lights,
            "Cornell box with a slide-projector beam casting a checkerboard pattern (pbrt-v4 ProjectionLight)",
            "Fast", "cornell-projection.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "E1", 197, "Homogeneous Medium", "homogeneous-medium", SceneCategories::Volumes,
            "Cornell box filled with a homogeneous scattering fog (pbrt-v4 HomogeneousMedium / HenyeyGreenstein)",
            "Slow", "homogeneous-medium.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "E2", 198, "Cloud Medium", "cloud-medium", SceneCategories::Volumes,
            "Open scene with a procedural Perlin-noise cloud volume (pbrt-v4 CloudMedium)",
            "Medium", "cloud-medium-scene.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "E3", 199, "Dielectric Medium Showcase", "dielectric-medium-showcase", SceneCategories::Volumes,
            "Three glass spheres containing colored internal fog at varying density - dielectric surface + participating medium combined (pbrt-v4 style)",
            "Fast", "dielectric-medium-showcase.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "E4", 70, "RGB Grid Medium", "rgb-grid-medium", SceneCategories::Volumes,
            "Heterogeneous nebula with an independent per-voxel R/G/B scattering grid (pbrt-v4 RGBGridMedium)",
            "Fast", "rgb-grid-nebula.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "D2", 186, "Orthographic Camera", "orthographic-camera", SceneCategories::Cameras,
            "Geometric showcase rendered with an orthographic (parallel-projection) camera (pbrt-v4 OrthographicCamera)",
            "Fast", "ortho-camera-scene.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "D3", 195, "Spherical Camera", "spherical-camera", SceneCategories::Cameras,
            "360-degree equirectangular panorama from a spherical camera (pbrt-v4 SphericalCamera)",
            "Fast", "spherical-camera-scene.pbrt"),
        // B14's synthetic-gold.bsdf is a synthetic glossy lobe, not a measurement (see the .pbrt file's header).
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B14", 205, "Measured BRDF", "measured-brdf", SceneCategories::Materials,
            "Sphere cluster with a real, importance-sampled measured BRDF (pbrt-v4 MeasuredBxDF) loaded from a synthetic .bsdf tensor file",
            "Fast", "measured-brdf-showroom.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B23", 131, "Glass Prism Dispersion", "glass-prism-dispersion", SceneCategories::Materials,
            "A real glass prism splitting a parallel white light into a visible chromatic fan (CPU --spectral, GPU --wavefront: real continuous spectral integration; GPU-recursive (--gpu, no --wavefront): a simplified 3-representative-wavelength RGB-channel approximation, same qualitative fan, see shade_material()'s inout_rgb_channel comment, optix_device_helpers.h - see dielectric's dispersive constructor, material_simple.h)",
            "Fast", "prism-dispersion.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B24", 136, "Frosted Prism Dispersion", "frosted-prism-dispersion", SceneCategories::Materials,
            "The same glass prism as Glass Prism Dispersion, frosted (rough) instead of smooth: the same colour fan, blurred by the roughness. Needs --spectral on the CPU, or the wavefront GPU renderer; the recursive GPU renderer approximates dispersion with three wavelengths.",
            "Fast", "frosted-prism-dispersion.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C7", 194, "Portal Infinite Light", "portal-infinite-light", SceneCategories::Lights,
            "Room scene with a sky visible through a windowed wall aperture (a flat sky_light behind a geometric hole, NOT the real pbrt-v4 PortalImageInfiniteLight class - see pbrt_scenes/portal-light.pbrt for that)",
            "Fast", "portal-window-room.pbrt", CameraMode::UserControlled),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "D4", 187, "Realistic Camera", "realistic-camera", SceneCategories::Cameras,
            "Spheres rendered through a thin-lens with realistic lens-element bokeh (pbrt-v4 RealisticCamera)",
            "Fast", "realistic-camera-scene.pbrt"),
        // D5-D8: the exact same classic Cornell box as A1 (build_cornell_box /
        // build_cornell_box_lights, backed by src/shared/cornell_box_data.h),
        // rendered by each of D1-D4's camera models in turn. Keeping the scene
        // fixed and only varying the camera makes the actual differences
        // between the four models (defocus blur, parallel projection, 360
        // panorama, real lens bokeh) directly comparable, which D1-D4's own
        // bespoke per-scene geometry doesn't support. D1-D4 are left
        // unchanged - these are additive, not replacements.
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "D5", 181, "Depth of Field (Cornell Box)", "depth-of-field-cornell-box", SceneCategories::Cameras,
            "The classic Cornell box (the same room as Cornell Box and the other Cornell Box camera scenes) with defocus blur from a thin-lens perspective camera.",
            "Fast", "cornell-dof.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "D6", 182, "Orthographic Camera (Cornell Box)", "orthographic-camera-cornell-box", SceneCategories::Cameras,
            "The classic Cornell box (the same room as Cornell Box and the other Cornell Box camera scenes) rendered with a parallel-projection orthographic camera.",
            "Fast", "cornell-orthographic.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "D7", 183, "Spherical Camera (Cornell Box)", "spherical-camera-cornell-box", SceneCategories::Cameras,
            "The classic Cornell box (the same room as Cornell Box and the other Cornell Box camera scenes), seen from its centre as a 360-degree equirectangular panorama.",
            "Fast", "cornell-spherical.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "D8", 184, "Realistic Camera (Cornell Box)", "realistic-camera-cornell-box", SceneCategories::Cameras,
            "The classic Cornell box (the same room as Cornell Box and the other Cornell Box camera scenes) rendered through a multi-element lens for realistic bokeh (pbrt-v4's realistic camera).",
            "Fast", "cornell-realistic.pbrt"),
        // Same Cornell box world as A1/D5-D8 - only the camera differs
        // (keyframed across the exposure instead of static). closes the
        // "no motion blur anywhere" gap from docs/FEATURE_INVENTORY.md -
        // CPU default path tracer (+SPPM), see camera.h's own
        // camera_is_animated comment, AND both GPU backends (see
        // GpuCameraParams::animated, gpu/optix/optix_types.h).
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "D13", 196, "Camera Motion Blur (Cornell Box)", "camera-motion-blur-cornell-box", SceneCategories::Cameras,
            "The classic Cornell box (the same room as Cornell Box and the other Cornell Box camera scenes) with the camera trucking sideways during the exposure, for real camera motion blur. The CPU and both GPU renderers interpolate the same two keyframes.",
            "Fast", "cornell-camera-motion-blur.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "F2", 188, "Triangle Mesh", "triangle-mesh", SceneCategories::Geometry,
            "Procedurally-generated icosahedron showcasing real triangle-mesh geometry (watertight Moller-Trumbore intersection)",
            "Fast", "triangle-mesh-scene.pbrt"),
    };
}

// Part 3 of 7 of the compiled-in scene table (the order of the parts is the order of the registry).
inline std::vector<SceneDescriptor> builtin_scenes_part3() {
    return {
        build_instanced_spheres_descriptor(),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "F4", 189, "Curve Fibers", "curve-fibers", SceneCategories::Geometry,
            "A windswept tuft of real Bezier curve strands. The CPU intersects the true curves; the GPU renders the same 70 strands as tapered tubes, so the tubes can look slightly faceted up close.",
            "Fast", "curve-fibers-scene.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G1", 38, "Stanford Bunny", "stanford-bunny", SceneCategories::Models,
            "Classic Stanford bunny scan (69,451 triangles) in polished bronze, loaded from an external .obj file (requires models/stanford-bunny.obj)",
            "Fast", "mesh-stanford-bunny.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G2", 39, "Stanford Armadillo", "stanford-armadillo", SceneCategories::Models,
            "Stanford armadillo scan (99,976 triangles) in gunmetal, loaded from an external .obj file (requires models/armadillo.obj)",
            "Fast", "mesh-stanford-armadillo.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G3", 40, "Stanford Happy Buddha", "stanford-happy-buddha", SceneCategories::Models,
            "Stanford happy buddha scan (98,601 triangles) in polished gold, loaded from an external .obj file (requires models/happy-buddha.obj)",
            "Fast", "mesh-stanford-happy-buddha.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G4", 41, "Stanford Lucy", "stanford-lucy", SceneCategories::Models,
            "Stanford Lucy angel figure (99,970 triangles) in bright silver, loaded from an external .obj file (requires models/lucy.obj)",
            "Fast", "mesh-stanford-lucy.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G5", 42, "Stanford XYZRGB Dragon", "stanford-xyzrgb-dragon", SceneCategories::Models,
            "The Stanford XYZRGB Dragon (249,882 triangles) in bright silver, from an external .obj file (needs models/xyzrgb_dragon.obj). The camera is pulled back further than for the other meshes because the dragon's lunging pose is much wider than tall.",
            "Fast", "mesh-stanford-dragon.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G6", 43, "Utah Teapot", "utah-teapot", SceneCategories::Models,
            "The classic Utah Teapot (6,320 triangles) in bright silver, loaded from an external .obj file (requires models/teapot.obj)",
            "Fast", "mesh-utah-teapot.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G7", 44, "Spot the Cow", "spot-the-cow", SceneCategories::Models,
            "Keenan Crane's Spot the Cow (5,856 triangles) in bright silver, loaded from an external .obj file (requires models/spot.obj)",
            "Fast", "mesh-spot-cow.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G8", 45, "Suzanne", "suzanne", SceneCategories::Models,
            "Blender's Suzanne monkey-head mascot (968 triangles after fan-triangulating its mostly-quad faces) in bright silver, loaded from an external .obj file (requires models/suzanne.obj). Unlike every other mesh scene, Suzanne is a disembodied head with no neck/shoulders/pedestal, so grounding its chin at y=0 (the shared statue convention) puts its face well above the generic eye-level camera - the camera below is raised and pulled in closer to look at roughly the model's own eye height instead.",
            "Fast", "mesh-suzanne.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G9", 46, "Nefertiti Bust", "nefertiti-bust", SceneCategories::Models,
            "Scanned bust of Nefertiti (99,938 triangles) in bright silver, loaded from an external .obj file (requires models/nefertiti.obj)",
            "Fast", "mesh-nefertiti-bust.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G10", 47, "Horse", "horse", SceneCategories::Models,
            "Classic geometry-processing test horse head/neck bust (96,966 triangles) in bright silver, loaded from an external .obj file (requires models/horse.obj)",
            "Fast", "mesh-horse.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G11", 48, "Cheburashka", "cheburashka", SceneCategories::Models,
            "Beloved cartoon-character bust from Keenan Crane's geometry-processing course (13,334 triangles) in bright silver, loaded from an external .obj file (requires models/cheburashka.obj)",
            "Fast", "mesh-cheburashka.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G12", 49, "Trophy Room", "trophy-room", SceneCategories::Models,
            "Four meshes (Stanford Bunny, Utah Teapot, Suzanne and Spot the Cow) lined up in bronze, chrome, gold and gunmetal. Needs models/stanford-bunny.obj, teapot.obj, suzanne.obj and spot.obj.",
            "Fast", "mesh-trophy-room.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G13", 50, "Glass Dragon", "glass-dragon", SceneCategories::Models,
            "The Stanford XYZRGB Dragon (249,882 triangles) in clear glass (index of refraction 1.5), from an external .obj file (needs models/xyzrgb_dragon.obj). Its glass surface stays noisy at any sample count: refraction through a deeply concave mesh is hard for camera-side sampling, and SPPM only helps diffuse surfaces. SPPM on the CPU does resolve the dragon's caustic on the floor; a clean glass surface would need BDPT or MLT.",
            "Fast", "mesh-glass-dragon.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G14", 51, "Beast", "beast", SceneCategories::Models,
            "Fantasy creature bust (common-3d-test-models) in bronze, loaded from an external .obj file (requires models/beast.obj)",
            "Fast", "mesh-beast.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G15", 52, "VW Beetle", "vw-beetle", SceneCategories::Models,
            "A classic CAD-style Volkswagen Beetle in bright chrome, from an external .obj file (needs models/beetle.obj). The camera is pulled back further than for the other meshes because the car is long.",
            "Fast", "mesh-vw-beetle.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G17", 54, "Bimba", "bimba", SceneCategories::Models,
            "Smooth abstract bust/statue (AIM@SHAPE repository test model) in gold, loaded from an external .obj file (requires models/bimba.obj)",
            "Fast", "mesh-bimba.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G18", 55, "Cow", "cow", SceneCategories::Models,
            "The classic Viewpoint/Alias cow test model (not Spot the Cow) in brass, from an external .obj file (needs models/cow.obj).",
            "Fast", "mesh-cow.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G19", 56, "Fandisk", "fandisk", SceneCategories::Models,
            "Classic CAD mechanical-engineering test model with sharp creases, in gunmetal, loaded from an external .obj file (requires models/fandisk.obj). Camera moved to a three-quarter elevated angle rather than the usual eye-level statue framing - this mesh's proportions are shallow along the default view axis, and a face-on shot showed only a smooth, featureless wedge with none of the sharp creases the model is known for.",
            "Fast", "mesh-fandisk.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G20", 57, "Homer", "homer", SceneCategories::Models,
            "Homer Simpson bust in gold, loaded from an external .obj file (requires models/homer.obj)",
            "Fast", "mesh-homer.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G21", 58, "Igea", "igea", SceneCategories::Models,
            "Classical Italian bust (Igea, Roman goddess of health) in bright silver, loaded from an external .obj file (requires models/igea.obj). An earlier camera here (raised and looking steeply down) was meant to compensate for this scan's upward-tilted face, but actually framed the shiny crown of the skull instead of the face - lowered/pulled back closer to the other mesh scenes' eye-level convention, which shows the face (eyes, nose, tilted-up chin) correctly.",
            "Fast", "mesh-igea.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G22", 59, "Max Planck", "max-planck", SceneCategories::Models,
            "Scanned bust of physicist Max Planck in aged bronze, loaded from an external .obj file (requires models/max-planck.obj). This scan's face points toward -Z, so the camera sits on that side (unlike the other mesh scenes' +Z default) to actually see the face instead of the back of the head.",
            "Fast", "mesh-max-planck.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G23", 60, "Ogre", "ogre", SceneCategories::Models,
            "Fantasy ogre head in dark olive metal, loaded from an external .obj file (requires models/ogre.obj)",
            "Fast", "mesh-ogre.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G24", 61, "Rocker Arm", "rocker-arm", SceneCategories::Models,
            "A mechanical engine part (a rocker arm) in gunmetal, from an external .obj file (needs models/rocker-arm.obj). The flat tops of its two bosses catch a strong mirror-like highlight from the overhead light, as a flat low-roughness surface should.",
            "Fast", "mesh-rocker-arm.pbrt", CameraMode::Fixed, /*requires_files=*/true),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Buildings,
            pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "H1", 62, "Crytek Sponza", "crytek-sponza", SceneCategories::LargeScene,
            "Crytek Sponza (262K triangles) - the classic architectural global-illumination benchmark scene, with real per-face .mtl materials and image textures (curtains, columns, floor) loaded from models/sponza_textures/, lit by an open sky, loaded from an external .obj file (requires models/sponza.obj). First 'whole environment' mesh scene here rather than a single statue -- see build_sponza()'s own comment for the full design rationale.",
            "Medium", "environment-sponza.pbrt", CameraMode::Fixed, /*requires_files=*/true)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Buildings,
            pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "H2", 63, "Amazon Lumberyard Bistro", "amazon-lumberyard-bistro", SceneCategories::LargeScene,
            "Amazon Lumberyard Bistro, exterior (2.84M triangles): a full outdoor street block with several buildings and a plaza, per-face materials and image textures (windows, doors, foliage) from models/bistro_textures/, lit by an open sky. From an external .obj file (needs models/bistro_exterior.obj).",
            "Slow", "environment-bistro-exterior.pbrt", CameraMode::Fixed, /*requires_files=*/true)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::VoxelWorlds,
            pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "H3", 64, "Rungholt", "rungholt", SceneCategories::LargeScene,
            "Rungholt (6.7M triangles): a giant blocky Minecraft-style town with per-face material colours and no image textures, from an external .obj file (needs models/rungholt.obj).",
            "Slow", "environment-rungholt.pbrt", CameraMode::Fixed, /*requires_files=*/true)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "H4", 73, "Fireplace Room", "fireplace-room", SceneCategories::LargeScene,
            "Fireplace Room: a small furnished living room (fireplace, wood floor, framed pictures, a potted plant) with materials and image textures from models/fireplace_room_textures/, lit by an open sky through its windows. From an external .obj file (needs models/fireplace_room.obj).",
            "Medium", "environment-fireplace-room.pbrt", CameraMode::Fixed, /*requires_files=*/true)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Buildings,
            pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "H5", 74, "San Miguel", "san-miguel", SceneCategories::LargeScene,
            "San Miguel (9.9M triangles): a dense Mexican hacienda courtyard, the classic benchmark scene, with tile, wood, fabric and foliage textures from models/san_miguel_textures/, lit by an open sky. From an external .obj file (needs models/san_miguel.obj).",
            "Very Slow", "environment-san-miguel.pbrt", CameraMode::Fixed, /*requires_files=*/true)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Buildings,
            pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "H6", 75, "Sibenik Cathedral", "sibenik-cathedral", SceneCategories::LargeScene,
            "Sibenik Cathedral: a Gothic cathedral interior (vaulted nave, stone columns, a rose window, coloured stained glass) with image textures and bump maps from models/sibenik_cathedral_textures/, lit through its doorway and arches. From an external .obj file (needs models/sibenik_cathedral.obj).",
            "Slow", "environment-sibenik-cathedral.pbrt", CameraMode::Fixed, /*requires_files=*/true)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "H7", 76, "Breakfast Room", "breakfast-room", SceneCategories::LargeScene,
            "Breakfast Room: a cosy dining interior with glassware, table settings and marble and tile textures from models/breakfast_room_textures/, lit by an open sky through its windows. From an external .obj file (needs models/breakfast_room.obj).",
            "Slow", "environment-breakfast-room.pbrt", CameraMode::Fixed, /*requires_files=*/true)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "H8", 77, "Salle de Bain", "salle-de-bain", SceneCategories::LargeScene,
            "Salle de Bain: a tiled bathroom with a mirror, a tub and a real ceiling light fixture that emits light, with image textures from models/salle_de_bain_textures/. From an external .obj file (needs models/salle_de_bain.obj).",
            "Medium", "environment-salle-de-bain.pbrt", CameraMode::Fixed, /*requires_files=*/true)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "H9", 78, "Gallery", "gallery", SceneCategories::LargeScene,
            "Gallery: the Hallwyl Museum picture gallery in Stockholm, an ornate room of framed paintings, chandeliers and a parquet floor, with a texture from models/gallery_textures/, lit by an open sky. From an external .obj file (needs models/gallery.obj).",
            "Medium", "environment-gallery.pbrt", CameraMode::Fixed, /*requires_files=*/true)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::VoxelWorlds,
            pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "H10", 79, "Lost Empire", "lost-empire", SceneCategories::LargeScene,
            "Lost Empire - a large half-buried ancient city exported from a Minecraft world, with temple platforms, staircases, and a lava chamber, with real per-face .mtl materials and an image texture loaded from models/lost_empire_textures/, lit by an open sky, loaded from an external .obj file (requires models/lost_empire.obj). Tenth 'whole environment' mesh scene, and the first at a scale (165 units deep) that suits a long video flythrough -- see build_lost_empire()'s own comment.",
            "Very Slow", "environment-lost-empire.pbrt", CameraMode::Fixed, /*requires_files=*/true)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::VoxelWorlds,
            pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "H11", 80, "Vokselia Spawn", "vokselia-spawn", SceneCategories::LargeScene,
            "Vokselia Spawn - a small floating voxel island, exported from the same Minecraft world as Lost Empire from its spawn point, with a real per-face .mtl material and an image texture loaded from models/vokselia_spawn_textures/, lit by an open sky, loaded from an external .obj file (requires models/vokselia_spawn.obj). Eleventh 'whole environment' mesh scene -- see build_vokselia_spawn()'s own comment.",
            "Fast", "environment-vokselia-spawn.pbrt", CameraMode::Fixed, /*requires_files=*/true)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Buildings,
            pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "H12", 81, "Power Plant", "power-plant", SceneCategories::LargeScene,
            "Power Plant - a complete model of an actual coal-fired power plant (12.76M triangles, 5.98M vertices), the largest scene in this collection by triangle count, with flat per-face .mtl colors (no image textures), lit by an open sky, loaded from an external .obj file (requires models/powerplant.obj). Twelfth 'whole environment' mesh scene, and the first needing a real coordinate rescale rather than raw OBJ units -- see build_power_plant()'s own comment.",
            "Slow", "environment-power-plant.pbrt", CameraMode::Fixed, /*requires_files=*/true)),
    };
}

// Part 4 of 7 of the compiled-in scene table (the order of the parts is the order of the registry).
inline std::vector<SceneDescriptor> builtin_scenes_part4() {
    return {
        // H13/H14: real pbrt-v4-scenes bundles (github.com/mmp/pbrt-v4-scenes),
        // NOT git-tracked (see build_curated_external_pbrt_scene_descriptor()'s
        // own comment, scene_registry.h, for why) - download into
        // pbrt_scenes/contemporary-bathroom/ and pbrt_scenes/barcelona-pavilion/
        // respectively (pbrt_scenes/README.md's "Getting scenes" section) for
        // these to render; both already existed locally as parser-verification
        // fixtures (see the many pbrt_cpu_builder.h/material_pbrt.h/
        // PBRT_SUPPORT.md comments citing them by name) well before either got
        // a curated registry entry of its own. Unlike H1-H12's hand-written
        // OBJ "whole environment" scenes, these load through the real pbrt-v4
        // parser/builder like every other pbrt-backed scene in this registry -
        // camera/lights/materials all come from the file itself, not a
        // hand-transcribed struct.
        //
        // They (and H19/H20 further below) used to pass a curated
        // recommended_exposure of 15-30x, because their Film "iso" (600, 500,
        // 150, 200) was ignored and they rendered near-black. The loader now
        // applies the film's imaging ratio (pbrt_flatten.h,
        // applyFilmImagingRatio) and they match pbrt-v4 at the neutral exposure 1.
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H13", 159, "Contemporary Bathroom", "contemporary-bathroom", SceneCategories::LargeScene,
            "A furnished bathroom interior from the pbrt-v4-scenes collection (not bundled with this repo; see pbrt_scenes/README.md): tile, wood, chrome, glass and fabric, with a blackbody-temperature light fixture.",
            "Very Slow", "contemporary-bathroom/contemporary-bathroom.pbrt")),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Buildings,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H14", 160, "Barcelona Pavilion", "barcelona-pavilion", SceneCategories::LargeScene,
            "Mies van der Rohe's Barcelona Pavilion from the pbrt-v4-scenes collection (not bundled with this repo; see pbrt_scenes/README.md): glass and marble architecture among dense foliage, in its daytime lighting.",
            "Slow", "barcelona-pavilion/pavilion-day.pbrt")),
        // H15-H17: three more real pbrt-v4-scenes bundles, same
        // build_curated_external_pbrt_scene_descriptor() mechanism as H13/H14
        // above - already sitting locally as parser-verification fixtures,
        // now given a proper curated entry. Download into
        // pbrt_scenes/sssdragon/, pbrt_scenes/ganesha/, pbrt_scenes/sportscar/
        // respectively (pbrt_scenes/README.md's "Getting scenes" section) for
        // these to render.
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Objects,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H15", 161, "Subsurface Dragon", "subsurface-dragon", SceneCategories::LargeScene,
            "A real pbrt-v4-scenes bundle (not bundled with this repo - see pbrt_scenes/README.md): a dragon statue rendered as translucent skin-like subsurface scattering material lit by an HDR environment map. Three density variants ship in the same directory (dragon_10/50/250.pbrt, differing only in the subsurface scale parameter, 10 being the most translucent); this entry renders the middle one.",
            "Very Slow", "sssdragon/dragon_50.pbrt")),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Objects,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H16", 162, "Ganesha", "ganesha", SceneCategories::LargeScene,
            "A detailed statue of Ganesha in a marble-like subsurface scattering material, lit by an HDR environment map, from the pbrt-v4-scenes collection (not bundled with this repo; see pbrt_scenes/README.md). A second subsurface showcase beside the Subsurface Dragon.",
            "Slow", "ganesha/ganesha.pbrt")),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Vehicles,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H17", 163, "Sports Car", "sports-car", SceneCategories::LargeScene,
            "A real pbrt-v4-scenes bundle (not bundled with this repo - see pbrt_scenes/README.md): a studio-lit sports car with real-world car-paint (layered coated conductor), glass, chrome, and rubber materials under an HDR sky. Renders the daytime sky-lit variant (sportscar-sky.pbrt); a separate area-lit studio variant also ships in the same directory.",
            "Slow", "sportscar/sportscar-sky.pbrt")),
        // H18: a genuinely NEW download (not a pre-existing local fixture
        // like H13-H17 were) - github.com/mmp/pbrt-v4-scenes' "zero-day"
        // scene, a detailed game-level-style interior (~422MB). It's an
        // animated camera fly-through shipped as 9 separate per-frame
        // .pbrt files (frame25/35/52/85/120/180/210/300/380.pbrt, each
        // Include-ing its own geometry/geometry-fNNN.pbrt subset); this
        // entry renders frame180, a representative mid-sequence interior
        // shot, the same "pick the reasonable middle option" call made for
        // H15's three subsurface-dragon density variants.
        //
        // Measured render cost is a real outlier even among this
        // registry's other "Very Slow" entries: a 400x400x32spp CPU render
        // still hadn't finished after 700s (H13-H17 all finish comparable
        // resolutions in under a minute to a few minutes), while a 100x100
        // render at any sample count finishes in ~45s - so the cost is
        // concentrated in per-scanline shading at this camera position
        // (151 ObjectInstance placements plus many layered `coateddiffuse`
        // materials), not scene loading/BVH build. The .pbrt file's own
        // authored "1024 pixelsamples" (which recommended_spp below is
        // honestly pulled from, like every other curated pbrt entry) would
        // make a full render impractically slow - said so explicitly here
        // rather than silently reusing the same "Very Slow" wording H13-H17
        // use for renders that actually finish quickly.
        pbrt_scene_registry::in_subcategory(SceneSubcategories::LightAndArt,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H18", 164, "Zero Day", "zero-day", SceneCategories::LargeScene,
            "A real pbrt-v4-scenes bundle (not bundled with this repo - see pbrt_scenes/README.md): a highly detailed, game-level-style interior scene (an office/atrium space) shot as an animated camera fly-through. This entry renders one representative frame (frame180) of the 9 that ship in the bundle. Note: this is markedly heavier than this project's other Large Scenes - per-scanline shading cost at this camera position is high (heavy instancing, many layered materials), so treat the file's own recommended sample count as a starting point to scale down from, not a target.",
            "Very Slow", "zero-day/frame180.pbrt")),
        // H19-H21: three more genuinely NEW downloads (same
        // build_curated_external_pbrt_scene_descriptor() mechanism, same
        // "download into pbrt_scenes/<name>/" convention as H18). All
        // three author their .pbrt with Integrator "volpath" rather than
        // the default path integrator - like every other scene here,
        // cpu_render_main always runs the default path tracer regardless
        // (a warning says so at render time); this only matters if the
        // scene also declares a participating medium (crown's gem
        // interior looks like the most likely candidate of the three) -
        // worth checking the render for a plausible look, since a real
        // medium under the wrong integrator can still look reasonable
        // without necessarily matching pbrt-v4's own reference render.
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Objects,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H19", 165, "Crown", "crown", SceneCategories::LargeScene,
            "A jewelled crown rendered with glass and gem dispersion and refraction, from the pbrt-v4-scenes collection (not bundled with this repo; see pbrt_scenes/README.md): a classic pbrt showcase scene.",
            "Slow", "crown/crown.pbrt")),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H20", 166, "Villa", "villa", SceneCategories::LargeScene,
            "A furnished villa, inside and out, from the pbrt-v4-scenes collection (not bundled with this repo; see pbrt_scenes/README.md), in its daylight lighting (villa-daylight.pbrt). A lights-on night variant ships in the same folder.",
            "Very Slow", "villa/villa-daylight.pbrt")),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::LightAndArt,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H21", 167, "Transparent Machines", "transparent-machines", SceneCategories::LargeScene,
            "A real pbrt-v4-scenes bundle (not bundled with this repo - see pbrt_scenes/README.md): detailed mechanical objects (gears, casings) rendered in transparent glass-like materials, shot as an animated camera fly-through. Five frames ship in the bundle (frame542/675/812/888/1266.pbrt); this entry renders a representative middle one (frame812).",
            "Medium", "transparent-machines/frame812.pbrt")),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H22", 206, "Grey and White Room", "grey-white-room", SceneCategories::LargeScene,
            "A calm grey-and-white living room with tall arched windows, a round table and soft daylight (\"The Grey & White Room\" by Wig42, CC BY 3.0), one of Benedikt Bitterli's rendering-resources scenes, run as its pbrt-v4 file. Not bundled with this repo: the GUI downloads it from benedikt-bitterli.me on request (see pbrt_scenes/README.md).",
            "Fast", "bitterli/living-room/scene-v4.pbrt", 3.0, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H23", 207, "Bedroom Interior", "bedroom-interior", SceneCategories::LargeScene,
            "A furnished bedroom with a wardrobe, a made bed and light falling through a large window (\"Bedroom\" by SlykDrako, CC0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Medium", "bitterli/bedroom/scene-v4.pbrt", 1.0, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H24", 208, "Country Kitchen", "country-kitchen", SceneCategories::LargeScene,
            "A country kitchen with a range, copper lamps and a table by a sash window (\"Country Kitchen\" by Jay-Artist, CC BY 3.0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Medium", "bitterli/kitchen/scene-v4.pbrt", 1.0, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H25", 209, "Modern Hall", "modern-hall", SceneCategories::LargeScene,
            "A bright modern hall with a floating wooden staircase, a glass balustrade and polished stone tiles (\"Modern Hall\" by NewSee2l035, CC BY 3.0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Medium", "bitterli/staircase2/scene-v4.pbrt", 1.0, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H26", 210, "Wooden Staircase", "wooden-staircase", SceneCategories::LargeScene,
            "A warm wooden staircase seen through an archway, with panelled walls and a polished floor (\"The Wooden Staircase\" by Wig42, CC BY 3.0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Medium", "bitterli/staircase/scene-v4.pbrt", 1.5, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H27", 211, "White Room", "white-room", SceneCategories::LargeScene,
            "A white living room with sofas and a paper lantern, lit by tall windows (\"The White Room\" by Jay-Artist, CC BY 3.0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Medium", "bitterli/living-room-2/scene-v4.pbrt", 1.0, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H28", 212, "Modern Living Room", "modern-living-room", SceneCategories::LargeScene,
            "A modern living room with a white sofa, pink cushions, a red rug and shelves (\"The Modern Living Room\" by Wig42, CC BY 3.0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Medium", "bitterli/living-room-3/scene-v4.pbrt", 1.0, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Objects,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H29", 213, "Glass of Water", "glass-of-water", SceneCategories::LargeScene,
            "A glass of water with ice cubes and a stream of water pouring in, in cool studio light (\"Glass of Water\" by aXel, CC0): refraction, caustics and rough dielectric surfaces, from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Medium", "bitterli/glass-of-water/scene-v4.pbrt", 0.5, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Vehicles,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H30", 214, "Pontiac GTO", "pontiac-gto", SceneCategories::LargeScene,
            "A 1967 Pontiac GTO in dark green paint on a studio floor (\"Pontiac GTO 67\" by thecali, CC0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Fast", "bitterli/car2/scene-v4.pbrt", 1.0, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Vehicles,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H31", 215, "Vintage Car", "vintage-car", SceneCategories::LargeScene,
            "An old vintage car in blue paint with chrome trim, in a grey studio (\"Old vintage car\" by piopis, CC0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Fast", "bitterli/car/scene-v4.pbrt", 0.6, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Objects,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H32", 216, "Coffee Maker", "coffee-maker", SceneCategories::LargeScene,
            "A retro orange coffee maker with a glass jug, in a dark studio (\"Coffee Maker\" by cekuhnen, CC BY 3.0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Fast", "bitterli/coffee/scene-v4.pbrt", 1.0, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Objects,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H33", 217, "Little Lamp", "little-lamp", SceneCategories::LargeScene,
            "An articulated desk lamp with its bulb on, casting striped shadows over the floor (\"Little Lamp\" by UP3D, CC0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md). A heavy render: glass around a small bright light converges slowly.",
            "Very Slow", "bitterli/lamp/scene-v4.pbrt", 1.0, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Buildings,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H34", 218, "Victorian House", "victorian-house", SceneCategories::LargeScene,
            "A white Victorian-style house with a bay window and a wraparound porch, lit by an open sky (\"Victorian Style House\" by MrChimp2313, CC0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Medium", "bitterli/house/scene-v4.pbrt", 1.0, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Vehicles,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H35", 219, "Spaceship", "spaceship", SceneCategories::LargeScene,
            "A detailed little spaceship with rocket engines and a glass canopy on a dark studio floor (\"4060.b Spaceship\" by thecali, CC0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md). A heavy render: about 15 minutes on the CPU at 400 pixels and 64 samples (the GPU is far faster).",
            "Very Slow", "bitterli/spaceship/scene-v4.pbrt", 0.7, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Interiors,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H36", 220, "Japanese Classroom", "japanese-classroom", SceneCategories::LargeScene,
            "A Japanese classroom with rows of wooden desks and chairs facing a blackboard, in daylight (\"Japanese Classroom\" by NovaZeeke, CC BY 3.0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Medium", "bitterli/classroom/scene-v4.pbrt", 1.0, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Objects,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H37", 221, "Dragon Statue", "dragon-statue", SceneCategories::LargeScene,
            "A dragon with spread wings on a round base, in white (\"Dragon\" by Delatronic, CC BY 3.0), from Benedikt Bitterli's rendering-resources scenes. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Fast", "bitterli/dragon/scene-v4.pbrt", 1.0, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Objects,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H38", 222, "Material Test Ball", "material-test-ball", SceneCategories::LargeScene,
            "A rough copper-coloured material ball with a T-shaped notch on a pedestal, on a checkered floor (CC0, Benedikt Bitterli's rendering-resources scenes): the classic scene for judging a material. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Fast", "bitterli/material-testball/scene-v4.pbrt", 1.0, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Nature,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H39", 223, "Landscape (Laubwerk)", "laubwerk-landscape", SceneCategories::LargeScene,
            "A meadow with birch trees, wildflowers and a footpath, built from 23,241 plant models (3.1 billion triangles through instancing, 24 million stored): 'Landscape' by Jan-Walter Schliep, Burak Kahraman and Timm Dapper of Laubwerk (CC BY 4.0), the cover picture of the third edition of Physically Based Rendering, from the pbrt-v4-scenes collection. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md). The heaviest scene here: a download of 1.4 GB, and over a minute just to load.",
            "Very Slow", "landscape/view-0.pbrt", 0.4, 512)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::Vehicles,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H40", 224, "BMW M6", "bmw-m6", SceneCategories::LargeScene,
            "A white BMW M6 coupe on a dark glossy floor (model by tyrant monkey on Blend Swap, CC0), from the pbrt-v4-scenes collection: car paint, glass and chrome under an environment light. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md). A download of 72 MB.",
            "Slow", "bmw-m6/bmw-m6.pbrt", 0.8, 1024)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::LightAndArt,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H41", 225, "Veach, Ajar", "veach-ajar", SceneCategories::LargeScene,
            "Eric Veach's classic 'Ajar' test in Benedikt Bitterli's version (CC0): a room lit only by the light that falls through a door left ajar, a hard case for a path tracer, with a picture on the wall and glass and metal on the table. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Fast", "bitterli/veach-ajar/scene-v4.pbrt", 0.8, 64)),
        pbrt_scene_registry::in_subcategory(SceneSubcategories::LightAndArt,
            pbrt_scene_registry::build_curated_external_pbrt_scene_descriptor(
            "H42", 226, "Water Caustic", "water-caustic", SceneCategories::LargeScene,
            "Light through a rippling water surface drawing caustics on the walls and floor of a small room (Benedikt Bitterli, CC0). The scene's file asks for photon mapping: choose the SPPM integrator (Render Options, or Apply recommended settings in the scene panel); a path tracer draws these caustics as dark noise, and SPPM has no triangle meshes on the GPU, so it runs on the CPU. Not bundled with this repo: the GUI downloads it on request (see pbrt_scenes/README.md).",
            "Medium", "bitterli/water-caustic/scene-v4.pbrt", 0.4, 200)),

    };
}

// Part 5 of 7 of the compiled-in scene table (the order of the parts is the order of the registry).
inline std::vector<SceneDescriptor> builtin_scenes_part5() {
    return {
        // ---------------------------------------------------------------
        // Education (I1-I6): curated demos of the Render Options tab's own
        // controls (Sampler, Spectral rendering, Exposure, Tone mapping,
        // OptiX AI denoiser) and, as of I5/I6, the Settings tab's
        // Integrator selector (SPPM; BDPT/MLT). Each reuses an existing
        // scene's pbrt file - same technique B23/F3 use to share content with another entry - rather
        // than being new renderer content: the description/technique-note
        // is the point, not the geometry. No entry for OptiX validation
        // mode - it has no visual effect by design (extra device-side
        // checks only), so "which scene shows the difference" doesn't
        // apply; I4's note says so instead. No entry for the 5 remaining
        // debug/reference integrators (RandomWalk/AO/SimplePath/
        // SimpleVolPath/LightPath) either - none has a distinct visual
        // showcase angle the way SPPM's caustics or BDPT/MLT's bidirectional
        // convergence do (AO isn't even a lit render), so their explanation
        // stays in the GUI's own per-integrator description text
        // (qt_gui/mainwindow_style.cpp's integratorDescription()) rather
        // than a dedicated scene here.
        // ---------------------------------------------------------------
            // Same world/lights as A1 (Cornell Box) - only the id, category,
            // description, and recommended_spp differ. 16 spp (vs A1's 100)
            // is deliberately low: at that count, different Sampler choices
            // (Sobol/Z-Sobol/Stratified/Halton/...) leave visibly different
            // clumping in the soft shadow penumbra. CPU-only, matching the
            // Sampler control's own tooltip (no effect on GPU) - so no GPU
            // case is needed here.
        // CPU-only by design (see the comment above): gpu_compatible is forced false after wiring, and
        // recommended_spp is set to 16 (the file's own Sampler line says otherwise) to keep this scene's
        // original sample count.
        [] {
            auto s = pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
                "I1", 132, "Sampler Comparison (Low Sample Count)", "sampler-comparison", SceneCategories::Education,
                "Cornell box rendered at a deliberately low 16 spp so different Sampler choices (Render Options tab) leave visibly different noise/clumping in the soft shadow.",
                "Fast", "cornell-box-native.pbrt", CameraMode::UserControlled);
            s.recommended_spp = 16;
            s.gpu_compatible = false;
            return s;
        }(),
            // Same world/lights/punctual-lights as B23 (Glass Prism
            // Dispersion) - the prism fan is already the clearest possible
            // demonstration of --spectral in this registry, so this entry
            // just re-frames it under Education with a description pointing
            // at the Spectral rendering checkbox instead of duplicating the
            // geometry. CPU-only, matching --spectral's own tooltip.
        // CPU-only by design (see the comment above): gpu_compatible is forced false after wiring, and
        // recommended_spp is set to 200 (the file's own Sampler line says otherwise) to keep this scene's
        // original sample count.
        [] {
            auto s = pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
                "I2", 133, "Spectral Rendering: Prism Dispersion", "spectral-prism-dispersion", SceneCategories::Education,
                "The same glass prism as Glass Prism Dispersion: white light fans into a visible spectrum only with Spectral rendering (Render Options tab) switched on. Off, every wavelength refracts by the same fixed amount.",
                "Fast", "prism-dispersion.pbrt", CameraMode::UserControlled);
            s.recommended_spp = 200;
            s.gpu_compatible = false;
            return s;
        }(),
        // I3 shares C1's pbrt file (same world and sky, a curated row for the Education category).
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "I3", 204, "Exposure & Tone Mapping (HDR Sky)", "exposure-tone-mapping", SceneCategories::Education,
            "The same HDR sky gradient as HDRI Sky: try raising and lowering Exposure, then compare ACES, Reinhard and None tone mapping (both on the Render Options tab) against this scene's bright sky and shadowed sphere.",
            "Fast", "hdri-sky-gradient.pbrt"),
        [] {
            // Same world/lights as A1 (Cornell Box) - migrated to
            // pbrt-backed alongside A1 (reuses cornell-box-native.pbrt
            // verbatim, see A1's own entry below). recommended_spp is
            // force-overridden to 32 after wiring (vs. the file's own 200,
            // which A1 uses) because this scene's whole point is being
            // genuinely noisy on the GPU recursive backend before
            // denoising - build_curated_pbrt_scene_descriptor() has no spp
            // parameter of its own (spp comes from the file's Sampler
            // directive), so this is the one pbrt-backed Education alias
            // that needs its own spp distinct from the file it reuses.
            // Was gpu/optix/scene_builder.cpp's case 135 (a near-verbatim
            // copy of case 0/A1's own GPU case) - deleted now that this
            // falls through to default: -> build_loaded_pbrt_scene() like
            // every other pbrt-backed scene.
            auto s = pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
                "I4", 135, "GPU Denoiser: Before & After", "gpu-denoiser-demo", SceneCategories::Education,
                "Cornell box at a deliberately low 32 spp - render once with the OptiX AI denoiser (Render Options tab, GPU only - both the recursive and wavefront backends have their own denoiser) off, once on, and compare. The neighboring OptiX validation mode checkbox has no visual effect either way - it only adds debugging checks.",
                "Fast", "cornell-box-native.pbrt", CameraMode::UserControlled);
            s.recommended_spp = 32;
            return s;
        }(),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            // Same world/lights as B3 (Cornell Rough Glass) - migrated to
            // pbrt-backed alongside B3 (reuses cornell-rough-glass.pbrt
            // verbatim, see B3's own entry below). No spp override needed:
            // the file's own Sampler already specifies 200, matching this
            // scene's original intended spp exactly. Was
            // gpu/optix/scene_builder.cpp's case 139 - deleted now that
            // this falls through to default: -> build_loaded_pbrt_scene()
            // like every other pbrt-backed scene. SPPM's own GPU capability
            // check (gpu/optix/optix_types.h's
            // sppm_gpu_material_supported()) already whitelists
            // RoughDielectric independent of this registry entry, so
            // --sppm --gpu keeps working here too, not just plain --sppm.
            "I5", 139, "SPPM: Rough Glass Caustic", "sppm-rough-glass-caustic", SceneCategories::Education,
            "The Cornell Rough Glass room (a frosted-glass sphere): render once with the default Path Tracer and once with SPPM (Integrator dropdown, Render Options tab) and compare how much faster the floor caustic cleans up. SPPM's photon mapping is built for exactly this case.",
            "Fast", "cornell-rough-glass.pbrt", CameraMode::UserControlled),
        [] {
            // Same world/lights as A1 (Cornell Box) - migrated to
            // pbrt-backed alongside A1 (reuses cornell-box-native.pbrt
            // verbatim, see A1's own entry below). recommended_spp is
            // force-overridden to 100 after wiring (vs. the file's own 200)
            // to preserve this scene's original BDPT/MLT-comparison spp -
            // see I4's own entry just above for why the override is needed
            // at all. Was gpu/optix/scene_builder.cpp's case 140 - deleted
            // now that this falls through to default: ->
            // build_loaded_pbrt_scene() like every other pbrt-backed scene.
            // BDPT/MLT themselves still have no GPU implementation at all
            // (CPU only, unconditionally; see main.cpp's own --gpu-ignored
            // warning under either flag) - unaffected by this migration.
            auto s = pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
                "I6", 140, "BDPT / MLT: Bidirectional Light Transport", "bdpt-mlt-demo", SceneCategories::Education,
                "The same Cornell box as Cornell Box: try BDPT or MLT (Integrator dropdown, Render Options tab) instead of the default Path Tracer. Both trace light paths from the camera and from the light and connect them, which can converge differently on scenes with indirect lighting like this one.",
                "Fast", "cornell-box-native.pbrt", CameraMode::UserControlled);
            s.recommended_spp = 100;
            return s;
        }(),
            // Same world/lights as A1 (Cornell Box) - deliberately CPU-only
            // (gpu_compatible=false, matching I1's own precedent) since
            // every alternate integrator this demonstrates is CPU-only:
            // --randomwalk (no NEE/MIS at all - pure uniform-sphere BSDF
            // sampling, pbrt-v4's unbiased reference), --simplepath-no-bsdf
            // (NEE/light-sampling only), --simplepath-no-lights (BSDF
            // importance sampling only), and the default MIS-combined
            // path tracer - all reachable from the same Integrator
            // dropdown (Render Options tab). This box's small ceiling
            // light against mostly-indirect Lambertian bounces is exactly
            // the case where NEE-only and BSDF-only converge very
            // differently (NEE nails the direct light term cheaply, BSDF
            // sampling instead has to get lucky and hit the small light by
            // chance) and MIS combines both - random-walk has neither and
            // is visibly the noisiest of the four at equal spp.
        // CPU-only by design (see the comment above): gpu_compatible is forced false after wiring, and
        // recommended_spp is set to 32 (the file's own Sampler line says otherwise) to keep this scene's
        // original sample count.
        [] {
            auto s = pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
                "I7", 155, "Light Transport Strategies (NEE / BSDF / MIS)", "light-transport-strategies", SceneCategories::Education,
                "The same Cornell box as Cornell Box: pick RandomWalk, SimplePath (then try its NEE and BSDF checkboxes) or the default Path Tracer (Integrator dropdown, Render Options tab) and compare noise at the same low sample count. Each uses a different mix of next-event estimation and BSDF sampling; MIS, the default, combines both well.",
                "Fast", "cornell-box-native.pbrt", CameraMode::UserControlled);
            s.recommended_spp = 32;
            s.gpu_compatible = false;
            return s;
        }(),
            // Purpose-built world (see build_light_sampler_comparison()'s
            // own comment, cornell_box_scene.h, for the full design/power-
            // ratio rationale) - the one Education scene not simply
            // reusing another entry's geometry unchanged, since no
            // existing scene has enough lights of different power to show
            // a light-sampler-strategy difference at all. CPU-only,
            // matching --lightsampler's own "CPU default path tracer
            // only" scope.
        // CPU-only by design (see the comment above): gpu_compatible is forced false after wiring, and
        // recommended_spp is set to 32 (the file's own Sampler line says otherwise) to keep this scene's
        // original sample count.
        [] {
            auto s = pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
                "I8", 156, "Light Sampler Strategy (Uniform / Power / BVH)", "light-sampler-comparison", SceneCategories::Education,
                "Cornell box with five ceiling lights of deliberately lopsided power (roughly 1:2:6:15:80) instead of one: try Uniform vs. Power vs. BVH (Light sampler, Render Options tab) at a low sample count - Uniform spends a fifth of its next-event-estimation samples on each light regardless of how much it actually contributes, so it stays noisier on the one dominant light than Power/BVH, which weight selection toward it instead.",
                "Fast", "cornell-light-sampler-comparison.pbrt", CameraMode::UserControlled);
            s.recommended_spp = 32;
            s.gpu_compatible = false;
            return s;
        }(),
            // Same world/lights as A1 (Cornell Box) - CPU-only
            // (gpu_compatible=false), matching --ao's own "CPU only" scope.
            // AOIntegrator skips material color and indirect lighting
            // entirely (pure occlusion visualization), so this renders as
            // flat grayscale with the box/sphere's own contact shadows and
            // crevices as the only visible structure - about as different
            // from this same box's usual lit render as any Integrator
            // switch in this registry produces.
        // CPU-only by design (see the comment above): gpu_compatible is forced false after wiring, and
        // recommended_spp is set to 64 (the file's own Sampler line says otherwise) to keep this scene's
        // original sample count.
        [] {
            auto s = pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
                "I9", 157, "Ambient Occlusion (Debug Integrator)", "ambient-occlusion-demo", SceneCategories::Education,
                "The same Cornell box as Cornell Box: switch to Ambient Occlusion (Integrator dropdown, Render Options tab), a debug mode with no material colour or indirect light, only a grey occlusion term from nearby geometry, and compare it with the default Path Tracer's full render of the same room.",
                "Fast", "cornell-box-native.pbrt", CameraMode::UserControlled);
            s.recommended_spp = 64;
            s.gpu_compatible = false;
            return s;
        }(),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            // Same world/lights as B3 (Cornell Rough Glass) - the same
            // hard-caustic scene I5 already reuses for SPPM, and for the
            // identical reason: it's the one CPU scene launcher_args.h's
            // own --sppm help text calls "verified end-to-end", and its
            // frosted-glass floor caustic is specifically the kind of
            // hard specular-then-diffuse path that produces fireflies
            // under plain path tracing - exactly the case --regularize
            // and --maxcomponentvalue each exist to tame, via two
            // different mechanisms (widening the BSDF vs. clamping the
            // sample directly). Migrated to pbrt-backed alongside B3/I5
            // (reuses cornell-rough-glass.pbrt verbatim). No spp override
            // needed: the file's own Sampler already specifies 200,
            // matching this scene's original intended spp exactly. Was
            // gpu/optix/scene_builder.cpp's case 158 - deleted now that
            // this falls through to default: -> build_loaded_pbrt_scene()
            // like every other pbrt-backed scene. Both --regularize and
            // --maxcomponentvalue still work on both GPU backends
            // (recursive exact, wavefront approximate for the clamp - see
            // each flag's own help text), unaffected by this migration.
            "I10", 158, "Firefly Suppression (Regularize / Clamp)", "firefly-suppression", SceneCategories::Education,
            "The same rough-glass Cornell box as Cornell Rough Glass: render once plain, once with Regularize checked and once with Firefly clamp (--maxcomponentvalue) checked (both on the Render Options tab). The hard caustic through the frosted sphere is what each is built to tame, one by blurring the BSDF and the other by clamping the sample.",
            "Fast", "cornell-rough-glass.pbrt", CameraMode::UserControlled),

    };
}

// Part 6 of 7 of the compiled-in scene table (the order of the parts is the order of the registry).
inline std::vector<SceneDescriptor> builtin_scenes_part6() {
    return {
        // ---------------------------------------------------------------
        // Curated pbrt_scenes/*.pbrt example scenes, under their real topic
        // tab instead of only the generic "Custom Scenes" bucket every
        // loaded .pbrt file auto-discovers into (see
        // pbrt_scene_registry::build_curated_pbrt_scene_descriptor()'s own
        // comment). Legacy ids 100+ - past every real case in
        // gpu/optix/scene_builder.cpp's switch, same reasoning as
        // append()'s own dynamically-assigned ids below.
        // ---------------------------------------------------------------

        // -- Materials --
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B15", 100, "Mix Material", "mix-material", SceneCategories::Materials,
            "Real per-shading-point stochastic pbrt \"mix\" material resolution on all three backends -- a fine-grained speckle of matte red diffuse and a conductor's real specular highlights, not one flat averaged color.",
            "Fast", "mix-material.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B16", 101, "Layered Materials", "layered-materials", SceneCategories::Materials,
            "Four pbrt material kinds no other bundled example scene touches: thindielectric, coatedconductor, diffusetransmission, and subsurface via a named measured-scattering preset (\"Marble\", no external file needed).",
            "Fast", "layered-materials.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B18", 103, "Conductor RGB Eta/K", "conductor-rgb-eta-k", SceneCategories::Materials,
            "Explicit RGB eta/k for pbrt's conductor material, plus named-metal-spectrum resolution for coatedconductor -- real complex-IOR GGX highlights instead of the flat fuzz-mirror/reflectance-only fallback.",
            "Fast", "conductor-rgb-eta-k.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B20", 105, "Hair Material", "hair-material", SceneCategories::Materials,
            "pbrt's hair material (Marschner/Chiang fibre scattering) on ordinary spheres, laid out like the native Hair Fibers demo for comparison.",
            "Fast", "hair-material.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B22", 107, "Named Material & Texture", "named-material-and-texture", SceneCategories::Materials,
            "pbrt's NamedMaterial referenced directly for a shape (not just as a \"mix\" sub-material), plus a texture-bound material parameter and AreaLightSource's twosided flag.",
            "Fast", "named-material-and-texture.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "B25", 146, "Glass Presets", "glass-presets", SceneCategories::Materials,
            "All seven of the dielectric material's named glass presets (BK7, BAF10, FK51A, LASF9, F5, F10, F11) as separate spheres.",
            "Fast", "glass-presets.pbrt"),

        // -- Textures --
        // Split out of Materials once it grew past 25 scenes mixing two
        // distinct concerns - see SceneCategories::Textures's own comment
        // (scene_descriptor.h). Ids keep their original legacy_id (the
        // stable internal scene_builder.cpp switch key - see
        // SceneDescriptor::legacy_id's comment) even though their letter-id
        // changed from B to J, matching this file's own precedent of
        // reassigning a scene's user-facing id when its category genuinely
        // changes rather than preserving a now-inconsistent one.
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "J1", 102, "CoatedDiffuse Texture", "coateddiffuse-texture", SceneCategories::Textures,
            "A texture driving the reflectance of pbrt's coated-diffuse material.",
            "Fast", "coateddiffuse-texture.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "J2", 104, "DiffuseTransmission Texture", "diffusetransmission-texture", SceneCategories::Textures,
            "Texture-bound reflectance/transmittance for pbrt's DiffuseTransmission material, threaded through both backends.",
            "Fast", "diffusetransmission-texture.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "J3", 106, "Nested Checker Texture", "nested-checker-texture", SceneCategories::Textures,
            "One level of nested imagemap texture reference inside a pbrt checkerboard/mix texture -- tex1/tex2 bound to a real image instead of only a flat literal color.",
            "Fast", "nested-checker-texture.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "J4", 147, "Texture Encoding & Wrap", "texture-encoding-and-wrap", SceneCategories::Textures,
            "Four quads isolating Texture \"imagemap\"'s \"encoding\"/\"wrap\"/\"invert\" params one at a time -- linear vs. sRGB decode, clamp vs. repeat past [0,1], and inverted channel values, none reachable via any other bundled scene's defaults.",
            "Fast", "texture-encoding-wrap-invert.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "J5", 148, "Procedural Texture Gallery", "procedural-texture-gallery", SceneCategories::Textures,
            "Four of pbrt's procedural texture types in one scene: windy turbulence, wrinkled (Perlin-octave) turbulence, dots, and bilerp corner blending.",
            "Fast", "procedural-textures-windy-wrinkled-dots-bilerp.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "J6", 149, "Nested Texture: 2 Levels", "nested-texture-2-levels", SceneCategories::Textures,
            "A second level of checkerboard-texture nesting (checker of a checker of a real image) -- real on CPU, GPU intentionally approximates the whole nested tree as one flat average colour and warns; compare both to see the documented divergence.",
            "Fast", "nested-texture-2level.pbrt"),

        // -- Lights --
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C8", 108, "Punctual Lights", "punctual-lights", SceneCategories::Lights,
            "All five of pbrt-v4's punctual (delta-distribution) light kinds in one scene: point, spot, distant, goniometric, and projection.",
            "Fast", "punctual-lights.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C9", 109, "Goniometric & Projection Lights", "goniometric-and-projection-lights", SceneCategories::Lights,
            "Goniometric and projection lights that read their own image files: a goniometric light's measured intensity pattern and a projection light's slide.",
            "Fast", "goniometric-projection.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C10", 110, "Blackbody Light", "blackbody-light", SceneCategories::Lights,
            "Colour-temperature (blackbody) area lights: two identical panels at 2500 K and 9000 K, one warm and one cool.",
            "Fast", "blackbody-light.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C11", 111, "Textured Two-Sided Lights", "textured-two-sided-lights", SceneCategories::Lights,
            "A real filename-textured, two-sided AreaLightSource on a non-triangle (sphere/quad) shape, on both backends.",
            "Fast", "textured-twosided-lights.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C12", 112, "Infinite Light", "infinite-light", SceneCategories::Lights,
            "pbrt's \"infinite\" constant-colour sky light in open geometry, actually lighting the scene from every direction rather than being blocked by a room.",
            "Fast", "infinite-light.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C13", 113, "Disk & Cylinder Lights", "disk-and-cylinder-lights", SceneCategories::Lights,
            "Disk and cylinder shapes as real NEE-samplable area lights on both GPU backends, converging as cleanly as CPU's solid-angle sampling instead of noisier hit-only emission.",
            "Fast", "disk-cylinder-light.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C14", 114, "Two Sphere Lights", "two-sphere-lights", SceneCategories::Lights,
            "Two sphere area lights in one scene, to check that every light is sampled correctly and not only the first.",
            "Fast", "two-sphere-lights.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C15", 115, "Triangle Fan Light", "triangle-fan-light", SceneCategories::Lights,
            "An area light that is NOT a parallelogram -- an irregular 5-triangle fan the quad-merge pass can't rejoin, exercising the GPU's per-triangle light sampling.",
            "Fast", "triangle-fan-light.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C16", 137, "ColorSpace + Blackbody Light", "colorspace-blackbody-light", SceneCategories::Lights,
            "Identical to the Blackbody Light example except for one added \"ColorSpace rec2020\" directive -- the same 2500K/9000K temperatures resolve to visibly different RGB under Rec.2020's wider primaries than the sRGB default.",
            "Fast", "colorspace-blackbody.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C17", 142, "Portal Light", "portal-light", SceneCategories::Lights,
            "pbrt-v4's portal infinite light: an equal-area environment map restricted to a single window, so only that opening shows real sky detail. The Portal Infinite Light scene only cuts a hole in a wall behind a flat sky; this is the real thing.",
            "Fast", "portal-light.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C18", 143, "Light Power Parameter", "light-power-parameter", SceneCategories::Lights,
            "pbrt-v4's \"float power\" parameter on point, spot, and area lights -- three otherwise-identical spheres, each lit only by one light type specifying total flux instead of intensity/radiance directly, so a wrong power-to-intensity conversion shows up as a visibly mismatched brightness.",
            "Fast", "light-power-parameter.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C19", 144, "Projection Light: Non-Square", "projection-light-non-square", SceneCategories::Lights,
            "LightSource \"projection\" with a non-square (8x4) slide image, the first bundled scene to exercise a real aspect-ratio mismatch between the image and the light's own field of view -- a correct render shows a clearly wide rectangular footprint, not a squished or stretched one.",
            "Fast", "projection-light-nonsquare.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "C20", 145, "Spectral Gamut Saturation", "spectral-gamut-saturation", SceneCategories::Lights,
            "Three saturated, close-range coloured lights on a plain diffuse surface under --spectral rendering, chosen so their overlap sits at the sRGB gamut boundary, where a gamut-clamping error would darken and desaturate the colours.",
            "Fast", "spectral-gamut-saturation.pbrt"),

    };
}

// Part 7 of 7 of the compiled-in scene table (the order of the parts is the order of the registry).
inline std::vector<SceneDescriptor> builtin_scenes_part7() {
    return {
        // -- Cameras --
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "D9", 116, "Depth of Field (pbrt file)", "depth-of-field-pbrt-file", SceneCategories::Cameras,
            "A perspective camera's thin-lens depth-of-field (lensradius/focaldistance) loaded from a pbrt file, on both backends.",
            "Fast", "depth-of-field.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "D10", 117, "Orthographic Camera (pbrt file)", "orthographic-camera-pbrt-file", SceneCategories::Cameras,
            "pbrt's orthographic (parallel-projection) camera loaded from a file -- two same-size spheres at different depths read as equal size, not perspective-foreshortened.",
            "Fast", "orthographic-camera.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "D11", 118, "Spherical Camera (pbrt file)", "spherical-camera-pbrt-file", SceneCategories::Cameras,
            "pbrt's spherical (equal-area) camera loaded from a file, positioned inside an enclosed room so it actually captures every direction at once.",
            "Fast", "spherical-camera.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "D12", 119, "Realistic Camera (pbrt file)", "realistic-camera-pbrt-file", SceneCategories::Cameras,
            "pbrt's realistic multi-element lens camera loaded from a file, including the lensfile-loading path a compiled-in scene never exercised.",
            "Fast", "realistic-camera.pbrt"),

        // -- Volumes --
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "E5", 120, "Cloud Medium (pbrt file)", "cloud-medium-pbrt-file", SceneCategories::Volumes,
            "pbrt's MakeNamedMedium \"cloud\" (Perlin-noise heterogeneous scattering) loaded from a file, on both backends.",
            "Fast", "cloud-medium.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "E6", 121, "Cylinder Medium", "cylinder-medium", SceneCategories::Volumes,
            "A homogeneous fog medium inside pbrt's cylinder shape.",
            "Fast", "cylinder-medium.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "E11", 201, "Thin Dielectric Medium", "thin-dielectric-medium", SceneCategories::Volumes,
            "A thin dielectric (a zero-thickness pane of glass) fused with a participating medium.",
            "Fast", "thin-dielectric-medium.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "E12", 202, "Rough Dielectric Medium", "rough-dielectric-medium", SceneCategories::Volumes,
            "A frosted (rough) dielectric fused with a participating medium: glossy refraction with fog inside.",
            "Fast", "rough-dielectric-medium.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "E7", 122, "RGB Grid Medium (pbrt file)", "rgb-grid-medium-pbrt-file", SceneCategories::Volumes,
            "pbrt's MakeNamedMedium \"rgbgrid\" (an RGB voxel grid) rendering as a soft coloured nebula, on both backends.",
            "Fast", "rgbgrid-medium.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "E8", 123, "Uniform Grid Medium", "uniform-grid-medium", SceneCategories::Volumes,
            "pbrt's MakeNamedMedium \"uniformgrid\" (a single-channel density voxel grid) rendering as a soft glowing blob, on both backends.",
            "Fast", "uniformgrid-medium.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "E9", 141, "NanoVDB Medium", "nanovdb-medium", SceneCategories::Volumes,
            "pbrt's NanoVDB medium (a sparse density grid read from an external .nvdb file) shown as a soft fog-volume sphere. CPU only: the GPU renders flat homogeneous fog instead.",
            "Fast", "nanovdb-medium.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "E10", 154, "Camera Medium", "camera-medium", SceneCategories::Volumes,
            "pbrt-v4's camera-medium idiom -- a MediumInterface issued before the Camera directive puts the camera itself inside a fog with no boundary shape at all, unlike every other bundled medium scene. Real on the CPU and both GPU backends.",
            "Fast", "camera-medium.pbrt"),

        // -- Geometry --
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "F5", 124, "PLY Mesh UV", "ply-mesh-uv", SceneCategories::Geometry,
            "A mesh with per-vertex UV coordinates read from a .ply file, textured through them.",
            "Fast", "plymesh-uv.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "F6", 125, "PLY Mesh Geometry", "ply-mesh-geometry", SceneCategories::Geometry,
            "pbrt's Shape \"plymesh\" loading a real external .ply file, including fan-triangulation of a non-triangular base face.",
            "Fast", "plymesh-geometry.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "F7", 126, "Curve Tuft", "curve-tuft", SceneCategories::Geometry,
            "A tuft of cubic Bezier curve strands (pbrt's curve shape), laid out like the native Curve Fibers demo for comparison.",
            "Fast", "curve-tuft.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "F8", 127, "Curve + Hair Tuft", "curve-hair-tuft", SceneCategories::Geometry,
            "Real curve geometry paired with Material \"hair\" for the first time -- the exact combination that motivated HairBxDF's own fiber-tangent fix.",
            "Fast", "curve-hair-tuft.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "F9", 128, "Triangle Mesh UV", "triangle-mesh-uv", SceneCategories::Geometry,
            "A triangle mesh whose texture coordinates come from its own point2 uv parameter.",
            "Fast", "trianglemesh-uv.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "F10", 129, "Pixel Filter: Box", "pixel-filter-box", SceneCategories::Geometry,
            "pbrt's PixelFilter directive end-to-end on both backends -- a box filter's harder, more aliased silhouette edges compared to the default Gaussian.",
            "Fast", "pixel-filter-box.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "F11", 150, "Object Motion Blur", "object-motion-blur", SceneCategories::Geometry,
            "Shape \"sphere\" object motion blur via ActiveTransform StartTime/EndTime -- a moving sphere renders as a soft directional streak instead of a crisp or doubled sphere, real on all three backends (CPU, GPU-recursive, GPU-wavefront).",
            "Fast", "object-motion-blur.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "F12", 151, "Disk & Cylinder Motion Blur", "disk-and-cylinder-motion-blur", SceneCategories::Geometry,
            "Disks and cylinders with object motion blur. CPU only: the GPU renders them frozen at their start pose, with a warning.",
            "Fast", "disk-cylinder-motion-blur.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "F13", 152, "ReverseOrientation", "reverseorientation", SceneCategories::Geometry,
            "The bare ReverseOrientation directive on two identical-winding quads -- one stays dark (normal facing away from the camera), the other is flipped visible by ReverseOrientation, so a mistake shows up as both quads dark or both lit.",
            "Fast", "reverseorientation.pbrt"),
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "F14", 153, "Cone & Paraboloid Gallery", "cone-and-paraboloid-gallery", SceneCategories::Geometry,
            "Cones and paraboloids (an extension of this renderer's pbrt-v3-compatible shapes) as diffuse shapes, an area-light emitter and a medium boundary in one gallery. The GPU draws them as triangle approximations.",
            "Fast", "cone-paraboloid-gallery.pbrt"),

        // -- Models --
        pbrt_scene_registry::build_curated_pbrt_scene_descriptor(
            "G25", 130, "Killeroo", "killeroo", SceneCategories::Models,
            "The classic pbrt-v4 \"killeroo\" statue example scene, loaded end-to-end from its own .pbrt file rather than a compiled-in scene.",
            "Fast", "killeroo-simple.pbrt"),
    };
}

inline const std::vector<SceneDescriptor>& get_builtin_scene_registry() {
    static const std::vector<SceneDescriptor> registry = [] {
        std::vector<SceneDescriptor> all;
        { auto part = builtin_scenes_part1(); all.insert(all.end(), part.begin(), part.end()); }
        { auto part = builtin_scenes_part2(); all.insert(all.end(), part.begin(), part.end()); }
        { auto part = builtin_scenes_part3(); all.insert(all.end(), part.begin(), part.end()); }
        { auto part = builtin_scenes_part4(); all.insert(all.end(), part.begin(), part.end()); }
        { auto part = builtin_scenes_part5(); all.insert(all.end(), part.begin(), part.end()); }
        { auto part = builtin_scenes_part6(); all.insert(all.end(), part.begin(), part.end()); }
        { auto part = builtin_scenes_part7(); all.insert(all.end(), part.begin(), part.end()); }
        return all;
    }();
    return registry;
}
