// scene_builder_composite.cpp -- the Scene Builder's "Render into the photo..." (in the camera's properties, once a photo is laid over the camera view): the scene, seen through the camera that was
// matched to the photo, is rendered three times (as it is, without the inserted objects, and as a mask of them) and put into the photo with its shadows.
//
// The method and its arithmetic are src/shared/photo_composite.h; this file runs the three renders with the renderer, reads them back as linear radiance, calls the compositor and saves the picture.
#include "scene_builder_widget.h"
#include "app_log.h"
#include "../src/shared/photo_composite.h"
#include "../src/external/tinyexr.h"

#include <QCheckBox>
#include <QComboBox>
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
#include <QRegularExpression>
#include <QProgressDialog>
#include <QHeaderView>
#include <QTableWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <algorithm>
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

bool SceneBuilderWidget::renderIntoPhoto(const QString &outPng, const std::vector<int> &groundObjects, double exposureStops, int samples, bool matchToPhoto, QString *error, QStringList *notes) {
	QImage picture;
	if (!renderIntoPhotoImage(&picture, groundObjects, exposureStops, samples, 1000, matchToPhoto, error, notes)) return false;
	if (!QDir().mkpath(QFileInfo(outPng).absolutePath()) || !picture.save(outPng)) {
		if (error) *error = tr("The picture could not be saved as %1.").arg(outPng);
		logLine(QStringLiteral("photo"), QStringLiteral("  the picture could not be saved as %1").arg(outPng), log_format::Level::Error);
		return false;
	}
	logLine(QStringLiteral("photo"), QStringLiteral("  saved %1").arg(outPng));
	return true;
}

bool SceneBuilderWidget::renderIntoPhotoImage(QImage *resultImage, const std::vector<int> &groundObjects, double exposureStops, int samples, int maxWidth, bool matchToPhoto, QString *error, QStringList *notes) {
	std::vector<photo_composite::Role> roles = photo_composite::guessRoles(m_doc);
	for (size_t i = 0; i < roles.size(); ++i) {
		if (roles[i] == photo_composite::Role::Light) continue;
		roles[i] = std::find(groundObjects.begin(), groundObjects.end(), static_cast<int>(i)) != groundObjects.end() ? photo_composite::Role::Ground : photo_composite::Role::Inserted;
	}
	return renderIntoPhotoRoles(resultImage, roles, exposureStops, samples, maxWidth, matchToPhoto, error, notes);
}

// What each object is for the picture, as last chosen in the Render into the photo dialog (by name, so it survives edits), else guessed.
std::vector<photo_composite::Role> SceneBuilderWidget::compositeRoles() const {
	std::vector<photo_composite::Role> roles = photo_composite::guessRoles(m_doc);
	for (size_t i = 0; i < roles.size() && i < m_doc.objects.size(); ++i)
		for (const auto &chosen : m_compositeRoles)
			if (chosen.first == m_doc.objects[i].name) { roles[i] = static_cast<photo_composite::Role>(chosen.second); break; }
	return roles;
}

