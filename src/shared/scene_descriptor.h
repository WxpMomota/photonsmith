// scene_descriptor.h -- the scene CATEGORY names (SceneCategories) and nothing else.
//
// All scene METADATA (name, slug, description, performance hint, recommended SPP, requires_files, gpu_compatible, camera) lives in one place: the row of
// src/TheRestOfYourLife/scene_registry_data.h, queried live by the Qt GUI via scene_metadata.dll / qt_gui/scene_metadata_client.h. A scene's name and its
// durable slug used to be two more tables (SceneNames here, kBuiltin in scene_slugs.h); each was used once, by that row, and adding a scene meant keeping
// three lists in step. They are the row's own arguments now. The category names stay here because the GUI and the GPU backends use them too.
//
// To add a scene (docs/SCENE_SELECTION.md has the full list):
//   1. Put its .pbrt file in pbrt_scenes/
//   2. Add its row to src/TheRestOfYourLife/scene_registry_data.h (build_curated_pbrt_scene_descriptor): id, legacy id, name (what the scene shows, not how it is
//      stored), slug (the durable name: it never changes when the name does), category, description, performance, file
//   3. Add its technique note to qt_gui/scene_technique_notes.h, keyed by the slug (a scene that needs no download)
//   4. Run RT_UPDATE_SNAPSHOT=1 ray_tracer_tests --gtest_filter=SceneRegistrySnapshot* and review the diff of tests/data/scene_registry_snapshot.txt; the same test
//      says which documents quote the number of scenes and are out of date
// A test fixture or a scene of your own needs none of this: a .pbrt file in pbrt_scenes/ is listed on its own (see "# @rt-category" in pbrt_discover.h).

#pragma once

#ifdef __cplusplus
#include <cstddef>
#include <cstring>

// -----------------------------------------------------------------------
// Canonical scene category constants
// -----------------------------------------------------------------------
// Categories group scenes by WHAT THEY DEMONSTRATE, which is the question a
// user browsing 78 scenes is actually asking - not by which book chapter or
// source file they came from. The Qt GUI turns these into a filter tab bar
// above the scene list (see qt_gui/mainwindow_tabs.cpp).
//
// Use these constants in the registry, never raw
// string literals, so a category can't drift between call sites. kAll is the
// display order for the tabs and is what tests/unit/scene_registry_tests.cpp
// validates every scene's category against - a typo'd category would
// otherwise silently produce a scene that appears under no tab at all.
namespace SceneCategories {
    constexpr const char* Basics     = "Basics";       // the book scenes
    constexpr const char* Materials  = "Materials";    // BxDF / surface appearance
    constexpr const char* Lights     = "Lights";       // light types and sampling
    constexpr const char* Cameras    = "Cameras";      // projection and lens models
    constexpr const char* Volumes    = "Volumes";      // participating media
    constexpr const char* Geometry   = "Geometry";     // shape primitives
    constexpr const char* Models     = "Models";       // single imported meshes
    constexpr const char* LargeScene = "Large Scenes"; // full textured environments
    // Curated demos of the Render Options tab's own controls (Sampler,
    // Spectral rendering, Exposure, Tone mapping, OptiX AI denoiser) and its
    // Integrator selector (SPPM/BDPT/MLT) - each entry
    // reuses an existing scene's geometry (same technique B23/F3 already use
    // to share content with another entry) rather than being new content in
    // its own right, so the description/technique-note is the point: which
    // control to try, and why this particular scene shows it clearly.
    constexpr const char* Education  = "Education";
    // Texture SYSTEM demos - encoding/wrap/invert, procedural texture
    // classes, nested texture references - as distinct from Materials'
    // BxDF/material-kind demos, which a texture is only ever bound INTO.
    // Split out once Materials grew past 25 scenes mixing both concerns
    // (a scene about "does imagemap wrap or clamp" and a scene about "what
    // does coateddiffuse look like" answer genuinely different questions,
    // even though both happen to declare a Material). Inserted here, after
    // every other compiled-in category and before CustomScenes (same
    // zero-disruption insertion point Education itself used - see kAll's
    // own comment below), so no other category's id letter moves.
    constexpr const char* Textures   = "Textures";
    // Scenes loaded from .pbrt files found on disk rather than compiled in.
    // Unlike every category above it, this one is populated at runtime and is
    // legitimately empty when the user has no scene collection installed -
    // which is why the registry tests exempt it from "every category has at
    // least one scene". Named CustomScenes rather than the more generic-
    // sounding "UserScenes" this used to be - see git history for the
    // rename - since what actually distinguishes this category is that its
    // scenes are described by a file on disk rather than compiled in, not
    // that a "user" made them (a git-tracked, bundled pbrt_scenes/*.pbrt
    // example is just as "yours" as anything in the other categories).
    constexpr const char* CustomScenes = "Custom Scenes";
    // The scenes this project's own tests render: closed-form furnaces, light-transport probes, one-feature fixtures. They live in pbrt_scenes/ and
    // are worth browsing (each isolates one thing), but they are not demos, so they stay out of the user-facing tabs above. A scene file puts itself
    // here with a "# @rt-category Test Scenes" line in its header. Last in kAll so that no earlier category's id letter moves.
    constexpr const char* Tests = "Test Scenes";
    // Scenes the user made: those saved from the Scene Builder ("Add to scene list" writes into the per-user scenes folder) or carrying a Scene Builder
    // title. Filled from disk like Custom Scenes and legitimately empty until the user makes one. Appended LAST in kAll (so it takes the next free id
    // letter and no earlier category's letter moves - Test Scenes keeps 'L'), but shown right after Textures in the GUI (kDisplayOrder below).
    constexpr const char* MyScenes = "My Scenes";
    // One still of every cartoon film (src/shared/cartoon/cartoons.h), written as ordinary scene files into the per-user cartoon folder and found like any
    // scene on disk (pbrt_discover.h). Listed only when that folder exists (the GUI sets it up); the film itself is a video preset. Appended last in kAll for
    // the same reason My Scenes was, shown after Textures in the GUI.
    constexpr const char* Cartoons = "Cartoons";

