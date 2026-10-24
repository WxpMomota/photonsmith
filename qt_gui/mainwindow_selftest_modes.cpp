// mainwindow_selftest_modes.cpp - the smaller self-test modes (options, builder, diagnostics, scenekeys, download, ui), each its own function called by
// runSelfTest() in mainwindow_selftest.cpp. A pure split of what used to be one 500-line function.

#include "mainwindow.h"
#include "scene_builder_array_dialog.h"
#include "../src/shared/oidn_runtime.h"
#include "denoiser_installer.h"
#include "live_ai_denoise.h"
#include "app_log.h"
#include "settings_keys.h"
#include "film_info.h"   // the cartoon films, as the renderer's library says them (this program does not compile them)
#include <iostream>
#include "../src/shared/pbrt_asset_check.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QPainter>
#include <QList>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTextStream>
#include <QTimer>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QAbstractItemView>
#include <QScreen>
#include <QSpinBox>
#include <functional>
#include <memory>
#include <cmath>
#include "scene_builder_widget.h"
#include "scene_technique_notes.h"
#include "scene_metadata_client.h"

// The Scene Builder's "Add to scene list" checks: where the copy goes (never into a .app bundle), that it is in the scene list at once under its

void MainWindow::runOptionsSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	// Every button, drop-down, line edit and spin box is the same height (46px: a 40px box and a 3px margin above and below - see the QPushButton rule in
	// mainwindow_style.cpp), on every tab. Looks at the visible ones; a control that sets its own height shows up here.
	{
		resize(1500, 1000);
		show();
		QStringList wrong;
		int checked = 0;
		for (int t = 0; t < m_tabWidget->count(); ++t) {
			m_tabWidget->setCurrentIndex(t);
			QApplication::processEvents();
			for (QWidget *w : m_tabWidget->currentWidget()->findChildren<QWidget *>()) {
				if (!w->isVisible()) continue;
				QString kind;
				if (qobject_cast<QPushButton *>(w)) kind = "button";
				else if (qobject_cast<QComboBox *>(w)) kind = "drop-down";
				else if (qobject_cast<QAbstractSpinBox *>(w)) kind = "spin box";
				else if (qobject_cast<QLineEdit *>(w) && !qobject_cast<QAbstractSpinBox *>(w->parentWidget()) && !qobject_cast<QComboBox *>(w->parentWidget())) kind = "line edit";
				else continue;
				++checked;
				if (w->height() != 46) {
					const QString text = qobject_cast<QPushButton *>(w) ? qobject_cast<QPushButton *>(w)->text() : QString();
					wrong << QString("%1 \"%2\" %3 (%4): %5px").arg(kind, text, w->objectName(), m_tabWidget->tabText(t)).arg(w->height());
				}
			}
		}
		log(QString("%1: %2 controls checked, every one 46px tall%3").arg(wrong.isEmpty() ? "ok" : "FAIL").arg(checked).arg(wrong.isEmpty() ? QString() : QString(" - not: ") + wrong.join("; ")));
		if (!wrong.isEmpty()) { QApplication::exit(1); return; }
	}
	// The Render Options tab with Live Preview selected, large enough to read: which live controls does this platform show?
	resize(1100, 1500);
#ifdef RT_GUI_HAVE_LIVE_PREVIEW
	const int idx = m_modeCombo->findData(static_cast<int>(OutputMode::LivePreview));
	if (idx >= 0) m_modeCombo->setCurrentIndex(idx);
#endif
	for (int i = 0; i < m_tabWidget->count(); ++i) {
		log(QString("tab %1: %2").arg(i).arg(m_tabWidget->tabText(i)));
		if (m_tabWidget->tabText(i).contains("Render Options")) m_tabWidget->setCurrentIndex(i);
	}
	QTimer::singleShot(800, this, [this, shot, log]() {
		shot("options");
#ifdef Q_OS_MAC
		// Ticking "AI denoise" (Live Preview) with no denoiser library present offers to download it; declining must untick the box again and not leave it
		// remembered as on. (Skipped when this machine has the library, or the app cannot install it.)
		QCheckBox *aiBox = nullptr;
		for (QCheckBox *c : findChildren<QCheckBox *>())
			if (c->text() == tr("AI denoise")) aiBox = c;
		if (aiBox && !oidn_runtime::available() && denoiser_installer::isSupportedHere()) {
			QTimer::singleShot(400, this, []() {
				if (auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
					for (QAbstractButton *b : question->buttons())
						if (question->buttonRole(b) == QMessageBox::NoRole) b->click();
			});
			aiBox->setChecked(true);   // returns once the question is answered
			const bool untickedAgain = !aiBox->isChecked() && !live_ai_denoise::savedEnabled();
			log(QString("%1: declining the denoiser download unticks \"AI denoise\" and does not remember it").arg(untickedAgain ? "ok" : "FAIL"));
			if (!untickedAgain) { QApplication::exit(1); return; }
		} else {
			log("skipped: the AI denoise download prompt (the denoiser library is present here, or not installable on this platform)");
		}
#endif
		// ...and scrolled to the bottom, where the Live Preview Settings group is.
		QList<QScrollArea *> areas = m_tabWidget->currentWidget()->findChildren<QScrollArea *>();
		if (auto *self = qobject_cast<QScrollArea *>(m_tabWidget->currentWidget())) areas.prepend(self);
		for (QScrollArea *sa : areas)
			sa->verticalScrollBar()->setValue(sa->verticalScrollBar()->maximum());
		QTimer::singleShot(500, this, [shot]() { shot("options_bottom"); QApplication::exit(0); });
	});
	return;
}

	// RT_GUI_SELFTEST=builder: drives the Scene Builder tab through an edit, undo/redo, a save and re-open, and a real preview render (needs
	// ray_tracer next to the GUI); saves screenshots <out>_builder_edit.png / _builder_preview.png and exits 0 if every step held.
// Duplicate, Array and Scatter (scene_array.h, the Array... window): each is one undo step, the copies are ordinary objects with their own names, and the window
// itself offers the same objects it would add. Leaves the document as it found it (everything is undone).
static void selfTestArray(SceneBuilderWidget *sb, const std::function<void(bool, const QString &)> &check) {
	const size_t n0 = sb->document().objects.size();
	sb->selectObject(2);   // the gold ball
	const std::string sourceName = sb->document().objects[2].name;
	sb->duplicateSelected();
	check(sb->document().objects.size() == n0 + 1 && sb->document().objects.back().name == sourceName + " 2", "Duplicate makes a numbered copy (" + QString::fromStdString(sb->document().objects.back().name) + ")");
	check(sb->document().objects.back().position.x > sb->document().objects[2].position.x, "the copy sits beside the original");
	check(sb->undo() && sb->document().objects.size() == n0, "one undo removes the duplicate");
	// A window with the defaults: a grid of three in a row (two new), a ring of eight (seven new), a scatter of twenty.
	ArrayDialog dialog(2, sb->document().objects, sb);
	check(dialog.copies().size() == 2, "the Array window's default grid adds 2 objects");
	dialog.setMode(ArrayDialog::Mode::Ring);
	check(dialog.copies().size() == 7, "its default ring of 8 adds 7 (the original is one of them)");
	dialog.setRingCount(5);
	check(dialog.copies().size() == 4, "a ring of 5 adds 4");
	dialog.setMode(ArrayDialog::Mode::Scatter);
	const size_t scattered = dialog.copies().size();
	check(scattered > 0 && scattered <= 20, "the scatter adds up to the 20 asked for (" + QString::number(scattered) + ")");
	// RT_GUI_SELFTEST_ARRAY_DIALOG_PNG=<prefix> saves a picture of each page of the window (<prefix>_grid.png, _ring.png, _scatter.png).
	const QString picture = qEnvironmentVariable("RT_GUI_SELFTEST_ARRAY_DIALOG_PNG");
	if (!picture.isEmpty()) {
		dialog.show();
		const std::pair<ArrayDialog::Mode, const char *> pages[] = {{ArrayDialog::Mode::Grid, "grid"}, {ArrayDialog::Mode::Ring, "ring"}, {ArrayDialog::Mode::Scatter, "scatter"}};
		for (const auto &page : pages) {
			dialog.setMode(page.first);
			QApplication::processEvents();
			dialog.grab().save(picture + "_" + page.second + ".png");
		}
		dialog.hide();
	}
	dialog.setMode(ArrayDialog::Mode::Grid);
	dialog.setGridCounts(4, 1, 3);
	const std::vector<scene_doc::Object> grid = dialog.copies();
	check(grid.size() == 11, "a 4 x 1 x 3 grid adds 11 objects");
	sb->addCopies(grid);
	check(sb->document().objects.size() == n0 + 11 && sb->document().objects[n0].name == sourceName + " 2", "the copies are added with their own names");
	check(sb->undo() && sb->document().objects.size() == n0, "all eleven are one undo step");
	check(sb->redo() && sb->document().objects.size() == n0 + 11, "and one redo");
	sb->addCopies(std::vector<scene_doc::Object>());
	check(sb->document().objects.size() == n0 + 11, "adding no copies changes nothing");
	check(sb->undo() && sb->document().objects.size() == n0, "back to the scene as it was");
	// A prop is several objects: the window ticks the other parts that belong with the one picked, and copies them as one.
	sb->addProp(scene_doc::PropKind::Tree);
	const int treeFirst = static_cast<int>(n0);
	const int treeParts = static_cast<int>(sb->document().objects.size() - n0);
	check(treeParts >= 2, "a tree prop is several objects (" + QString::number(treeParts) + ")");
	ArrayDialog treeDialog(treeFirst, sb->document().objects, sb);
	check(static_cast<int>(treeDialog.unit().size()) == treeParts, "the window ticks the tree's other parts to go with the one picked");
	treeDialog.setGridCounts(3, 1, 1);
	check(static_cast<int>(treeDialog.copies().size()) == 2 * treeParts, "a row of three trees adds two trees' worth of objects");
	treeDialog.tickPart(QString::fromStdString(sb->document().objects[n0 + 1].name), false);
	check(static_cast<int>(treeDialog.unit().size()) == treeParts - 1, "unticking a part leaves it out");
	sb->undo();
	check(sb->document().objects.size() == n0, "the tree is removed again by one undo");
	// A light is duplicated too (and numbered).
	const size_t lights0 = sb->document().lights.size();
	sb->selectLight(0);
	sb->duplicateSelected();
	check(sb->document().lights.size() == lights0 + 1, "a light can be duplicated");
	check(sb->undo() && sb->document().lights.size() == lights0, "and undone");
}