bool SceneBuilderWidget::renderIntoPhotoRoles(QImage *resultImage, const std::vector<photo_composite::Role> &rolesGiven, double exposureStops, int samples, int maxWidth, bool matchToPhoto, QString *error, QStringList *notes) {
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
	// the picture: the photo, at most `maxWidth` pixels across (three renders at that size)
	const int w = std::min(m_cameraPhoto.width(), std::max(64, maxWidth));
	const int h = std::max(16, static_cast<int>(std::lround(static_cast<double>(w) * m_cameraPhoto.height() / m_cameraPhoto.width())));
	const QImage photo = m_cameraPhoto.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
	std::vector<unsigned char> photoPixels(static_cast<size_t>(w) * h * 3);
	for (int y = 0; y < h; ++y) std::copy(photo.constScanLine(y), photo.constScanLine(y) + w * 3, photoPixels.begin() + static_cast<std::ptrdiff_t>(y) * w * 3);

	std::vector<photo_composite::Role> roles = rolesGiven;
	roles.resize(m_doc.objects.size(), photo_composite::Role::Inserted);
	int inserted = 0, grounds = 0;
	for (const photo_composite::Role r : roles) {
		inserted += r == photo_composite::Role::Inserted ? 1 : 0;
		grounds += r == photo_composite::Role::Ground ? 1 : 0;
	}
	if (inserted == 0) {
		QStringList what;
		for (size_t i = 0; i < roles.size(); ++i)
			what << QStringLiteral("%1: %2").arg(QString::fromStdString(m_doc.objects[i].name), roles[i] == photo_composite::Role::Ground ? tr("ground") : tr("light"));
		if (error) *error = tr("Every object is marked as the ground or a light, so there is nothing to put into the photo (%1). Set one to \"Put into the photo\" in Camera and image > Render into the photo...").arg(what.join(QStringLiteral(", ")));
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
	const QString cat = QStringLiteral("photo");
	logLine(cat, QStringLiteral("render into the photo: %1 x %2, %3 samples, %4 inserted object(s), %5 ground object(s), %6; three renders (as it is, without the inserted objects, mask of them)")
	                 .arg(w).arg(h).arg(samples).arg(inserted).arg(grounds).arg(m_gpuCheck && m_gpuCheck->isChecked() ? QStringLiteral("GPU") : QStringLiteral("CPU")));
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
		const QStringList launch = {m_gpuCheck && m_gpuCheck->isChecked() ? "--gpu" : "--cpu", "--output", exr, "--height", QString::number(h), QString::number(w), QString::number(list[p].samples), QString::number(list[p].depth), scene};
		logLine(cat, QStringLiteral("  render %1 of 3 (%2, %3 samples): %4 %5").arg(p + 1).arg(QString::fromLatin1(list[p].name)).arg(list[p].samples).arg(launcherPath(), launch.join(QLatin1Char(' '))));
		QElapsedTimer passClock;
		passClock.start();
		process.start(launcherPath(), {m_gpuCheck && m_gpuCheck->isChecked() ? "--gpu" : "--cpu", "--output", exr, "--height", QString::number(h), QString::number(w), QString::number(list[p].samples), QString::number(list[p].depth), scene});
		// what the renderer says: its progress goes to the progress window and (a line in four) the log, the rest straight to the log
		QString partial, outputTail;   // (outputTail: the last of what it said, for an error message)
		int lastQuarter = -1, kept = 0;
		auto drain = [&]() {
			partial += QString::fromLocal8Bit(process.readAll());
			const QStringList parts = partial.split(QRegularExpression(QStringLiteral("[%1r%1n]").arg(QChar(92))));
			partial = parts.last();
			for (int k = 0; k + 1 < parts.size(); ++k) {
				const QString line = parts[k].trimmed();
				if (line.isEmpty()) continue;
				outputTail = (outputTail + line + QChar(10)).right(1200);
				if (line.startsWith(QLatin1String("Scanlines remaining:"))) {
					const int left = line.mid(20).trimmed().toInt();
					const int percent = std::min(100, std::max(0, 100 * (h - left) / h));
					busy.setLabelText(tr("Rendering the scene for the photo: %1 of 3, %2%...").arg(p + 1).arg(percent));
					if (percent / 25 != lastQuarter) {
						lastQuarter = percent / 25;
						logLine(cat, QStringLiteral("    render %1 of 3: %2%").arg(p + 1).arg(percent));
					}
				} else if (kept++ < 80) {
					logLine(cat, QStringLiteral("    renderer: %1").arg(line));
				}
			}
		};
		for (int waited = 0; process.state() != QProcess::NotRunning && waited < 20 * 60 * 1000 && !busy.wasCanceled(); waited += 50) {
			process.waitForFinished(50);
			drain();
			QCoreApplication::processEvents();
		}
		drain();
		if (process.state() != QProcess::NotRunning) {
			process.kill();
			process.waitForFinished(3000);
			if (error && !busy.wasCanceled()) *error = tr("A render took too long and was stopped.");
			logLine(cat, busy.wasCanceled() ? QStringLiteral("  cancelled by the user") : QStringLiteral("  a render took too long and was stopped"), log_format::Level::Warn);
			return false;
		}
		int rw = 0, rh = 0;
		if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !readLinear(exr, &rw, &rh, &rgb[p])) {
			if (error) *error = tr("The renderer could not make the %1 picture. Its output:\n%2").arg(QString::fromLatin1(list[p].name), outputTail.right(400));
			logLine(cat, QStringLiteral("  render %1 of 3 failed (exit code %2): %3").arg(p + 1).arg(process.exitCode()).arg(outputTail.right(400).simplified()), log_format::Level::Error);
			return false;
		}
		logLine(cat, QStringLiteral("  render %1 of 3 done in %2 s (%3 x %4)").arg(p + 1).arg(passClock.elapsed() / 1000.0, 0, 'f', 1).arg(rw).arg(rh));
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
	QString matchNote;
	bool matched = false;
	if (matchToPhoto) {
		bool physicalSky = false;
		for (const scene_doc::Light &l : m_doc.lights) physicalSky = physicalSky || (l.kind == scene_doc::LightKind::Infinite && l.physicalSky);
		if (!physicalSky) {
			matchNote = tr("The objects' exposure was not matched to the photo: that reads the photo's sky, and the scene has no Sun & sky (set it from the same photo with Light from a photo).");
		} else {
			const photo_composite::Match match = photo_composite::matchToPhoto(photoPixels.data(), rgb[0].data(), mask.data(), w, h);
			if (match.ok) {
				matched = true;
				options.exposureStops += match.exposureStops;
				for (int c = 0; c < 3; ++c) options.channelGain[c] = match.gain[c];
				matchNote = tr("The objects' exposure (%1 stops) and colour were matched to the photo's sky.").arg(match.exposureStops, 0, 'f', 2);
				if (!match.why.empty()) matchNote += QStringLiteral(" ") + QString::fromStdString(match.why);
			} else {
				matchNote = tr("The objects' exposure was not matched to the photo: %1").arg(QString::fromStdString(match.why));
			}
		}
	}
	const std::vector<unsigned char> out = photo_composite::composite(photoPixels.data(), rgb[0].data(), rgb[1].data(), mask.data(), w, h, options);
	if (out.empty()) {
		if (error) *error = tr("The picture could not be put together.");
		return false;
	}
	QImage result(w, h, QImage::Format_RGB888);
	for (int y = 0; y < h; ++y) std::copy(out.begin() + static_cast<std::ptrdiff_t>(y) * w * 3, out.begin() + static_cast<std::ptrdiff_t>(y + 1) * w * 3, result.scanLine(y));
	if (resultImage) *resultImage = result;
	logLine(QStringLiteral("photo"), QStringLiteral("render into the photo done (%1 x %2, %3 inserted objects, %4 ground, %5 samples, %6 s in all)").arg(w).arg(h).arg(inserted).arg(grounds).arg(samples).arg(clock.elapsed() / 1000.0, 0, 'f', 1));
	if (!matchNote.isEmpty()) logLine(cat, QStringLiteral("  %1").arg(matchNote), matched ? log_format::Level::Info : log_format::Level::Warn);
	if (notes) {
		*notes << tr("%n object(s) were put into the photo; the ground they stand on is not drawn (only the shadows and reflections they cause are).", "", inserted);
		if (!matchNote.isEmpty()) *notes << matchNote;
		if (!matched) *notes << tr("The objects are shown at the exposure the Builder gives them%1; the photo's own exposure is not known, so use Exposure if they look too bright or too dark.").arg(exposureStops != 0.0 ? tr(" (%1 stops)").arg(exposureStops, 0, 'f', 2) : QString());
		if (grounds == 0) *notes << tr("No object is marked as the ground, so there are no shadows on the photo: tick the floor the objects stand on.");
	}
	return true;
}

