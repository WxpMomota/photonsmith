// The Scene Builder's "Object from a photo" (Add menu): one photo -> a textured mesh through the optional photo helper (photo_import.h).
#include "scene_builder_widget.h"
#include "app_log.h"

#include "photo_import.h"
#include "scene_builder_common.h"
#include "../src/shared/scene_room.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>
#include <QVBoxLayout>

using scene_doc::Object;
using scene_doc::ShapeKind;
using namespace scene_builder_ui;

static const char *kPhotoNoticeKey = "sceneBuilder/photoNoticeSeen";

// A folder name made of the photo's own name: letters, digits, dash and underscore only.
static QString safeFolderName(const QString &base) {
	QString s;
	for (const QChar &c : base) s += (c.isLetterOrNumber() && c.unicode() < 128) || c == '-' || c == '_' ? c : QChar('_');
	return s.isEmpty() ? QStringLiteral("photo") : s.left(40);
}

void SceneBuilderWidget::addObjectFromPhoto() {
	const photo_import::Setup setup = photo_import::locate();
	if (!setup.ready) {
		QMessageBox box(QMessageBox::Information, tr("Photo helper not installed"),
		                tr("Turning a photo into a 3D object needs an optional helper: an AI model that runs on your own computer (about 5 GB to install, and an "
		                   "graphics card - NVIDIA on Windows, Apple silicon on a Mac - is strongly recommended).\n\n%1\n\n"
		                   "To set it up, use the Diagnostics tab: Run Diagnostics, then Install Photo Helper. Or run this once yourself:\n\n%2\n\n"
		                   "Then choose this again. The guide (docs/PHOTO_TO_SCENE.md) explains what it does and where its limits are.")
		                    .arg(setup.why,
#ifdef Q_OS_WIN
		                         QStringLiteral("powershell -ExecutionPolicy Bypass -File scripts\\setup_photo_to_mesh.ps1")),
#else
		                         QStringLiteral("bash scripts/setup_photo_to_mesh.sh")),
#endif
		                QMessageBox::Ok, this);
		box.exec();
		return;
	}

	QSettings settings;
	if (!settings.value(kPhotoNoticeKey, false).toBool()) {
		QMessageBox box(QMessageBox::Information, tr("Object from a photo"),
		                tr("One photo is turned into a 3D object by an AI model running on this computer; the photo is not uploaded anywhere.\n\n"
		                   "The shape is a guess: the back is invented and fine detail is soft. It works best on one object against a plain background. "
		                   "The first run downloads the model (about 1.7 GB) and can take several minutes."),
		                QMessageBox::Ok | QMessageBox::Cancel, this);
		auto *again = new QCheckBox(tr("Do not show this again"), &box);
		box.setCheckBox(again);
		if (box.exec() != QMessageBox::Ok) return;
		if (again->isChecked()) settings.setValue(kPhotoNoticeKey, true);
	}

	const QString photo = QFileDialog::getOpenFileName(this, tr("Choose a photo"), QString(), tr("Photos (*.png *.jpg *.jpeg *.bmp *.webp)"));
	if (photo.isEmpty()) return;
	QString error;
	if (!importPhoto(photo, &error) && !error.isEmpty()) QMessageBox::warning(this, tr("Could not make the object"), error);
}

