// mainwindow_selftest_models.cpp -- RT_GUI_SELFTEST=modelthumbs: makes models/thumbnails/<stem>.png for the library's glTF models, with the real importer and the real renderer (so a thumbnail shows the model
// as the Builder will add it, colours and all). The .obj models' thumbnails are made by scripts/make_model_thumbnails.sh.
//
//   RT_GUI_SELFTEST=modelthumbs RT_GUI_SELFTEST_MODELS=<the models folder> [RT_GUI_SELFTEST_MODELS_ALL=1] RayTracerGUI.exe
//
// A model that already has a thumbnail is left alone unless RT_GUI_SELFTEST_MODELS_ALL is set. Each model is imported into an empty scene with a sky and a sun, seen from the front right above, 160 x 160.
#include "mainwindow.h"
#include "scene_builder_widget.h"

#include "../src/shared/gltf_import.h"
#include "../src/shared/model_library.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QTimer>

#include <algorithm>
#include <cmath>

void MainWindow::runModelThumbsSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	Q_UNUSED(shot);
	if (m_sceneBuilder) m_tabWidget->setCurrentWidget(m_sceneBuilder);
	resize(1500, 950);
	SceneBuilderWidget *sb = m_sceneBuilder;
	bool ok = sb != nullptr;
	const QString folder = qEnvironmentVariable("RT_GUI_SELFTEST_MODELS");
	const bool all = !qEnvironmentVariable("RT_GUI_SELFTEST_MODELS_ALL").isEmpty();
	if (!sb || folder.isEmpty() || !QDir(folder).exists()) {
		log("FAIL: RT_GUI_SELFTEST_MODELS must name the models folder");
		ok = false;
	} else {
		QDir().mkpath(folder + "/thumbnails");
		int made = 0, skipped = 0, failed = 0;
		for (const model_library::Entry &e : model_library::catalog()) {
			if (!model_library::isGltf(e)) continue;
			const QString file = folder + "/" + QString::fromStdString(model_library::fileOf(e));
			const QString thumbnail = folder + "/thumbnails/" + QString::fromUtf8(e.stem) + ".png";
			if (!QFileInfo::exists(file)) continue;
			if (!all && QFileInfo::exists(thumbnail)) { ++skipped; continue; }
			// the model's own size, for the camera: the importer's extents scaled as the Builder scales the objects
			QFile source(file);
			if (!source.open(QIODevice::ReadOnly)) { log("FAIL: cannot read " + file); ++failed; continue; }
			const QByteArray bytes = source.readAll();
			const gltf_import::Result parsed = gltf_import::parse(std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
			if (!parsed.ok) { log("FAIL: " + file + ": " + QString::fromStdString(parsed.error)); ++failed; continue; }
			sb->newScene();
			sb->clearObjectsForTest();
			QString message;
			if (!sb->importGltf(file, &message)) { log("FAIL: " + file + ": " + message); ++failed; continue; }
			const scene_doc::Document &doc = sb->document();
			const scene_doc::Object &part = doc.objects.back();
			const gltf_import::Model &m = parsed.model;
			const double extent[3] = {m.hi[0] - m.lo[0], m.hi[1] - m.lo[1], m.hi[2] - m.lo[2]};
			const double widest = std::max({extent[0], extent[1], extent[2]});
			const double norm = std::max(extent[1], 0.01 * widest);
			const double scale = part.meshScale * 2.0 / norm;   // (the importer's: file units to scene units)
			const double size = widest * scale;
			const double cx = part.position.x, cy = part.position.y + 0.5 * extent[1] * scale, cz = part.position.z;
			const double d = 1.9 * size;
			sb->setThumbnailViewForTest({cx + 0.6 * d, cy + 0.35 * d, cz + 0.8 * d}, {cx, cy, cz});
			sb->runActionForTest("lighting sunny");
			const QString scenePath = QDir::tempPath() + "/rt_thumb_scene.pbrt", ppm = QDir::tempPath() + "/rt_thumb.ppm";
			QFile text(scenePath);
			const bool wrote = text.open(QIODevice::WriteOnly) && text.write(sb->pbrtTextForTest().toUtf8()) > 0;
			text.close();
			QFile::remove(ppm);
			QProcess renderer;
			renderer.setProcessChannelMode(QProcess::MergedChannels);
			renderer.setWorkingDirectory(QCoreApplication::applicationDirPath());
			renderer.start(sb->launcherPathForTest(), {"--cpu", "--seed", "2", "--output", ppm, "--height", "160", "160", "48", "6", scenePath});
			const bool finished = wrote && renderer.waitForFinished(300000) && renderer.exitCode() == 0;
			const QImage picture(ppm);
			if (!finished || picture.isNull() || !picture.save(thumbnail, "PNG")) { log(QString("FAIL: render of %1 (exit %2, %3): %4").arg(QString::fromUtf8(e.stem)).arg(renderer.exitCode()).arg(renderer.exitStatus() == QProcess::CrashExit ? "crashed" : "ended").arg(QString::fromLocal8Bit(renderer.readAll()).right(300).simplified())); ++failed; continue; }
			log("ok: " + thumbnail);
			++made;
			QCoreApplication::processEvents();
		}
		log(QString("thumbnails: %1 made, %2 left as they were, %3 failed").arg(made).arg(skipped).arg(failed));
		ok = failed == 0;
	}
	QTimer::singleShot(200, this, [log, ok]() {
		log(ok ? "RESULT: OK" : "RESULT: FAIL");
		QApplication::exit(ok ? 0 : 1);
	});
}
