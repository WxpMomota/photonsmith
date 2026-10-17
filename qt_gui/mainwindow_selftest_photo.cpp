// RT_GUI_SELFTEST=photo RT_GUI_SELFTEST_PHOTO=<image> RT_GUI_SELFTEST_OUT=<prefix>: the Scene Builder's "Object from a photo" without its dialogs.
// RT_GUI_SELFTEST=room RT_GUI_SELFTEST_ROOM_PHOTO=<image> [RT_GUI_SELFTEST_ROOM_FOV=<degrees>] [RT_GUI_SELFTEST_ROOM_HEIGHT=<metres>]: "Room from a photo" the same way
// (see runRoomSelfTest at the end of this file).
// Runs the photo helper on the image (it must be set up; RAY_TRACER_PHOTO3D_PYTHON points at another install), checks the mesh object it adds, and
// renders a preview of it. Screenshots <prefix>_photo_edit.png and <prefix>_photo_preview.png; exit 0 if every step held.
#include "mainwindow.h"

#ifndef Q_OS_WIN
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "photo_import.h"
#include "scene_builder_widget.h"

#include "../src/shared/material_from_photo.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPushButton>
#include <QTimer>

#include <cmath>

#include <memory>

void MainWindow::runPhotoSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	if (m_sceneBuilder) m_tabWidget->setCurrentWidget(m_sceneBuilder);
	resize(1500, 950);
	SceneBuilderWidget *sb = m_sceneBuilder;
	bool ok = sb != nullptr;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	QString err;
	// A photo that does not exist must fail with the helper's own words, not silently (a closing progress window once looked like a cancel).
	QString badError;
	const bool badOk = sb && sb->importPhoto("C:/no/such/photo_for_the_selftest.png", &badError);
	check(sb && !badOk && !badError.isEmpty(), "a missing photo fails with a message: " + badError.left(100));
	const QString photo = qEnvironmentVariable("RT_GUI_SELFTEST_PHOTO");
	check(sb && sb->importPhoto(photo, &err), "made an object from " + photo + " " + err);
	if (sb && ok) {
		const scene_doc::Document &doc = sb->document();
		const scene_doc::Object &o = doc.objects.back();
		check(o.shape == scene_doc::ShapeKind::Mesh && QFile::exists(QString::fromStdString(o.meshFile)) && QFile::exists(QString::fromStdString(o.material.imageFile)),
		      "the new object is a mesh with its texture");
		check(!scene_doc::hasErrors(scene_doc::validate(doc)), "the scene has no errors");
	}
	QTimer::singleShot(600, this, [this, shot, log, sb, ok]() {
		shot("photo_edit");
		if (!ok) {
			log("RESULT: FAIL");
			QApplication::exit(1);
			return;
		}
		log("starting a preview render");
		sb->startPreview([this, shot, log](bool done, const QString &message) {
			log(QString("preview: %1 - %2").arg(done ? "ok" : "FAIL", message));
			QTimer::singleShot(300, this, [shot, log, done]() {
				shot("photo_preview");
				log(done ? "RESULT: OK" : "RESULT: FAIL");
				QApplication::exit(done ? 0 : 1);
			});
		});
	});
}

