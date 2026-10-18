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
#include "photo_dialog.h"
#include "scene_builder_widget.h"
#include "sample_photos.h"
#include "scene_3d_view.h"

#include "../src/shared/camera_from_photo.h"
#include "../src/shared/light_from_photo.h"
#include "../src/shared/material_from_photo.h"
#include "../src/shared/scene_arrange.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

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

// RT_GUI_SELFTEST=sky RT_GUI_SELFTEST_OUT=<prefix>: the Scene Builder's "Match to a photo..." for a Sun & sky light, without its dialogs. Makes a photo of a sky with the sun in it (a blue
// gradient, a grass ground, a white sun disc 20 degrees to the right of the centre and 12 degrees above the horizon, the camera looking 8 degrees down so that the horizon is in the picture), sets the starter scene's sky from it, and checks the
// sun's height, its direction on the Builder's compass (the starter camera looks along -Z, compass 90, so 20 degrees to its right is 70), the sky picture that was made and that one undo
// puts it back. Exit 0 if every step held.
void MainWindow::runSkySelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	if (m_sceneBuilder) m_tabWidget->setCurrentWidget(m_sceneBuilder);
	resize(1500, 950);
	SceneBuilderWidget *sb = m_sceneBuilder;
	bool ok = sb != nullptr;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	if (sb) {
		int skyIndex = -1;
		for (size_t i = 0; i < sb->document().lights.size(); ++i)
			if (sb->document().lights[i].kind == scene_doc::LightKind::Infinite) { skyIndex = static_cast<int>(i); break; }
		check(skyIndex >= 0, "the starter scene has a sky light");
		const double beforeElevation = skyIndex >= 0 ? sb->document().lights[static_cast<size_t>(skyIndex)].sky.sunElevation : 0.0;
		const bool wasSky = skyIndex >= 0 && sb->document().lights[static_cast<size_t>(skyIndex)].physicalSky;
		QString badError;
		const bool badOk = sb->applyLightPhoto(skyIndex, "C:/no/such/photo_for_the_selftest.png", 65.0, false, 0.0, &badError);
		check(!badOk && !badError.isEmpty(), "a missing photo fails with a message: " + badError.left(100));
		// the photo
		const int w = 640, h = 480;
		const double focal = (w / 2.0) / std::tan(65.0 * light_from_photo::kPi / 360.0), cx = w / 2.0, cy = h / 2.0, tilt = -8.0 * light_from_photo::kPi / 180.0;
		const double horizon = cy + focal * std::tan(tilt);
		QImage photo(w, h, QImage::Format_RGB32);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				if (y >= horizon) { photo.setPixel(x, y, qRgb(70, 105, 55)); continue; }
				const double t = y / horizon;   // 0 at the top, 1 at the horizon
				photo.setPixel(x, y, qRgb(static_cast<int>(55 + 140 * t), static_cast<int>(105 + 100 * t), static_cast<int>(230 + 15 * t)));
			}
		const double el = 12.0 * light_from_photo::kPi / 180.0, az = 20.0 * light_from_photo::kPi / 180.0;
		const double dx = std::cos(el) * std::sin(az), dy = std::sin(el), dz = std::cos(el) * std::cos(az);
		const double yc = dy * std::cos(tilt) - dz * std::sin(tilt), zc = dy * std::sin(tilt) + dz * std::cos(tilt);
		const double su = cx + focal * dx / zc, sv = cy - focal * yc / zc;
		// the sun: a white disc with the glow every real one has (the glare falls off the same way all round it)
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				const double d = std::sqrt((x - su) * (x - su) + (y - sv) * (y - sv));
				if (d < 5.0) { photo.setPixel(x, y, qRgb(255, 255, 255)); continue; }
				const double glow = 0.85 * std::exp(-(d - 5.0) / 14.0);
				const QRgb base = photo.pixel(x, y);
				if (y < horizon) photo.setPixel(x, y, qRgb(static_cast<int>(qRed(base) + (255 - qRed(base)) * glow), static_cast<int>(qGreen(base) + (255 - qGreen(base)) * glow), static_cast<int>(qBlue(base) + (255 - qBlue(base)) * glow)));
			}
		const QString path = QDir::tempPath() + "/rt_selftest_sky.png";
		photo.save(path);

		QString err;
		QStringList notes;
		const bool made = sb->applyLightPhoto(skyIndex, path, 65.0, false, 0.0, &err, &notes);
		check(made, "the sky was set from " + path + " " + err);
		if (made) {
			const scene_doc::Light &sky = sb->document().lights[static_cast<size_t>(skyIndex)];
			check(sky.physicalSky, "the light is a Sun & sky now");
			check(std::fabs(sky.sky.sunElevation - 12.0) < 4.0, QString("the sun is 12 degrees up (set to %1)").arg(sky.sky.sunElevation));
			check(std::fabs(sky.sky.sunAzimuth - 70.0) < 4.0, QString("the sun is 20 degrees to the right of the Builder camera's view: compass 70 (set to %1)").arg(sky.sky.sunAzimuth));
			check(!sky.imageFile.empty() && QFile::exists(QString::fromStdString(sky.imageFile)), "the sky picture of that sun was made");
			bool sunLight = false;
			for (const scene_doc::Light &l : sb->document().lights)
				if (l.kind == scene_doc::LightKind::Distant) sunLight = std::fabs(l.position.x - l.target.x) > 1.0 && l.position.y > l.target.y;
			check(sunLight, "the Sun light follows: it shines from the right and above");
			check(!notes.isEmpty() && notes.join(" ").contains("in the photo"), "the user is told how it was read: " + notes.join(" / ").left(120));
			check(!scene_doc::hasErrors(scene_doc::validate(sb->document())), "the scene has no errors");
			check(sb->undo(), "undo is available");
			const scene_doc::Light &back = sb->document().lights[static_cast<size_t>(skyIndex)];
			check(back.sky.sunElevation == beforeElevation && back.physicalSky == wasSky, "one undo puts the sky back as it was");
		}
	}
	QTimer::singleShot(600, this, [this, shot, log, ok]() {
		shot("sky_edit");
		log(ok ? "RESULT: OK" : "RESULT: FAIL");
		QApplication::exit(ok ? 0 : 1);
	});
}