// Several items at once (scene_selection.h, scene_builder_multi.cpp): picking by list, click and box, dragging, copying, deleting and grouping them together, each
// one undo step. Leaves the document as it found it.
static void selfTestMultiSelect(SceneBuilderWidget *sb, const std::function<void(bool, const QString &)> &check) {
	using Sel = BuilderSelection;
	const size_t n0 = sb->document().objects.size();
	const auto objectsPicked = [sb]() { return sb->pickedItems().objects; };
	sb->pickObjects({1, 2});
	check(objectsPicked() == std::vector<int>({1, 2}) && sb->pickedRowsForTest() == 2, "two objects can be picked together (the list shows both)");
	const auto pos = [sb](int i) { return sb->document().objects[static_cast<size_t>(i)].position; };
	const scene_doc::Float3 glass0 = pos(1), gold0 = pos(2);
	check(sb->dragPickedForTest(1, QPointF(40, 0)), "dragging one of the two with the mouse moves it");
	const double dx = pos(1).x - glass0.x, dz = pos(1).z - glass0.z;
	check(dx != 0.0 && std::abs((pos(2).x - gold0.x) - dx) < 1e-9 && std::abs((pos(2).z - gold0.z) - dz) < 1e-9, "and the other moves by the same amount");
	check(sb->undo() && pos(1).x == glass0.x && pos(2).x == gold0.x, "the drag of both is one undo step");
	sb->moveSelectedBy({0.0, 1.0, 0.0});
	check(std::abs(pos(1).y - (glass0.y + 1.0)) < 1e-9 && std::abs(pos(2).y - (gold0.y + 1.0)) < 1e-9, "Move all by lifts both");
	check(sb->undo() && pos(2).y == gold0.y, "undone");

	sb->duplicateSelected();
	check(sb->document().objects.size() == n0 + 2 && objectsPicked() == std::vector<int>({static_cast<int>(n0), static_cast<int>(n0) + 1}), "Duplicate copies both and picks the copies");
	check(std::abs((pos(static_cast<int>(n0) + 1).x - pos(static_cast<int>(n0)).x) - (gold0.x - glass0.x)) < 1e-9, "the copies keep their places relative to each other");
	check(sb->undo() && sb->document().objects.size() == n0, "one undo removes both copies");

	// Click and Ctrl-click in the layout view.
	sb->selectObject(1);
	sb->clickItemForTest(Sel{Sel::Kind::Object, 2}, Qt::ControlModifier);
	check(objectsPicked() == std::vector<int>({1, 2}), "Ctrl-click adds an object to what is picked");
	sb->clickItemForTest(Sel{Sel::Kind::Object, 2}, Qt::ControlModifier);
	check(objectsPicked() == std::vector<int>({1}), "Ctrl-click on a picked object takes it out");
	sb->pickObjects({1, 2});
	sb->clickItemForTest(Sel{Sel::Kind::Object, 2});
	check(objectsPicked() == std::vector<int>({2}), "a plain click on one of several leaves just that one");

	// Turning and scaling several as one, about the middle of them.
	{
		sb->pickObjects({1, 2});
		const scene_doc::Document before = sb->document();
		const auto gap = [sb]() { const auto &a = sb->document().objects[1].position, &b = sb->document().objects[2].position; return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); };
		const double gap0 = gap();
		const scene_doc::Float3 centre0 = scene_doc::centroidOf(sb->document(), sb->pickedItems());
		sb->turnSelectedBy(1, 90.0);
		const scene_doc::Float3 centre1 = scene_doc::centroidOf(sb->document(), sb->pickedItems());
		check(std::abs(gap() - gap0) < 1e-9 && std::abs(centre1.x - centre0.x) < 1e-9 && std::abs(centre1.z - centre0.z) < 1e-9 && std::abs(pos(1).x - before.objects[1].position.x) > 0.1 && std::abs(sb->document().objects[1].rotation.y - before.objects[1].rotation.y) > 1.0,
		      "Turn all by turns the set about its middle: the gap and the middle stay, the objects turn");
		check(sb->undo() && pos(1).x == before.objects[1].position.x && sb->document().objects[1].rotation.y == before.objects[1].rotation.y, "one undo puts them back");
		const double radius0 = sb->document().objects[2].radius;
		sb->scaleSelectedBy(2.0);
		check(std::abs(gap() - 2.0 * gap0) < 1e-9 && std::abs(sb->document().objects[2].radius - 2.0 * radius0) < 1e-9, "Scale all to 200 % doubles the gap and every size");
		check(sb->undo() && sb->document().objects[2].radius == radius0, "undone");
		// The 3D view's ring and handle do the same with the mouse, one undo step for the whole drag.
		sb->show3dView(true);
		check(sb->gizmoButtonEnabled(1) && sb->gizmoButtonEnabled(2), "Rotate and Scale are available while several are picked");
		check(sb->dragPickedRing3dForTest(1, 40.0) && std::abs(gap() - gap0) < 1e-6, "dragging the Rotate ring turns the set as one (the gap stays)");
		check(sb->undo() && pos(1).x == before.objects[1].position.x && pos(2).x == before.objects[2].position.x, "the whole drag is one undo step");
		check(sb->dragPickedScale3dForTest(0, 1.5) && gap() > gap0 * 1.2, "dragging a Scale handle stretches the set about its middle");
		check(sb->undo() && std::abs(gap() - gap0) < 1e-9, "and that is one undo step too");
		sb->show3dView(false);
	}

	// A box: whatever is inside it (computed from where the view draws things).
	const QPointF a = sb->itemScreenPosForTest(Sel{Sel::Kind::Object, 1}), b = sb->itemScreenPosForTest(Sel{Sel::Kind::Object, 2});
	const QPointF topLeft(std::min(a.x(), b.x()) - 12, std::min(a.y(), b.y()) - 12), bottomRight(std::max(a.x(), b.x()) + 12, std::max(a.y(), b.y()) + 12);
	std::vector<int> expected;
	for (int i = 0; i < static_cast<int>(n0); ++i) {
		const QPointF p = sb->itemScreenPosForTest(Sel{Sel::Kind::Object, i});
		if (p.x() >= topLeft.x() && p.x() <= bottomRight.x() && p.y() >= topLeft.y() && p.y() <= bottomRight.y()) expected.push_back(i);
	}
	sb->selectObject(3);
	sb->boxSelectForTest(topLeft, bottomRight);
	std::vector<int> want = expected;
	want.push_back(3);
	std::sort(want.begin(), want.end());
	want.erase(std::unique(want.begin(), want.end()), want.end());
	check(objectsPicked() == want && expected.size() >= 2, "a Ctrl-dragged box adds the objects inside it to what was picked (" + QString::number(expected.size()) + " inside)");

	// Groups.
	sb->pickObjects({1, 2});
	sb->groupSelected();
	const std::string group = sb->document().objects[1].group;
	check(!group.empty() && sb->document().objects[2].group == group && sb->document().objects[3].group.empty(), "Group makes the two a group");
	sb->selectObject(3);
	sb->clickItemForTest(Sel{Sel::Kind::Object, 2});
	check(objectsPicked() == std::vector<int>({1, 2}), "clicking one member picks the whole group");
	sb->duplicateSelected();
	const std::string copyGroup = sb->document().objects[n0].group;
	check(!copyGroup.empty() && copyGroup != group && sb->document().objects[n0 + 1].group == copyGroup, "a copy of a group is a group of its own");
	check(sb->undo() && sb->document().objects.size() == n0, "and one undo removes it");
	sb->pickObjects({1, 2});
	sb->deleteSelected();
	check(sb->document().objects.size() == n0 - 2 && sb->pickedItems().empty(), "Delete removes both");
	check(sb->undo() && sb->document().objects.size() == n0 && sb->document().objects[1].group == group, "one undo brings both back, still a group");
	sb->pickObjects({1, 2});
	sb->ungroupSelected();
	check(sb->document().objects[1].group.empty() && sb->document().objects[2].group.empty(), "Ungroup frees them");
	check(sb->undo() && sb->document().objects[1].group == group, "undone");
	check(sb->undo() && sb->document().objects[1].group.empty(), "and the grouping itself is one undo step");

	// The Array window takes the other picked objects along as the unit; the look of the main one goes to the rest.
	sb->pickObjects({2, 1});
	ArrayDialog dialog(2, sb->document().objects, sb, {1});
	check(dialog.unit().size() == 2 && dialog.unit()[1].name == sb->document().objects[1].name, "the Array window copies the picked objects together");
	sb->useLookOfMainObject();
	check(sb->document().objects[1].material.kind == sb->document().objects[2].material.kind && sb->document().objects[1].material.kind == scene_doc::MaterialKind::Conductor, "the main object's look is given to the other picked ones");
	check(sb->undo() && sb->document().objects[1].material.kind == scene_doc::MaterialKind::Dielectric, "undone");

	// A prop is a group: clicking its crown picks the trunk too.
	sb->addProp(scene_doc::PropKind::Tree);
	const int treeParts = static_cast<int>(sb->document().objects.size() - n0);
	check(static_cast<int>(sb->pickedItems().objects.size()) == treeParts && !sb->document().objects[n0].group.empty(), "a tree prop arrives as a group, all of it picked (" + QString::number(treeParts) + " parts)");
	sb->moveSelectedBy({-4.0, 0.0, 3.0});   // clear of the spotlight an earlier step left at the middle (a light is hit before an object)
	check(std::abs(sb->document().objects[n0].position.x - sb->document().objects[n0 + 1].position.x) < 1e-9, "the tree moves as one");
	sb->selectObject(0);
	sb->clickItemForTest(Sel{Sel::Kind::Object, static_cast<int>(n0)});
	{
		QStringList names;
		for (int i : sb->pickedItems().objects) names << QString::fromStdString(sb->document().objects[static_cast<size_t>(i)].name);
		check(static_cast<int>(sb->pickedItems().objects.size()) == treeParts, "clicking one part of the tree picks all of it (picked: " + names.join(", ") + ")");
	}
	check(sb->undo() && sb->undo() && sb->document().objects.size() == n0, "the tree and its move are two undo steps");

	sb->selectAll();
	size_t movable = 0;
	for (const scene_doc::Light &l : sb->document().lights) movable += l.kind != scene_doc::LightKind::Infinite ? 1 : 0;
	check(sb->pickedItems().objects.size() == n0 && sb->pickedItems().lights.size() == movable, "Select all picks every object and light but the sky");
	// RT_GUI_SELFTEST_MULTI_PNG=<file> saves a picture of the tab with three objects picked.
	const QString picture = qEnvironmentVariable("RT_GUI_SELFTEST_MULTI_PNG");
	if (!picture.isEmpty()) {
		sb->pickObjects({1, 2, 3});
		QApplication::processEvents();
		sb->grab().save(picture);
		sb->show3dView(true);
		sb->setGizmoToolForTest(1);   // Rotate: the rings are drawn at the middle of the three
		QApplication::processEvents();
		sb->grab().save(picture + ".3d_rotate.png");
		sb->setGizmoToolForTest(2);
		QApplication::processEvents();
		sb->grab().save(picture + ".3d_scale.png");
		sb->setGizmoToolForTest(0);
		sb->show3dView(false);
	}
	sb->selectObject(1);
}

// Saved camera views (scene_camera_views.h, scene_builder_camera_views.cpp): look through the camera, set it from the 3D view, save, use, update, rename and delete a
// view, each one undo step. Leaves the document as it found it.
static void selfTestCameraViews(SceneBuilderWidget *sb, const std::function<void(bool, const QString &)> &check) {
	const auto near3 = [](const scene_doc::Float3 &a, const scene_doc::Float3 &b) { return std::abs(a.x - b.x) < 1e-6 && std::abs(a.y - b.y) < 1e-6 && std::abs(a.z - b.z) < 1e-6; };
	const scene_doc::Camera camera0 = sb->document().camera;
	const size_t views0 = sb->document().cameraViews.size();
	sb->show3dView(true);
	sb->lookThroughCamera(true);
	check(sb->lookingThroughCameraForTest() && near3(sb->viewEyeForTest(), camera0.position), "Through camera puts the 3D view's eye at the scene's camera");
	sb->orbit3dForTest(120.0, 10.0);
	check(!sb->lookingThroughCameraForTest(), "orbiting the view leaves the camera's view");
	check(!near3(sb->viewEyeForTest(), camera0.position), "and the view is somewhere else now");

	const scene_doc::Float3 eye = sb->viewEyeForTest();
	sb->cameraFromView();
	check(near3(sb->document().camera.position, eye) && sb->document().camera.fov == camera0.fov, "Camera from view puts the camera where the view is, keeping its lens");
	sb->lookThroughCamera(true);
	check(near3(sb->viewEyeForTest(), eye), "looking through the camera now shows that place");
	check(sb->undo() && near3(sb->document().camera.position, camera0.position), "one undo puts the camera back");

	// Save, change the camera, use the view.
	sb->cameraFromView();   // (back to the orbit's place first: through the camera, this does nothing)
	sb->lookThroughCamera(false);
	sb->orbit3dForTest(40.0, 30.0);
	const scene_doc::Float3 farEye = sb->viewEyeForTest();
	const int saved = sb->saveCameraViewNamed("Bird's eye");
	check(saved == static_cast<int>(views0) && sb->document().cameraViews.size() == views0 + 1 && near3(sb->document().cameraViews.back().camera.position, farEye), "Save view remembers where the 3D view is");
	check(sb->document().cameraViews.back().name == "Bird's eye" && sb->viewComboCountForTest() == static_cast<int>(views0) + 2, "under its name, which is in the list of saved views");
	check(sb->saveCameraViewNamed("Bird's eye") >= 0 && sb->document().cameraViews.back().name == "Bird's eye 2", "a second view of the same name is numbered");
	sb->deleteCameraView(static_cast<int>(views0) + 1);
	check(sb->document().cameraViews.size() == views0 + 1, "a view can be deleted");
	check(sb->undo() && sb->document().cameraViews.size() == views0 + 2 && sb->undo() && sb->document().cameraViews.size() == views0 + 1, "and each is one undo step");

	sb->useCameraView(static_cast<int>(views0));
	check(near3(sb->document().camera.position, farEye) && sb->lookingThroughCameraForTest(), "Use puts the camera at the saved view and looks through it");
	sb->renameCameraView(static_cast<int>(views0), "Roof");
	check(sb->document().cameraViews[views0].name == "Roof", "a view can be renamed");
	sb->updateCameraViewFromNow(static_cast<int>(views0), true);
	check(near3(sb->document().cameraViews[views0].camera.position, farEye), "Update saves the camera over it");
	// The flythrough's path in the 3D view: drawn once there are two saved views (the orange curve), and the Camera path switch hides it.
	sb->lookThroughCamera(false);
	sb->orbit3dForTest(200.0, 35.0);
	check(sb->saveCameraViewNamed("Second") >= 0 && sb->document().cameraViews.size() >= 2, "a second saved view, from somewhere else");
	sb->frame3dForTest();   // the whole scene in view, so the curve between the camera and the view saved at the orbit's eye is on screen
	const int shown = sb->cameraPathPixelsForTest(true), hidden = sb->cameraPathPixelsForTest(false);
	check(shown > hidden + 30, "with two saved views the 3D view draws the flythrough path, and the Camera path switch hides it (orange pixels " + QString::number(shown) + " against " + QString::number(hidden) + ")");
	// Dragging the path's markers edits the saved view: its eye on the floor plane (snapped to the grid), up and down with Shift, its target once the view is the active one.
	{
		const size_t second = sb->document().cameraViews.size() - 1;
		const scene_doc::Camera before = sb->document().cameraViews[second].camera;
		const BuilderSelection selected = sb->selectionForTest();
		check(sb->dragViewMarker3dForTest(static_cast<int>(second), 0, QPointF(45, 12)), "dragging a saved view's marker moves the view");
		const scene_doc::Camera after = sb->document().cameraViews[second].camera;
		check(after.position.y == before.position.y && (after.position.x != before.position.x || after.position.z != before.position.z), "along the floor plane (its height stays)");
		check(std::fabs(after.position.x * 4 - std::round(after.position.x * 4)) < 1e-9 && std::fabs(after.position.z * 4 - std::round(after.position.z * 4)) < 1e-9, "on the 0.25 grid");
		check(near3(after.target, before.target) && after.fov == before.fov, "what it looks at and its lens stay");
		check(sb->selectionForTest() == selected, "and the selection did not change");
		check(sb->undo() && near3(sb->document().cameraViews[second].camera.position, before.position), "one undo puts the view back");
		check(sb->dragViewMarker3dForTest(static_cast<int>(second), 0, QPointF(0, -30), Qt::ShiftModifier) && sb->document().cameraViews[second].camera.position.y > before.position.y, "Shift lifts it");
		check(sb->undo(), "undone");
		check(sb->dragViewMarker3dForTest(static_cast<int>(second), 1, QPointF(-30, 10)), "after pressing a view, its target can be dragged too");
		const scene_doc::Camera looked = sb->document().cameraViews[second].camera;
		check(near3(looked.position, before.position) && !near3(looked.target, before.target), "the target moves and the eye stays");
		check(sb->undo() && near3(sb->document().cameraViews[second].camera.target, before.target), "one undo");
	}
	// The Flythrough slider: the 3D view looks from a point along the path, the first view at 0 and the last at 1, and orbiting leaves it.
	{
		const auto &views = sb->document().cameraViews;
		const scene_doc::Camera camera0 = sb->document().camera;
		check(sb->scrubForTest(0.0) && sb->lookingThroughCameraForTest() && near3(sb->viewEyeForTest(), views.front().camera.position), "the Flythrough slider at the start looks from the first saved view");
		check(sb->scrubForTest(1.0) && near3(sb->viewEyeForTest(), views.back().camera.position), "and at the end from the last");
		check(sb->scrubForTest(0.5), "somewhere in the middle");
		const scene_doc::Float3 mid = sb->viewEyeForTest();
		check(!near3(mid, views.front().camera.position) && !near3(mid, views.back().camera.position), "it is on the way, neither at the first view nor at the last");
		check(near3(sb->document().camera.position, camera0.position) && near3(sb->document().camera.target, camera0.target), "and the scene's own camera is not touched");
		sb->orbit3dForTest(120.0, 10.0);
		check(!sb->lookingThroughCameraForTest(), "orbiting the view leaves the preview");
		check(sb->scrubForTest(0.25) && sb->lookingThroughCameraForTest(), "and the slider starts it again");
		// Play: the preview runs by itself (10 s for the whole path), pauses, stops at the last view, and starts over from there.
		check(sb->scrubForTest(0.0), "back at the start");
		sb->playForTest(true);
		check(sb->playingForTest(), "Play starts the preview running");
		{   // the timer itself: about half a second of real time is about 5% of the 10 s path
			QElapsedTimer wait;
			wait.start();
			while (wait.elapsed() < 500) QApplication::processEvents();
			const double ran = sb->playProgressForTest();
			check(ran > 0.02 && ran < 0.15, QString("the timer moves it on in real time (%1 of the path in half a second)").arg(ran, 0, 'f', 3));
			sb->playForTest(false);
			check(sb->scrubForTest(0.0), "back at the start");
			sb->playForTest(true);
		}
		sb->advancePlayForTest(5.0);
		const scene_doc::Float3 half = sb->viewEyeForTest();
		check(std::fabs(sb->playProgressForTest() - 0.5) < 1e-9 && !near3(half, views.front().camera.position) && !near3(half, views.back().camera.position), "5 s later it is half way along");
		sb->playForTest(false);
		check(!sb->playingForTest() && near3(sb->viewEyeForTest(), half), "Pause holds it where it is");
		sb->playForTest(true);
		sb->advancePlayForTest(6.0);
		check(!sb->playingForTest() && std::fabs(sb->playProgressForTest() - 1.0) < 1e-9 && near3(sb->viewEyeForTest(), views.back().camera.position), "it stops at the last view");
		sb->playForTest(true);
		check(sb->playingForTest() && near3(sb->viewEyeForTest(), views.front().camera.position), "and Play again starts from the first");
		sb->orbit3dForTest(120.0, 10.0);
		check(!sb->playingForTest(), "orbiting the view stops it");
		check(sb->scrubForTest(0.25), "the slider starts the preview again");
		sb->cameraFromView();
		check(!near3(sb->document().camera.position, camera0.position), "Camera from view takes the point being looked from");
		check(sb->undo() && near3(sb->document().camera.position, camera0.position), "one undo");
		sb->orbit3dForTest(40.0, 30.0);
	}
	sb->undo();
	// RT_GUI_SELFTEST_CAMERA_PNG=<file> saves a picture of the tab looking through the camera, with the camera's properties (and its saved view) beside it.
	const QString picture = qEnvironmentVariable("RT_GUI_SELFTEST_CAMERA_PNG");
	if (!picture.isEmpty()) {
		sb->selectCameraForTest();
		sb->lookThroughCamera(true);
		QApplication::processEvents();
		sb->grab().save(picture);
	}
	// Back to how it was: three more undo steps (update changed nothing, so it took none of the log, but the edits above are still steps).
	while (sb->document().cameraViews.size() > views0 && sb->undo()) {}
	while (!near3(sb->document().camera.position, camera0.position) && sb->undo()) {}
	check(sb->document().cameraViews.size() == views0 && near3(sb->document().camera.position, camera0.position), "everything undoes back to the scene as it was");
	sb->lookThroughCamera(false);
	sb->show3dView(false);
}

