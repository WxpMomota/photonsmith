// scene_builder_actions.cpp - the Scene Builder's "search for an action" box (F3 or Ctrl+K): type what you want to do ("wire", "drop", "add fog", "top view") and run it,
// without finding its button. The matching is src/shared/action_search.h; this builds the list of what can be done and shows the box.
#include "scene_builder_widget.h"

#include "app_log.h"
#include "scene_builder_common.h"
#include "../src/shared/action_search.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include <algorithm>

using namespace scene_builder_ui;

namespace {

// Up, Down, Page Up and Page Down in the text box move the highlight in the list (so the hands stay on the keyboard).
class ListKeys : public QObject {
public:
	ListKeys(QListWidget *list, QObject *parent) : QObject(parent), m_list(list) {}
	bool eventFilter(QObject *, QEvent *e) override {
		if (e->type() != QEvent::KeyPress) return false;
		const int key = static_cast<QKeyEvent *>(e)->key();
		if (key != Qt::Key_Down && key != Qt::Key_Up && key != Qt::Key_PageDown && key != Qt::Key_PageUp) return false;
		const int step = (key == Qt::Key_PageDown || key == Qt::Key_PageUp) ? 8 : 1;
		const int dir = (key == Qt::Key_Down || key == Qt::Key_PageDown) ? 1 : -1;
		if (m_list->count() == 0) return true;
		m_list->setCurrentRow(std::clamp(m_list->currentRow() + dir * step, 0, m_list->count() - 1));
		return true;
	}

private:
	QListWidget *m_list;
};

}  // namespace

// Everything the search box can do, each with a name that starts with its kind ("Add: Sphere", "Arrange: Hide in the views", "View: Top"). The names are what the user types
// against, so they are in the user's language.
std::vector<SceneBuilderWidget::Command> SceneBuilderWidget::buildCommands() {
	std::vector<Command> out;
	const auto add = [&out](const QString &kind, const QString &name, std::function<void()> run) { out.push_back({QStringLiteral("%1: %2").arg(kind, name), std::move(run)}); };
	const QString kAdd = tr("Add"), kLighting = tr("Lighting"), kEdit = tr("Edit"), kArrange = tr("Arrange"), kView = tr("View"), kRender = tr("Render");

	for (scene_doc::ShapeKind k : scene_doc::allShapeKinds()) add(kAdd, shapeLabel(k), [this, k]() { addObject(k); });
	for (scene_doc::VolumeKind k : {scene_doc::VolumeKind::Fog, scene_doc::VolumeKind::Smoke, scene_doc::VolumeKind::Cloud}) add(kAdd, volumeLabel(k), [this, k]() { addVolume(k); });
	for (scene_doc::LightKind k : {scene_doc::LightKind::Point, scene_doc::LightKind::Spot, scene_doc::LightKind::Distant, scene_doc::LightKind::Infinite})
		add(kAdd, lightLabel(k), [this, k]() { addLight(k); });
	for (scene_doc::PropKind k : scene_doc::allPropKinds()) add(kAdd, propLabel(k), [this, k]() { addProp(k); });
	for (scene_doc::BlockyKind k : scene_doc::allBlockyKinds()) add(kAdd, blockyLabel(k), [this, k]() { addBlocky(k); });
	add(kAdd, tr("Model library..."), [this]() { showModelLibrary(); });
	add(kAdd, tr("Object from a photo..."), [this]() { addObjectFromPhoto(); });

	for (scene_doc::lighting::Preset p : scene_doc::lighting::allPresets()) add(kLighting, lightingLabel(p), [this, p]() { applyLightingPreset(p); });

	add(kEdit, tr("Duplicate"), [this]() { duplicateSelected(); });
	add(kEdit, tr("Array..."), [this]() { showArrayDialog(); });
	add(kEdit, tr("Delete"), [this]() { deleteSelected(); });
	add(kEdit, tr("Select all"), [this]() { selectAll(); });
	add(kEdit, tr("Group"), [this]() { groupSelected(); });
	add(kEdit, tr("Ungroup"), [this]() { ungroupSelected(); });
	add(kEdit, tr("Undo"), [this]() { undo(); });
	add(kEdit, tr("Redo"), [this]() { redo(); });

	add(kArrange, tr("Rename..."), [this]() { renameSelected(); });
	add(kArrange, tr("Drop to floor"), [this]() { dropSelectedToFloor(); });
	add(kArrange, tr("Frame selected"), [this]() { frameSelected(); });
	add(kArrange, tr("Hide in the views"), [this]() { hideSelected(true); });
	add(kArrange, tr("Show everything hidden"), [this]() { showAllHidden(); });
	add(kArrange, tr("Lock"), [this]() { lockSelected(true); });
	add(kArrange, tr("Unlock"), [this]() { lockSelected(false); });
	add(kArrange, tr("Unlock everything locked"), [this]() { unlockAllLocked(); });

	add(kView, tr("Frame all"), [this]() { m_view->frameAll(); m_view3d->frameAll(); });
	const std::pair<QString, scene_view::StandardView> standard[] = {{tr("Front"), scene_view::StandardView::Front}, {tr("Back"), scene_view::StandardView::Back},
	                                                                 {tr("Right"), scene_view::StandardView::Right}, {tr("Left"), scene_view::StandardView::Left},
	                                                                 {tr("Top"), scene_view::StandardView::Top}, {tr("Bottom"), scene_view::StandardView::Bottom}};
	for (const auto &s : standard) {
		const scene_view::StandardView sv = s.second;
		add(kView, s.first, [this, sv]() { show3dView(true); m_view3d->setStandardView(sv); });
	}
	add(kView, tr("Wireframe"), [this]() { show3dView(true); if (m_wireButton) m_wireButton->toggle(); });
	add(kView, tr("Orthographic"), [this]() { show3dView(true); if (m_orthoButton) m_orthoButton->toggle(); });
	add(kView, tr("Through camera"), [this]() { show3dView(true); if (m_throughButton) m_throughButton->toggle(); });
	add(kView, tr("Camera from view"), [this]() { cameraFromView(); });
	add(kView, tr("Snap to objects"), [this]() { if (m_snapObjectsCheck) m_snapObjectsCheck->toggle(); });

	add(kRender, tr("Turntable..."), [this]() { showVideoDialog(false); });
	add(kRender, tr("Flythrough..."), [this]() { showVideoDialog(true); });
	return out;
}