void SceneBuilderWidget::renderIntoPhotoDialog() {
	if (m_cameraPhoto.isNull()) {
		QMessageBox::information(this, tr("Render into the photo"), tr("Match the camera to a photo first (Camera section: Match to a photo...): the scene is put into that photo."));
		return;
	}
	const std::vector<photo_composite::Role> roles = compositeRoles();
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Render into the photo"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(tr("The scene, seen through the camera matched to the photo, is rendered and put into the photo, with the shadows and reflections it casts on the ground. Say what each object is: "
	                            "the ground the photo already has (not drawn: only the shadows it catches count), a light (lights the scene, not shown), or an object to put into the photo. "
	                            "The scene is rendered three times, so it takes about three times as long as one picture."),
	                         &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *objects = new QTableWidget(static_cast<int>(m_doc.objects.size()), 2, &dialog);
	objects->setHorizontalHeaderLabels({tr("Object"), tr("Is")});
	objects->horizontalHeader()->setStretchLastSection(true);
	objects->verticalHeader()->hide();
	objects->setEditTriggers(QAbstractItemView::NoEditTriggers);
	objects->setSelectionMode(QAbstractItemView::NoSelection);
	std::vector<QComboBox *> roleBoxes;
	for (size_t i = 0; i < m_doc.objects.size(); ++i) {
		objects->setItem(static_cast<int>(i), 0, new QTableWidgetItem(QString::fromStdString(m_doc.objects[i].name)));
		auto *box = new QComboBox;
		box->addItem(tr("Put into the photo"), static_cast<int>(photo_composite::Role::Inserted));
		box->addItem(tr("Ground (the photo has it)"), static_cast<int>(photo_composite::Role::Ground));
		box->addItem(tr("Light (lights the scene, not shown)"), static_cast<int>(photo_composite::Role::Light));
		box->setCurrentIndex(box->findData(static_cast<int>(roles[i])));
		objects->setCellWidget(static_cast<int>(i), 1, box);
		roleBoxes.push_back(box);
	}
	objects->verticalHeader()->setDefaultSectionSize(36);
	objects->resizeColumnsToContents();
	objects->setMinimumHeight(40 + 36 * static_cast<int>(std::min<size_t>(m_doc.objects.size(), 8)));
	dialog.setMinimumWidth(620);
	objects->setToolTip(tr("Ground: the floor the photo already has (its shadows are used, it is not drawn). Light: a lamp, which lights the scene and is not shown. Put into the photo: everything you want to see in it."));
	layout->addWidget(objects);
	auto *form = new QFormLayout();
	bool physicalSky = false;
	for (const scene_doc::Light &l : m_doc.lights) physicalSky = physicalSky || (l.kind == scene_doc::LightKind::Infinite && l.physicalSky);
	auto *match = new QCheckBox(tr("Match the objects' exposure and colour to the photo's sky"), &dialog);
	match->setChecked(physicalSky);
	match->setToolTip(tr("Reads how bright and how warm or cool the photo's sky is, and gives the objects the same exposure and colour cast. Needs the Sun & sky set from the same photo (Light from a photo). The setting below is added to it."));
	layout->addWidget(match);
	auto *exposure = new QDoubleSpinBox(&dialog);
	exposure->setRange(-4.0, 4.0);
	exposure->setSingleStep(0.25);
	exposure->setDecimals(2);
	exposure->setSuffix(tr(" stops"));
	exposure->setValue(m_compositeExposure);
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
	layout->activate();
	dialog.resize(dialog.sizeHint().expandedTo(QSize(620, 0)));   // (sized for the wrapped text and the whole table before it is shown)
	if (dialog.exec() != QDialog::Accepted) return;
	m_compositeRoles.clear();
	std::vector<photo_composite::Role> chosen(m_doc.objects.size(), photo_composite::Role::Inserted);
	for (size_t i = 0; i < m_doc.objects.size(); ++i) {
		chosen[i] = static_cast<photo_composite::Role>(roleBoxes[i]->currentData().toInt());
		m_compositeRoles.push_back({m_doc.objects[i].name, static_cast<int>(chosen[i])});
	}
	m_compositeExposure = exposure->value();

	QImage picture;
	QString error;
	QStringList notes;
	QElapsedTimer clock;
	clock.start();
	if (!renderIntoPhotoRoles(&picture, chosen, exposure->value(), quality->value(), 1000, match->isChecked(), &error, &notes)) {
		if (!error.isEmpty()) {
			logLine(QStringLiteral("photo"), QStringLiteral("render into the photo failed: %1").arg(error), log_format::Level::Warn);
			QMessageBox::warning(this, tr("Could not render into the photo"), error);
		}
		return;
	}
	showInPreview(QPixmap::fromImage(picture), tr("Put into the photo in %1 s (%2 x %3). %4").arg(clock.elapsed() / 1000.0, 0, 'f', 1).arg(picture.width()).arg(picture.height()).arg(notes.join(QLatin1Char(' '))));
}

