#pragma once
// scene_builder_common.h - colour conversion and display names shared by the Scene Builder's layout view, property panel and widget.

#include <QColor>
#include <QCoreApplication>
#include <QFormLayout>
#include <QKeySequence>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QObject>
#include <QWidget>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>

#include "../src/shared/scene_document.h"
#include "../src/shared/scene_props.h"
#include "../src/shared/scene_blocks.h"

namespace scene_builder_ui {

// The toolbar buttons are plain and close together, so a row of them fits (and wraps) in a narrow window: less side padding than the application's own button style.
// (The vertical padding is the common one, so the height is too: only the width is tighter.)
inline void compactStyle(QWidget *button) { button->setStyleSheet(QStringLiteral("padding: 6px 8px;")); }

// A long explanation under a group of properties (what the mix amount means, how the projector's strength falls off...) is folded to one "About this" line, so the properties page is
// the rows a person edits and not three paragraphs they have read already. One click opens it, and the choice is kept for the pages built after it (every hint opens together).
inline bool &hintsOpen() {
	static bool open = false;
	return open;
}
inline void addHint(QFormLayout *f, const QString &text) {
	auto *host = new QWidget;
	auto *column = new QVBoxLayout(host);
	column->setContentsMargins(0, 0, 0, 0);
	column->setSpacing(2);
	auto *toggle = new QToolButton(host);
	toggle->setAutoRaise(true);
	toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	toggle->setCursor(Qt::PointingHandCursor);
	auto *body = new QLabel(text, host);
	body->setWordWrap(true);
	const auto show = [toggle, body](bool open) {
		toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);   // (Qt's own arrow: a triangle character is not in every font)
		toggle->setText(QCoreApplication::translate("SceneBuilderWidget", "About this"));
		body->setVisible(open);
	};
	show(hintsOpen());
	QObject::connect(toggle, &QToolButton::clicked, host, [show]() {
		hintsOpen() = !hintsOpen();
		show(hintsOpen());
	});
	column->addWidget(toggle, 0, Qt::AlignLeft);
	column->addWidget(body);
	f->addRow(host);
}

using scene_doc::LightKind;
using scene_doc::MaterialKind;
using scene_doc::Rgb;
using scene_doc::ShapeKind;

inline double toSrgb(double c) {
	c = std::clamp(c, 0.0, 1.0);
	return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}
inline double toLinear(double s) {
	s = std::clamp(s, 0.0, 1.0);
	return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}
// Colours are stored linear (what the renderer wants) and picked and drawn as sRGB (what a colour picker shows).
inline QColor toQColor(const Rgb &c) { return QColor::fromRgbF(toSrgb(c.r), toSrgb(c.g), toSrgb(c.b)); }
inline Rgb fromQColor(const QColor &q) { return Rgb{toLinear(q.redF()), toLinear(q.greenF()), toLinear(q.blueF())}; }

inline QString shapeLabel(ShapeKind k) {
	switch (k) {
		case ShapeKind::Sphere: return QObject::tr("Sphere");
		case ShapeKind::Box: return QObject::tr("Box");
		case ShapeKind::Quad: return QObject::tr("Quad (flat panel)");
		case ShapeKind::Disk: return QObject::tr("Disk");
		case ShapeKind::Cylinder: return QObject::tr("Cylinder");
		case ShapeKind::Cone: return QObject::tr("Cone");
		case ShapeKind::Mesh: return QObject::tr("Mesh (.ply file)");
		case ShapeKind::Pyramid: return QObject::tr("Pyramid");
		case ShapeKind::Wedge: return QObject::tr("Wedge (ramp)");
		case ShapeKind::Stairs: return QObject::tr("Stairs");
		case ShapeKind::Torus: return QObject::tr("Torus (ring)");
		case ShapeKind::Capsule: return QObject::tr("Capsule");
		case ShapeKind::Dome: return QObject::tr("Dome (half sphere)");
		case ShapeKind::Tube: return QObject::tr("Tube (pipe)");
		case ShapeKind::RoundedBox: return QObject::tr("Rounded box");
		case ShapeKind::Cyclorama: return QObject::tr("Cyclorama (studio backdrop)");
		case ShapeKind::Terrain: return QObject::tr("Terrain (rolling ground)");
	}
	return QString();
}
// What this platform calls the key that overrides snapping (Alt on Windows, Option on a Mac), and a standard shortcut as the user's keyboard writes it
// (Ctrl+Z, or the Command symbol on a Mac), so a tooltip never names a key that does nothing here.
inline QString altKeyName() {
#ifdef Q_OS_MAC
	return QObject::tr("Option");
#else
	return QStringLiteral("Alt");
#endif
}
// The key that makes a drag a free move (Qt's Ctrl is the Command key on a Mac).
inline QString ctrlKeyName() {
#ifdef Q_OS_MAC
	return QObject::tr("Command");
#else
	return QStringLiteral("Ctrl");
#endif
}
inline QString shortcutText(QKeySequence::StandardKey key) { return QKeySequence(key).toString(QKeySequence::NativeText); }
inline QString viewHint2d() {
	return QObject::tr("Wheel: zoom. Drag the background or right-drag: pan. %1- or Shift-click adds an item to what is picked or takes it out; %1- or Shift-drag the background picks what a box holds. The arrow keys nudge what is picked (Page Up and Page Down lift it; Shift: a whole unit, %1: a fine step).").arg(ctrlKeyName());
}
inline QString viewHint3d() {
	return QObject::tr("Drag the background: orbit (Shift-drag: pan). Right-drag also pans. Wheel: zoom. Pick Move, Rotate or Scale (W, E, R) and drag the arrows, rings or squares; drag an object to slide it on the floor, Shift-drag to lift it, and drag the white dot (or hold %1 while dragging) to move it in any direction. Shift- or %1-click (without dragging) adds an item to what is picked or takes it out. The arrow keys nudge what is picked (Page Up and Page Down lift it; Shift: a whole unit, %1: a fine step).").arg(ctrlKeyName());
}