QStringList SceneBuilderWidget::actionNamesForTest() {
	QStringList names;
	for (const Command &c : buildCommands()) names << c.label;
	return names;
}

// Runs the best match for the words (as if picked in the box); false if nothing fits.
bool SceneBuilderWidget::runActionForTest(const QString &words) {
	const std::vector<Command> commands = buildCommands();
	std::vector<std::string> names;
	for (const Command &c : commands) names.push_back(c.label.toStdString());
	const std::vector<int> hits = action_search::match(names, words.toStdString());
	if (hits.empty()) return false;
	commands[static_cast<std::size_t>(hits.front())].run();
	return true;
}

// The box: a line to type in and the actions that fit below it; Enter (or a double-click) runs the highlighted one, Up and Down move, Escape closes.
void SceneBuilderWidget::showActionSearch() {
	const std::vector<Command> commands = buildCommands();
	std::vector<std::string> names;
	for (const Command &c : commands) names.push_back(c.label.toStdString());

	QDialog dialog(this);
	dialog.setWindowTitle(tr("Search actions"));
	dialog.setMinimumSize(420, 380);
	auto *layout = new QVBoxLayout(&dialog);
	auto *edit = new QLineEdit(&dialog);
	edit->setPlaceholderText(tr("Type what you want to do: wireframe, drop, add fog, top view..."));
	edit->setClearButtonEnabled(true);
	auto *list = new QListWidget(&dialog);
	layout->addWidget(edit);
	layout->addWidget(list, 1);
	std::vector<int> shown;
	const auto refill = [&]() {
		shown = action_search::match(names, edit->text().toStdString());
		list->clear();
		for (int i : shown) list->addItem(commands[static_cast<std::size_t>(i)].label);
		if (list->count() > 0) list->setCurrentRow(0);
	};
	connect(edit, &QLineEdit::textChanged, &dialog, [&](const QString &) { refill(); });
	edit->installEventFilter(new ListKeys(list, &dialog));
	int chosen = -1;
	const auto choose = [&]() {
		const int row = list->currentRow();
		if (row < 0 || row >= static_cast<int>(shown.size())) return;
		chosen = shown[static_cast<std::size_t>(row)];
		dialog.accept();
	};
	connect(edit, &QLineEdit::returnPressed, &dialog, choose);
	connect(list, &QListWidget::itemDoubleClicked, &dialog, [&](QListWidgetItem *) { choose(); });
	refill();
	edit->setFocus();
	dialog.exec();
	if (chosen >= 0) {
		AppLog::info(QStringLiteral("builder"), QStringLiteral("action: %1").arg(commands[static_cast<std::size_t>(chosen)].label));
		commands[static_cast<std::size_t>(chosen)].run();   // (after the box has closed, so a window the action opens is not under it)
	}
}

// F3 and Ctrl+K (Command+K on a Mac, where F3 is taken by the system), from anywhere in the tab, text boxes too.
void SceneBuilderWidget::createActionSearchShortcuts() {
	for (const QKeySequence &key : {QKeySequence(Qt::Key_F3), QKeySequence(Qt::CTRL | Qt::Key_K)}) {
		auto *shortcut = new QShortcut(key, this);
		shortcut->setContext(Qt::WidgetWithChildrenShortcut);
		connect(shortcut, &QShortcut::activated, this, [this]() { showActionSearch(); });
	}
}
