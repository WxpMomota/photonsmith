// mainwindow_selftest_builderui.cpp -- the "builderui" self-test mode: pictures of the Scene Builder tab in the states a person meets (a scene open, an object with a mixed material selected, a
// light, the camera, the Add menu), for looking over the layout at a given window size and language. It checks nothing; it writes <prefix>_builderui_N_<name>.png.
//
//   RT_GUI_SELFTEST=builderui RT_GUI_SELFTEST_OUT=<prefix> [RT_GUI_SELFTEST_WIDTH=1400 RT_GUI_SELFTEST_HEIGHT=900] [RT_GUI_SELFTEST_LANGUAGE=ja] [RT_GUI_SELFTEST_THEME=...]
#include "mainwindow.h"

#include "scene_builder_widget.h"
#include "../src/shared/scene_showcase.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMenu>
#include <QPushButton>
#include <QTimer>

namespace {
// Lets the layout settle: the properties are rebuilt on a zero timer and a view paints after that.
void settle(int ms = 350) {
	QEventLoop loop;
	QTimer::singleShot(ms, &loop, &QEventLoop::quit);
	loop.exec();
	QApplication::processEvents();
}
}  // namespace

void MainWindow::runBuilderUiSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	const int w = qEnvironmentVariableIntValue("RT_GUI_SELFTEST_WIDTH") > 0 ? qEnvironmentVariableIntValue("RT_GUI_SELFTEST_WIDTH") : 1400;
	const int h = qEnvironmentVariableIntValue("RT_GUI_SELFTEST_HEIGHT") > 0 ? qEnvironmentVariableIntValue("RT_GUI_SELFTEST_HEIGHT") : 900;
	resize(w, h);
	if (qEnvironmentVariableIsSet("RT_GUI_SELFTEST_THEME")) switchTheme(qEnvironmentVariable("RT_GUI_SELFTEST_THEME"));
	SceneBuilderWidget *sb = m_sceneBuilder;
	if (!sb) { log("FAIL: no Scene Builder tab"); QApplication::exit(1); return; }
	m_tabWidget->setCurrentWidget(sb);
	settle();
	// The showcase scene, written to a scratch file and opened like any scene of the user's.
	const QString file = QDir::temp().filePath("rt_builderui_showcase.pbrt");
	{
		QFile f(file);
		if (f.open(QIODevice::WriteOnly)) f.write(QByteArray::fromStdString(scene_doc::showcaseFileText()));
	}
	QString error;
	if (!sb->openFile(file, &error)) log("FAIL: could not open the showcase: " + error);
	sb->show3dView(true);
	sb->frame3dForTest();
	settle(500);
	shot("builderui_0_scene");
	const scene_doc::Document &doc = sb->document();
	auto objectNamed = [&](const char *name) {
		for (std::size_t i = 0; i < doc.objects.size(); ++i)
			if (doc.objects[i].name == name) return static_cast<int>(i);
		return -1;
	};
	const struct { const char *name; const char *tag; } objects[] = {{"Worn paint box", "builderui_1_mixed_material"}, {"Tile", "builderui_2_picture_tile"}, {"Candle wax ball", "builderui_3_subsurface"},
	                                                                    {"Backdrop", "builderui_4_cyclorama"}};
	// The whole properties page as well as the window: the window shows only what fits, the page is what a person scrolls through.
	const auto propertiesPage = [&](const QString &tag) {
		if (QWidget *page = sb->inspectorContentForTest()) {
			const QString path = qEnvironmentVariable("RT_GUI_SELFTEST_OUT") + "_" + tag + "_props.png";
			log(QString("properties page %1: %2 (%3 x %4)").arg(path, page->grab().save(path) ? "saved" : "FAILED").arg(page->width()).arg(page->height()));
		}
	};
	for (const auto &o : objects) {
		const int i = objectNamed(o.name);
		if (i < 0) { log(QString("FAIL: the showcase has no object named %1").arg(o.name)); continue; }
		sb->selectObject(i);
		settle();
		shot(o.tag);
		propertiesPage(o.tag);
	}
	for (std::size_t i = 0; i < doc.lights.size(); ++i) {
		if (doc.lights[i].name == "Window light") { sb->selectLight(static_cast<int>(i)); settle(); shot("builderui_5_projector"); propertiesPage("builderui_5_projector"); }
		if (doc.lights[i].name == "Key light") { sb->selectLight(static_cast<int>(i)); settle(); shot("builderui_6_spotlight"); propertiesPage("builderui_6_spotlight"); }
	}
	sb->selectCameraForTest();
	settle();
	shot("builderui_7_camera");
	propertiesPage("builderui_7_camera");
	// The Add menu, open: it is a popup of its own, so its own picture.
	for (QPushButton *b : sb->findChildren<QPushButton *>()) {
		if (b->text() != tr("Add") || !b->menu()) continue;
		QMenu *menu = b->menu();
		menu->popup(b->mapToGlobal(QPoint(0, b->height())));
		settle(500);
		const QString path = qEnvironmentVariable("RT_GUI_SELFTEST_OUT") + "_builderui_8_add_menu.png";
		log(QString("screenshot %1: %2 (menu %3 x %4)").arg(path, menu->grab().save(path) ? "saved" : "FAILED").arg(menu->width()).arg(menu->height()));
		menu->close();
		break;
	}
	// The 3D view as the Rendered viewport draws it (a picture of the scene that has had some time to refine), with the mixed-material box selected: what the gallery shows.
	if (SceneBuilderWidget::renderedViewportAvailable()) {
		sb->selectObject(objectNamed("Worn paint box"));
		sb->setRenderedViewport(true);
		QElapsedTimer waited;
		waited.start();
		while (waited.elapsed() < 90000 && sb->renderedSamplesForTest() < 100) settle(250);
		settle(500);
		shot("builderui_9_rendered");
		sb->setRenderedViewport(false);
	} else {
		log("skipped the rendered viewport picture: the Live Preview renderer is not available here");
	}
	QFile::remove(file);
	log("RESULT: OK");
	QApplication::exit(0);
}