inline QString propLabel(scene_doc::PropKind k) {
	switch (k) {
		case scene_doc::PropKind::Table: return QObject::tr("Table");
		case scene_doc::PropKind::Chair: return QObject::tr("Chair");
		case scene_doc::PropKind::Tree: return QObject::tr("Tree");
		case scene_doc::PropKind::Snowman: return QObject::tr("Snowman");
		case scene_doc::PropKind::Column: return QObject::tr("Column");
		case scene_doc::PropKind::StreetLamp: return QObject::tr("Street lamp");
	}
	return QString();
}
inline QString blockyLabel(scene_doc::BlockyKind k) {
	using K = scene_doc::BlockyKind;
	switch (k) {
		case K::GrassBlock: return QObject::tr("Grass block");
		case K::DirtBlock: return QObject::tr("Dirt block");
		case K::StoneBlock: return QObject::tr("Stone block");
		case K::PlanksBlock: return QObject::tr("Planks block");
		case K::LogBlock: return QObject::tr("Log block");
		case K::SandBlock: return QObject::tr("Sand block");
		case K::GlassBlock: return QObject::tr("Glass block");
		case K::WaterBlock: return QObject::tr("Water block");
		case K::TntBlock: return QObject::tr("TNT block");
		case K::GoldBlock: return QObject::tr("Gold block");
		case K::DiamondOre: return QObject::tr("Diamond ore");
		case K::GlowBlock: return QObject::tr("Glowing block");
		case K::LavaBlock: return QObject::tr("Lava block");
		case K::Pumpkin: return QObject::tr("Pumpkin");
		case K::LanternPumpkin: return QObject::tr("Lantern pumpkin");
		case K::GreenMonster: return QObject::tr("Green monster");
		case K::Pig: return QObject::tr("Pig");
		case K::Sheep: return QObject::tr("Sheep");
		case K::Explorer: return QObject::tr("Explorer");
		case K::Skeleton: return QObject::tr("Skeleton");
		case K::OakTree: return QObject::tr("Oak tree");
		case K::Cottage: return QObject::tr("Cottage");
		case K::Torch: return QObject::tr("Torch");
		case K::Chest: return QObject::tr("Chest");
		case K::CraftingTable: return QObject::tr("Crafting table");
		case K::Furnace: return QObject::tr("Furnace");
		case K::Bed: return QObject::tr("Bed");
		case K::Poppy: return QObject::tr("Poppy");
		case K::Dandelion: return QObject::tr("Dandelion");
		case K::Fence: return QObject::tr("Fence");
		case K::RainbowWool: return QObject::tr("Rainbow wool");
		case K::PurplePortal: return QObject::tr("Purple portal");
	}
	return QString();
}
inline QString volumeLabel(scene_doc::VolumeKind k) {
	switch (k) {
		case scene_doc::VolumeKind::Fog: return QObject::tr("Fog");
		case scene_doc::VolumeKind::Smoke: return QObject::tr("Smoke");
		case scene_doc::VolumeKind::Cloud: return QObject::tr("Cloud");
		case scene_doc::VolumeKind::None: break;
	}
	return QString();
}
// How a volume is drawn in the views: a see-through blob of what it looks like (white mist, grey smoke, a pale cloud), so what is behind it still shows.
inline QColor volumeDrawColor(const scene_doc::Volume &v) {
	QColor c = v.kind == scene_doc::VolumeKind::Cloud ? QColor(246, 249, 255) : toQColor(v.color).lighter(v.kind == scene_doc::VolumeKind::Smoke ? 100 : 110);
	c.setAlpha(v.kind == scene_doc::VolumeKind::Smoke ? 115 : 80);
	return c;
}
// The colour a marble or noise pattern is drawn in, in the views (they only show a flat colour per object): the renderer's marble is pale stone, its noise soft grey.
inline QColor patternDrawColor(scene_doc::Pattern p) { return p == scene_doc::Pattern::Marble ? QColor(222, 222, 230) : QColor(160, 160, 168); }
inline QString lightLabel(LightKind k) {
	switch (k) {
		case LightKind::Point: return QObject::tr("Point light");
		case LightKind::Spot: return QObject::tr("Spotlight");
		case LightKind::Distant: return QObject::tr("Sun (distant light)");
		case LightKind::Infinite: return QObject::tr("Sky (surrounds the scene)");
		case LightKind::Projector: return QObject::tr("Projector (slide light)");
	}
	return QString();
}
// The lighting setups (scene_lighting.h), named and described in the user's language.
inline QString lightingLabel(scene_doc::lighting::Preset p) {
	switch (p) {
		case scene_doc::lighting::Preset::Studio: return QObject::tr("Studio (three lights)");
		case scene_doc::lighting::Preset::Overcast: return QObject::tr("Overcast day");
		case scene_doc::lighting::Preset::Sunny: return QObject::tr("Sunny day");
		case scene_doc::lighting::Preset::GoldenHour: return QObject::tr("Golden hour");
		case scene_doc::lighting::Preset::Moonlit: return QObject::tr("Moonlit night");
		case scene_doc::lighting::Preset::LightBox: return QObject::tr("Light box");
	}
	return QString();
}
inline QString lightingNote(scene_doc::lighting::Preset p) {
	switch (p) {
		case scene_doc::lighting::Preset::Studio: return QObject::tr("A key light from the camera's left, a softer fill from its right and a rim light from behind, on a dark backdrop.");
		case scene_doc::lighting::Preset::Overcast: return QObject::tr("Even, soft light from a grey-white sky with no sun and no hard shadows.");
		case scene_doc::lighting::Preset::Sunny: return QObject::tr("A clear sky with the sun high behind the camera's right shoulder.");
		case scene_doc::lighting::Preset::GoldenHour: return QObject::tr("A clear sky with the sun low and to the side, long shadows and warm light.");
		case scene_doc::lighting::Preset::Moonlit: return QObject::tr("A dark blue sky and a dim cool moon from above and behind.");
		case scene_doc::lighting::Preset::LightBox: return QObject::tr("A bright white surround with a little light from above, as for a product shot.");
	}
	return QString();
}
// "Sphere", then "Sphere 2", "Sphere 3", ...: the first of those not already in `taken`.
inline QString uniqueName(const QString &base, const QStringList &taken) {
	QString name = base;
	for (int n = 2; taken.contains(name); ++n) name = QString("%1 %2").arg(base).arg(n);
	return name;
}