// RT_GUI_SELFTEST=camera RT_GUI_SELFTEST_OUT=<prefix>: the Scene Builder's "Match to a photo..." for the camera, without its dialogs. Makes a 4:3 photo of a camera tilted 6 degrees up and rolled 3
// degrees clockwise (a pale blue sky over a green ground, the horizon where that geometry puts it), matches the starter scene's camera to it and checks the camera's height, pitch and roll, the
// picture's new shape, the photo laid over the camera view, that one undo puts the camera back, and (with the sky asked for) the Sun & sky. Exit 0 if every step held.
void MainWindow::runCameraSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
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
		const bool badOk = sb->applyCameraPhoto("C:/no/such/photo_for_the_selftest.png", 65.0, 1.6, false, 0.0, false, 0.0, false, &badError);
		check(!badOk && !badError.isEmpty(), "a missing photo fails with a message: " + badError.left(100));
		// the photo: x right, y up, z ahead; the camera tilted up by t and rolled counter-clockwise by r (here -3: clockwise); the horizon is where a ray runs level
		const int w = 640, h = 480;
		const double focal = (w / 2.0) / std::tan(65.0 * camera_from_photo::kPi / 360.0), cx = w / 2.0, cy = h / 2.0;
		const double t = 6.0 * camera_from_photo::kPi / 180.0, r = -3.0 * camera_from_photo::kPi / 180.0;
		QImage photo(w, h, QImage::Format_RGB32);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				const double X = x + 0.5 - cx, Y = y + 0.5 - cy;
				const double horizon = (focal * std::tan(t) + X * std::sin(r)) / std::cos(r);   // the row (from the middle) where the horizon is at this column
				if (Y >= horizon) { photo.setPixel(x, y, qRgb(76, 92, 56)); continue; }
				const double e = std::min(1.0, (horizon - Y) / (h * 0.6));   // paler towards the horizon, deeper blue above
				photo.setPixel(x, y, qRgb(static_cast<int>(190 - 120 * e), static_cast<int>(210 - 80 * e), static_cast<int>(240 - 10 * e)));
			}
		const QString path = QDir::tempPath() + "/rt_selftest_camera.png";
		photo.save(path);

		const scene_doc::Camera before = sb->document().camera;
		const int beforeHeight = sb->document().render.height;
		QString err;
		QStringList notes;
		const bool made = sb->applyCameraPhoto(path, 65.0, 1.6, false, 0.0, false, 0.0, false, &err, &notes);
		check(made, "the camera was matched to " + path + " " + err);
		if (made) {
			const scene_doc::Camera &c = sb->document().camera;
			const double fx = c.target.x - c.position.x, fy = c.target.y - c.position.y, fz = c.target.z - c.position.z, fl = std::sqrt(fx * fx + fy * fy + fz * fz);
			check(std::fabs(c.position.y - 1.6) < 1e-9 && std::fabs(c.position.x - before.position.x) < 1e-9 && std::fabs(c.position.z - before.position.z) < 1e-9, "the camera is 1.6 m up, where it stood");
			check(std::fabs(std::asin(fy / fl) * 180.0 / camera_from_photo::kPi - 6.0) < 1.0, QString("it looks 6 degrees up (%1)").arg(std::asin(fy / fl) * 180.0 / camera_from_photo::kPi));
			// the roll: up against the unrolled up, about the way it looks
			const double hl = std::sqrt(fx * fx + fz * fz), hx = fx / hl, hz = fz / hl, pitch = std::asin(fy / fl);
			const double u0[3] = {-hx * std::sin(pitch), std::cos(pitch), -hz * std::sin(pitch)}, f[3] = {fx / fl, fy / fl, fz / fl};
			const double right[3] = {f[1] * u0[2] - f[2] * u0[1], f[2] * u0[0] - f[0] * u0[2], f[0] * u0[1] - f[1] * u0[0]};
			const double rollDegrees = std::atan2(-(c.up.x * right[0] + c.up.y * right[1] + c.up.z * right[2]), c.up.x * u0[0] + c.up.y * u0[1] + c.up.z * u0[2]) * 180.0 / camera_from_photo::kPi;
			check(std::fabs(rollDegrees + 3.0) < 1.0, QString("and is rolled 3 degrees clockwise (%1)").arg(rollDegrees));
			check(std::fabs(c.fov - 2.0 * std::atan(std::tan(32.5 * camera_from_photo::kPi / 180.0) * 0.75) * 180.0 / camera_from_photo::kPi) < 0.01, QString("the field of view is that of 65 degrees across a 4:3 picture (%1 up and down)").arg(c.fov));
			check(sb->document().render.width == 800 && sb->document().render.height == 600, QString("the picture has the photo's shape (%1 x %2)").arg(sb->document().render.width).arg(sb->document().render.height));
			check(sb->cameraPhotoShownForTest() && sb->lookingThroughCameraForTest() && sb->view3dShownForTest(), "the photo is laid over the camera view, which is looked through and showing");
			check(!notes.isEmpty() && notes.join(" ").contains("horizon"), "the user is told how it was read: " + notes.join(" / ").left(140));
			check(!scene_doc::hasErrors(scene_doc::validate(sb->document())), "the scene has no errors");
			check(sb->undo(), "undo is available");
			const scene_doc::Camera &back = sb->document().camera;
			check(std::fabs(back.position.y - before.position.y) < 1e-9 && std::fabs(back.fov - before.fov) < 1e-9 && sb->document().render.height == beforeHeight, "one undo puts the camera and the picture's shape back");
		}
		// with the sky asked for
		QStringList skyNotes;
		const bool both = sb->applyCameraPhoto(path, 65.0, 1.6, false, 0.0, false, 0.0, true, &err, &skyNotes);
		check(both, "the camera and the sun and sky were set from the photo " + err);
		bool physical = false;
		for (const scene_doc::Light &l : sb->document().lights) physical = physical || (l.kind == scene_doc::LightKind::Infinite && l.physicalSky);
		check(physical, "the sky light is a Sun & sky now: " + skyNotes.join(" / ").right(120));
	}
	QTimer::singleShot(600, this, [this, shot, log, ok]() {
		shot("camera_edit");
		log(ok ? "RESULT: OK" : "RESULT: FAIL");
		QApplication::exit(ok ? 0 : 1);
	});
}