// RT_GUI_SELFTEST=installphoto: the Diagnostics tab's "Install Photo Helper" flow with stand-in installer scripts instead of the real multi-GB one.
// RAY_TRACER_PHOTO3D_PYTHON must name a python that does not exist (so the report lists the helper as missing) and RAY_TRACER_PHOTO3D_SETUP a
// script that prints some lines (including a \r progress update) and exits 0; RT_GUI_SELFTEST_SETUP_FAIL names one that exits 1. Checks the
// button's states, the output reaching the caller, a clean success, and a failure's message; exit 0 if every step held.
void MainWindow::runInstallPhotoSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	for (int i = 0; i < m_tabWidget->count(); ++i)
		if (m_tabWidget->tabText(i).contains("Diagnostics")) m_tabWidget->setCurrentIndex(i);
	resize(1100, 800);
	auto ok = std::make_shared<bool>(true);
	auto check = [ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		*ok = *ok && cond;
	};
	check(m_installPhotoHelperButton && !m_installPhotoHelperButton->isEnabled(), "before any diagnostics the install button is disabled");
	onRunDiagnosticsClicked();
	auto *poll = new QTimer(this);
	auto stage = std::make_shared<int>(0);
	auto waited = std::make_shared<int>(0);
	const QString failScript = qEnvironmentVariable("RT_GUI_SELFTEST_SETUP_FAIL");
	connect(poll, &QTimer::timeout, this, [=]() {
		if (++*waited > 400) { log("FAIL: timed out"); QApplication::exit(1); return; }
		if (*stage == 0 && !diagnosticsBusy() && m_lastDiagReport.contains("=== Photo helper")) {
			*stage = 1;
			const QStringList missing = photo_import::missingFacts(m_lastDiagReport);
			check(!missing.isEmpty(), "the report lists what the helper is missing (" + missing.join(" | ") + ")");
			check(m_installPhotoHelperButton->isEnabled(), "so the install button is enabled");
			check(m_installPhotoHelperButton->toolTip().contains("Python Environment"), "and its tooltip says what is missing");
			shot("installphoto_report");
			startPhotoHelperInstall(false, [=](bool done, const QString &message) {
				check(done, "the stand-in installer succeeded " + message);
				*stage = 2;
			});
		} else if (*stage == 2 && !diagnosticsBusy()) {
			// After a successful install the diagnostics run again by themselves.
			*stage = 3;
			check(m_lastDiagReport.contains("=== Photo helper"), "the diagnostics ran again after the install");
			if (failScript.isEmpty()) { poll->stop(); log(*ok ? "RESULT: OK" : "RESULT: FAIL"); QApplication::exit(*ok ? 0 : 1); return; }
			qputenv("RAY_TRACER_PHOTO3D_SETUP", failScript.toLocal8Bit());
			startPhotoHelperInstall(false, [=](bool done, const QString &message) {
				check(!done && message.contains("ERROR: no network (CRLF)"), "a failing installer reports failure with the last lines of its output, CRLF or not: " + message.left(120));
				*stage = 4;
			});
		} else if (*stage == 4 && !qEnvironmentVariable("RT_GUI_SELFTEST_SETUP_HANG").isEmpty()) {
			// Cancel: the stand-in starts a long-running child (its pid is written to a file) and waits; Cancel must stop the child too, not only the shell.
			*stage = 6;
			qputenv("RAY_TRACER_PHOTO3D_SETUP", qEnvironmentVariable("RT_GUI_SELFTEST_SETUP_HANG").toLocal8Bit());
			startPhotoHelperInstall(false, [=](bool done, const QString &message) {
				check(!done && message.contains("Cancelled"), "Cancel stops the installer: " + message);
				*stage = 7;
			});
			QTimer::singleShot(1500, this, [this]() { if (m_photoInstaller) m_photoInstaller->cancel(); });
		} else if (*stage == 7) {
			*stage = 4;   // continue below once the cancelled run is accounted for
#ifndef Q_OS_WIN
			QFile pidFile(qEnvironmentVariable("RT_GUI_SELFTEST_HANG_PID"));
			const qint64 pid = pidFile.open(QIODevice::ReadOnly) ? pidFile.readAll().trimmed().toLongLong() : 0;
			QTimer::singleShot(600, this, [=]() {
				check(pid > 0 && ::kill(static_cast<pid_t>(pid), 0) != 0, "the installer's child process (pid " + QString::number(pid) + ") was stopped with it");
				if (pid > 0 && ::kill(static_cast<pid_t>(pid), 0) == 0) ::kill(static_cast<pid_t>(pid), SIGKILL);
			});
#endif
			qputenv("RT_GUI_SELFTEST_SETUP_HANG", "");
		} else if (*stage == 4) {
			poll->stop();
			QTimer::singleShot(900, this, [=]() {
				check(!m_photoInstaller, "nothing is left running");
				log(*ok ? "RESULT: OK" : "RESULT: FAIL");
				QApplication::exit(*ok ? 0 : 1);
			});
			*stage = 5;
		}
	});
	poll->start(300);
}