// The names of the ready-made materials (src/shared/scene_materials.h), translated: the table there is Qt-free, so the strings are declared for translation
// here and looked up by their English name.
inline QString presetLabel(const char *name) { return QCoreApplication::translate("MaterialPreset", name); }
inline QString sensorLabel(const char *name) { return QCoreApplication::translate("SensorPreset", name); }
inline QString lensLabel(const char *name) { return QCoreApplication::translate("LensPreset", name); }
[[maybe_unused]] inline void cameraPresetNamesForTranslation() {
	QT_TRANSLATE_NOOP("SensorPreset", "Full frame (36 x 24 mm)"); QT_TRANSLATE_NOOP("SensorPreset", "APS-C (23.5 x 15.6 mm)"); QT_TRANSLATE_NOOP("SensorPreset", "APS-C Canon (22.3 x 14.9 mm)");
	QT_TRANSLATE_NOOP("SensorPreset", "Micro Four Thirds (17.3 x 13 mm)"); QT_TRANSLATE_NOOP("SensorPreset", "Super 35 film (24.9 x 18.7 mm)");
	QT_TRANSLATE_NOOP("SensorPreset", "Medium format (43.8 x 32.9 mm)"); QT_TRANSLATE_NOOP("SensorPreset", "IMAX 70 mm film (70.4 x 52.6 mm)");
	QT_TRANSLATE_NOOP("SensorPreset", "1 inch (13.2 x 8.8 mm)"); QT_TRANSLATE_NOOP("SensorPreset", "Phone (7.6 x 5.7 mm)");
	QT_TRANSLATE_NOOP("LensPreset", "14 mm ultra wide"); QT_TRANSLATE_NOOP("LensPreset", "24 mm wide"); QT_TRANSLATE_NOOP("LensPreset", "35 mm street");
	QT_TRANSLATE_NOOP("LensPreset", "50 mm normal"); QT_TRANSLATE_NOOP("LensPreset", "85 mm portrait"); QT_TRANSLATE_NOOP("LensPreset", "135 mm telephoto");
	QT_TRANSLATE_NOOP("LensPreset", "200 mm long telephoto"); QT_TRANSLATE_NOOP("LensPreset", "400 mm super telephoto");
}
inline QString presetGroupLabel(const char *group) { return QCoreApplication::translate("MaterialPresetGroup", group); }
[[maybe_unused]] inline void presetNamesForTranslation() {
	QT_TRANSLATE_NOOP("MaterialPreset", "Chalk"); QT_TRANSLATE_NOOP("MaterialPreset", "Black rubber"); QT_TRANSLATE_NOOP("MaterialPreset", "Terracotta");
	QT_TRANSLATE_NOOP("MaterialPreset", "Concrete"); QT_TRANSLATE_NOOP("MaterialPreset", "Red plastic"); QT_TRANSLATE_NOOP("MaterialPreset", "Blue plastic");
	QT_TRANSLATE_NOOP("MaterialPreset", "White ceramic"); QT_TRANSLATE_NOOP("MaterialPreset", "Car paint"); QT_TRANSLATE_NOOP("MaterialPreset", "Gold");
	QT_TRANSLATE_NOOP("MaterialPreset", "Copper"); QT_TRANSLATE_NOOP("MaterialPreset", "Silver"); QT_TRANSLATE_NOOP("MaterialPreset", "Aluminium");
	QT_TRANSLATE_NOOP("MaterialPreset", "Chrome"); QT_TRANSLATE_NOOP("MaterialPreset", "Brushed steel"); QT_TRANSLATE_NOOP("MaterialPreset", "Clear glass");
	QT_TRANSLATE_NOOP("MaterialPreset", "Frosted glass"); QT_TRANSLATE_NOOP("MaterialPreset", "Water"); QT_TRANSLATE_NOOP("MaterialPreset", "Diamond");
	QT_TRANSLATE_NOOP("MaterialPreset", "Wax"); QT_TRANSLATE_NOOP("MaterialPreset", "Leaf"); QT_TRANSLATE_NOOP("MaterialPreset", "Paper");
	QT_TRANSLATE_NOOP("MaterialPreset", "Skin"); QT_TRANSLATE_NOOP("MaterialPreset", "Milk"); QT_TRANSLATE_NOOP("MaterialPreset", "Marble stone");
	QT_TRANSLATE_NOOP("MaterialPreset", "Jade"); QT_TRANSLATE_NOOP("MaterialPreset", "Candle wax");
	QT_TRANSLATE_NOOP("MaterialPresetGroup", "Matte"); QT_TRANSLATE_NOOP("MaterialPresetGroup", "Plastic"); QT_TRANSLATE_NOOP("MaterialPresetGroup", "Metal");
	QT_TRANSLATE_NOOP("MaterialPresetGroup", "Glass"); QT_TRANSLATE_NOOP("MaterialPresetGroup", "Translucent"); QT_TRANSLATE_NOOP("MaterialPresetGroup", "Subsurface");
}

