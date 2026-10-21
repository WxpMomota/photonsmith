#pragma once
// scene_bucket.h -- the four buckets of the Settings tab's availability tab bar (MainWindow::m_sceneAvailabilityTabs): the scenes split into "Self-Contained" and "Requires External
// Files", and the stills of the cartoon films (category Cartoons) split the same way. A bucket is the tab's index. Qt-free, so a test can check it.

namespace scene_bucket {

enum Bucket { SelfContained = 0, ExternalFiles = 1, Cartoons = 2, CartoonsExternal = 3 };
constexpr int kCount = 4;

constexpr bool isCartoons(int bucket) { return bucket == Cartoons || bucket == CartoonsExternal; }
constexpr bool requiresFiles(int bucket) { return bucket == ExternalFiles || bucket == CartoonsExternal; }
constexpr int of(bool cartoons, bool requiresFilesToo) { return cartoons ? (requiresFilesToo ? CartoonsExternal : Cartoons) : (requiresFilesToo ? ExternalFiles : SelfContained); }

}  // namespace scene_bucket
