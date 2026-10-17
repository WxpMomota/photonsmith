// scene_builder_material_photo.cpp -- the Scene Builder's "Make from a photo..." for a material (the button under Picture in the properties): one photo of a surface becomes the
// material's colour picture (the photo's broad light taken out, made to repeat with no joins), a height map for the bump, and starting values for its gloss and relief.
//
// The image processing is src/shared/material_from_photo.h (no model, nothing to install); this file loads the photo, makes the pictures, saves them and sets the object's material.
#include "scene_builder_widget.h"
#include "app_log.h"
#include "../src/shared/material_from_photo.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

// The photo upright, cut to the square in its middle (a little inside its edges, where a lens is weakest) and scaled to `size`; null with `error` set if it cannot be read.
QImage squarePatch(const QString &path, int size, QString *error) {
	QImageReader reader(path);
	reader.setAutoTransform(true);   // a phone photo stores its turn as a flag
	const QImage photo = reader.read();
	if (photo.isNull()) {
		if (error) *error = QObject::tr("The photo %1 could not be read: %2").arg(path, reader.errorString());
		return QImage();
	}
	const int side = static_cast<int>(std::min(photo.width(), photo.height()) * 0.92);
	if (side < 32) {
		if (error) *error = QObject::tr("The photo is too small to make a material from.");
		return QImage();
	}
	const QRect middle((photo.width() - side) / 2, (photo.height() - side) / 2, side, side);
	return photo.copy(middle).convertToFormat(QImage::Format_RGB888).scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// How many times the picture repeats across the object, for a patch of `patchMetres` of the surface: a quad and a box take the repeat count for the faces we see (the thin side of a slab
// is its face). False for a shape that writes its own texture coordinates (the repeat count has no meaning there).
bool repeatsFor(const scene_doc::Object &o, double patchMetres, double *u, double *v) {
	if (patchMetres <= 0.0) return false;
	if (o.shape == scene_doc::ShapeKind::Quad) {
		*u = o.size.x / patchMetres;
		*v = o.size.z / patchMetres;
		return true;
	}
	if (o.shape != scene_doc::ShapeKind::Box) return false;
	// a Box's picture: +-Y faces u = z, v = x; +-X faces u = y, v = z; +-Z faces u = x, v = y (scene_pbrt_writer.h writeBox). A slab's big faces are across its two longer sides.
	const scene_doc::Float3 s = o.size;
	if (s.y <= s.x && s.y <= s.z) { *u = s.z / patchMetres; *v = s.x / patchMetres; }
	else if (s.x <= s.y && s.x <= s.z) { *u = s.y / patchMetres; *v = s.z / patchMetres; }
	else { *u = s.x / patchMetres; *v = s.y / patchMetres; }
	return true;
}

QString safeName(const QString &base) {
	QString s;
	for (const QChar c : base) s += (c.isLetterOrNumber() || c == '-' || c == '_') ? c : QChar('-');
	return s.isEmpty() ? QStringLiteral("surface") : s.left(40);
}

}  // namespace

