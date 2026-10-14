#pragma once
// scene_builder_sky_gallery.h -- the Scene Builder's "Sky gallery...": a window of thumbnails of the ready-made skies (scene_sky.h presets) for the Sun & sky light.
#include <QWidget>

// Shows the gallery; returns the index into scene_doc::sky::presets() of the sky the user chose (OK or a double click), or -1 when they cancelled. `current` is highlighted when it
// is a valid index (-1: none).
int chooseSkyPreset(QWidget *parent, int current);