// RT_GUI_SELFTEST=composite RT_GUI_SELFTEST_OUT=<prefix>: the Scene Builder's "Render into the photo..." without its dialogs. Matches the starter scene's camera to a made-up photo (a pale sky over a flat
// green ground, the camera 1.6 m up and tilted 6 degrees), renders the scene into it with the floor as the ground, and checks the picture that was saved: the photo's shape, the sky and the far
// ground exactly the photo's, the objects put in (many pixels changed), the shadows (some ground pixels darker), and that a scene with only ground and lights, or no photo, fails with a message.
void MainWindow::runCompositeSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	if (m_sceneBuilder) m_tabWidget->setCurrentWidget(m_sceneBuilder);
	resize(1500, 950);
	SceneBuilderWidget *sb = m_sceneBuilder;
	bool ok = sb != nullptr;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	if (sb) {
		sb->setUseGpu(!qEnvironmentVariable("RT_GUI_SELFTEST_COMPOSITE_GPU").isEmpty());   // (the CPU, unless asked: a machine with an NVIDIA GPU can run the GPU path of it, denoiser included)
		const QString out = QDir::tempPath() + "/rt_selftest_composite_out.png";
		QFile::remove(out);
		QString noPhotoError;
		const bool noPhotoOk = sb->renderIntoPhoto(out, {0}, 0.0, 16, false, &noPhotoError);   // (the message is built after the call: argument evaluation order is not defined)
		check(!noPhotoOk && !noPhotoError.isEmpty(), "with no photo it fails with a message: " + noPhotoError.left(100));

		const int w = 640, h = 480;
		const double focal = (w / 2.0) / std::tan(65.0 * camera_from_photo::kPi / 360.0), cx = w / 2.0, cy = h / 2.0;
		const double t = 6.0 * camera_from_photo::kPi / 180.0;
		QImage photo(w, h, QImage::Format_RGB32);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				const double Y = y + 0.5 - cy, horizon = focal * std::tan(t);
				if (Y >= horizon) { photo.setPixel(x, y, qRgb(76, 92, 56)); continue; }
				const double e = std::min(1.0, (horizon - Y) / (h * 0.6));
				photo.setPixel(x, y, qRgb(static_cast<int>(190 - 120 * e), static_cast<int>(210 - 80 * e), static_cast<int>(240 - 10 * e)));
			}
		const QString path = QDir::tempPath() + "/rt_selftest_composite.png";
		photo.save(path);
		QString err;
		check(sb->applyCameraPhoto(path, 65.0, 1.6, false, 0.0, false, 0.0, false, &err), "the camera was matched to the photo " + err);

		QStringList notes;
		QElapsedTimer clock;
		clock.start();
		const bool made = sb->renderIntoPhoto(out, {0}, 0.0, 16, false, &err, &notes);
		check(made, QString("the scene was rendered into the photo (%1 s) %2").arg(clock.elapsed() / 1000.0, 0, 'f', 1).arg(err));
		// what it did is in the Log Output pane as it went: the three renders with their times, and the picture that was saved
		const QString pane = m_logTextEdit ? m_logTextEdit->toPlainText() : QString();
		{
			QFile paneFile(qEnvironmentVariable("RT_GUI_SELFTEST_OUT") + "_pane.txt");   // (kept next to the log, to read what the pane showed)
			if (paneFile.open(QIODevice::WriteOnly)) paneFile.write(pane.toUtf8());
		}
		check(pane.contains("[Scene Builder] render into the photo:") && pane.contains("render 1 of 3") && pane.contains("render 3 of 3 done") && pane.contains("render into the photo done (") && pane.contains("saved "),
		      "the Log Output pane shows the three renders and the saved picture: " + pane.right(300).replace(QChar(10), " | "));
		if (made) {
			QImage result(out);
			check(!result.isNull() && result.width() == w && result.height() == h, QString("the picture has the photo's shape (%1 x %2)").arg(result.width()).arg(result.height()));
			if (!result.isNull() && result.width() == w && result.height() == h) {
				result = result.convertToFormat(QImage::Format_RGB32);
				int skyChanged = 0, skyPixels = 0, changed = 0, darker = 0;
				for (int y = 0; y < h; ++y)
					for (int x = 0; x < w; ++x) {
						const QRgb a = photo.pixel(x, y), b = result.pixel(x, y);
						const int d = std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b));
						if (y < h / 4) { ++skyPixels; skyChanged += d > 3 ? 1 : 0; }
						changed += d > 30 ? 1 : 0;
						darker += (qGreen(b) < qGreen(a) * 0.85) ? 1 : 0;
					}
				check(skyChanged == 0 && skyPixels > 0, QString("the sky is the photo's own (%1 of %2 pixels changed)").arg(skyChanged).arg(skyPixels));
				check(changed > 1500, QString("the objects were put into the photo (%1 pixels changed)").arg(changed));
				check(darker > 100, QString("they cast shadows on the ground (%1 pixels darker)").arg(darker));
			}
			check(!notes.isEmpty(), "the user is told what was done: " + notes.join(" / ").left(120));
		}
		// The photo dialogs apply what is clicked or changed at once and show the result beside the settings: nothing is applied before the first click, two quick changes are one apply, a failure is shown
		// in the panel (not a pop-up) and the next change tries again.
		{
			QDialog probe(this);
			auto *probeLayout = new QVBoxLayout(&probe);
			auto *page = new QWidget(&probe);
			auto *pageLayout = new QVBoxLayout(page);
			auto *trigger = new QPushButton("change", page);
			pageLayout->addWidget(trigger);
			probeLayout->addWidget(page);
			auto *probeButtons = new QDialogButtonBox(QDialogButtonBox::Close, &probe);
			probeLayout->addWidget(probeButtons);
			int tries = 0;
			bool hintFirst = false, failureShown = false, oneApplyForTwo = false, resultShown = false, appliedByButton = false;
			auto panelText = [&probe](const QString &starts) {
				for (QLabel *label : probe.findChildren<QLabel *>()) if (label->text().startsWith(starts) && label->isVisible()) return true;
				return false;
			};
			QTimer::singleShot(300, [&]() { hintFirst = tries == 0 && panelText("the hint"); trigger->click(); });
			QTimer::singleShot(900, [&]() { failureShown = tries == 1 && panelText("failed"); trigger->click(); trigger->click(); });   // two quick changes
			QTimer::singleShot(1500, [&]() { oneApplyForTwo = tries == 2; resultShown = panelText("done 2"); });
			QTimer::singleShot(1700, [&]() {   // the Apply button applies now, without the pause
				for (QPushButton *b : probe.findChildren<QPushButton *>()) if (b->text() == "Apply") { b->click(); break; }
				appliedByButton = tries == 3 && panelText("done 3");
				probeButtons->button(QDialogButtonBox::Close)->click();
			});
			photo_dialog::run(probe, page, probeButtons, "the hint", [&](const std::function<void()> &schedule) { QObject::connect(trigger, &QPushButton::clicked, &probe, schedule); },
			                  [&tries](QString *result) {
				                  ++tries;
				                  *result = tries == 1 ? QString("failed") : QString("done %1").arg(tries);
				                  return tries != 1;
			                  }, 100);
			check(hintFirst && failureShown && oneApplyForTwo && resultShown && appliedByButton && tries == 3, QString("the photo windows apply at once: hint first %1, a failure is shown in the panel %2, two quick changes are one apply %3, the result is shown %4, the Apply button applies now %6 (%5 applies)").arg(hintFirst).arg(failureShown).arg(oneApplyForTwo).arg(resultShown).arg(tries).arg(appliedByButton));
		}
		// the dialog: it opens with a role to choose for every object (a picture of it is saved), and a cancel changes nothing
		int roleBoxes = 0;
		QTimer::singleShot(1200, [&roleBoxes]() {
			if (QWidget *w = QApplication::activeModalWidget()) {
				w->grab().save(qEnvironmentVariable("RT_GUI_SELFTEST_OUT") + "_dialog.png");
				roleBoxes = w->findChildren<QComboBox *>().size();
				if (QDialog *d = qobject_cast<QDialog *>(w)) d->reject();
			}
		});
		sb->renderIntoPhotoDialog();
		check(roleBoxes == static_cast<int>(sb->document().objects.size()), QString("the Render into the photo dialog has a role to choose for each of the %1 objects (%2 choices)").arg(sb->document().objects.size()).arg(roleBoxes));
		// every object marked as the ground (and the lamp is a light): nothing to put into the photo
		std::vector<int> all;
		for (size_t i = 0; i < sb->document().objects.size(); ++i) all.push_back(static_cast<int>(i));
		QString nothing;
		const bool nothingOk = sb->renderIntoPhoto(out, all, 0.0, 16, false, &nothing);
		check(!nothingOk && nothing.contains("Floor: ground") && nothing.contains("Put into the photo"), "with everything marked as the ground there is nothing to put in, and it says which object is what: " + nothing.left(220));
		// Denoise: the renders are denoised before the objects go in. Where a denoiser is there (Open Image Denoise installed, or the GPU's) the grain on the objects falls and the notes say so; where there
		// is none the picture is made all the same and the notes say why it is not denoised.
		{
			const bool wasDenoising = sb->denoiseTickedForTest();
			auto grain = [](const QImage &img) {   // the mean difference between neighbouring pixels in the lower half, where the objects and their shadows are (the photo itself is the same in both)
				double sum = 0.0;
				long count = 0;
				for (int y = img.height() / 2; y < img.height(); ++y)
					for (int x = 0; x + 1 < img.width(); ++x) {
						sum += std::abs(qGreen(img.pixel(x, y)) - qGreen(img.pixel(x + 1, y)));
						++count;
					}
				return count ? sum / count : 0.0;
			};
			QImage plain, cleaned;
			QString plainError, cleanError;
			QStringList plainNotes, cleanNotes;
			sb->setDenoiseForTest(false);
			const bool plainOk = sb->renderIntoPhotoImage(&plain, {0}, 0.0, 8, 480, false, &plainError, &plainNotes);
			sb->setDenoiseForTest(true);
			const bool cleanOk = sb->renderIntoPhotoImage(&cleaned, {0}, 0.0, 8, 480, false, &cleanError, &cleanNotes);
			sb->setDenoiseForTest(wasDenoising);
			check(plainOk && cleanOk, "the picture is made with and without the denoiser " + plainError + cleanError);
			if (plainOk && cleanOk) {
				plain.save(qEnvironmentVariable("RT_GUI_SELFTEST_OUT") + "_plain.png");   // (kept next to the log, to look at)
				cleaned.save(qEnvironmentVariable("RT_GUI_SELFTEST_OUT") + "_cleaned.png");
				const bool denoised = cleanNotes.join(" ").contains("were denoised");
				if (denoised) check(grain(cleaned) < 0.8 * grain(plain), QString("the denoiser took the grain off the objects (%1 down from %2)").arg(grain(cleaned), 0, 'f', 2).arg(grain(plain), 0, 'f', 2));
				else check(cleanNotes.join(" ").contains("could not be denoised"), "no denoiser here, and the notes say so: " + cleanNotes.join(" / ").right(200));
				check(!plainNotes.join(" ").contains("denoised"), "and without the tick nothing is said about denoising");
			}
		}
		// "Preview in the photo": no questions, and the picture appears in the preview pane (at the Preview's Draft size) with Save picture... ready
		sb->previewIntoPhoto();
		check(sb->previewHasPictureForTest() && sb->previewPictureSizeForTest() == QSize(480, 360), QString("Preview in the photo shows its picture in the preview pane (%1 x %2)").arg(sb->previewPictureSizeForTest().width()).arg(sb->previewPictureSizeForTest().height()));
		QApplication::processEvents();
		shot("composite_preview");
		// with the Sun & sky set from the photo too, the objects' exposure and colour are read from the photo's sky (or it says why not): the picture is made either way
		QStringList matchNotes;
		QString matchError;
		const bool both = sb->applyCameraPhoto(path, 65.0, 1.6, false, 0.0, false, 0.0, true, &matchError);
		const bool matchedOk = both && sb->renderIntoPhoto(out, {0}, 0.0, 16, true, &matchError, &matchNotes);
		check(matchedOk, "with the exposure matched to the photo " + matchError);
		check(matchNotes.join(" ").contains("exposure"), "and the user is told what was done about it: " + matchNotes.join(" / ").left(200));
		// Place by clicking: the Red box is picked and a click on the ground low in the camera view stands it there; then it is made 1.8 m tall
		sb->selectObject(3);
		Scene3DView *view = sb->view3dForTest();
		scene_doc::Float3 ground;
		const bool onGround = view && view->groundAt(QPointF(view->width() * 0.5, view->height() * 0.85), ground);
		check(onGround, "a click low in the camera view meets the ground");
		if (onGround) {
			check(sb->placePickedAt(ground), "the picked box is stood where the click met the ground");
			const scene_doc::Object &box = sb->document().objects[3];
			check(std::fabs(box.position.x - ground.x) < 1e-6 && std::fabs(box.position.z - ground.z) < 1e-6, QString("it is at the clicked place (%1, %2)").arg(ground.x).arg(ground.z));
			scene_doc::Float3 lo, hi;
			scene_doc::boundsOf(sb->document(), scene_doc::makeItemSet(sb->document(), {3}), lo, hi);
			check(std::fabs(lo.y) < 1e-6, QString("and stands on the ground (its base is at %1)").arg(lo.y));
			check(sb->setPickedHeight(1.8), "it is made 1.8 m tall");
			scene_doc::boundsOf(sb->document(), scene_doc::makeItemSet(sb->document(), {3}), lo, hi);
			check(std::fabs((hi.y - lo.y) - 1.8) < 1e-6 && std::fabs(lo.y) < 1e-6, QString("and is 1.8 m tall (%1) still on the ground").arg(hi.y - lo.y));
			view->beginPickPlace();
			check(view->pickingPlace(), "the view waits for clicks while placing");
			// a real left click on the view while it waits: the picked box goes to the ground under the mouse
			const QPointF at(view->width() * 0.3, view->height() * 0.9);
			scene_doc::Float3 under;
			check(view->groundAt(at, under), "the ground is under that pixel too");
			QMouseEvent click(QEvent::MouseButtonPress, at, view->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(view, &click);
			const scene_doc::Object &moved = sb->document().objects[3];
			check(std::fabs(moved.position.x - under.x) < 1e-6 && std::fabs(moved.position.z - under.z) < 1e-6, QString("a click on the picture stands it under the mouse (%1, %2)").arg(moved.position.x).arg(moved.position.z));
			check(view->pickingPlace(), "and the view goes on waiting for the next click");
			view->endPickPlace();
			check(!view->pickingPlace(), "and stops when asked to");
			// Place is over once the camera's picture is left (it would go on taking every click in a view that is no longer the photo's) or the view is hidden
			view->beginPickPlace();
			view->orbitForTest(20.0, 30.0);
			check(!view->pickingPlace(), "Place ends when the camera's view is left");
			sb->lookThroughCamera(true);
			view->beginPickPlace();
			view->hide();
			check(!view->pickingPlace(), "and when the view is hidden");
			view->show();
		}
		// the sample photos built into the program: each one is there, is a 1280-pixel picture, and sets a camera and a sky; the beach is put into with the exposure matched
		const QList<sample_photos::Sample> samples = sample_photos::all();
		check(samples.size() >= 4, QString("the program has %1 sample photos").arg(samples.size()));
		for (const sample_photos::Sample &s : samples) {
			const QImage picture(s.resource);
			check(!picture.isNull() && picture.width() == 1280 && !s.credit.isEmpty() && s.credit.contains("CC"), QString("sample %1 is in the program (%2 x %3) with its credit").arg(s.resource).arg(picture.width()).arg(picture.height()));
			QString sampleError;
			check(sb->applyCameraPhoto(s.resource, 0.0, 1.6, false, 0.0, false, 0.0, true, &sampleError), QString("the camera and sky are set from %1 %2").arg(s.resource, sampleError));
		}
		QStringList beachNotes;
		QString beachError;
		const bool beachOk = sb->applyCameraPhoto(samples[0].resource, 0.0, 1.6, false, 0.0, false, 0.0, true, &beachError) && sb->renderIntoPhoto(qEnvironmentVariable("RT_GUI_SELFTEST_OUT") + "_sample.png", {0}, 0.0, 24, true, &beachError, &beachNotes);
		check(beachOk, "the beach sample was put into " + beachError);
		check(beachNotes.join(" ").contains("matched to the photo's sky"), "and its sky gave the objects' exposure: " + beachNotes.join(" / ").right(160));
		// a real photo, when one is given: the camera, the sky and the picture from it (the result is saved next to the log for a look)
		const QString real = qEnvironmentVariable("RT_GUI_SELFTEST_COMPOSITE_PHOTO");
		if (!real.isEmpty()) {
			QString realError;
			QStringList realNotes;
			const bool madeReal = sb->applyCameraPhoto(real, 0.0, 1.6, false, 0.0, false, 0.0, true, &realError, &realNotes) && sb->renderIntoPhoto(qEnvironmentVariable("RT_GUI_SELFTEST_OUT") + "_real.png", {0}, 0.0, 48, true, &realError, &realNotes);
			check(madeReal, "the real photo " + real + " was put into: " + realError);
			for (const QString &n : realNotes) log("note: " + n);
		}
		// Match to a photo for the camera is one window with no Ok: a click on a sample applies it at once and the result is beside the settings; another click applies that one (taking the first back, so the
		// undo list grows by one apply, not two); one's own photo joins the row and applies too.
		{
			const QString out = qEnvironmentVariable("RT_GUI_SELFTEST_OUT");
			QString seen;
			int samplesListed = 0;
			bool fillsScreen = false, hasApply = false;
			std::size_t depthAtStart = sb->undoDepth(), depthAfterTwo = 0;
			auto panelHas = [](QWidget *w, const QString &what) {
				for (QLabel *label : w->findChildren<QLabel *>()) if (label->isVisible() && label->text().contains(what)) return true;
				return false;
			};
			auto firstList = [](QWidget *w) { return w->findChildren<QListWidget *>().first(); };
			QTimer::singleShot(900, [&]() {
				if (QWidget *w = QApplication::activeModalWidget()) {
					seen += panelHas(w, "Click a photo to use it") ? "hint " : "";
					for (QPushButton *b : w->findChildren<QPushButton *>()) hasApply = hasApply || b->text() == "Apply";
					if (const QScreen *screen = QGuiApplication::primaryScreen()) fillsScreen = w->width() >= screen->availableGeometry().width() * 6 / 10 && w->height() >= screen->availableGeometry().height() * 85 / 100;   // (it opens tall: most of the screen's height)
					samplesListed = firstList(w)->count();
					w->grab().save(out + "_camera_settings.png");
					firstList(w)->setCurrentRow(1);   // the second sample is clicked (a click makes it the current one, then says it was clicked)
					emit firstList(w)->itemClicked(firstList(w)->item(1));
				}
			});
			QTimer::singleShot(3500, [&]() {
				if (QWidget *w = QApplication::activeModalWidget()) {
					seen += panelHas(w, "north-sea.jpg") ? "north-sea " : "";
					w->grab().save(out + "_camera_result.png");
					firstList(w)->setCurrentRow(0);   // and then the first, as an arrow key would
				}
			});
			QTimer::singleShot(6000, [&]() {
				if (QWidget *w = QApplication::activeModalWidget()) {
					seen += panelHas(w, "beach.jpg") ? "beach " : "";
					depthAfterTwo = sb->undoDepth();
					if (auto *picker = w->findChild<sample_photos::Picker *>()) picker->chooseOwnPhotoForTest(QDir::tempPath() + "/rt_selftest_composite.png");
				}
			});
			QTimer::singleShot(8500, [&]() {
				if (QWidget *w = QApplication::activeModalWidget()) {
					seen += panelHas(w, "rt_selftest_composite.png") ? "own " : "";
					for (QPushButton *b : w->findChildren<QPushButton *>())
						if (b->text().remove(QLatin1Char('&')) == "Close") { b->click(); break; }
				}
			});
			sb->cameraFromPhoto();
			check(seen == "hint north-sea beach own " && samplesListed == static_cast<int>(samples.size()) && depthAfterTwo - depthAtStart <= 2 && fillsScreen && hasApply,
			      QString("Match to a photo for the camera is one window that applies on click (%1; %2 samples listed; two applies left %3 undo steps; fills the screen %4, has an Apply button %5)").arg(seen).arg(samplesListed).arg(depthAfterTwo - depthAtStart).arg(fillsScreen).arg(hasApply));
		}
	}
	QTimer::singleShot(600, this, [this, shot, log, ok]() {
		shot("composite_edit");
		log(ok ? "RESULT: OK" : "RESULT: FAIL");
		QApplication::exit(ok ? 0 : 1);
	});
}
