#pragma once
// scene_slugs.h -- stable string keys for scenes, and the rule that makes them.
//
// A scene's slug ("cornell-box") is its durable identity: it does not change when a category is added, when a file is dropped into pbrt_scenes/, or
// when a scene is renamed for display. The id ("A1", "K37") is a short code that is only as stable as the registry order, so anything that is saved
// or typed by a person (recent renders, scripts, the command line, notes keyed to a scene) should use the slug; the id stays accepted everywhere as
// an alias. SceneDescriptor::slug is an argument of the scene's row in scene_registry_data.h for compiled-in scenes and is made from the file name for .pbrt files found on disk.
//
// Slugs are lower-case, digits and single hyphens only (no underscore, so a name like "render_<slug>_<time>.png" splits unambiguously).

#include <cctype>
#include <cstddef>
#include <set>
#include <string>

namespace scene_slugs {

// "Depth of Field (pbrt file)" -> "depth-of-field-pbrt-file"; any run of characters outside a-z and 0-9 becomes one hyphen; "&" reads as "and".
inline std::string slugify(const std::string& text) {
    std::string spaced;
    for (char c : text) {
        if (c == '&') spaced += " and ";
        else spaced += c;
    }
    std::string out;
    bool pendingHyphen = false;
    for (unsigned char c : spaced) {
        if (std::isalnum(c) && c < 128) {
            if (pendingHyphen && !out.empty()) out += '-';
            pendingHyphen = false;
            out += static_cast<char>(std::tolower(c));
        } else {
            pendingHyphen = true;
        }
    }
    return out.empty() ? std::string("scene") : out;
}

// A readable name for a file's stem, for scenes that have no title of their own: "bdpt-box-room" -> "BDPT Box Room", "cornell_dof" -> "Cornell DoF".
// Words split on hyphen, underscore, dot and space; known acronyms keep their capitals and small joining words stay lower case.
inline std::string prettyName(const std::string& stem) {
    static const struct { const char* lower; const char* shown; } kSpecial[] = {
        {"bdpt", "BDPT"}, {"mlt", "MLT"}, {"sppm", "SPPM"}, {"rgb", "RGB"}, {"dof", "DoF"}, {"hdri", "HDRI"}, {"hdr", "HDR"}, {"pbrt", "pbrt"},
        {"gpu", "GPU"}, {"cpu", "CPU"}, {"brdf", "BRDF"}, {"bsdf", "BSDF"}, {"nee", "NEE"}, {"mis", "MIS"}, {"ior", "IOR"}, {"uv", "UV"},
        {"ply", "PLY"}, {"ao", "AO"}, {"vdb", "VDB"}, {"nanovdb", "NanoVDB"}, {"vw", "VW"}, {"obj", "OBJ"}};
    static const char* const kSmall[] = {"a", "an", "and", "at", "for", "from", "in", "of", "on", "the", "to", "with"};
    std::string out, word;
    bool first = true;
    auto flush = [&]() {
        if (word.empty()) return;
        std::string shown;
        for (const auto& sp : kSpecial)
            if (word == sp.lower) shown = sp.shown;
        if (shown.empty()) {
            bool joiner = false;
            for (const char* w : kSmall) joiner = joiner || word == w;
            shown = word;
            if (!(joiner && !first) && std::isalpha(static_cast<unsigned char>(shown[0]))) shown[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(shown[0])));
        }
        if (!out.empty()) out += ' ';
        out += shown;
        word.clear();
        first = false;
    };
    for (unsigned char c : stem) {
        if (c == '-' || c == '_' || c == '.' || c == ' ') flush();
        else word += static_cast<char>(std::tolower(c));
    }
    flush();
    return out.empty() ? stem : out;
}

// The base slug, or base-2, base-3, ...: the first one not already in the taken set (it is added to the set).
inline std::string uniqueSlug(const std::string& base, std::set<std::string>& taken) {
    std::string slug = base;
    for (int n = 2; taken.count(slug); ++n) slug = base + "-" + std::to_string(n);
    taken.insert(slug);
    return slug;
}

// True for a string shaped like a slug: starts with a letter, then lower-case letters, digits and hyphens. (An id such as "B10" is upper case and
// never matches, which is how the two kinds of key are told apart.)
inline bool looksLikeSlug(const std::string& s) {
    if (s.empty() || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

} // namespace scene_slugs
