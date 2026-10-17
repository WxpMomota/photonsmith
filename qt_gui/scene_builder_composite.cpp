// scene_builder_composite.cpp -- the Scene Builder's "Render into the photo..." (in the camera's properties, once a photo is laid over the camera view): the scene, seen through the camera that was
// matched to the photo, is rendered three times (as it is, without the inserted objects, and as a mask of them) and put into the photo with its shadows.
//
// The method and its arithmetic are src/shared/photo_composite.h; this file runs the three renders with the renderer, reads them back as linear radiance, calls the compositor and saves the picture.
#include "scene_builder_widget.h"
#include "app_log.h"
#include "../src/shared/photo_composite.h"
#include "../src/external/tinyexr.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QProcess>
#include <QProgressDialog>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <cmath>
#include <cstdlib>

namespace {

// A linear EXR as w x h x 3 floats (what the renderer wrote with --output x.exr); false when it cannot be read.
bool readLinear(const QString &path, int *w, int *h, std::vector<float> *rgb) {
	float *rgba = nullptr;
	const char *err = nullptr;
	const QByteArray name = QFile::encodeName(path);
	if (LoadEXR(&rgba, w, h, name.constData(), &err) != TINYEXR_SUCCESS) {
		if (err) FreeEXRErrorMessage(err);
		return false;
	}
	rgb->resize(static_cast<size_t>(*w) * static_cast<size_t>(*h) * 3);
	for (int i = 0; i < *w * *h; ++i)
		for (int c = 0; c < 3; ++c) (*rgb)[3 * static_cast<size_t>(i) + static_cast<size_t>(c)] = std::isfinite(rgba[4 * i + c]) ? rgba[4 * i + c] : 0.0f;
	std::free(rgba);
	return true;
}

}  // namespace