inline QString materialLabel(MaterialKind k) {
	switch (k) {
		case MaterialKind::Diffuse: return QObject::tr("Matte (diffuse)");
		case MaterialKind::Conductor: return QObject::tr("Metal");
		case MaterialKind::Dielectric: return QObject::tr("Glass");
		case MaterialKind::CoatedDiffuse: return QObject::tr("Glossy paint (coated)");
		case MaterialKind::DiffuseTransmission: return QObject::tr("Translucent (paper, leaves)");
		case MaterialKind::Subsurface: return QObject::tr("Subsurface (skin, wax, milk)");
	}
	return QString();
}



// Rotates p by the object's three angles, in the order the writer applies them: about X, then Y, then Z.
inline scene_doc::Float3 rotateXYZ(scene_doc::Float3 p, const scene_doc::Float3 &deg) {
	constexpr double kPi = 3.14159265358979323846;
	auto rad = [](double d) { return d * kPi / 180.0; };
	if (deg.x != 0.0) {
		const double c = std::cos(rad(deg.x)), s = std::sin(rad(deg.x));
		const double y = p.y * c - p.z * s, z = p.y * s + p.z * c;
		p.y = y; p.z = z;
	}
	if (deg.y != 0.0) {
		const double c = std::cos(rad(deg.y)), s = std::sin(rad(deg.y));
		const double x = p.x * c + p.z * s, z = -p.x * s + p.z * c;
		p.x = x; p.z = z;
	}
	if (deg.z != 0.0) {
		const double c = std::cos(rad(deg.z)), s = std::sin(rad(deg.z));
		const double x = p.x * c - p.y * s, y = p.x * s + p.y * c;
		p.x = x; p.y = y;
	}
	return p;
}

} // namespace scene_builder_ui