// Arrange (scene_arrange.h, scene_builder_arrange.cpp): hide, lock, drop to the floor, frame and rename. Leaves the document as it found it.
static void selfTestArrange(SceneBuilderWidget *sb, const std::function<void(bool, const QString &)> &check) {
	using Sel = BuilderSelection;
	const auto pos = [sb](int i) { return sb->document().objects[static_cast<size_t>(i)].position; };
	// Lock: the arrow keys leave a locked object alone, and a mouse press where it is picks what is behind it; Unlock gives it back.
	sb->selectObject(3);
	sb->lockSelected(true);
	check(sb->document().objects[3].locked && sb->listTextForTest(3 + 1).contains(SceneBuilderWidget::tr(", locked")), "Lock marks the object in the document and in the list");
	const double x0 = pos(3).x;
	sb->sendKeyForTest(false, Qt::Key_Right);
	check(pos(3).x == x0, "the arrow keys leave a locked object where it is");
	sb->clickItemForTest(Sel{Sel::Kind::Object, 3});
	check(!(sb->selectionForTest() == Sel{Sel::Kind::Object, 3}), "a click where a locked object is does not pick it (it picks what is behind it)");
	sb->selectObject(3);
	sb->lockSelected(false);
	sb->sendKeyForTest(false, Qt::Key_Right);
	check(pos(3).x > x0 && !sb->document().objects[3].locked, "Unlock gives the arrow keys back");
	check(sb->undo() && sb->undo() && sb->undo(), "each is one undo step");
	// Hide: not drawn or picked in the views, shown dimmed in the list, still in the scene; Show everything brings it back.
	sb->selectObject(2);
	sb->hideSelected(true);
	check(sb->document().objects[2].hidden && sb->listTextForTest(2 + 1).contains(SceneBuilderWidget::tr(", hidden")), "Hide marks the object in the document and in the list");
	sb->clickItemForTest(Sel{Sel::Kind::Object, 2});
	check(!(sb->selectionForTest() == Sel{Sel::Kind::Object, 2}), "a hidden object is not picked by a click in a view");
	check(scene_doc::toPbrt(sb->document()).find("Gold ball") != std::string::npos, "but it is still in the scene file, so it still renders");
	sb->showAllHidden();
	check(!sb->document().objects[2].hidden, "Show everything hidden brings it back");
	check(sb->undo() && sb->undo(), "undone");
	// Drop to floor: lift the gold ball, then put it back on the floor (its lowest point on y = 0).
	sb->selectObject(2);
	const double y0 = pos(2).y;
	for (int n = 0; n < 8; ++n) sb->sendKeyForTest(false, Qt::Key_PageUp);
	check(pos(2).y > y0 + 1.0, "lifted");
	sb->dropSelectedToFloor();
	check(std::abs(pos(2).y - y0) < 1e-9, "Drop to floor puts the ball back on the floor (its radius above y = 0)");
	check(sb->undo() && sb->undo(), "undone");
	// Frame selected: the 3D view looks at it from near.
	sb->show3dView(true);
	sb->frame3dForTest();
	const scene_doc::Float3 eye0 = sb->viewEyeForTest();
	sb->selectObject(2);
	sb->frameSelected();
	const scene_doc::Float3 eye1 = sb->viewEyeForTest();
	check(std::abs(eye1.x - eye0.x) + std::abs(eye1.y - eye0.y) + std::abs(eye1.z - eye0.z) > 0.1, "Frame selected moves the 3D view in on what is picked");
	sb->show3dView(false);
	// Rename: a name already taken is made different.
	sb->selectObject(2);
	const std::string was = sb->document().objects[2].name;
	sb->renameItem(Sel{Sel::Kind::Object, 2}, QString::fromStdString(sb->document().objects[1].name));
	check(sb->document().objects[2].name != sb->document().objects[1].name && sb->document().objects[2].name.find(sb->document().objects[1].name) == 0, "Rename to a name that is taken gives it a number");
	check(sb->undo() && sb->document().objects[2].name == was, "one undo");
	sb->selectObject(1);
}

// The 3D view's display modes (scene_view_math.h): standard views and the numpad, orthographic, wireframe. Leaves the view as it found it.
static void selfTestViewModes(SceneBuilderWidget *sb, const std::function<void(bool, const QString &)> &check) {
	using scene_view::StandardView;
	sb->show3dView(true);
	sb->frame3dForTest();
	Scene3DView *v = sb->view3dForTest();
	v->setStandardView(StandardView::Top);
	check(v->viewName() == SceneBuilderWidget::tr("Top") && v->currentPose().eye().y > v->currentPose().target.y + 1.0 && std::abs(v->currentPose().eye().x - v->currentPose().target.x) < 0.01, "View > Top looks straight down at the target");
	sb->sendKeyForTest(true, Qt::Key_1, Qt::KeypadModifier);
	check(v->viewName() == SceneBuilderWidget::tr("Front") && std::abs(v->currentPose().eye().y - v->currentPose().target.y) < 0.01 && v->currentPose().eye().z > v->currentPose().target.z, "numpad 1 is the front view");
	sb->sendKeyForTest(true, Qt::Key_3, Qt::KeypadModifier | Qt::ControlModifier);
	check(v->viewName() == SceneBuilderWidget::tr("Left") && v->currentPose().eye().x < v->currentPose().target.x, "Ctrl+numpad 3 is the left view");
	sb->orbit3dForTest(40.0, 25.0);
	check(v->viewName().isEmpty(), "turning the view by hand leaves the standard view");
	check(!v->orthographic(), "the view starts in perspective");
	sb->sendKeyForTest(true, Qt::Key_5, Qt::KeypadModifier);
	check(v->orthographic(), "numpad 5 makes it orthographic");
	// In orthographic a drag in the 3D view still moves things where the mouse goes.
	check(sb->dragObject3dForTest(1, QPointF(60, 20)), "dragging an object in an orthographic view moves it");
	check(sb->undo(), "undone");
	check(sb->wireframeDifferenceForTest() > 200, "Wireframe changes the picture (only edges are drawn)");
	sb->sendKeyForTest(true, Qt::Key_5, Qt::KeypadModifier);
	check(!v->orthographic(), "numpad 5 again is perspective");
	// RT_GUI_SELFTEST_VIEWS_PNG=<prefix> saves pictures of the modes: <prefix>_persp.png, <prefix>_ortho_front.png, <prefix>_wire_top.png.
	const QString prefix = qEnvironmentVariable("RT_GUI_SELFTEST_VIEWS_PNG");
	if (!prefix.isEmpty()) {
		sb->orbit3dForTest(30.0, 25.0);
		sb->show3dView(true);
		QApplication::processEvents();
		v->grab().save(prefix + "_persp.png");
		v->setOrthographic(true);
		v->setStandardView(StandardView::Front);
		v->grab().save(prefix + "_ortho_front.png");
		v->setWireframe(true);
		v->setStandardView(StandardView::Top);
		v->grab().save(prefix + "_wire_top.png");
		v->setOrthographic(false);
	}
	v->setWireframe(false);
	sb->show3dView(false);
}

// Search actions (F3, scene_builder_actions.cpp, action_search.h): the list has what can be done, and the best match for some words runs. Leaves the document as it found it.
static void selfTestActionSearch(SceneBuilderWidget *sb, const std::function<void(bool, const QString &)> &check) {
	const QStringList names = sb->actionNamesForTest();
	check(names.size() > 60 && names.contains(QStringLiteral("Add: Fog")) && names.contains(QStringLiteral("View: Wireframe")),
	      QString("the action list has %1 actions, among them Add: Fog and View: Wireframe").arg(names.size()));
	check(!sb->runActionForTest("zzzz nothing like this"), "words that fit nothing run nothing");
	const size_t before = sb->document().objects.size();
	check(sb->runActionForTest("add fog") && sb->document().objects.size() == before + 1 && sb->document().objects.back().volume.kind == scene_doc::VolumeKind::Fog, "\"add fog\" adds a fog volume");
	check(sb->undo() && sb->document().objects.size() == before, "one undo");
	sb->show3dView(true);
	const bool wire = sb->view3dForTest()->wireframe();
	check(sb->runActionForTest("wireframe") && sb->view3dForTest()->wireframe() != wire, "\"wireframe\" toggles the wireframe view");
	check(sb->runActionForTest("view wireframe") && sb->view3dForTest()->wireframe() == wire, "and again");
	check(sb->runActionForTest("top view") && sb->view3dForTest()->viewName() == SceneBuilderWidget::tr("Top"), "\"top view\" puts the 3D view on top (any word order)");
	sb->orbit3dForTest(30.0, 25.0);
	sb->selectObject(2);
	const double y0 = sb->document().objects[2].position.y;
	sb->sendKeyForTest(false, Qt::Key_PageUp);
	check(sb->runActionForTest("drop floor") && std::abs(sb->document().objects[2].position.y - y0) < 1e-9, "\"drop floor\" puts the selected object on the floor");
	check(sb->undo() && sb->undo(), "undone");
	sb->show3dView(false);
	sb->selectObject(1);
}

// The camera lens rows (scene_camera_lens.h): focal length, lens presets, sensor, aperture and Pick focus. Leaves the document as it found it.
static void selfTestCameraLens(SceneBuilderWidget *sb, const std::function<void(bool, const QString &)> &check) {
	using scene_doc::aspectOf;
	const scene_doc::Camera camera0 = sb->document().camera;
	sb->selectCameraForTest();
	QApplication::processEvents();
	const auto find = [sb](QDoubleSpinBox *&focal, QDoubleSpinBox *&aperture, QDoubleSpinBox *&fovBox, QComboBox *&lens, QComboBox *&sensor) {
		for (QDoubleSpinBox *s : sb->findChildren<QDoubleSpinBox *>()) {
			if (s->maximum() == 2000.0 && s->suffix().contains("mm")) focal = s;
			if (s->maximum() == 64.0 && !s->specialValueText().isEmpty()) aperture = s;
			if (s->maximum() == 170.0 && s->minimum() == 1.0) fovBox = s;
		}
		for (QComboBox *c : sb->findChildren<QComboBox *>()) {
			if (c->count() == 9 && c->findData(24.0) >= 0 && c->itemData(0).toDouble() < 0) lens = c;   // Choose a lens... and 8 presets
			if (c->count() == static_cast<int>(scene_doc::sensorPresets().size()) + 1 && c->findData(36.0) >= 0) sensor = c;
		}
	};
	QDoubleSpinBox *focal = nullptr, *aperture = nullptr, *fovBox = nullptr;
	QComboBox *lens = nullptr, *sensor = nullptr;
	find(focal, aperture, fovBox, lens, sensor);
	check(focal && aperture && fovBox && lens && sensor, "the camera properties have Focal length, Lens, Sensor and Aperture rows");
	if (!(focal && aperture && fovBox && lens && sensor)) return;
	const double aspect = aspectOf(sb->document().render), sensorW = sb->document().camera.sensorWidthMm;
	check(std::abs(focal->value() - scene_doc::focalLengthMm(camera0.fov, aspect, sensorW)) < 0.06, QString("the focal length shown (%1 mm) is the one the field of view means").arg(focal->value()));
	focal->setValue(85.0);
	QApplication::processEvents();
	check(std::abs(sb->document().camera.fov - scene_doc::fovFromFocalLength(85.0, aspect, sensorW)) < 1e-9, "typing a focal length of 85 mm sets the field of view");
	check(std::abs(fovBox->value() - sb->document().camera.fov) < 0.06, "and the Field of view row follows");
	const int wide = lens->findData(24.0);
	lens->setCurrentIndex(wide);
	emit lens->activated(wide);
	QApplication::processEvents();
	check(std::abs(sb->document().camera.fov - scene_doc::fovFromFocalLength(24.0, aspect, sensorW)) < 1e-9, "choosing the 24 mm lens does the same");
	focal = aperture = fovBox = nullptr;
	lens = sensor = nullptr;
	find(focal, aperture, fovBox, lens, sensor);
	const double fovBefore = sb->document().camera.fov;
	const int aps = sensor->findData(23.5);
	sensor->setCurrentIndex(aps);
	emit sensor->activated(aps);
	QApplication::processEvents();
	check(sb->document().camera.sensorWidthMm == 23.5 && sb->document().camera.fov == fovBefore, "choosing a smaller sensor keeps the view and changes the focal length it means");
	check(std::abs(focal->value() - scene_doc::focalLengthMm(fovBefore, aspect, 23.5)) < 0.06 && focal->value() < 24.0, "(24 mm of full frame is about 16 mm on APS-C)");
	check(aperture->value() == 0.0, "no lens radius shows as a pinhole");
	aperture->setValue(2.8);
	QApplication::processEvents();
	const double mm = scene_doc::focalLengthMm(sb->document().camera.fov, aspect, 23.5);
	check(std::abs(sb->document().camera.lensRadius - scene_doc::lensRadiusFromFNumber(mm, 2.8)) < 1e-9 && sb->document().camera.lensRadius > 0.0, "an aperture of f/2.8 sets the lens radius");
	// Pick focus: a click on the gold ball in the 3D view puts the near side of the ball in focus.
	sb->show3dView(true);
	sb->frame3dForTest();
	if (const QString lensPng = qEnvironmentVariable("RT_GUI_SELFTEST_LENS_PNG"); !lensPng.isEmpty()) {   // a picture of the focus plane (the camera selected, with depth of field)
		sb->view3dForTest()->setStandardView(scene_view::StandardView::Right);
		QApplication::processEvents();
		sb->view3dForTest()->grab().save(lensPng);
		sb->orbit3dForTest(30.0, 25.0);
	}
	sb->lookThroughCamera(true);
	QApplication::processEvents();
	const scene_doc::Object &ball = sb->document().objects[2];
	const double centreDistance = scene_doc::focusDistanceTo(sb->document().camera, ball.position);
	check(sb->clickPickFocusForTest(2), "Pick focus takes the next click in the 3D view");
	const double picked = sb->document().camera.focusDistance;
	check(picked < centreDistance + 0.02 && picked > centreDistance - ball.radius - 0.15,
	      QString("and puts the surface it hit in focus (%1; the middle of the ball is %2 away, its radius %3)").arg(picked, 0, 'f', 2).arg(centreDistance, 0, 'f', 2).arg(ball.radius, 0, 'f', 2));
	sb->lookThroughCamera(false);
	sb->show3dView(false);
	const auto same = [&]() {
		const scene_doc::Camera &c = sb->document().camera;
		return c.fov == camera0.fov && c.lensRadius == camera0.lensRadius && c.focusDistance == camera0.focusDistance && c.sensorWidthMm == camera0.sensorWidthMm;
	};
	while (!same() && sb->undo()) {}
	check(same(), "everything undoes back to the camera as it was");
	sb->selectObject(1);
}