    // Display order for the GUI's category tabs. Education and Textures sit
    // after the other compiled-in categories and before CustomScenes, which
    // comes next so the built-in tabs never shift position when a scene
    // folder appears; Test Scenes, also filled from disk, is last.
    constexpr const char* kAll[] = {
        Basics, Materials, Lights, Cameras, Volumes, Geometry, Models, LargeScene,
        Education, Textures, CustomScenes, Tests, MyScenes, Cartoons
    };
    constexpr std::size_t kAllCount = sizeof(kAll) / sizeof(kAll[0]);

    // The order of the GUI's category tabs. kAll's order is the id-letter order (append-only, so no scene id ever changes); this is the reading order:
    // the user's own scenes first among the ones read from disk. Same entries as kAll, in a different order (a unit test checks that).
    constexpr const char* kDisplayOrder[] = {
        Basics, Materials, Lights, Cameras, Volumes, Geometry, Models, LargeScene,
        Education, Textures, Cartoons, MyScenes, CustomScenes, Tests
    };

    // A category's id letter (used to build scene ids like "B10", the 10th
    // Materials scene - see scene_registry.h's SceneDescriptor::id) is just
    // 'A' + its position in kAll: A, B, C, ... in display order, no gaps.
    // This used to be a hand-maintained parallel kAllLetters array - real
    // drift, twice: Education's own insertion once shifted CustomScenes
    // from 'I' to 'J', then Textures's insertion shifted it again from 'J'
    // to 'K', and both times at least one comment elsewhere in the codebase
    // citing the old letter was missed and went stale (caught by code
    // review, not by any compiler or test). Deriving the letter directly
    // from kAll's own index removes the second array entirely, so the next
    // category addition is purely a one-line kAll edit - there is no
    // parallel array left to forget to update, and no letter left to go
    // stale in a comment written against yesterday's kAll.
    static_assert(kAllCount <= 26, "kAll has grown past 'Z' - letter_for_category() needs a two-letter or numeric id scheme");

    // Returns the letter for a category, or '\0' if category doesn't match
    // any entry in kAll (a typo'd category, same failure mode kAll's own
    // scene_registry_tests.cpp check exists to catch). strcmp, not pointer
    // identity - correct regardless of whether the caller's `category` and
    // kAll's entries happen to be the same interned string literal.
    inline char letter_for_category(const char* category) {
        for (std::size_t i = 0; i < kAllCount; ++i) {
            if (std::strcmp(kAll[i], category) == 0)
                return static_cast<char>('A' + static_cast<int>(i));
        }
        return '\0';
    }
} // namespace SceneCategories

// -----------------------------------------------------------------------
// Subcategories: a second level inside a category that has grown too big to browse as one list (Large Scenes: 42 and counting). A scene's subcategory is an
// argument of its registry row (in_subcategory() in scene_registry.h); it never changes the scene's id (the letter is the CATEGORY's), so adding or moving a
// scene between subcategories moves no id. The GUI shows a row of subcategory tabs under the category tabs when a category has two or more. A category with
// no subcategories (every other one today) simply has none; a scene with an empty subcategory under a category that has some is listed under "All" only.
// Use these constants in the registry, never raw string literals; the order here is the order of the tabs.
// -----------------------------------------------------------------------
namespace SceneSubcategories {
    // Large Scenes
    constexpr const char* Interiors   = "Interiors";           // rooms, halls, kitchens: lit by windows and lamps
    constexpr const char* Buildings   = "Buildings & Streets"; // atrium, cathedral, pavilion, houses, streets, a power plant
    constexpr const char* VoxelWorlds = "Voxel Worlds";        // Minecraft-style towns and islands
    constexpr const char* Nature      = "Nature";              // outdoor scenes of plants and land
    constexpr const char* Vehicles    = "Vehicles";            // cars and a spaceship
    constexpr const char* Objects     = "Objects";             // statues, product shots and material studies
    constexpr const char* LightAndArt = "Light & Art";         // light-transport test scenes and artists' pieces
    constexpr const char* kLargeScenes[] = {Interiors, Buildings, VoxelWorlds, Nature, Vehicles, Objects, LightAndArt};
    constexpr std::size_t kLargeScenesCount = sizeof(kLargeScenes) / sizeof(kLargeScenes[0]);
} // namespace SceneSubcategories

#endif // __cplusplus