// RT_GUI_SELFTEST=room: the Scene Builder's "Room from a photo" without its dialogs. Runs the room helper on the photo (the photo helper must be set up, with a depth model
// downloaded or a network to get it), checks the scene it makes (surfaces, a light, a usable camera, no errors from the checks) and renders a preview of it.
// Screenshots <prefix>_room_edit.png and <prefix>_room_preview.png; exit 0 if every step held.
void MainWindow::runRoomSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	if (m_sceneBuilder) m_tabWidget->setCurrentWidget(m_sceneBuilder);
	resize(1500, 950);
	SceneBuilderWidget *sb = m_sceneBuilder;
	bool ok = sb != nullptr;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	QString err;
	QString badError;
	const bool badOk = sb && sb->importRoom("C:/no/such/photo_for_the_selftest.png", 0.0, 0.0, 0, &badError);
	check(sb && !badOk && !badError.isEmpty(), "a missing photo fails with a message: " + badError.left(100));
	const QString photo = qEnvironmentVariable("RT_GUI_SELFTEST_ROOM_PHOTO");
	const double fov = qEnvironmentVariable("RT_GUI_SELFTEST_ROOM_FOV").toDouble();
	const double height = qEnvironmentVariable("RT_GUI_SELFTEST_ROOM_HEIGHT").toDouble();
	const int furniture = qEnvironmentVariable("RT_GUI_SELFTEST_ROOM_FURNITURE").toInt();
	QStringList notes;
	const size_t before = sb ? sb->document().objects.size() : 0;
	check(sb && sb->importRoom(photo, height, fov, furniture, &err, &notes, !qEnvironmentVariable("RT_GUI_SELFTEST_ROOM_BOX").isEmpty()), "made a room from " + photo + " " + err);
	if (sb && ok) {
		const scene_doc::Document &doc = sb->document();
		int emitters = 0, boxes = 0;
		for (const scene_doc::Object &o : doc.objects) {
			if (o.emissive) ++emitters;
			if (o.shape == scene_doc::ShapeKind::Box) ++boxes;
		}
		check(boxes >= 7 && emitters >= 1, QString("the scene is a room of boxes with a light (%1 boxes, %2 lights; it had %3 objects before)").arg(boxes).arg(emitters).arg(before));
		int meshes = 0;
		for (const scene_doc::Object &o : doc.objects)
			if (o.shape == scene_doc::ShapeKind::Mesh && QFile::exists(QString::fromStdString(o.meshFile))) ++meshes;
		log(QString("furniture: %1 mesh object(s) asked for up to %2").arg(meshes).arg(furniture));
		// (a photo may give only measured boxes, such as a bed or a table, and no mesh: the helper's own note counts both)
		bool madeFurniture = meshes >= 1;
		for (const QString &n : notes)
			if (n.contains("of furniture were made")) madeFurniture = true;
		if (furniture > 0) check(madeFurniture, "at least one piece of furniture was made");
		check(doc.camera.fov > 10.0 && doc.camera.fov < 140.0, QString("the camera has a believable field of view (%1 degrees)").arg(doc.camera.fov));
		check(!scene_doc::hasErrors(scene_doc::validate(doc)), "the scene has no errors");
		for (const QString &n : notes) log("note: " + n);
		// The room as the file the Builder would save, so a caller can render it at full quality: <prefix>_room.pbrt.
		const QString outPrefix = qEnvironmentVariable("RT_GUI_SELFTEST_OUT");
		QFile scene(outPrefix + "_room.pbrt");
		if (!outPrefix.isEmpty() && scene.open(QIODevice::WriteOnly)) scene.write(QByteArray::fromStdString(scene_doc::toPbrt(doc)));
	}
	QTimer::singleShot(600, this, [this, shot, log, sb, ok]() {
		shot("room_edit");
		if (!ok) {
			log("RESULT: FAIL");
			QApplication::exit(1);
			return;
		}
		log("starting a preview render");
		sb->startPreview([this, shot, log](bool done, const QString &message) {
			log(QString("preview: %1 - %2").arg(done ? "ok" : "FAIL", message));
			QTimer::singleShot(300, this, [shot, log, done]() {
				shot("room_preview");
				log(done ? "RESULT: OK" : "RESULT: FAIL");
				QApplication::exit(done ? 0 : 1);
			});
		});
	});
}