bool SceneBuilderWidget::renderIntoPhoto(const QString &outPng, const std::vector<int> &groundObjects, double exposureStops, int samples, QString *error, QStringList *notes) {
	if (m_cameraPhoto.isNull()) {
		if (error) *error = tr("Match the camera to a photo first (Camera section: Match to a photo...): the scene is put into that photo.");
		return false;
	}
	if (!QFileInfo::exists(launcherPath())) {
		if (error) *error = tr("The renderer (%1) was not found next to the program.").arg(launcherPath());
		return false;
	}
	if (scene_doc::hasErrors(scene_doc::validate(m_doc))) {
		if (error) *error = tr("Fix the problems listed under the properties first.");
		return false;
	}
	// the picture: the photo, at most 1000 pixels across (three renders at that size)
	const int w = std::min(m_cameraPhoto.width(), 1000);
	const int h = std::max(16, static_cast<int>(std::lround(static_cast<double>(w) * m_cameraPhoto.height() / m_cameraPhoto.width())));
	const QImage photo = m_cameraPhoto.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
	std::vector<unsigned char> photoPixels(static_cast<size_t>(w) * h * 3);
	for (int y = 0; y < h; ++y) std::copy(photo.constScanLine(y), photo.constScanLine(y) + w * 3, photoPixels.begin() + static_cast<std::ptrdiff_t>(y) * w * 3);

	std::vector<photo_composite::Role> roles = photo_composite::guessRoles(m_doc);
	for (size_t i = 0; i < roles.size(); ++i) {
		if (roles[i] == photo_composite::Role::Light) continue;
		roles[i] = std::find(groundObjects.begin(), groundObjects.end(), static_cast<int>(i)) != groundObjects.end() ? photo_composite::Role::Ground : photo_composite::Role::Inserted;
	}
	int inserted = 0, grounds = 0;
	for (const photo_composite::Role r : roles) {
		inserted += r == photo_composite::Role::Inserted ? 1 : 0;
		grounds += r == photo_composite::Role::Ground ? 1 : 0;
	}
	if (inserted == 0) {
		if (error) *error = tr("Every object is marked as the ground or a light, so there is nothing to put into the photo.");
		return false;
	}
	const photo_composite::Passes passes = photo_composite::makePasses(m_doc, roles);

	struct Pass { const char *name; const scene_doc::Document *doc; int samples, depth; };
	const Pass list[3] = {{"full", &m_doc, samples, m_doc.render.maxDepth}, {"ground", &passes.ground, samples, m_doc.render.maxDepth}, {"mask", &passes.mask, 16, 2}};
	const QString dir = workFolder() + "/composite";
	QDir().mkpath(dir);
	QProgressDialog busy(tr("Rendering the scene for the photo..."), tr("Cancel"), 0, 3, this);
	busy.setWindowTitle(tr("Render into the photo"));
	busy.setWindowModality(Qt::WindowModal);
	busy.setMinimumDuration(0);
	busy.show();
	std::vector<float> rgb[3];
	QElapsedTimer clock;
	clock.start();
	for (int p = 0; p < 3; ++p) {
		busy.setValue(p);
		busy.setLabelText(tr("Rendering the scene for the photo: %1 of 3...").arg(p + 1));
		QCoreApplication::processEvents();
		const QString scene = dir + "/" + list[p].name + ".pbrt", exr = dir + "/" + list[p].name + ".exr";
		QFile::remove(exr);
		QFile file(scene);
		if (!file.open(QIODevice::WriteOnly)) {
			if (error) *error = tr("Could not write %1.").arg(scene);
			return false;
		}
		file.write(QByteArray::fromStdString(scene_doc::toPbrt(*list[p].doc)));
		file.close();
		QProcess process;
		process.setProcessChannelMode(QProcess::MergedChannels);
		process.setWorkingDirectory(QCoreApplication::applicationDirPath());
		process.start(launcherPath(), {m_gpuCheck && m_gpuCheck->isChecked() ? "--gpu" : "--cpu", "--output", exr, "--height", QString::number(h), QString::number(w), QString::number(list[p].samples), QString::number(list[p].depth), scene});
		for (int waited = 0; process.state() != QProcess::NotRunning && waited < 20 * 60 * 1000 && !busy.wasCanceled(); waited += 50) {
			process.waitForFinished(50);
			QCoreApplication::processEvents();
		}
		if (process.state() != QProcess::NotRunning) {
			process.kill();
			process.waitForFinished(3000);
			if (error && !busy.wasCanceled()) *error = tr("A render took too long and was stopped.");
			return false;
		}
		int rw = 0, rh = 0;
		if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !readLinear(exr, &rw, &rh, &rgb[p])) {
			if (error) *error = tr("The renderer could not make the %1 picture. Its output:\n%2").arg(QString::fromLatin1(list[p].name), QString::fromLocal8Bit(process.readAll()).right(400));
			return false;
		}
		if (rw != w || rh != h) {
			if (error) *error = tr("The renderer made a picture of %1 x %2, not the %3 x %4 asked for.").arg(rw).arg(rh).arg(w).arg(h);
			return false;
		}
	}
	busy.setValue(3);
	std::vector<float> mask(static_cast<size_t>(w) * h);
	for (size_t i = 0; i < mask.size(); ++i) mask[i] = std::min(1.0f, std::max(0.0f, (rgb[2][3 * i] + rgb[2][3 * i + 1] + rgb[2][3 * i + 2]) / 3.0f));
	photo_composite::Options options;
	options.exposureStops = exposureStops;
	const std::vector<unsigned char> out = photo_composite::composite(photoPixels.data(), rgb[0].data(), rgb[1].data(), mask.data(), w, h, options);
	if (out.empty()) {
		if (error) *error = tr("The picture could not be put together.");
		return false;
	}
	QImage result(w, h, QImage::Format_RGB888);
	for (int y = 0; y < h; ++y) std::copy(out.begin() + static_cast<std::ptrdiff_t>(y) * w * 3, out.begin() + static_cast<std::ptrdiff_t>(y + 1) * w * 3, result.scanLine(y));
	if (!QDir().mkpath(QFileInfo(outPng).absolutePath()) || !result.save(outPng)) {
		if (error) *error = tr("The picture could not be saved as %1.").arg(outPng);
		return false;
	}
	AppLog::info(QStringLiteral("photo"), QStringLiteral("render into the photo: %1 (%2 x %3, %4 inserted objects, %5 ground, %6 samples, %7 s)").arg(outPng).arg(w).arg(h).arg(inserted).arg(grounds).arg(samples).arg(clock.elapsed() / 1000.0, 0, 'f', 1));
	if (notes) {
		*notes << tr("%n object(s) were put into the photo; the ground they stand on is not drawn (only the shadows and reflections they cause are).", "", inserted);
		*notes << tr("The objects are shown at the exposure the Builder gives them%1; the photo's own exposure is not known, so use Exposure if they look too bright or too dark.").arg(exposureStops != 0.0 ? tr(" (%1 stops)").arg(exposureStops, 0, 'f', 2) : QString());
		if (grounds == 0) *notes << tr("No object is marked as the ground, so there are no shadows on the photo: tick the floor the objects stand on.");
	}
	return true;
}