// The rendered viewport (scene_builder_rendered.cpp): the 3D view shows a real progressive render of the scene (the Live Preview GPU renderer), refining, kept up to date as the
// scene is edited, and following the camera. Needs the GPU renderer; skipped (and said so) where it is not there. Leaves the scene as it found it.
static void selfTestRenderedViewport(SceneBuilderWidget *sb, const std::function<void(bool, const QString &)> &check, const std::function<void(const QString &)> &log) {
	if (!SceneBuilderWidget::renderedViewportAvailable()) {
		log("skipped: the rendered viewport needs the Live Preview renderer, which is not available here");
		return;
	}
	const auto wait = [](int ms, const std::function<bool()> &done) {
		QElapsedTimer t;
		t.start();
		while (t.elapsed() < ms && !done()) {
			QApplication::processEvents(QEventLoop::AllEvents, 20);
			QThread::msleep(10);
		}
		return done();
	};
	// How many pixels are clearly red (the red box) in a picture.
	const auto redPixels = [](const QImage &img) {
		int n = 0;
		for (int y = 0; y < img.height(); ++y)
			for (int x = 0; x < img.width(); ++x) {
				const QColor c = img.pixelColor(x, y);
				if (c.red() > c.green() + 60 && c.red() > c.blue() + 60) ++n;
			}
		return n;
	};
	sb->show3dView(true);
	sb->frame3dForTest();
	sb->orbit3dForTest(25.0, 30.0);
	sb->setRenderedViewport(true);
	check(sb->renderedViewport() && sb->view3dForTest()->renderedMode(), "the Rendered switch turns the viewport on");
	const bool frames = wait(90000, [&]() { return sb->renderedSamplesForTest() >= 6; });
	check(frames, QString("it produces frames and refines them (%1 frames, %2 samples)").arg(sb->renderedFramesForTest()).arg(sb->renderedSamplesForTest()));
	if (!frames) {
		sb->setRenderedViewport(false);
		return;
	}
	// The picture follows the view's shape. The pane may still be settling when the first frames arrive (the splitter and the rows round the view lay out a moment after the tab shows), in which case
	// the viewport restarts at the new size: wait for that to finish before looking, rather than catching the one instant between the view changing and the picture following.
	const auto shapeMatches = [&]() {
		const QImage img = sb->renderedImageForTest();
		const double v = double(sb->view3dForTest()->width()) / sb->view3dForTest()->height();
		return !img.isNull() && std::abs(v - double(img.width()) / img.height()) < 0.01 * v && img.width() <= 800;
	};
	wait(15000, shapeMatches);
	const QImage first = sb->renderedImageForTest();
	const double viewAspect = double(sb->view3dForTest()->width()) / sb->view3dForTest()->height(), pictureAspect = first.isNull() ? 0.0 : double(first.width()) / first.height();
	check(!first.isNull() && std::abs(viewAspect - pictureAspect) < 0.01 * viewAspect && first.width() <= 800,
	      QString("the picture has the view's shape, at a size the renderer can refine quickly (%1 x %2 for a %3 x %4 view)").arg(first.width()).arg(first.height()).arg(sb->view3dForTest()->width()).arg(sb->view3dForTest()->height()));
	QColor lo = QColor(255, 255, 255), hi = QColor(0, 0, 0);
	double mean = 0;
	for (int y = 0; y < first.height(); y += 4)
		for (int x = 0; x < first.width(); x += 4) {
			const int l = qGray(first.pixel(x, y));
			mean += l;
			lo = QColor(std::min(lo.red(), l), 0, 0);
			hi = QColor(std::max(hi.red(), l), 0, 0);
		}
	mean /= (first.height() / 4.0) * (first.width() / 4.0);
	check(mean > 20.0 && hi.red() - lo.red() > 60, QString("and it is a picture of the lit scene (mean grey %1, from %2 to %3)").arg(mean, 0, 'f', 1).arg(lo.red()).arg(hi.red()));
	sb->view3dForTest()->setOrthographic(true);
	check(!sb->view3dForTest()->orthographic(), "orthographic cannot be switched on over the rendered picture (it is perspective only)");
	const int redBefore = redPixels(first);
	check(redBefore > 40, QString("the red box is in it (%1 red pixels)").arg(redBefore));
	// Edit and see: the box turns green; the picture follows once the edit has paused.
	scene_doc::Material green = sb->document().objects[3].material;
	green.color = {0.1, 0.8, 0.15};
	sb->setMaterialForTest(3, green);
	const bool changed = wait(60000, [&]() { return redPixels(sb->renderedImageForTest()) < redBefore / 3; });
	check(changed, QString("recolouring the red box changes the picture within a few seconds (red pixels %1 -> %2)").arg(redBefore).arg(redPixels(sb->renderedImageForTest())));
	// Dragging the (now green) box along X only moves it: the renderer is told its offset and redraws, without writing the copy and starting again.
	const auto greenCentre = [](const QImage &img) {
		double sx = 0;
		int n = 0;
		for (int y = 0; y < img.height(); ++y)
			for (int x = 0; x < img.width(); ++x) {
				const QColor c = img.pixelColor(x, y);
				if (c.green() > c.red() + 50 && c.green() > c.blue() + 50) { sx += x; ++n; }
			}
		return n > 20 ? sx / n : -1.0;
	};
	if (changed) {
		wait(30000, [&]() { return greenCentre(sb->renderedImageForTest()) >= 0.0; });
		const double centreBefore = greenCentre(sb->renderedImageForTest());
		const int restartsBefore = sb->renderedRestartsForTest();
		sb->setGizmoToolForTest(0);
		const bool dragged = sb->dragAxis3dForTest(3, 0, 130);
		const bool followed = wait(60000, [&]() { return std::abs(greenCentre(sb->renderedImageForTest()) - centreBefore) > 5.0; });
		check(dragged && followed, QString("dragging the box along X moves it in the picture (its centre %1 -> %2)").arg(centreBefore, 0, 'f', 1).arg(greenCentre(sb->renderedImageForTest()), 0, 'f', 1));
		check(sb->renderedRestartsForTest() == restartsBefore, QString("and the renderer was told the move, not started again (%1 restarts during the drag)").arg(sb->renderedRestartsForTest() - restartsBefore));
	}
	// The camera: turning the view keeps it rendering (the picture follows without a restart).
	const int framesBefore = sb->renderedFramesForTest();
	sb->orbit3dForTest(70.0, 25.0);
	const bool moved = wait(30000, [&]() { return sb->renderedFramesForTest() > framesBefore + 3; });
	check(moved, "orbiting the view keeps the picture coming");
	// Copies of a cone are written as instances (scene_instancing.h), which the renderer numbers after every plain object: moving one of them still moves that one, and not the sphere
	// that follows them in the document.
	{
		const auto centreOf = [](const QImage &img, const std::function<bool(const QColor &)> &is) {
			double sx = 0;
			int n = 0;
			for (int y = 0; y < img.height(); ++y)
				for (int x = 0; x < img.width(); ++x)
					if (is(img.pixelColor(x, y))) { sx += x; ++n; }
			return n > 20 ? sx / n : -1.0;
		};
		const auto isGreen = [](const QColor &c) { return c.green() > c.red() + 50 && c.green() > c.blue() + 50; };
		const auto isMagenta = [](const QColor &c) { return c.red() > c.green() + 80 && c.blue() > c.green() + 80; };
		scene_doc::Object pine = scene_doc::makeObject(scene_doc::ShapeKind::Cone, "Pine");
		pine.radius = 0.5;
		pine.height = 1.6;
		pine.position = {-2.4, 0.8, 1.6};
		pine.material.color = {0.1, 0.8, 0.15};
		scene_doc::Object pine2 = pine;
		scene_doc::scaleObject(pine2, 1.25);
		pine2.name = "Pine 2";
		pine2.position = {2.4, 1.0, 1.6};
		scene_doc::Object marker = scene_doc::makeObject(scene_doc::ShapeKind::Sphere, "Marker");
		marker.radius = 0.5;
		marker.position = {0.0, 0.5, 2.4};
		marker.material.color = {0.9, 0.1, 0.9};
		sb->addCopies({pine, pine2});
		sb->addCopies({marker});
		const int pine2Index = static_cast<int>(sb->document().objects.size()) - 2;
		check(sb->pbrtTextForTest().contains("ObjectInstance"), "two equal cones are written as instances of one");
		sb->frame3dForTest();
		const bool seen = wait(90000, [&]() { return centreOf(sb->renderedImageForTest(), isGreen) >= 0.0 && centreOf(sb->renderedImageForTest(), isMagenta) >= 0.0; });
		check(seen, "the cones and the sphere are in the rendered picture");
		if (seen) {
			wait(8000, []() { return false; });   // (let the picture refine a little, and the renderer settle after the restart)
			const double greenBefore = centreOf(sb->renderedImageForTest(), isGreen), markerBefore = centreOf(sb->renderedImageForTest(), isMagenta);
			const int restartsBefore = sb->renderedRestartsForTest();
			sb->setGizmoToolForTest(0);
			const bool dragged = sb->dragAxis3dForTest(pine2Index, 0, 130);
			const bool followed = wait(60000, [&]() { return std::abs(centreOf(sb->renderedImageForTest(), isGreen) - greenBefore) > 3.0; });
			check(dragged && followed, QString("dragging the second cone moves it in the picture (green centre %1 -> %2)").arg(greenBefore, 0, 'f', 1).arg(centreOf(sb->renderedImageForTest(), isGreen), 0, 'f', 1));
			check(std::abs(centreOf(sb->renderedImageForTest(), isMagenta) - markerBefore) < 2.0, "and the sphere did not move");
			check(sb->renderedRestartsForTest() == restartsBefore, QString("and the renderer was told the move, not started again (%1 restarts)").arg(sb->renderedRestartsForTest() - restartsBefore));
		}
		sb->undo();   // (the drag, the marker, the cones)
		sb->undo();
		sb->undo();
	}
	sb->undo();   // (the move, and then the recolour)
	sb->undo();
	sb->setRenderedViewport(false);
	check(!sb->renderedViewport() && !sb->view3dForTest()->renderedMode(), "the Rendered switch turns it off again (the 3D view draws its faces)");
	sb->view3dForTest()->setOrthographic(true);
	check(sb->view3dForTest()->orthographic(), "and orthographic is available again");
	sb->view3dForTest()->setOrthographic(false);
	sb->show3dView(false);
	sb->selectObject(1);
}

// Snap to objects (scene_snap.h): dragging the red box of the starter scene to near the gold ball's middle lands exactly on it when snapping to objects is on, and on the
// 0.25 grid when it is off. Leaves the document as it found it.
static void selfTestSnap(SceneBuilderWidget *sb, const std::function<void(bool, const QString &)> &check) {
	using Sel = BuilderSelection;
	const auto pos = [sb](int i) { return sb->document().objects[static_cast<size_t>(i)].position; };
	const double x0 = pos(3).x;
	const QPointF a = sb->itemScreenPosForTest(Sel{Sel::Kind::Object, 3}), b = sb->itemScreenPosForTest(Sel{Sel::Kind::Object, 2});
	const double pixelsPerUnit = (b.x() - a.x()) / (pos(2).x - pos(3).x);   // the top view's scale
	const QPointF drag(1.3 * pixelsPerUnit, 0.0);                           // to about 0.1 from the gold ball's middle
	sb->setSnapToObjects(false);
	const bool movedOff = sb->dragObjectForTest(3, drag);
	const double offX = pos(3).x;
	check(movedOff && std::abs(offX - 1.5) < 1e-9, "with Snap to objects off a drag lands on the 0.25 grid (x " + QString::number(offX, 'f', 3) + ")");
	check(sb->undo() && pos(3).x == x0, "undone");
	sb->setSnapToObjects(true);
	const bool movedOn = sb->dragObjectForTest(3, drag);
	const double onX = pos(3).x;
	// Lined up: one of the box's sides (it is turned, so its box is wider than it looks) or its middle is exactly on one of the gold ball's.
	const scene_doc::Aabb moved = scene_doc::aabbOf(sb->document().objects[3]), gold = scene_doc::aabbOf(sb->document().objects[2]);
	const double mine[3] = {moved.low(0), moved.centre(0), moved.high(0)}, theirs[3] = {gold.low(0), gold.centre(0), gold.high(0)};
	bool lined = false;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) lined = lined || (std::abs(mine[i] - theirs[j]) < 1e-9 && (i == 1) == (j == 1));
	check(movedOn && lined && std::abs(onX - 1.5) > 1e-6, "with it on the same drag lines up with the gold ball instead of the grid (x " + QString::number(onX, 'f', 3) + ")");
	check(sb->undo() && pos(3).x == x0, "that is one undo step too");
	// RT_GUI_SELFTEST_SNAP_PNG=<prefix> saves pictures of the tab in the middle of such a drag, in the top view and in 3D (<prefix>_top.png, <prefix>_3d.png).
	const QString picture = qEnvironmentVariable("RT_GUI_SELFTEST_SNAP_PNG");
	if (!picture.isEmpty()) {
		sb->show3dView(false);
		sb->holdDragForTest(3, drag, false);
		QApplication::processEvents();
		sb->grab().save(picture + "_top.png");
		sb->releaseDragForTest(false);
		sb->undo();
		sb->show3dView(true);
		sb->holdDragForTest(3, QPointF(60, 0), true);
		QApplication::processEvents();
		sb->grab().save(picture + "_3d.png");
		sb->releaseDragForTest(true);
		sb->undo();
		sb->show3dView(false);
	}
}

