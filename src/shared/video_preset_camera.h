#pragma once
// video_preset_camera.h -- the video presets that move a camera round a scene of the registry (V1..V6), and the preset record itself, without the cartoon films: what the GUI
// needs (its Flythrough Preset box), so that it does not compile the films (it asks the renderer's library about them: qt_gui/film_info.h). video_preset.h adds a preset for
// each film, for the launcher.
#include <cstring>

namespace video_preset {


struct VideoPreset {
    const char* id;           // short scene-registry-style id, e.g. "V1" - stable
                               // for the lifetime of this table's ordering (see
                               // all()'s own comment), used by --video-preset and
                               // shown as the GUI combo's "[V1]" prefix, mirroring
                               // how scene_registry.h's SceneDescriptor::id ("A1",
                               // "G6"...) is displayed in the scene combo.
    const char* key;          // descriptive slug, e.g. "cornell-orbit" - also
                               // accepted by --video-preset, since it stays
                               // meaningful in a shell history the way a bare "V1"
                               // does not.
    const char* name;         // "Cornell Box Orbit" - shown in the GUI combo
    const char* description;  // one line, shown as a tooltip
    const char* scene_id;     // SceneDescriptor::id, e.g. "A1"
    const char* camera_path;  // one of camera_path.h's named types, or "cartoon:<key>" (src/shared/cartoon/cartoons.h): the video then renders that film's own scene at
                               // every frame instead of moving a camera round a fixed scene (scene_id is then only a placeholder: "A1")
    int frames;
    int fps;
    double speed;

    // A cartoon preset renders a film's own scene at every frame instead of moving a camera round a fixed scene; its camera_path is "cartoon:<key>" (cartoon/cartoons.h; the film
    // itself: filmOf in video_preset.h).
    bool isCartoon() const { return std::strncmp(camera_path, "cartoon:", 8) == 0; }
};

// Six presets, each pairing a distinct scene already in the registry with a
// camera path already implemented in camera_path.h - no new rendering
// features, just known-good bundles of existing ones. Ordered roughly by
// render cost (fastest first) so the GUI combo's default selection is cheap
// to try. `id` follows that same order (V1..V6) - like scene ids, it is a
// stable label for a specific preset, not a live rank, so inserting a new
// preset later should append rather than renumber the ones before it.
inline const VideoPreset kCameraPresets[] = {
    {
        "V1", "cornell-orbit", "Cornell Box Orbit",
        "A slow 360-degree orbit around ray tracing's most iconic reference scene.",
        "A1", "orbit", 90, 30, 1.0
    },
    {
        "V2", "teapot-spin", "Utah Teapot Spin",
        "The Utah teapot - computer graphics' most iconic test object - rotating in place.",
        "G6", "orbit", 90, 30, 1.0
    },
    {
        "V3", "one-weekend-flyby", "One Weekend Flyby",
        "A flythrough of the Ray Tracing in One Weekend cover scene - hundreds of "
        "random spheres with real per-object motion blur.",
        "A2", "linear", 90, 30, 0.8
    },
    {
        "V4", "next-week-finale", "Next Week Finale",
        "The Ray Tracing: The Next Week finale scene - moving spheres, volumes, and "
        "a subsurface sphere over a box-grid ground.",
        "A9", "figure8", 120, 30, 0.6
    },
    {
        "V5", "glass-dragon-caustics", "Glass Dragon Caustics",
        "A spiral zoom into a refractive glass dragon, showing off dielectric "
        "caustics as the camera closes in.",
        "G13", "spiral", 120, 30, 0.5
    },
    {
        "V6", "sponza-flythrough", "Sponza Flythrough",
        "A flythrough of Crytek Sponza, the rendering community's canonical "
        "global-illumination showcase scene.",
        "H1", "linear", 120, 24, 0.5
    },
};

// A camera preset by id ("V1") or key ("cornell-orbit"); null for any other (a film's preset is video_preset::find's).
inline const VideoPreset* findCamera(const char* id_or_key) {
    if (!id_or_key) return nullptr;
    for (const VideoPreset& p : kCameraPresets)
        if (std::strcmp(p.id, id_or_key) == 0 || std::strcmp(p.key, id_or_key) == 0) return &p;
    return nullptr;
}

} // namespace video_preset