// Runs the helper on `photo` behind a progress dialog and adds the result as a mesh object. Returns false with `error` set (empty when the
// user cancelled) if there is no object to add.
bool SceneBuilderWidget::importPhoto(const QString &photo, QString *error) {
	const photo_import::Setup setup = photo_import::locate();
	AppLog::info(QStringLiteral("photo"), QStringLiteral("object from a photo: %1 (helper %2)").arg(photo, setup.ready ? QStringLiteral("ready") : QStringLiteral("not ready: ") + setup.why));
	if (!setup.ready) {
		if (error) *error = setup.why;
		return false;
	}
	const QString baseName = QFileInfo(photo).completeBaseName();
	const QString outFolder =
	    photo_import::resultsFolder() + "/" + safeFolderName(baseName) + "-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
	if (!QDir().mkpath(outFolder)) {
		if (error) *error = tr("Could not create the folder %1.").arg(outFolder);
		return false;
	}

	QProgressDialog progress(tr("Starting..."), tr("Cancel"), 0, 100, this);
	progress.setWindowTitle(tr("Making a 3D object from the photo"));
	progress.setWindowModality(Qt::WindowModal);
	progress.setMinimumDuration(0);
	progress.setMinimumWidth(420);
	progress.setValue(0);

	PhotoToMeshJob job(setup, photo, outFolder, true);
	bool ok = false, cancelled = false;
	QString message;
	QEventLoop loop;
	connect(&job, &PhotoToMeshJob::progress, &progress, [&progress](int percent, const QString &text) {
		progress.setValue(percent);
		progress.setLabelText(text);
	});
	connect(&job, &PhotoToMeshJob::finished, &loop, [&](bool good, const QString &text) {
		ok = good;
		message = text;
		loop.quit();
	});
	const QMetaObject::Connection cancelConnection = connect(&progress, &QProgressDialog::canceled, &job, [&]() {
		cancelled = true;
		job.cancel();
	});
	job.start();
	loop.exec();
	// QProgressDialog::closeEvent emits canceled(): once the job is over, closing the window is not the user cancelling (a real failure
	// would be reported as a cancel and its message dropped).
	disconnect(cancelConnection);
	progress.close();

	AppLog::write(ok ? log_format::Level::Info : cancelled ? log_format::Level::Warn : log_format::Level::Error, QStringLiteral("photo"),
	              QStringLiteral("%1: %2").arg(ok ? QStringLiteral("done") : cancelled ? QStringLiteral("cancelled by the user") : QStringLiteral("failed"), message));
	if (!ok) {
		QDir(outFolder).removeRecursively();
		if (error) *error = cancelled ? QString() : message;
		return false;
	}

	edit(QString(), [&]() {
		QStringList names;
		for (const Object &existing : m_doc.objects) names << QString::fromStdString(existing.name);
		Object o = scene_doc::makeObject(ShapeKind::Mesh, uniqueName(baseName.isEmpty() ? tr("Photo object") : baseName, names).toStdString());
		o.meshFile = QDir::toNativeSeparators(outFolder + "/mesh.obj").toStdString();
		o.material.imageFile = QDir::toNativeSeparators(outFolder + "/texture.png").toStdString();
		const scene_doc::Float3 c = dropPoint();
		o.position = {c.x, 0.0, c.z};  // the mesh stands on y = 0, about 2 units tall
		m_doc.objects.push_back(o);
		m_sel = {SelKind::Object, static_cast<int>(m_doc.objects.size()) - 1};
	});
	rebuildList();
	setSelection(m_sel);
	emit statusMessage(tr("Added %1 from the photo. The shape is a guess; check it from every side.").arg(baseName));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Room from a photo
// ---------------------------------------------------------------------------------------------------------------------------------
static const char *kRoomNoticeKey = "sceneBuilder/roomNoticeSeen";

// The two things only the person who took the photo knows: how high the camera was and (when the photo carries no lens) how wide it saw. 0 means "let the helper decide".
static bool askRoomOptions(QWidget *parent, double *cameraHeight, double *fov) {
	QDialog dialog(parent);
	dialog.setWindowTitle(QObject::tr("Room from a photo"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(QObject::tr("Both are optional: leave a 0 and the program estimates it. The size of the room is only as good as the camera height, so if you know it, say it."), &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *form = new QFormLayout();
	auto *height = new QDoubleSpinBox(&dialog);
	height->setRange(0.0, 6.0);
	height->setDecimals(2);
	height->setSingleStep(0.1);
	height->setSuffix(QObject::tr(" m"));
	height->setSpecialValueText(QObject::tr("estimate"));
	height->setToolTip(QObject::tr("How high the camera was above the floor. Standing at eye level is about 1.5 m."));
	auto *angle = new QDoubleSpinBox(&dialog);
	angle->setRange(0.0, 140.0);
	angle->setDecimals(1);
	angle->setSingleStep(5.0);
	angle->setSuffix(QObject::tr(" degrees"));
	angle->setSpecialValueText(QObject::tr("from the photo"));
	angle->setToolTip(QObject::tr("How wide the photo sees, left to right. A phone's main camera is about 65 to 70 degrees. 0 reads it from the photo's own data, or assumes a phone."));
	form->addRow(QObject::tr("Camera height above the floor"), height);
	form->addRow(QObject::tr("Photo's field of view (horizontal)"), angle);
	layout->addLayout(form);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted) return false;
	*cameraHeight = height->value();
	*fov = angle->value();
	return true;
}

void SceneBuilderWidget::addRoomFromPhoto() {
	const photo_import::Setup setup = photo_import::locate();
	if (!setup.ready || setup.roomScript.isEmpty()) {
		QMessageBox box(QMessageBox::Information, tr("Photo helper not installed"),
		                tr("Turning a photo of a room into a room needs the same optional helper as Object from a photo: an AI model that runs on your own computer.\n\n%1\n\n"
		                   "To set it up, use the Diagnostics tab: Run Diagnostics, then Install Photo Helper (about 5 GB). Then choose this again. "
		                   "The guide (docs/PHOTO_TO_SCENE.md) says what it does and where its limits are.")
		                    .arg(setup.ready ? tr("The room script (tools/photo_to_room/photo_to_room.py) was not found next to the program.") : setup.why),
		                QMessageBox::Ok, this);
		box.exec();
		return;
	}
	QSettings settings;
	if (!settings.value(kRoomNoticeKey, false).toBool()) {
		QMessageBox box(QMessageBox::Information, tr("Room from a photo"),
		                tr("A photo of a room becomes a room you can edit: its walls, floor and ceiling at about the right size and colour, windows and lamps as lights, and a camera that sees what the "
		                   "photo saw. A small AI model on this computer guesses the distances; the photo is not uploaded anywhere.\n\n"
		                   "It is a first draft: the room is empty (furniture is not modelled), a wall the photo does not show is assumed, and the size can be off by a fifth or more. "
		                   "The first run downloads the model (about 100 MB).\n\n"
		                   "This replaces the scene you have now (Undo brings it back)."),
		                QMessageBox::Ok | QMessageBox::Cancel, this);
		auto *again = new QCheckBox(tr("Do not show this again"), &box);
		box.setCheckBox(again);
		if (box.exec() != QMessageBox::Ok) return;
		if (again->isChecked()) settings.setValue(kRoomNoticeKey, true);
	}
	const QString photo = QFileDialog::getOpenFileName(this, tr("Choose a photo of a room"), QString(), tr("Photos (*.png *.jpg *.jpeg *.bmp *.webp)"));
	if (photo.isEmpty()) return;
	double height = 0.0, fov = 0.0;
	if (!askRoomOptions(this, &height, &fov)) return;
	QString error;
	QStringList notes;
	if (!importRoom(photo, height, fov, &error, &notes)) {
		if (!error.isEmpty()) QMessageBox::warning(this, tr("Could not make the room"), error);
		return;
	}
	if (!notes.isEmpty()) QMessageBox::information(this, tr("About this room"), tr("What the program had to assume:\n\n- %1").arg(notes.join("\n- ")));
}

bool SceneBuilderWidget::importRoom(const QString &photo, double cameraHeight, double fov, QString *error, QStringList *notes) {
	const photo_import::Setup setup = photo_import::locate();
	AppLog::info(QStringLiteral("photo"), QStringLiteral("room from a photo: %1 (helper %2, camera height %3, fov %4)")
	                                          .arg(photo, setup.ready ? QStringLiteral("ready") : QStringLiteral("not ready: ") + setup.why, QString::number(cameraHeight), QString::number(fov)));
	if (!setup.ready || setup.roomScript.isEmpty()) {
		if (error) *error = setup.ready ? tr("The room script (tools/photo_to_room/photo_to_room.py) was not found next to the program.") : setup.why;
		return false;
	}
	const QString baseName = QFileInfo(photo).completeBaseName();
	const QString outFolder = photo_import::resultsFolder() + "/room-" + safeFolderName(baseName) + "-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
	if (!QDir().mkpath(outFolder)) {
		if (error) *error = tr("Could not create the folder %1.").arg(outFolder);
		return false;
	}

	QProgressDialog progress(tr("Starting..."), tr("Cancel"), 0, 100, this);
	progress.setWindowTitle(tr("Making a room from the photo"));
	progress.setWindowModality(Qt::WindowModal);
	progress.setMinimumDuration(0);
	progress.setMinimumWidth(420);
	progress.setValue(0);

	PhotoToMeshJob job(setup, photo, outFolder, true);
	job.useRoomHelper(cameraHeight, fov);
	bool ok = false, cancelled = false;
	QString message;
	QEventLoop loop;
	connect(&job, &PhotoToMeshJob::progress, &progress, [&progress](int percent, const QString &text) {
		progress.setValue(percent);
		progress.setLabelText(text);
	});
	connect(&job, &PhotoToMeshJob::finished, &loop, [&](bool good, const QString &text) {
		ok = good;
		message = text;
		loop.quit();
	});
	const QMetaObject::Connection cancelConnection = connect(&progress, &QProgressDialog::canceled, &job, [&]() {
		cancelled = true;
		job.cancel();
	});
	job.start();
	loop.exec();
	disconnect(cancelConnection);   // (closing the dialog after the job is over is not the user cancelling)
	progress.close();

	scene_room::Import room;
	if (ok) {
		QFile file(outFolder + "/room.json");
		QString problem;
		if (!file.open(QIODevice::ReadOnly)) {
			problem = tr("The helper finished but its room file could not be read.");
		} else {
			std::string why;
			if (!scene_room::fromJson(file.readAll().toStdString(), room, why)) problem = tr("The helper's room file is not usable: %1").arg(QString::fromStdString(why));
		}
		if (!problem.isEmpty()) {
			ok = false;
			message = problem;
		}
	}
	AppLog::write(ok ? log_format::Level::Info : cancelled ? log_format::Level::Warn : log_format::Level::Error, QStringLiteral("photo"),
	              QStringLiteral("%1: %2").arg(ok ? QStringLiteral("room done") : cancelled ? QStringLiteral("cancelled by the user") : QStringLiteral("room failed"), message));
	if (!ok) {
		QDir(outFolder).removeRecursively();
		if (error) *error = cancelled ? QString() : message;
		return false;
	}

	const double lightFactor = calibrateRoomLight(room, outFolder);
	AppLog::info(QStringLiteral("photo"), QStringLiteral("room light level: the lights were scaled by %1 to match the photo's brightness %2").arg(lightFactor, 0, 'g', 3).arg(room.photoBrightness, 0, 'g', 3));
	for (const char *suffix : {"/calibrate.pbrt", "/calibrate.ppm", "/calibrate.png"}) QFile::remove(outFolder + QString::fromLatin1(suffix));
	const std::string title = tr("Room from %1").arg(baseName.isEmpty() ? tr("a photo") : baseName).toStdString();
	edit(QString(), [&]() {
		scene_room::applyTo(room, m_doc, title);
		m_sel = {SelKind::None, 0};
	});
	rebuildList();
	setSelection(m_sel);
	if (notes)
		for (const std::string &n : room.notes) *notes << QString::fromStdString(n);
	emit statusMessage(tr("Made a room from %1. It is empty and a first draft: add furniture, and check the size.").arg(baseName));
	return true;
}

double SceneBuilderWidget::calibrateRoomLight(scene_room::Import &room, const QString &folder) {
	if (room.photoBrightness <= 0.0 || !QFileInfo::exists(launcherPath())) return 1.0;
	QProgressDialog busy(tr("Setting the room's light level..."), QString(), 0, 0, this);
	busy.setWindowTitle(tr("Making a room from the photo"));
	busy.setWindowModality(Qt::WindowModal);
	busy.setMinimumDuration(0);
	busy.setCancelButton(nullptr);
	busy.show();

	const int w = 192;
	const int h = std::clamp(static_cast<int>(std::lround(w * static_cast<double>(room.height) / std::max(room.width, 1))), 64, 400);
	const QString sceneFile = folder + "/calibrate.pbrt", base = folder + "/calibrate";
	const auto measure = [&](double factor) -> double {
		QCoreApplication::processEvents();
		scene_room::Import trial = room;
		scene_room::scaleLights(trial, factor);
		scene_doc::Document d;
		scene_room::applyTo(trial, d, "calibration");
		d.render.width = w;
		d.render.height = h;
		QFile out(sceneFile);
		if (!out.open(QIODevice::WriteOnly)) return -1.0;
		out.write(QByteArray::fromStdString(scene_doc::toPbrt(d)));
		out.close();
		QFile::remove(base + ".png");
		QProcess process;
		process.setProcessChannelMode(QProcess::MergedChannels);
		process.setWorkingDirectory(QCoreApplication::applicationDirPath());
		process.start(launcherPath(), {"--cpu", "--output", base + ".ppm", "--height", QString::number(h), QString::number(w), "24", "6", sceneFile});
		if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
			process.kill();
			return -1.0;
		}
		QImage image(base + ".png");
		if (image.isNull()) return -1.0;
		image = image.convertToFormat(QImage::Format_RGB888);
		std::vector<unsigned char> bytes;
		bytes.reserve(static_cast<size_t>(image.width()) * static_cast<size_t>(image.height()) * 3);
		for (int y = 0; y < image.height(); ++y) bytes.insert(bytes.end(), image.constScanLine(y), image.constScanLine(y) + static_cast<size_t>(image.width()) * 3);
		return scene_room::pictureBrightness(bytes.data(), bytes.size());
	};
	const double factor = scene_room::solveLightFactor(measure, room.photoBrightness);
	scene_room::scaleLights(room, factor);
	busy.close();
	return factor;
}