// The arrow keys (Page Up and Down lift): they nudge what is picked by 0.25, a whole unit with Shift, 0.05 with Ctrl, in the direction they point in the view; a run of
// presses is one undo step. Leaves the document as it found it.
static void selfTestNudge(SceneBuilderWidget *sb, const std::function<void(bool, const QString &)> &check) {
	using Sel = BuilderSelection;
	const auto pos = [sb](int i) { return sb->document().objects[static_cast<size_t>(i)].position; };
	const auto same = [](double a, double b) { return std::abs(a - b) < 1e-9; };
	sb->show3dView(false);
	sb->selectObject(3);
	const scene_doc::Float3 p0 = pos(3);
	sb->setSnapToObjects(false);
	sb->sendKeyForTest(false, Qt::Key_Right);
	check(same(pos(3).x, p0.x + 0.25) && same(pos(3).z, p0.z), "Right in the top view moves the object 0.25 to the right");
	sb->sendKeyForTest(false, Qt::Key_Up);
	check(same(pos(3).z, p0.z - 0.25), "Up moves it away from you (towards -Z)");
	sb->sendKeyForTest(false, Qt::Key_Down, Qt::ShiftModifier);
	check(same(pos(3).z, p0.z + 0.75), "Shift+Down moves it a whole unit towards you");
	sb->sendKeyForTest(false, Qt::Key_Left, Qt::ControlModifier);
	check(same(pos(3).x, p0.x + 0.2), "Ctrl+Left moves it a fine 0.05");
	sb->sendKeyForTest(false, Qt::Key_PageUp);
	check(same(pos(3).y, p0.y + 0.25), "Page Up lifts it");
	check(sb->undo() && same(pos(3).x, p0.x) && same(pos(3).y, p0.y) && same(pos(3).z, p0.z), "a run of presses is one undo step");
	// Several picked move together; the camera moves with its target.
	sb->pickObjects({2, 3});
	const scene_doc::Float3 gold0 = pos(2);
	sb->sendKeyForTest(false, Qt::Key_Right);
	check(same(pos(2).x, gold0.x + 0.25) && same(pos(3).x, p0.x + 0.25), "with several picked, all of them move");
	check(sb->undo() && same(pos(2).x, gold0.x), "undone");
	sb->selectCameraForTest();
	const scene_doc::Camera cam0 = sb->document().camera;
	sb->sendKeyForTest(false, Qt::Key_Right);
	check(same(sb->document().camera.position.x, cam0.position.x + 0.25) && same(sb->document().camera.target.x, cam0.target.x + 0.25), "the selected camera moves with what it looks at");
	check(sb->undo() && same(sb->document().camera.position.x, cam0.position.x), "undone");
	// In 3D the arrows follow the screen: from +X looking back (yaw 90) Right is -Z and Up is -X.
	sb->selectObject(3);
	sb->show3dView(true);
	sb->orbit3dForTest(90.0, 20.0);
	sb->sendKeyForTest(true, Qt::Key_Right);
	check(same(pos(3).z, p0.z - 0.25) && same(pos(3).x, p0.x), "in 3D, seen from +X, Right moves it along -Z");
	sb->sendKeyForTest(true, Qt::Key_Up);
	check(same(pos(3).x, p0.x - 0.25), "and Up moves it away from you along -X");
	sb->sendKeyForTest(true, Qt::Key_PageDown);
	check(same(pos(3).y, p0.y - 0.25), "Page Down lowers it");
	check(sb->undo() && same(pos(3).x, p0.x) && same(pos(3).y, p0.y) && same(pos(3).z, p0.z), "that is one undo step too");
	sb->setSnapToObjects(true);
	sb->orbit3dForTest(30.0, 25.0);
	sb->show3dView(false);
}

void MainWindow::runBuilderSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot, const QString &outPrefix) {
	resize(qEnvironmentVariableIntValue("RT_GUI_SELFTEST_WIDTH") > 0 ? qEnvironmentVariableIntValue("RT_GUI_SELFTEST_WIDTH") : 1500, qEnvironmentVariableIntValue("RT_GUI_SELFTEST_HEIGHT") > 0 ? qEnvironmentVariableIntValue("RT_GUI_SELFTEST_HEIGHT") : 950);  // (..._WIDTH / ..._HEIGHT: a smaller window)
	if (m_sceneBuilder) m_tabWidget->setCurrentWidget(m_sceneBuilder);  // by widget, so it works in every language
	SceneBuilderWidget *sb = m_sceneBuilder;
	bool ok = sb != nullptr;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	check(sb && sb->document().objects.size() == 5 && sb->document().lights.size() == 1, "starter scene has 5 objects and 1 light");
	const size_t objects0 = sb->document().objects.size();
	sb->addObject(scene_doc::ShapeKind::Cylinder);
	sb->addLight(scene_doc::LightKind::Spot);
	check(sb->document().objects.size() == objects0 + 1 && sb->document().lights.size() == 2, "added a cylinder and a spotlight");
	check(sb->isDirty(), "the scene is marked unsaved");
	check(sb->undo() && sb->document().lights.size() == 1, "undo removes the spotlight");
	check(sb->redo() && sb->document().lights.size() == 2, "redo brings it back");
	// A real mouse drag in the layout view moves the object (snapped to the 0.25 grid) as one undo step.
	const scene_doc::Float3 ball0 = sb->document().objects[1].position;
	sb->setSnapToObjects(false);   // (this step is about the grid; snapping to objects has its own step below)
	check(sb->dragObjectForTest(1, QPointF(80, -40)), "dragging the glass ball in the layout view moves it");
	sb->setSnapToObjects(true);
	const scene_doc::Float3 ball1 = sb->document().objects[1].position;
	check(ball1.x > ball0.x + 0.3 && ball1.z < ball0.z - 0.1 && ball1.y == ball0.y, "it moved right and away from the camera, height unchanged");
	check(std::fabs(ball1.x * 4 - std::round(ball1.x * 4)) < 1e-9, "the new position is on the grid");
	check(sb->undo() && sb->document().objects[1].position.x == ball0.x && sb->document().objects[1].position.z == ball0.z, "one undo puts it back");
	check(sb->redo(), "redo moves it again");
	selfTestShapes(sb, check);
	selfTestArray(sb, check);
	selfTestMultiSelect(sb, check);
	selfTestCameraViews(sb, check);
	selfTestArrange(sb, check);
	selfTestViewModes(sb, check);
	selfTestActionSearch(sb, check);
	selfTestCameraLens(sb, check);
	selfTestRenderedViewport(sb, check, log);
	sb->selectObject(1);
	check(sb->problemsText().isEmpty(), "the scene has no problems or notes");
	const QString pbrt = outPrefix + "_builder.pbrt";
	check(sb->saveFile(pbrt) && !sb->isDirty(), "saved " + pbrt);
	const std::string before = scene_doc::toJson(sb->document());
	QString err;
	sb->newScene();
	check(sb->openFile(pbrt, &err) && scene_doc::toJson(sb->document()) == before, "re-opened the saved file unchanged " + err);
	selfTestSceneList(sb, check);
	selfTestDeleteScenes(sb, check);
	selfTestLog(sb, check);
	selfTestWheelGuard(check);
	selfTestWindowGeometry(check);
	selfTestCrashRecovery(check);
	selfTestRenderQueue(check);
	// The screenshots show the starter scene (the edits above are done), with the gold ball picked.
	sb->newScene();
	selfTestSnap(sb, check);
	selfTestNudge(sb, check);
	sb->selectObject(2);
	QTimer::singleShot(600, this, [this, shot, log, sb, ok, outPrefix]() mutable {
		shot("builder_edit");
		// One preview on the CPU, then (RT_GUI_SELFTEST_GPU=1, set by scripts/gui_selftest.py --live-preview on a machine with a GPU)
		// one through "Use the GPU" - Metal on a Mac - which must also give a lit picture of about the same brightness.
		const bool alsoGpu = qEnvironmentVariableIsSet("RT_GUI_SELFTEST_GPU");
		auto meanGrey = [](const QString &path) {
			const QImage img(path);
			double sum = 0;
			for (int y = 0; y < img.height(); ++y)
				for (int x = 0; x < img.width(); ++x) sum += qGray(img.pixel(x, y));
			return img.isNull() ? 0.0 : sum / (double(img.width()) * img.height());
		};
		log("starting a preview render");
		sb->startPreview([this, shot, log, sb, ok, alsoGpu, meanGrey, outPrefix](bool done, const QString &message) mutable {
			log(QString("preview: %1 - %2").arg(done ? "ok" : "FAIL", message));
			bool good = ok && done;
			double cpuMean = 0.0;
			if (done) {
				cpuMean = meanGrey(sb->previewImagePath());
				log(QString("preview picture, mean grey %1").arg(cpuMean));
				good = good && cpuMean > 10.0 && cpuMean < 245.0;
			}
			const auto conclude = [this, shot, log](bool result) {
				QTimer::singleShot(300, this, [shot, log, result]() {
					shot("builder_preview");
					log(result ? "RESULT: OK" : "RESULT: FAIL");
					QApplication::exit(result ? 0 : 1);
				});
			};
			// The turntable (scene_builder_turntable.cpp) after the previews: a short video of the starter scene on the CPU, which must be a real MP4 (it needs ffmpeg on Windows and
			// Linux; a machine without it skips this step, a Mac has its own encoder).
			const auto finish = [log, sb, outPrefix, conclude](bool result) {
				if (!result) { conclude(false); return; }
#ifndef Q_OS_MAC
				if (QStandardPaths::findExecutable("ffmpeg").isEmpty()) {
					log("ok: the turntable step is skipped: ffmpeg was not found");
					conclude(true);
					return;
				}
#endif
				const QString video = outPrefix + "_turntable.mp4";
				QFile::remove(video);
				log("starting a turntable render (6 frames)");
				sb->startTurntable(6, 12, 96, 2, video, [log, conclude, sb, outPrefix, video](bool turned, const QString &turnMessage) {
					const qint64 size = QFileInfo(video).size();
					log(QString("turntable: %1 - %2 (%3 bytes)").arg(turned ? "ok" : "FAIL", turnMessage).arg(size));
					if (!turned || size <= 1000) { conclude(false); return; }
					// A flythrough through two saved views (the camera, and the 3D view orbited round).
					sb->saveCameraViewNamed("Start", true);
					sb->setCameraViewHold(static_cast<int>(sb->document().cameraViews.size()) - 1, 0.2);   // a pause at the first view
					sb->show3dView(true);
					sb->orbit3dForTest(100.0, 20.0);
					sb->saveCameraViewNamed("Side");
					sb->show3dView(false);
					const QString fly = outPrefix + "_flythrough.mp4";
					QFile::remove(fly);
					log("starting a flythrough render (6 frames)");
					sb->startFlythrough(6, 12, 96, 2, fly, true, false, [log, conclude, fly](bool flew, const QString &flyMessage) {
						const qint64 flySize = QFileInfo(fly).size();
						log(QString("flythrough: %1 - %2 (%3 bytes)").arg(flew ? "ok" : "FAIL", flyMessage).arg(flySize));
						conclude(flew && flySize > 1000);
					});
				});
			};
			if (!alsoGpu || !good) { finish(good); return; }
			sb->setUseGpu(true);
			log("starting a preview render on the GPU");
			sb->startPreview([log, sb, cpuMean, meanGrey, finish](bool gpuDone, const QString &gpuMessage) {
				log(QString("GPU preview: %1 - %2").arg(gpuDone ? "ok" : "FAIL", gpuMessage));
				bool gpuGood = gpuDone;
				if (gpuDone) {
					const double gpuMean = meanGrey(sb->previewImagePath());
					log(QString("GPU preview picture, mean grey %1 (CPU %2)").arg(gpuMean).arg(cpuMean));
					// Different renderers and sample noise, but the same scene: the same order of brightness.
					gpuGood = gpuMean > 10.0 && gpuMean < 245.0 && std::abs(gpuMean - cpuMean) < 40.0;
				}
				sb->setUseGpu(false);
				finish(gpuGood);
			});
		});
	});
	return;
}

	// RT_GUI_SELFTEST=diagnostics: runs the Diagnostics tab's action (the CLI report plus the GUI's Network section) and logs the
	// finished report. RT_ASSET_BASE_URL can point the network check at a local or dead address.
void MainWindow::runDiagnosticsSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	for (int i = 0; i < m_tabWidget->count(); ++i)
		if (m_tabWidget->tabText(i).contains("Diagnostics")) m_tabWidget->setCurrentIndex(i);
	onRunDiagnosticsClicked();
	auto *poll = new QTimer(this);
	auto *waited = new int(0);
	connect(poll, &QTimer::timeout, this, [this, poll, waited, log]() {
		const bool done = !diagnosticsBusy() && m_lastDiagReport.contains("=== Photo helper");
		if (!done && ++*waited < 240) return;
		poll->stop();
		log(m_lastDiagReport);
		const bool sections = m_lastDiagReport.contains("=== Network ===") && m_lastDiagReport.contains("=== Photo helper");
		log(sections ? "RESULT: OK" : "RESULT: FAIL (no Network or Photo helper section)");
		QApplication::exit(sections ? 0 : 1);
	});
	poll->start(500);
	return;
}

	// RT_GUI_SELFTEST=subcategories: the third row of scene tabs. Choosing a car (a Large Scene) shows the Vehicles tab selected and only vehicles in the list;
	// "All" lists every Large Scene; another subcategory drops the car; a category without subcategories hides the row. Exit 0 if every check held.