void SceneBuilderWidget::renderIntoPhotoDialog() {
	if (m_cameraPhoto.isNull()) {
		QMessageBox::information(this, tr("Render into the photo"), tr("Match the camera to a photo first (Camera section: Match to a photo...): the scene is put into that photo."));
		return;
	}
	const std::vector<photo_composite::Role> guessed = photo_composite::guessRoles(m_doc);
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Render into the photo"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(tr("The scene, seen through the camera matched to the photo, is rendered and put into the photo, with the shadows and reflections it casts on the ground. Tick the objects that are the "
	                            "ground: the photo already has it, so it is not drawn, and only the shadows it catches count. Everything else (except lights) is put into the photo. The scene is rendered three times, "
	                            "so it takes about three times as long as one picture."),
	                         &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *objects = new QListWidget(&dialog);
	std::vector<int> indices;
	for (size_t i = 0; i < m_doc.objects.size(); ++i) {
		if (guessed[i] == photo_composite::Role::Light) continue;   // (a lamp lights the scene and is not put into the photo)
		auto *item = new QListWidgetItem(QString::fromStdString(m_doc.objects[i].name), objects);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(guessed[i] == photo_composite::Role::Ground ? Qt::Checked : Qt::Unchecked);
		indices.push_back(static_cast<int>(i));
	}
	objects->setToolTip(tr("Ticked: the ground the photo already has (its shadows are used, it is not drawn). Unticked: put into the photo."));
	layout->addWidget(objects);
	auto *form = new QFormLayout();
	auto *exposure = new QDoubleSpinBox(&dialog);
	exposure->setRange(-4.0, 4.0);
	exposure->setSingleStep(0.25);
	exposure->setDecimals(2);
	exposure->setSuffix(tr(" stops"));
	exposure->setToolTip(tr("How much brighter (+) or darker (-) the objects are made, to match the photo's exposure. Each stop doubles or halves their light."));
	form->addRow(tr("Exposure of the objects"), exposure);
	auto *quality = new QSpinBox(&dialog);
	quality->setRange(8, 4096);
	quality->setValue(std::max(32, std::min(512, m_doc.render.samples)));
	quality->setToolTip(tr("Samples per pixel: more makes the shadows smoother and takes longer."));
	form->addRow(tr("Samples per pixel"), quality);
	layout->addLayout(form);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted) return;
	std::vector<int> ground;
	for (int row = 0; row < objects->count(); ++row)
		if (objects->item(row)->checkState() == Qt::Checked) ground.push_back(indices[static_cast<size_t>(row)]);
	const QString suggested = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/render-into-photo-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".png";
	const QString path = QFileDialog::getSaveFileName(this, tr("Save the picture as"), suggested, tr("PNG pictures (*.png)"));
	if (path.isEmpty()) return;

	QString error;
	QStringList notes;
	if (!renderIntoPhoto(path, ground, exposure->value(), quality->value(), &error, &notes)) {
		if (!error.isEmpty()) QMessageBox::warning(this, tr("Could not render into the photo"), error);
		return;
	}
	QDialog shown(this);
	shown.setWindowTitle(tr("Render into the photo"));
	auto *box = new QVBoxLayout(&shown);
	auto *picture = new QLabel(&shown);
	const QPixmap pixmap(path);
	picture->setPixmap(pixmap.width() > 900 ? pixmap.scaledToWidth(900, Qt::SmoothTransformation) : pixmap);
	auto *scroll = new QScrollArea(&shown);
	scroll->setWidget(picture);
	scroll->setMinimumSize(std::min(920, pixmap.width() + 20), std::min(640, pixmap.height() + 20));
	box->addWidget(scroll);
	auto *text = new QLabel(tr("Saved as %1.\n\n- %2").arg(QDir::toNativeSeparators(path), notes.join("\n- ")), &shown);
	text->setWordWrap(true);
	text->setTextInteractionFlags(Qt::TextSelectableByMouse);
	box->addWidget(text);
	auto *ok = new QDialogButtonBox(QDialogButtonBox::Ok, &shown);
	connect(ok, &QDialogButtonBox::accepted, &shown, &QDialog::accept);
	box->addWidget(ok);
	shown.exec();
}