// "Preview in the photo": no questions, the Preview's Quality, what was last given in the dialog (the first time: the floor is the ground and the sky's exposure is matched when there is a Sun & sky).
void SceneBuilderWidget::previewIntoPhoto() {
	if (m_cameraPhoto.isNull()) {
		m_previewStatus->setText(tr("Match the camera to a photo first (Camera and image: Match to a photo...): the scene is put into that photo."));
		return;
	}
	flushEditLog();
	static const int widths[3] = {480, 720, 960};
	static const int spps[3] = {16, 64, 256};
	const int q = std::clamp(m_qualityCombo->currentData().toInt(), 0, 2);
	const std::vector<photo_composite::Role> roles = compositeRoles();
	bool physicalSky = false;
	for (const scene_doc::Light &l : m_doc.lights) physicalSky = physicalSky || (l.kind == scene_doc::LightKind::Infinite && l.physicalSky);
	m_previewStatus->setText(tr("Putting the scene into the photo..."));
	QImage picture;
	QString error;
	QStringList notes;
	QElapsedTimer clock;
	clock.start();
	if (!renderIntoPhotoRoles(&picture, roles, m_compositeExposure, spps[q], widths[q], physicalSky, &error, &notes)) {
		logLine(QStringLiteral("photo"), QStringLiteral("preview in the photo failed: %1").arg(error), log_format::Level::Warn);
		m_previewStatus->setText(error.isEmpty() ? tr("The render was cancelled.") : error);
		return;
	}
	showInPreview(QPixmap::fromImage(picture), tr("Put into the photo in %1 s (%2 x %3, %4 samples). %5").arg(clock.elapsed() / 1000.0, 0, 'f', 1).arg(picture.width()).arg(picture.height()).arg(spps[q]).arg(notes.join(QLatin1Char(' '))));
}

void SceneBuilderWidget::showInPreview(const QPixmap &picture, const QString &status) {
	m_previewPixmap = picture;
	updatePreviewPixmap();
	m_previewStatus->setText(status);
	if (m_saveButton) m_saveButton->setEnabled(!picture.isNull());
	if (m_centreScroll) m_centreScroll->ensureWidgetVisible(m_previewLabel, 0, 40);   // (the preview pane is below the fold in a short window: scroll to the picture)
	emit statusMessage(tr("The picture is in the preview pane."));
}

void SceneBuilderWidget::onSavePictureClicked() {
	if (m_previewPixmap.isNull()) return;
	const QString suggested = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/scene-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".png";
	QString path = QFileDialog::getSaveFileName(this, tr("Save the picture as"), suggested, tr("PNG pictures (*.png)"));
	if (path.isEmpty()) return;
	if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
	if (m_previewPixmap.save(path)) {
		m_previewStatus->setText(tr("Saved %1.").arg(QDir::toNativeSeparators(path)));
		logLine(QStringLiteral("builder-render"), QStringLiteral("saved the picture in the preview pane as %1").arg(path));
	} else {
		m_previewStatus->setText(tr("Could not save to %1.").arg(path));
	}
}