void MainWindow::runSubcategoriesSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	bool ok = true;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	resize(1100, 900);
	selectSceneById("H17");   // the Sports Car
	QApplication::processEvents();
	check(m_sceneSubcategoryTabs && m_sceneSubcategoryTabs->count() > 2, QString("the subcategory row has tabs (%1)").arg(m_sceneSubcategoryTabs ? m_sceneSubcategoryTabs->count() : -1));
	const QString current = m_sceneSubcategoryTabs ? m_sceneSubcategoryTabs->tabData(m_sceneSubcategoryTabs->currentIndex()).toString() : QString();
	check(current == "Vehicles", "choosing a car selects the Vehicles tab (" + current + ")");
	bool onlyVehicles = m_sceneCombo->count() > 1, hasCar = false;
	for (int i = 0; i < m_sceneCombo->count(); ++i) {
		const QString id = m_sceneCombo->itemData(i).toString();
		onlyVehicles = onlyVehicles && SceneMetadataClient::sceneSubcategory(id) == "Vehicles";
		hasCar = hasCar || id == "H17";
	}
	check(onlyVehicles && hasCar, QString("the list holds %1 vehicles, the car among them").arg(m_sceneCombo->count()));
	shot("subcategories");
	m_sceneSubcategoryTabs->setCurrentIndex(0);   // All
	QApplication::processEvents();
	int largeScenes = 0;
	for (int i = 0, n = SceneMetadataClient::sceneCount(); i < n; ++i) {
		const QString id = SceneMetadataClient::sceneIdAtIndex(i);
		if (SceneMetadataClient::sceneCategory(id) == "Large Scenes" && SceneMetadataClient::sceneRequiresFiles(id)) ++largeScenes;
	}
	check(m_sceneCombo->count() == largeScenes && largeScenes > 30, QString("All lists every Large Scene (%1 of %2)").arg(m_sceneCombo->count()).arg(largeScenes));
	for (int i = 0; i < m_sceneSubcategoryTabs->count(); ++i)
		if (m_sceneSubcategoryTabs->tabData(i).toString() == "Interiors") m_sceneSubcategoryTabs->setCurrentIndex(i);
	QApplication::processEvents();
	bool carGone = true, anyInterior = m_sceneCombo->count() > 5;
	for (int i = 0; i < m_sceneCombo->count(); ++i) {
		carGone = carGone && m_sceneCombo->itemData(i).toString() != "H17";
		anyInterior = anyInterior && SceneMetadataClient::sceneSubcategory(m_sceneCombo->itemData(i).toString()) == "Interiors";
	}
	check(carGone && anyInterior, QString("Interiors lists %1 rooms and not the car").arg(m_sceneCombo->count()));
	selectSceneById("A1");   // a category with no subcategories
	QApplication::processEvents();
	check(m_sceneSubcategoryTabs->count() == 0 && !m_sceneSubcategoryTabs->isVisible(), "a category without subcategories shows no subcategory row");
	selectSceneById("H41");   // and back: Veach, Ajar is in Light & Art
	QApplication::processEvents();
	check(m_sceneSubcategoryTabs->tabData(m_sceneSubcategoryTabs->currentIndex()).toString() == "Light & Art", "choosing Veach, Ajar selects Light & Art");
	log(ok ? "RESULT: OK" : "RESULT: FAIL");
	QApplication::exit(ok ? 0 : 1);
}

	// RT_GUI_SELFTEST=scenekeys: the scene keys the GUI saves and looks up by (slugs, with ids still accepted) round-trip through the metadata library, the
	// technique notes are found by either, and the thumbnail cache is keyed by slug. Exit 0 if every check held.
void MainWindow::runSceneKeysSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	bool ok = true;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	const QString slug = SceneMetadataClient::sceneSlug("A1");
	check(slug == "cornell-box", "A1's slug is cornell-box (got \"" + slug + "\")");
	check(SceneMetadataClient::sceneIdForKey("cornell-box") == "A1", "the slug resolves to A1");
	check(SceneMetadataClient::sceneIdForKey("A1") == "A1", "an id resolves to itself");
	check(SceneMetadataClient::sceneIdForKey("no-such-scene").isEmpty(), "an unknown key resolves to nothing");
	check(scene_technique_notes::hasNote("A1") && scene_technique_notes::hasNote("cornell-box"), "the Cornell Box note is found by id and by slug");
	check(scene_technique_notes::forScene("A1") == scene_technique_notes::forScene("cornell-box"), "both give the same note");
	check(thumbnailCachePath("A1").endsWith("/thumbnails/cornell-box.png"), "the thumbnail cache file is named by slug: " + thumbnailCachePath("A1"));
	// every compiled-in scene's slug is unique and resolves back to its own id
	QSet<QString> slugs;
	int count = SceneMetadataClient::sceneCount(), bad = 0;
	for (int i = 0; i < count; ++i) {
		const QString id = SceneMetadataClient::sceneIdAtIndex(i);
		const QString s = SceneMetadataClient::sceneSlug(id);
		if (s == id || slugs.contains(s) || SceneMetadataClient::sceneIdForKey(s) != id) ++bad;
		slugs.insert(s);
	}
	check(bad == 0, QString("%1 scenes: every slug is unique and resolves back to its id (%2 bad)").arg(count).arg(bad));
	// The technique notes (scene_technique_notes.h, translated GUI text, so kept out of the Qt-free registry) and the registry must describe the
	// same scenes: every self-contained built-in scene has a note, and every note belongs to one.
	QSet<QString> expected, missingNote, staleNote;
	for (int i = 0; i < count; ++i) {
		const QString id = SceneMetadataClient::sceneIdAtIndex(i);
		const QString category = SceneMetadataClient::sceneCategory(id);
		if (category == "Custom Scenes" || category == "Test Scenes" || category == "My Scenes" || category == "Cartoons" || SceneMetadataClient::sceneRequiresFiles(id)) continue;
		expected.insert(SceneMetadataClient::sceneSlug(id));
	}
	for (const QString &slug : expected)
		if (!scene_technique_notes::notes().contains(slug)) missingNote.insert(slug);
	for (auto it = scene_technique_notes::notes().constBegin(); it != scene_technique_notes::notes().constEnd(); ++it)
		if (!expected.contains(it.key())) staleNote.insert(it.key());
	check(missingNote.isEmpty(), QString("%1 self-contained scenes, %2 notes: missing a note: %3").arg(expected.size()).arg(scene_technique_notes::notes().size()).arg(QStringList(missingNote.begin(), missingNote.end()).join(", ")));
	check(staleNote.isEmpty(), "notes for no self-contained scene: " + QStringList(staleNote.begin(), staleNote.end()).join(", "));
	log(ok ? "RESULT: OK" : "RESULT: FAIL");
	QApplication::exit(ok ? 0 : 1);
	return;
}

	// RT_GUI_SELFTEST=download RT_GUI_SELFTEST_SCENE=<id>: selects the scene, runs the "Download missing files" action
	// without dialogs (point RT_ASSET_BASE_URL at a local server to avoid the network), and checks the files arrived and
	// the scene no longer reports them missing. Exit 0 on success.
void MainWindow::runDownloadSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	selectSceneById(qEnvironmentVariable("RT_GUI_SELFTEST_SCENE"));
	const QList<asset_downloader::Job> jobs = m_downloadableAssetJobs;
	const scene_packs::Pack *pack = m_downloadablePack;
	if (jobs.isEmpty() && pack) {
		log(QString("scene=%1, pack \"%2\": %3 file(s), %4 MB to download").arg(m_sceneCombo->currentData().toString(), pack->title).arg(pack->fileCount()).arg(pack->downloadBytes() / 1.0e6, 0, 'f', 1));
		QTimer::singleShot(1800000, this, [log]() { log("RESULT: FAIL (timed out)"); QApplication::exit(1); });
		startPackDownload(*pack, false, [this, log](bool ok, const QString &error) {
			const bool stillMissing = SceneMetadataClient::missingAssets(m_sceneCombo->currentData().toString()).any;
			// A scene the registry listed while its file was missing has the camera of a missing file (the origin) until the download is read again: the camera boxes the Live
			// Preview starts from must now be the file's camera.
			const bool cameraRead = m_cameraPosX->value() != 0.0 || m_cameraPosY->value() != 0.0 || m_cameraPosZ->value() != 0.0;
			log(QString("download ok=%1 error=\"%2\" scene-still-reports-missing=%3 camera boxes (%4, %5, %6) read from the file=%7").arg(ok).arg(error).arg(stillMissing)
				.arg(m_cameraPosX->value()).arg(m_cameraPosY->value()).arg(m_cameraPosZ->value()).arg(cameraRead));
			log(ok && !stillMissing && cameraRead ? "RESULT: OK" : "RESULT: FAIL");
			QApplication::exit(ok && !stillMissing && cameraRead ? 0 : 1);
		});
		return;
	}
	log(QString("scene=%1, %2 downloadable missing file(s), button visible: %3")
		.arg(m_sceneCombo->currentData().toString()).arg(jobs.size()).arg(m_downloadAssetsButton && !m_downloadAssetsButton->isHidden()));
	if (jobs.isEmpty()) { log("RESULT: FAIL (nothing to download for this scene)"); QApplication::exit(1); return; }
	QTimer::singleShot(120000, this, [log]() { log("RESULT: FAIL (timed out)"); QApplication::exit(1); });
	startAssetDownload(jobs, false, [this, jobs, log](bool ok, const QString &error) {
		bool allThere = ok;
		for (const auto &j : jobs) allThere = allThere && QFileInfo(j.destination).size() == j.entry.size;
		const bool stillMissing = SceneMetadataClient::missingAssets(m_sceneCombo->currentData().toString()).any;
		log(QString("download ok=%1 error=\"%2\" files-present=%3 scene-still-reports-missing=%4").arg(ok).arg(error).arg(allThere).arg(stillMissing));
		log(allThere && !stillMissing ? "RESULT: OK" : "RESULT: FAIL");
		QApplication::exit(allThere && !stillMissing ? 0 : 1);
	});
	return;
}

void MainWindow::runUiSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	// RT_GUI_SELFTEST_SCENE=<id> selects that scene first, so the scene info (including the
	// missing-files warning) can be checked; its text is logged as well as pictured.
	const QString sceneOverride = qEnvironmentVariable("RT_GUI_SELFTEST_SCENE");
	if (!sceneOverride.isEmpty()) {
		selectSceneById(sceneOverride);
		resize(1100, 900);   // large enough to see the scene group, including the download button
	}
	log(QString("scene info: %1").arg(m_sceneInfoLabel ? m_sceneInfoLabel->text() : QString()));
	// The renderer libraries print their errors to std::cerr. This program's stderr used to be left CLOSED on Windows (the redirect into the log failed after closing
	// it), so the first such line aborted the whole program (0xC0000409). Writing one here makes that regression a crash of the self-test; when the session log says
	// stderr goes into it, the line must have arrived there.
	std::cerr << "[selftest] a line written to stderr" << std::endl;
	const QStringList logTail = AppLog::tail(400);
	const bool redirected = logTail.join('\n').contains(QStringLiteral("stderr of this process"));
	const bool arrived = logTail.join('\n').contains(QStringLiteral("[selftest] a line written to stderr"));
	log(QString("stderr: %1").arg(redirected ? (arrived ? "written to the log" : "FAIL (redirected, but the line is not in the log)") : "left where it was (a terminal)"));
	if (redirected && !arrived) { QApplication::exit(1); return; }
	// The list of recent renders is built at start-up from the renders found in the default folders. Every file found used to get an entry (each asks the scene list about its scene, tens of
	// milliseconds), so a folder that had collected a few hundred renders (the command line writes there too) made the program take half a minute to start. Now only the newest few get one.
	{
		QTemporaryDir folder;
		bool ok = folder.isValid();
		for (int i = 0; ok && i < 400; ++i) {
			QFile f(folder.filePath(QStringLiteral("render_A1_20260101_%1.png").arg(i, 6, 10, QLatin1Char('0'))));
			ok = f.open(QIODevice::WriteOnly);
		}
		qputenv("RAY_TRACER_RECENT_SCAN_DIR", folder.path().toLocal8Bit());
		QElapsedTimer clock;
		clock.start();
		const int listed = ok ? static_cast<int>(loadRecentRenders().size()) : -1;
		const qint64 ms = clock.elapsed();
		qunsetenv("RAY_TRACER_RECENT_SCAN_DIR");
		const bool fast = ok && listed >= 1 && listed <= 20 && ms < 3000;
		log(QString("recent renders: %1 listed from 400 files in %2 ms: %3").arg(listed).arg(ms).arg(fast ? "ok" : "FAIL (too many, or too slow: start-up reads this list)"));
		if (!fast) { QApplication::exit(1); return; }
	}
	QTimer::singleShot(600, this, [shot]() { shot("ui"); QApplication::exit(0); });
	return;
}