bool SceneBuilderWidget::applyMaterialPhoto(int index, const QString &photo, int surfaceKind, double patchCm, QString *error, QStringList *notes) {
	if (index < 0 || index >= static_cast<int>(m_doc.objects.size())) {
		if (error) *error = tr("There is no object to put the material on.");
		return false;
	}
	const auto &kinds = material_from_photo::surfaceKinds();
	const material_from_photo::SurfaceKind &kind = kinds[static_cast<size_t>(std::clamp(surfaceKind, 0, static_cast<int>(kinds.size()) - 1))];

	QImageReader probe(photo);
	probe.setAutoTransform(true);
	const QSize shown = probe.size().isValid() ? probe.size() : QSize(1024, 1024);
	const int size = material_from_photo::pictureSize(std::min(shown.width(), shown.height()));
	const QImage patch = squarePatch(photo, size, error);
	if (patch.isNull()) return false;
	std::vector<unsigned char> rgb(static_cast<size_t>(size) * size * 3);
	for (int y = 0; y < size; ++y) std::copy(patch.constScanLine(y), patch.constScanLine(y) + size * 3, rgb.begin() + static_cast<std::ptrdiff_t>(y) * size * 3);
	const material_from_photo::Result made = material_from_photo::make(rgb.data(), size);
	if (made.size == 0) {
		if (error) *error = tr("A material could not be made from this photo.");
		return false;
	}

	// the pictures, kept with the other pictures the Builder makes (the scene refers to them by path)
	const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pictures";
	const QFileInfo info(photo);
	const QString key = QString::number(qHash(info.absoluteFilePath() + QString::number(info.lastModified().toSecsSinceEpoch())), 16);
	const QString stem = folder + "/material-" + safeName(info.completeBaseName()) + "-" + key;
	if (!QDir().mkpath(folder)) {
		if (error) *error = tr("Could not create the folder %1.").arg(folder);
		return false;
	}
	QImage colour(made.albedo.data(), size, size, size * 3, QImage::Format_RGB888);
	QImage height(made.height.data(), size, size, size, QImage::Format_Grayscale8);
	if (!colour.copy().save(stem + "-colour.png") || !height.copy().save(stem + "-height.png")) {
		if (error) *error = tr("The material's pictures could not be saved in %1.").arg(folder);
		return false;
	}

	double repeatU = 1.0, repeatV = 1.0;
	const bool setRepeats = repeatsFor(m_doc.objects[static_cast<size_t>(index)], patchCm / 100.0, &repeatU, &repeatV);
	edit(QString(), [&]() {
		scene_doc::Material &m = m_doc.objects[static_cast<size_t>(index)].material;
		m.kind = kind.coated ? scene_doc::MaterialKind::CoatedDiffuse : scene_doc::MaterialKind::Diffuse;
		if (kind.coated) {
			m.roughness = kind.coatRoughness;
			m.ior = 1.5;
		}
		m.imageFile = (stem + "-colour.png").toStdString();
		m.checker = false;
		m.pattern = scene_doc::Pattern::None;
		m.bumpFile = (stem + "-height.png").toStdString();
		m.bumpKind = scene_doc::BumpKind::Height;
		m.bumpStrength = kind.reliefMetres;
		m.color = {std::clamp(made.averageColor[0], 0.03, 0.9), std::clamp(made.averageColor[1], 0.03, 0.9), std::clamp(made.averageColor[2], 0.03, 0.9)};
		if (setRepeats) {
			m.uvScaleU = std::max(repeatU, 0.05);
			m.uvScaleV = std::max(repeatV, 0.05);
			m.uvOffsetU = m.uvOffsetV = 0.0;
		}
	});
	QTimer::singleShot(0, this, [this]() { rebuildInspector(); });   // (the properties show the new type, picture and bump map)
	AppLog::info(QStringLiteral("photo"), QStringLiteral("material from a photo: %1 (%2, %3 px, lighting range %4)").arg(photo, QString::fromLatin1(kind.name)).arg(size).arg(made.lightingRange, 0, 'g', 3));
	if (notes) {
		if (made.lightingRange > 1.5) *notes << tr("The photo was lit unevenly (the light varied %1 to 1 across it); that was taken out, so the colour is the surface's own.").arg(made.lightingRange, 0, 'f', 1);
		*notes << tr("The relief is a guess made from the picture alone (dark is low, light is high); change Bump height if it looks too strong or too flat.");
		if (patchCm > 0.0 && !setRepeats) *notes << tr("This shape sets its own texture coordinates, so the number of repeats was not set: use the Texture repeats rows if the pattern looks too big or too small.");
		if (patchCm <= 0.0) *notes << tr("The number of repeats was left as it was: use the Texture repeats rows to make the pattern the right size.");
	}
	return true;
}