// RT_GUI_SELFTEST=material [RT_GUI_SELFTEST_MATERIAL_PHOTO=<image>] RT_GUI_SELFTEST_OUT=<prefix>: the Scene Builder's "Make from a photo..." for a material, without its dialogs. With no
// photo given one is made here (warm planks with dark grooves, lit by a window on the right). Adds a quad, makes the material on it, checks the picture and bump map it gets (they exist,
// the picture repeats with no joins, the relief is a height map) and the repeat count, and writes <prefix>_material.pbrt so a caller can render it. Exit 0 if every step held.
void MainWindow::runMaterialSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	if (m_sceneBuilder) m_tabWidget->setCurrentWidget(m_sceneBuilder);
	resize(1500, 950);
	SceneBuilderWidget *sb = m_sceneBuilder;
	bool ok = sb != nullptr;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	if (sb) {
		QString badError;
		const bool badOk = sb->applyMaterialPhoto(0, "C:/no/such/photo_for_the_selftest.png", 0, 40.0, &badError);
		check(!badOk && !badError.isEmpty(), "a missing photo fails with a message: " + badError.left(100));
		QString photo = qEnvironmentVariable("RT_GUI_SELFTEST_MATERIAL_PHOTO");
		if (photo.isEmpty()) {
			QImage planks(1200, 800, QImage::Format_RGB32);
			for (int y = 0; y < planks.height(); ++y)
				for (int x = 0; x < planks.width(); ++x) {
					const double groove = (x % 160) < 8 ? 0.25 : 1.0;
					const double grain = 0.85 + 0.15 * std::sin(y * 0.05 + (x / 160) * 1.7) * std::sin(x * 0.31);
					const double light = 0.35 + 0.65 * x / (planks.width() - 1);
					const double v = groove * grain * light;
					planks.setPixel(x, y, qRgb(static_cast<int>(230 * v), static_cast<int>(150 * v), static_cast<int>(85 * v)));
				}
			photo = QDir::tempPath() + "/rt_selftest_planks.png";
			planks.save(photo);
		}
		sb->addObject(scene_doc::ShapeKind::Quad);
		const int index = static_cast<int>(sb->document().objects.size()) - 1;
		QString err;
		QStringList notes;
		const bool made = sb->applyMaterialPhoto(index, photo, 0, 40.0, &err, &notes);
		check(made, "a material was made from " + photo + " " + err);
		if (made) {
			const scene_doc::Object &o = sb->document().objects[static_cast<size_t>(index)];
			const scene_doc::Material &m = o.material;
			check(m.kind == scene_doc::MaterialKind::CoatedDiffuse && m.roughness > 0.0, "wood is a coated diffuse with a rough coat");
			check(QFile::exists(QString::fromStdString(m.imageFile)) && QFile::exists(QString::fromStdString(m.bumpFile)), "the picture and the bump map were saved");
			check(m.bumpKind == scene_doc::BumpKind::Height && m.bumpStrength > 0.0 && m.bumpStrength < 0.02, "the bump map is a height map a few millimetres tall");
			check(std::fabs(m.uvScaleU - o.size.x / 0.4) < 1e-6 && std::fabs(m.uvScaleV - o.size.z / 0.4) < 1e-6, QString("the picture repeats as often as a 40 cm patch says (%1 x %2 across a %3 x %4 quad)").arg(m.uvScaleU).arg(m.uvScaleV).arg(o.size.x).arg(o.size.z));
			QImage colour(QString::fromStdString(m.imageFile));
			QImage relief(QString::fromStdString(m.bumpFile));
			check(!colour.isNull() && colour.width() == colour.height() && colour.width() >= 256, "the picture is square");
			if (!colour.isNull()) {
				colour = colour.convertToFormat(QImage::Format_RGB888);
				std::vector<unsigned char> px(static_cast<size_t>(colour.width()) * colour.width() * 3);
				for (int y = 0; y < colour.height(); ++y) std::copy(colour.constScanLine(y), colour.constScanLine(y) + colour.width() * 3, px.begin() + static_cast<std::ptrdiff_t>(y) * colour.width() * 3);
				const double seam = material_from_photo::seamRatio(px.data(), colour.width());
				check(seam < 2.0, QString("the picture repeats with no visible join (the seam is %1 times a normal pixel step; the photo's own is far more)").arg(seam, 0, 'f', 2));
			}
			check(!relief.isNull() && relief.format() == QImage::Format_Grayscale8, "the bump map is grayscale");
			check(!notes.isEmpty(), "the user is told what was assumed: " + notes.join(" / ").left(120));
			check(!scene_doc::hasErrors(scene_doc::validate(sb->document())), "the scene has no errors");
			const QString outPrefix = qEnvironmentVariable("RT_GUI_SELFTEST_OUT");
			QFile scene(outPrefix + "_material.pbrt");
			if (!outPrefix.isEmpty() && scene.open(QIODevice::WriteOnly)) scene.write(QByteArray::fromStdString(scene_doc::toPbrt(sb->document())));
		}
	}
	QTimer::singleShot(600, this, [this, shot, log, sb, ok]() {
		shot("material_edit");
		// the button is in the properties of the object that was picked (a quad with a picture: matte or glossy paint)
		bool found = false;
		if (sb) {
			QWidget *page = sb->inspectorContentForTest();
			for (QPushButton *b : page->findChildren<QPushButton *>())
				if (b->text().startsWith("Make from a photo")) found = true;
			const QString outPrefix = qEnvironmentVariable("RT_GUI_SELFTEST_OUT");
			if (!outPrefix.isEmpty()) page->grab().save(outPrefix + "_material_properties.png");
		}
		log(QString("%1: the properties have the Make from a photo button").arg(found ? "ok" : "FAIL"));
		const bool all = ok && found;
		log(all ? "RESULT: OK" : "RESULT: FAIL");
		QApplication::exit(all ? 0 : 1);
	});
}