// RT_GUI_SELFTEST=cartoonui: a cartoon film has its own camera, so while a film is the scene of a video the Flythrough Preset and Camera Path drop-downs are greyed out (and the film's path
// is the one selected), and any other scene gives them back, without the films' own paths among the choices.
void MainWindow::runCartoonUiSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	resize(1500, 1000);
	bool ok = true;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	QString filmScene, otherScene;
	for (int i = 0, n = SceneMetadataClient::sceneCount(); i < n && (filmScene.isEmpty() || otherScene.isEmpty()); ++i) {
		const QString id = SceneMetadataClient::sceneIdAtIndex(i);
		SceneMetadataClient::SceneMetadata meta;
		if (!SceneMetadataClient::sceneMetadata(id, meta)) continue;
		if (!meta.filmKey.isEmpty()) { if (filmScene.isEmpty()) filmScene = id; }
		else if (otherScene.isEmpty()) otherScene = id;
	}
	check(!filmScene.isEmpty() && !otherScene.isEmpty(), "a film scene (" + filmScene + ") and another scene (" + otherScene + ") are in the list");
	const int video = m_modeCombo->findData(static_cast<int>(OutputMode::Video));
	check(video >= 0, "there is a Generate Video mode");
	if (!ok) { log("RESULT: FAIL"); QApplication::exit(1); return; }
	m_modeCombo->setCurrentIndex(video);
	selectSceneById(otherScene);
	check(m_cameraPathCombo->isEnabled() && m_videoPresetCombo->isEnabled(), "an ordinary scene: the camera path and the flythrough preset can be chosen");
	bool anyCartoon = false;
	for (int i = 0; i < m_cameraPathCombo->count(); ++i) anyCartoon = anyCartoon || m_cameraPathCombo->itemData(i).toString().startsWith(QStringLiteral("cartoon:"));
	check(!anyCartoon, "the films' own paths are not among the choices");
	selectSceneById(filmScene);
	QApplication::processEvents();
	check(!m_cameraPathCombo->isEnabled() && !m_videoPresetCombo->isEnabled(), "a film is the scene: the camera path and the flythrough preset are greyed out");
	check(m_cameraGroupBox && !m_cameraGroupBox->isEnabled(), "and so is the Camera Position (the film has its own camera)");
	check(videoCameraPath().startsWith(QStringLiteral("cartoon:")), "and the video's path is the film's own: " + videoCameraPath());
	check(!m_cameraPathCombo->toolTip().isEmpty() && m_cameraPathCombo->toolTip().contains("cartoon", Qt::CaseInsensitive), "with a tooltip that says why");
	shot("cartoonui_film");
	selectSceneById(otherScene);
	QApplication::processEvents();
	check(m_cameraPathCombo->isEnabled() && m_videoPresetCombo->isEnabled(), "another scene again: both can be chosen");
	check(m_cameraGroupBox && m_cameraGroupBox->isEnabled(), "and the Camera Position is back");
	check(!videoCameraPath().startsWith(QStringLiteral("cartoon:")), "and the video's path is the chosen one again: " + videoCameraPath());
	// A picture of a film: the moment it shows reads as a moment of the whole film ("15.0 s of 60 s"), and the shot box names the shot that moment is in.
	if (const int image = m_modeCombo->findData(static_cast<int>(OutputMode::Image)); image >= 0) m_modeCombo->setCurrentIndex(image);
	const film_info::Film *film = nullptr;   // one with several shots
	for (int i = 0, n = SceneMetadataClient::sceneCount(); i < n && !film; ++i) {
		const QString id = SceneMetadataClient::sceneIdAtIndex(i);
		SceneMetadataClient::SceneMetadata meta;
		if (!SceneMetadataClient::sceneMetadata(id, meta) || meta.filmKey.isEmpty()) continue;
		const film_info::Film *f = film_info::find(meta.filmKey.toUtf8().constData());
		if (f && f->shotStarts.size() >= 3) {
			film = f;
			filmScene = id;
		}
	}
	selectSceneById(filmScene);
	QApplication::processEvents();
	if (film) {
		check(m_filmTimeRow->isVisibleTo(this), "a picture of a film shows the moment row");
		const QString of = QStringLiteral(" of %1 s").arg(film->duration, 0, 'f', 0);
		check(m_filmTimeSpin->text().endsWith(of), "the moment says which length it is a moment of: " + m_filmTimeSpin->text());
		const std::vector<double> starts = film->shotStarts;
		int expected = 0;
		for (std::size_t i = 0; i < starts.size(); ++i)
			if (film->stillTime >= starts[i]) expected = static_cast<int>(i);
		check(m_filmShotCombo->currentIndex() == expected, QString("the shot box names the still's shot: \"%1\"").arg(m_filmShotCombo->currentText()));
		m_filmTimeSpin->setValue(starts.back() + 0.5);
		check(m_filmShotCombo->currentIndex() == static_cast<int>(starts.size()) - 1, "a time in the last shot names the last shot: " + m_filmShotCombo->currentText());
		m_filmShotCombo->setCurrentIndex(0);
		emit m_filmShotCombo->activated(0);
		check(m_filmTimeSpin->value() < (starts.size() > 1 ? starts[1] : film->duration), "choosing the first shot moves the moment into it");
		shot("cartoonui_film_picture");
	} else
		check(false, "a film with several shots is in the list");
	log(ok ? "RESULT: OK" : "RESULT: FAIL");
	QApplication::exit(ok ? 0 : 1);
}

// RT_GUI_SELFTEST=framesstrip: the strip of finished frames under the progress bar (frames_strip.h) lists a frames folder as it fills: new pictures only, in frame order whatever order they
// arrive in, nothing that is still being written, only the latest ones when there are too many, and nothing on the Progress tab before a video renders.
void MainWindow::runFramesStripSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	bool ok = true;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	check(m_framesStrip && m_framesStrip->isHidden() && m_framesStripLabel && m_framesStripLabel->isHidden(), "the strip is not shown before a video renders");
	QTemporaryDir dir;
	check(dir.isValid(), "a temporary frames folder");
	auto picture = [&](const QString &name, int shade) {
		QImage image(160, 90, QImage::Format_RGB32);
		image.fill(QColor(shade, 255 - shade, 128));
		image.save(dir.filePath(name));
	};
	FramesStrip strip;
	strip.resize(900, 140);
	strip.start(dir.path(), 5);
	check(strip.refresh() == 0 && strip.count() == 0, "an empty folder lists nothing");
	for (int i = 0; i < 3; ++i) picture(QString("enc_%1.png").arg(i, 4, 10, QChar('0')), 40 * i);
	check(strip.refresh() == 3 && strip.count() == 3 && strip.pictures() == 3, "three finished frames are listed");
	check(strip.item(0)->text() == "1" && strip.item(2)->text() == "3", "numbered from 1, in order");
	check(strip.refresh() == 0, "a second look finds nothing new, and loads nothing twice");
	picture("enc_0003.png", 200);
	picture("enc_0004.png.tmp.png", 10);   // a picture still being written
	QFile(dir.filePath("notes.txt")).open(QIODevice::WriteOnly);
	check(strip.refresh() == 1 && strip.count() == 4, "only the finished frame is added, not the one being written or a stray file");
	picture("enc_0009.png", 90);
	picture("enc_0006.png", 60);
	check(strip.refresh() == 2 && strip.item(strip.count() - 2)->data(Qt::UserRole).toInt() == 6 && strip.item(strip.count() - 1)->data(Qt::UserRole).toInt() == 9, "frames that arrive out of order are put in their place");
	check(strip.count() == 5 && strip.item(0)->data(Qt::UserRole).toInt() == 1, "only the latest five are kept (the strip's limit): the oldest went");
	check(strip.pictures() == 6, "the whole count is still the folder's: 6");
	// many at once (a resumed video): at most `maxNew` per look, and only the newest ones shown
	QTemporaryDir many;
	for (int i = 0; i < 40; ++i) {
		QImage image(32, 18, QImage::Format_RGB32);
		image.fill(Qt::gray);
		image.save(many.filePath(QString("enc_%1.png").arg(i, 4, 10, QChar('0'))));
	}
	FramesStrip big;
	big.start(many.path(), 10);
	check(big.refresh(4) == 4 && big.count() == 4, "a look loads at most as many as it is told to");
	while (big.refresh(4) > 0) {}
	check(big.count() == 10 && big.item(big.count() - 1)->data(Qt::UserRole).toInt() == 39 && big.item(0)->data(Qt::UserRole).toInt() == 30, "40 frames at once: the latest ten are shown");
	check(big.pictures() == 40, "and all 40 are counted");
	check(FramesStrip::pathOf(big.item(0)).endsWith("enc_0030.png"), "a picture knows its file (a double-click opens it)");
	strip.setWindowTitle("framesstrip");
	strip.show();
	QApplication::processEvents();
	shot("framesstrip");
	log(ok ? "RESULT: OK" : "RESULT: FAIL");
	QApplication::exit(ok ? 0 : 1);
}

// RT_GUI_SELFTEST=videoframes: a real video render (12 frames of a cartoon, tiny, on the CPU) with the Progress tab showing: the frames strip fills as the frames come and holds all twelve when
// the render is over. Takes a picture of the tab part-way through.
void MainWindow::runVideoFramesSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	if (m_progressTabIndex >= 0) m_tabWidget->setCurrentIndex(m_progressTabIndex);
	resize(1100, 800);
	auto fail = [log](const QString &what) {
		log("FAIL: " + what);
		log("RESULT: FAIL");
		QApplication::exit(1);
	};
	for (int i = 0; i < m_renderModeCombo->count(); ++i)
		if (!m_renderModeCombo->itemData(i).toBool()) m_renderModeCombo->setCurrentIndex(i);   // the CPU
	QString filmScene;
	for (int i = 0, n = SceneMetadataClient::sceneCount(); i < n && filmScene.isEmpty(); ++i) {
		const QString id = SceneMetadataClient::sceneIdAtIndex(i);
		SceneMetadataClient::SceneMetadata meta;
		if (SceneMetadataClient::sceneMetadata(id, meta) && meta.filmKey == "ball-parade") filmScene = id;
	}
	if (filmScene.isEmpty()) { fail("no scene for the ball-parade film"); return; }
	selectOutputMode(OutputMode::Video);
	selectSceneById(filmScene);
	m_videoFramesSpinBox->setValue(12);
	m_qualityPresetCombo->setCurrentIndex(6);   // Custom: the size below
	m_widthSpinBox->setValue(96);
	m_heightSpinBox->setValue(54);
	m_samplesSpinBox->setValue(2);
	m_maxDepthSpinBox->setValue(2);
	onRenderClicked();
	log("started a 12-frame video render of " + filmScene);
	auto *poll = new QTimer(this);
	auto *waited = new int(0);
	auto *shotTaken = new bool(false);
	connect(poll, &QTimer::timeout, this, [this, poll, waited, shotTaken, log, shot, fail]() {
		if (++*waited > 600) { poll->stop(); fail("the video did not finish in 120 s"); return; }
		if (!*shotTaken && m_framesStrip->count() >= 3 && m_isRendering) {
			*shotTaken = true;
			log(QString("part-way: %1 frames in the strip, shown=%2").arg(m_framesStrip->count()).arg(m_framesStrip->isVisible()));
			shot("videoframes_midway");
		}
		if (m_isRendering || m_queueModel->finishedCount() < 1) return;
		poll->stop();
		bool ok = true;
		auto check = [&ok, log](bool cond, const QString &what) { log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what)); ok = ok && cond; };
		check(m_framesStrip->isVisible() && m_framesStripLabel->isVisible(), "the strip is shown for a video");
		check(m_framesStrip->pictures() == 12, QString("the folder holds 12 frames (%1)").arg(m_framesStrip->pictures()));
		check(m_framesStrip->count() == 12, QString("and the strip lists all 12 (%1)").arg(m_framesStrip->count()));
		check(m_framesStrip->item(0)->text() == "1" && m_framesStrip->item(11)->text() == "12", "in order");
		check(m_framesStripLabel->text().contains("12"), "the label says how many: " + m_framesStripLabel->text());
		log(*shotTaken ? "a picture was taken while frames were coming in" : "(the video was too quick for a picture part-way)");
		shot("videoframes_done");
		// the Preview tab opens for the finished video a moment later: its sidebar (the render's info, the scene's description and the technique note in one column) is photographed then
		QTimer::singleShot(3000, this, [this, shot, log, ok]() {
			if (m_previewTabIndex >= 0) m_tabWidget->setCurrentIndex(m_previewTabIndex);
			QApplication::processEvents();
			shot("videoframes_preview");
			log(ok ? "RESULT: OK" : "RESULT: FAIL");
			QApplication::exit(ok ? 0 : 1);
		});
	});
	poll->start(200);
}

// RT_GUI_SELFTEST=banner: the "these controls do not apply in this mode" banners (Live Preview, Generate Video) are as tall as their text needs and no taller: a word-wrapped label in a form
// layout used to take the height of several lines for one.
void MainWindow::runBannerSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	resize(1500, 950);
	if (m_tabWidget) m_tabWidget->setCurrentIndex(0);
	bool ok = true;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	auto measure = [&](QLabel *label, const QString &name) {
		if (!label) return;
		label->show();
		QApplication::processEvents();
		const int width = label->width();
		const int needed = label->heightForWidth(width);
		log(QString("%1: width %2, height %3, its text needs %4").arg(name).arg(width).arg(label->height()).arg(needed));
		check(label->height() <= needed + 6, name + " is no taller than its text needs");
	};
#ifdef RT_GUI_HAVE_LIVE_PREVIEW
	if (const int live = m_modeCombo->findData(static_cast<int>(OutputMode::LivePreview)); live >= 0) m_modeCombo->setCurrentIndex(live);
	measure(m_liveModeWarningLabel, "the Live Preview banner");
#endif
	if (const int video = m_modeCombo->findData(static_cast<int>(OutputMode::Image)); video >= 0) m_modeCombo->setCurrentIndex(video);
	measure(m_videoModeWarningLabel, "the Video settings banner");
	shot("banner");
	log(ok ? "RESULT: OK" : "RESULT: FAIL");
	QApplication::exit(ok ? 0 : 1);
}