void SceneBuilderWidget::materialFromPhoto(int index) {
	if (index < 0 || index >= static_cast<int>(m_doc.objects.size())) return;
	const QString photo = QFileDialog::getOpenFileName(this, tr("Choose a photo of the surface"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp *.tga)"));
	if (photo.isEmpty()) return;

	// a first look: the surface repeated 2 x 2, and the height map, so a photo with a hard-to-hide pattern or the wrong part of it is seen before it is used
	QString problem;
	const int previewSize = 256;
	const QImage patch = squarePatch(photo, previewSize, &problem);
	if (patch.isNull()) {
		QMessageBox::warning(this, tr("Could not read the photo"), problem);
		return;
	}
	std::vector<unsigned char> rgb(static_cast<size_t>(previewSize) * previewSize * 3);
	for (int y = 0; y < previewSize; ++y) std::copy(patch.constScanLine(y), patch.constScanLine(y) + previewSize * 3, rgb.begin() + static_cast<std::ptrdiff_t>(y) * previewSize * 3);
	const material_from_photo::Result preview = material_from_photo::make(rgb.data(), previewSize);
	const QImage colour(preview.albedo.data(), previewSize, previewSize, previewSize * 3, QImage::Format_RGB888);
	const QImage relief(preview.height.data(), previewSize, previewSize, previewSize, QImage::Format_Grayscale8);
	QPixmap tiled(previewSize * 2, previewSize * 2);
	{
		QPainter p(&tiled);
		for (int ty = 0; ty < 2; ++ty)
			for (int tx = 0; tx < 2; ++tx) p.drawImage(tx * previewSize, ty * previewSize, colour);
	}

	QDialog dialog(this);
	dialog.setWindowTitle(tr("Material from a photo"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(tr("The photo becomes the object's picture, made to repeat with no joins and with its uneven light taken out, plus a bump map that guesses the relief from it. "
	                            "On the left the picture repeated twice across and down; on the right the bump map (light is high)."),
	                         &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *pictures = new QHBoxLayout();
	auto *tiledLabel = new QLabel(&dialog);
	tiledLabel->setPixmap(tiled.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	auto *reliefLabel = new QLabel(&dialog);
	reliefLabel->setPixmap(QPixmap::fromImage(relief.copy()).scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	pictures->addWidget(tiledLabel);
	pictures->addWidget(reliefLabel);
	layout->addLayout(pictures);
	auto *form = new QFormLayout();
	auto *what = new QComboBox(&dialog);
	// (in the order of material_from_photo::surfaceKinds())
	const QStringList kindNames = {tr("Wood"), tr("Tile or polished stone"), tr("Brick, rough stone or concrete"), tr("Fabric or carpet"), tr("Leather"), tr("Plaster or paint"), tr("Something else")};
	for (size_t k = 0; k < material_from_photo::surfaceKinds().size(); ++k) what->addItem(k < static_cast<size_t>(kindNames.size()) ? kindNames[static_cast<int>(k)] : QString::fromLatin1(material_from_photo::surfaceKinds()[k].name));
	what->setToolTip(tr("Sets the gloss (a varnished floor, a glazed tile, leather have a coat) and how tall the relief is. These are starting values: change them in the properties."));
	form->addRow(tr("What it is"), what);
	auto *patch_cm = new QDoubleSpinBox(&dialog);
	patch_cm->setRange(0.0, 1000.0);
	patch_cm->setDecimals(0);
	patch_cm->setSingleStep(10.0);
	patch_cm->setValue(40.0);
	patch_cm->setSuffix(tr(" cm"));
	patch_cm->setSpecialValueText(tr("leave the repeats as they are"));
	patch_cm->setToolTip(tr("How wide the photographed patch really is. The picture then repeats across the object at that size (a quad or a box; other shapes keep their own texture repeats)."));
	form->addRow(tr("Width of the patch in the photo"), patch_cm);
	layout->addLayout(form);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted) return;

	QString error;
	QStringList notes;
	if (!applyMaterialPhoto(index, photo, what->currentIndex(), patch_cm->value(), &error, &notes)) {
		QMessageBox::warning(this, tr("Could not make the material"), error);
		return;
	}
	emit statusMessage(tr("Made a material from %1. %2").arg(QFileInfo(photo).fileName(), notes.isEmpty() ? QString() : notes.last()));
}