// RT_GUI_SELFTEST=resolution: the Resolution box has square and non-square presets; a preset fills in Width and Height, a typed size of any shape shows "Custom
// size", and the render takes Width x Height whatever the Quality preset; the Power box (Full power first), the graphics card and the video helper memory limit
// (No limit unless chosen) are in the Render Settings group. A screenshot of the group with a widescreen size chosen.
void MainWindow::runResolutionSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	resize(1500, 950);
	if (m_tabWidget) m_tabWidget->setCurrentIndex(0);
	bool ok = true;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	int wide = 0, tall = 0;
	for (int i = 0; i < m_resolutionCombo->count(); ++i) {
		const QSize s = m_resolutionCombo->itemData(i).toSize();
		if (s.isValid() && s.width() > s.height()) ++wide;
		if (s.isValid() && s.width() < s.height()) ++tall;
	}
	check(wide >= 5 && tall >= 2, QString("the Resolution box offers wide (%1) and tall (%2) sizes, not only squares").arg(wide).arg(tall));
	check(m_resolutionCombo->findData(QSize(426, 240)) >= 0 && m_resolutionCombo->findData(QSize(214, 120)) >= 0, "240p and 120p are presets");
	// The list opens against its box, with a choice half-way down it, whether the box is low on the screen (the list opens upwards) or high (downwards). Fusion's own
	// popup put the chosen item over the box instead, and cut short in height the list hung far above it.
	{
		show();
		m_resolutionCombo->setCurrentIndex(m_resolutionCombo->findData(QSize(214, 120)));
		const QRect screen = m_resolutionCombo->screen()->availableGeometry();
		for (const bool low : {true, false}) {
			QApplication::processEvents();
			const QPoint at = m_resolutionCombo->mapToGlobal(QPoint(0, 0));
			move(pos() + QPoint(0, (low ? screen.bottom() - 60 : screen.top() + 60) - at.y()));
			QApplication::processEvents();
			const QRect box(m_resolutionCombo->mapToGlobal(QPoint(0, 0)), m_resolutionCombo->size());
			m_resolutionCombo->showPopup();
			QApplication::processEvents();
			const QWidget *popup = m_resolutionCombo->view()->window();
			const QRect list = popup->geometry();
			const bool touches = std::abs(list.top() - box.bottom()) <= 6 || std::abs(list.bottom() - box.top()) <= 6;
			check(popup->isVisible() && touches && list.height() <= screen.height(),
				  QString("box %1: the list (%2..%3) opens against the box (%4..%5)").arg(low ? "low" : "high").arg(list.top()).arg(list.bottom()).arg(box.top()).arg(box.bottom()));
			if (low) shot("resolution_popup");
			m_resolutionCombo->hidePopup();
		}
	}
	const int fullHd = m_resolutionCombo->findData(QSize(1920, 1080));
	check(fullHd >= 0, "1920 x 1080 is a preset");
	m_resolutionCombo->setCurrentIndex(fullHd);
	check(m_widthSpinBox->value() == 1920 && m_heightSpinBox->value() == 1080, "choosing it fills in Width 1920 and Height 1080");
	m_qualityPresetCombo->setCurrentIndex(2);   // Good: the size used to come from the box, squares only, under any preset but Custom
	RenderJob job = captureRenderJob();
	check(job.width == 1920 && job.height == 1080, QString("the render is %1 x %2 under the Good quality preset").arg(job.width).arg(job.height));
	m_widthSpinBox->setValue(1000);
	m_heightSpinBox->setValue(700);
	check(!m_resolutionCombo->currentData().toSize().isValid(), "a typed 1000 x 700 shows Custom size: " + m_resolutionCombo->currentText());
	job = captureRenderJob();
	check(job.width == 1000 && job.height == 700, QString("and the render is %1 x %2").arg(job.width).arg(job.height));
	m_widthSpinBox->setValue(1080);
	m_heightSpinBox->setValue(1920);
	check(m_resolutionCombo->currentData().toSize() == QSize(1080, 1920), "typing 1080 x 1920 picks the portrait preset: " + m_resolutionCombo->currentText());
	m_resolutionCombo->setCurrentIndex(fullHd);
	// The renderer is told the height: its own default is a square (the height is the width), so a size that only reached the job used to render square.
	{
		std::unique_ptr<RenderController> controller(makeRenderController(captureRenderJob()));
		const QStringList args = controller->commandArguments();
		const int at = args.indexOf(QStringLiteral("--height"));
		check(at >= 0 && at + 1 < args.size() && args[at + 1] == QStringLiteral("1080") && args.contains(QStringLiteral("1920")),
			  "the command line asks for 1920 x 1080: " + args.join(' '));
	}
	// And a picture really comes out that shape: a small 320 x 180 render, run the way the GUI runs it.
	{
		m_widthSpinBox->setValue(320);
		m_heightSpinBox->setValue(180);
		const int samplesWere = m_samplesSpinBox->value();
		m_samplesSpinBox->setValue(4);
		RenderJob job = captureRenderJob();
		const QString out = QFileInfo(m_outputPathEdit->text()).absolutePath() + QStringLiteral("/selftest_wide_render.png");
		QFile::remove(out);
		job.outputPath = out;
		std::unique_ptr<RenderController> controller(makeRenderController(job));
		QProcess renderer;
		renderer.setWorkingDirectory(QCoreApplication::applicationDirPath());
		renderer.start(QCoreApplication::applicationDirPath() + QStringLiteral("/ray_tracer") + (QSysInfo::productType() == "windows" ? ".exe" : ""), controller->commandArguments());
		const bool ran = renderer.waitForFinished(600000) && renderer.exitCode() == 0;
		const QImage picture(out);
		check(ran && picture.width() == 320 && picture.height() == 180,
			  QString("a 320 x 180 render comes out %1 x %2 (exit %3)").arg(picture.width()).arg(picture.height()).arg(renderer.exitCode()));
		QFile::remove(out);
		QFile::remove(out.left(out.size() - 4) + QStringLiteral(".ppm"));
		m_samplesSpinBox->setValue(samplesWere);
		m_resolutionCombo->setCurrentIndex(fullHd);
	}
	const auto inRenderSettings = [](QWidget *w) {
		for (QWidget *p = w ? w->parentWidget() : nullptr; p; p = p->parentWidget())
			if (auto *g = qobject_cast<QGroupBox *>(p)) return g->title() == tr("Render Settings");
		return false;
	};
	check(inRenderSettings(m_powerCombo), "the Power box is in Render Settings");
	check(m_powerCombo->itemData(0).toBool() == false && m_powerCombo->itemData(1).toBool() == true, "its first choice is Full power, its second Keep the PC responsive");
	check(m_workerMemorySpin->value() == m_workerMemorySpin->minimum() || QSettings(settings_keys::kOrg, settings_keys::kApp).contains(settings_keys::kWorkerMaxGbKey),
		  "the video helper memory limit is No limit unless one was chosen: " + m_workerMemorySpin->text());
	check(inRenderSettings(m_workerMemorySpin), "the video helper memory limit is in Render Settings");
#ifndef Q_OS_MAC
	check(inRenderSettings(m_gpuDeviceSpin), "the graphics card is in Render Settings");
#endif
	if (auto *scroll = qobject_cast<QScrollArea *>(m_tabWidget->widget(0))) {
		show();
		QApplication::processEvents();
		scroll->ensureWidgetVisible(m_resolutionCombo, 50, 300);
		QApplication::processEvents();
	}
	shot("resolution");
	log(ok ? "RESULT: OK" : "RESULT: FAIL");
	QApplication::exit(ok ? 0 : 1);
}

// RT_GUI_SELFTEST=defaults: every settings section (but Scene) has a Defaults button; pressing one puts that section's controls back to their defaults - and the
// controls tied to them (Resolution with Width and Height, Quality with Samples and Max Depth) - and leaves every other section as it was. A screenshot of the
// Settings tab's sections with their buttons.
void MainWindow::runDefaultsSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	resize(1500, 950);
	if (m_tabWidget) m_tabWidget->setCurrentIndex(0);
	show();
	QApplication::processEvents();
	bool ok = true;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	QStringList titles;
	for (InfoGroupBox *s : m_resetSections) titles << s->title();
	log("sections with a Defaults button: " + titles.join(", "));
	check(m_resetSections.size() >= 10, QString("%1 sections have a Defaults button").arg(m_resetSections.size()));
	check(!titles.contains(tr("Scene")), "the Scene section has none (choosing a scene is not a setting with a default)");
	const auto sectionOf = [this](QWidget *w) -> InfoGroupBox * {
		for (QWidget *p = w ? w->parentWidget() : nullptr; p; p = p->parentWidget())
			if (auto *g = qobject_cast<InfoGroupBox *>(p); g && m_resetSections.contains(g)) return g;
		return nullptr;
	};
	InfoGroupBox *render = sectionOf(m_powerCombo), *statsSection = sectionOf(m_statsCheck);
	check(render && statsSection && render != statsSection, "the Power box and the Stats box are in two sections with buttons");
	if (!render || !statsSection) {
		log("RESULT: FAIL");
		QApplication::exit(1);
		return;
	}
	const int samplesBefore = m_samplesSpinBox->value();
	const bool statsBefore = m_statsCheck->isChecked();
	m_powerCombo->setCurrentIndex(1);
	m_resolutionCombo->setCurrentIndex(m_resolutionCombo->findData(QSize(1920, 1080)));
	m_samplesSpinBox->setValue(777);
	m_maxDepthSpinBox->setValue(13);
	m_statsCheck->setChecked(!statsBefore);
	resetSection(render);
	check(m_powerCombo->currentIndex() == 0, "Render Settings' Defaults: Power is Full power again");
	check(m_widthSpinBox->value() == 800 && m_heightSpinBox->value() == 800 && m_resolutionCombo->currentData().toSize() == QSize(800, 800),
		  QString("the size is 800 x 800 again, Width and Height with it (%1 x %2)").arg(m_widthSpinBox->value()).arg(m_heightSpinBox->value()));
	check(m_samplesSpinBox->value() == samplesBefore && m_maxDepthSpinBox->value() == 50,
		  QString("Samples is the scene's own again (%1) and Max Depth the Good preset's (%2)").arg(m_samplesSpinBox->value()).arg(m_maxDepthSpinBox->value()));
	check(m_statsCheck->isChecked() != statsBefore, "a setting in another section is left as it was");
	resetSection(statsSection);
	check(m_statsCheck->isChecked() == statsBefore, "that section's own Defaults puts it back");
	check(m_powerCombo->currentIndex() == 0 && m_widthSpinBox->value() == 800, "and leaves Render Settings as it was");
	if (auto *scroll = qobject_cast<QScrollArea *>(m_tabWidget->widget(0))) {
		scroll->ensureWidgetVisible(m_powerCombo, 50, 400);
		QApplication::processEvents();
	}
	shot("defaults");
	log(ok ? "RESULT: OK" : "RESULT: FAIL");
	QApplication::exit(ok ? 0 : 1);
}

// RT_GUI_SELFTEST=recommend: the scene tip lists everything the selected film (or scene file) was made for that the settings differ from - its GPU renderer, max depth,
// picture size, the denoiser, and in video mode its length and frame rate - and Apply sets them all, after which the tip has nothing left to say. A pbrt scene with a
// Film line offers its picture size too. A screenshot of the tip.
void MainWindow::runRecommendSelfTest(const std::function<void(const QString &)> &log, const std::function<void(const QString &)> &shot) {
	resize(1500, 950);
	if (m_tabWidget) m_tabWidget->setCurrentIndex(0);
	show();
	QApplication::processEvents();
	bool ok = true;
	auto check = [&ok, log](bool cond, const QString &what) {
		log(QString("%1: %2").arg(cond ? "ok" : "FAIL", what));
		ok = ok && cond;
	};
	QString turtle, wideScene;
	for (int i = 0, n = SceneMetadataClient::sceneCount(); i < n; ++i) {
		const QString id = SceneMetadataClient::sceneIdAtIndex(i);
		SceneMetadataClient::SceneMetadata meta;
		if (!SceneMetadataClient::sceneMetadata(id, meta)) continue;
		if (meta.filmKey == QLatin1String("turtle-escape")) turtle = id;
		if (meta.filmKey.isEmpty() && wideScene.isEmpty() && meta.filmWidth > 0 && meta.filmWidth != meta.filmHeight && !meta.requiresFiles) wideScene = id;
	}
	check(!turtle.isEmpty() && !wideScene.isEmpty(), "the turtle film's scene (" + turtle + ") and a non-square scene file (" + wideScene + ") are listed");
	const film_info::Film *film = film_info::find("turtle-escape");
	if (turtle.isEmpty() || !film) {
		log("RESULT: FAIL");
		QApplication::exit(1);
		return;
	}
	// the turtle film in video mode, with settings it was not made for
	if (const int video = m_modeCombo->findData(static_cast<int>(OutputMode::Video)); video >= 0) m_modeCombo->setCurrentIndex(video);
	selectSceneById(turtle);
	if (const int gpu = m_renderModeCombo->findData(true); gpu >= 0) m_renderModeCombo->setCurrentIndex(gpu);
	const bool wavefrontOffered = kGpuOptionAvailable && m_gpuBackendCombo->isEnabled();
	if (wavefrontOffered) m_gpuBackendCombo->setCurrentIndex(m_gpuBackendCombo->findData(false));
	m_maxDepthSpinBox->setValue(50);
	m_widthSpinBox->setValue(800);
	m_heightSpinBox->setValue(800);
	if (m_denoiseCheck->isEnabled()) m_denoiseCheck->setChecked(false);
	m_videoFramesSpinBox->setValue(120);
	m_videoFPSSpinBox->setValue(30);
	QApplication::processEvents();
	const QString tip = m_sceneRecommendedSettingsHint->text();
	log("tip: " + tip);
	check(m_sceneRecommendedSettingsHint->isVisible() && m_applyRecommendedSettingsButton->isVisible(), "the tip and its Apply button show");
	check(tip.contains(QStringLiteral("1920 x 1080")) && tip.contains(QStringLiteral("max ray depth 8")) && tip.contains(QStringLiteral("8184 frames at 24 fps")),
		  "it lists the film's picture size, max depth and length");
	if (wavefrontOffered) check(tip.contains(QStringLiteral("Wavefront")), "and the GPU renderer it was made for");
	for (int k = 0; k < 3; ++k) QApplication::processEvents();
	{
		QLabel *label = m_sceneRecommendedSettingsHint;
		const int needed = label->heightForWidth(label->width());
		log(QString("tip label: width %1, height %2, its text needs %3").arg(label->width()).arg(label->height()).arg(needed));
		check(label->height() >= needed - 2, "the whole tip shows (no line cut off)");
	}
	shot("recommend");
	applyRecommendedSettings();
	QApplication::processEvents();
	check(m_widthSpinBox->value() == film->width && m_heightSpinBox->value() == film->height, QString("Apply: %1 x %2").arg(m_widthSpinBox->value()).arg(m_heightSpinBox->value()));
	check(m_maxDepthSpinBox->value() == 8 && m_samplesSpinBox->value() == film->samples, QString("max depth %1, %2 samples").arg(m_maxDepthSpinBox->value()).arg(m_samplesSpinBox->value()));
	check(m_videoFramesSpinBox->value() == film_info::frameCountOf(*film) && m_videoFPSSpinBox->value() == film->fps, "the film's own length and frame rate");
	if (wavefrontOffered) check(m_gpuBackendCombo->currentData().toBool() == film->wavefront, "its GPU renderer: " + m_gpuBackendCombo->currentText());
	if (m_denoiseCheck->isEnabled()) check(m_denoiseCheck->isChecked(), "the denoiser on");
	check(!m_sceneRecommendedSettingsHint->isVisible() && !m_applyRecommendedSettingsButton->isVisible(), "and the tip has nothing left to say");
	// a scene file made for a non-square picture offers its size
	if (!wideScene.isEmpty()) {
		if (const int image = m_modeCombo->findData(static_cast<int>(OutputMode::Image)); image >= 0) m_modeCombo->setCurrentIndex(image);
		selectSceneById(wideScene);
		m_widthSpinBox->setValue(800);
		m_heightSpinBox->setValue(800);
		QApplication::processEvents();
		SceneMetadataClient::SceneMetadata meta;
		SceneMetadataClient::sceneMetadata(wideScene, meta);
		check(m_sceneRecommendedSettingsHint->text().contains(QStringLiteral("%1 x %2").arg(meta.filmWidth).arg(meta.filmHeight)),
			  QString("%1 offers its own %2 x %3").arg(wideScene).arg(meta.filmWidth).arg(meta.filmHeight));
	}
	log(ok ? "RESULT: OK" : "RESULT: FAIL");
	QApplication::exit(ok ? 0 : 1);
}
