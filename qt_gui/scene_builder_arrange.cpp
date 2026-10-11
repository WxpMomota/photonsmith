// scene_builder_arrange.cpp - the Scene Builder's tidying tools: hide and lock what is picked, put it on the floor, rename it and frame it (src/shared/scene_arrange.h has the
// geometry; this is the buttons, the menu, the shortcuts and the undo steps).
#include "scene_builder_widget.h"

#include "app_log.h"
#include "scene_builder_common.h"
#include "../src/shared/scene_arrange.h"

#include <QAction>
#include <QApplication>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QShortcut>
#include <QTimer>

#include <algorithm>

using scene_doc::ItemSet;
using namespace scene_builder_ui;

// Whether the item cannot be moved in the views: locked, or hidden (a hidden one is not drawn there either).
bool SceneBuilderWidget::isLockedItem(const BuilderSelection &s) const {
	if (s.kind == SelKind::Object && s.index >= 0 && s.index < static_cast<int>(m_doc.objects.size())) return m_doc.objects[s.index].locked || m_doc.objects[s.index].hidden;
	if (s.kind == SelKind::Light && s.index >= 0 && s.index < static_cast<int>(m_doc.lights.size())) return m_doc.lights[s.index].locked || m_doc.lights[s.index].hidden;
	return false;
}

// The picked items that a drag, a nudge or a ring in a view may move: not the locked or hidden ones.
ItemSet SceneBuilderWidget::unlockedPicked() const {
	ItemSet items = pickedItems();
	items.objects.erase(std::remove_if(items.objects.begin(), items.objects.end(), [this](int i) { return m_doc.objects[i].locked || m_doc.objects[i].hidden; }), items.objects.end());
	items.lights.erase(std::remove_if(items.lights.begin(), items.lights.end(), [this](int i) { return m_doc.lights[i].locked || m_doc.lights[i].hidden; }), items.lights.end());
	return items;
}

void SceneBuilderWidget::hideSelected(bool hide) {
	const ItemSet items = pickedItems();
	if (items.empty()) return;
	int changed = 0;
	edit(QString(), [&]() { changed = scene_doc::setHidden(m_doc, items, hide); });
	if (changed == 0) return;
	AppLog::info(QStringLiteral("builder"), QStringLiteral("%1 %2 item(s)").arg(hide ? QStringLiteral("hid") : QStringLiteral("showed")).arg(changed));
	emit statusMessage(hide ? tr("Hidden in the views (it still renders). Alt+H shows everything again.") : tr("Shown in the views again."));
	QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
}

void SceneBuilderWidget::lockSelected(bool lock) {
	const ItemSet items = pickedItems();
	if (items.empty()) return;
	int changed = 0;
	edit(QString(), [&]() { changed = scene_doc::setLocked(m_doc, items, lock); });
	if (changed == 0) return;
	AppLog::info(QStringLiteral("builder"), QStringLiteral("%1 %2 item(s)").arg(lock ? QStringLiteral("locked") : QStringLiteral("unlocked")).arg(changed));
	emit statusMessage(lock ? tr("Locked: it is not picked or moved by clicking and dragging in the views (it can still be picked in the list).") : tr("Unlocked."));
	QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
}

void SceneBuilderWidget::showAllHidden() {
	int n = 0;
	edit(QString(), [&]() { n = scene_doc::showAll(m_doc); });
	if (n == 0) { emit statusMessage(tr("Nothing is hidden.")); return; }
	emit statusMessage(tr("Showed %1 hidden item(s) again.").arg(n));
	QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
}

void SceneBuilderWidget::unlockAllLocked() {
	int n = 0;
	edit(QString(), [&]() { n = scene_doc::unlockAll(m_doc); });
	if (n == 0) { emit statusMessage(tr("Nothing is locked.")); return; }
	emit statusMessage(tr("Unlocked %1 item(s).").arg(n));
	QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
}

// Puts the picked items on the floor, together (one undo step).
void SceneBuilderWidget::dropSelectedToFloor() {
	const ItemSet items = pickedItems();
	if (items.empty()) return;
	bool moved = false;
	edit(QString(), [&]() { moved = scene_doc::dropToFloor(m_doc, items); });
	if (!moved) {
		emit statusMessage(tr("Nothing to drop: it already stands on the floor, or its size is not known (a mesh file)."));
		return;
	}
	refreshInspectorValues();
	emit statusMessage(tr("Put on the floor."));
}

// Both views look at what is picked (the camera, if that is what is selected): the 3D view at its middle from far enough to hold it, the layout view zoomed to it.
void SceneBuilderWidget::frameSelected() {
	scene_doc::Float3 lo, hi;
	bool any = false;
	if (m_sel.kind == SelKind::Camera) {
		const scene_doc::Camera &c = m_doc.camera;
		lo = {std::min(c.position.x, c.target.x), std::min(c.position.y, c.target.y), std::min(c.position.z, c.target.z)};
		hi = {std::max(c.position.x, c.target.x), std::max(c.position.y, c.target.y), std::max(c.position.z, c.target.z)};
		any = true;
	} else {
		any = scene_doc::boundsOf(m_doc, pickedItems(), lo, hi);
	}
	if (!any) {
		emit statusMessage(tr("Pick something to frame it."));
		return;
	}
	m_view->frameBox(lo, hi);
	m_view3d->frameBox(lo, hi);
}

// A new name for the selected object or light, asked for in a small window (F2, or double-click it in the list).
void SceneBuilderWidget::renameSelected() {
	if (m_sel.kind != SelKind::Object && m_sel.kind != SelKind::Light) return;
	const bool object = m_sel.kind == SelKind::Object;
	const int index = m_sel.index;
	if (index < 0 || index >= static_cast<int>(object ? m_doc.objects.size() : m_doc.lights.size())) return;
	const QString current = QString::fromStdString(object ? m_doc.objects[index].name : m_doc.lights[index].name);
	bool ok = false;
	const QString wanted = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal, current, &ok).trimmed();
	if (!ok || wanted.isEmpty() || wanted == current) return;
	renameItem(m_sel, wanted);
}

// Gives the item the name (made different from the others' if it is not), as one undo step.
void SceneBuilderWidget::renameItem(const BuilderSelection &s, const QString &wanted) {
	const bool object = s.kind == SelKind::Object;
	if (!object && s.kind != SelKind::Light) return;
	QStringList taken;
	for (std::size_t i = 0; i < (object ? m_doc.objects.size() : m_doc.lights.size()); ++i)
		if (static_cast<int>(i) != s.index) taken << QString::fromStdString(object ? m_doc.objects[i].name : m_doc.lights[i].name);
	const QString name = uniqueName(wanted, taken);
	edit(QString(), [&]() {
		if (object && s.index < static_cast<int>(m_doc.objects.size())) m_doc.objects[s.index].name = name.toStdString();
		else if (!object && s.index < static_cast<int>(m_doc.lights.size())) m_doc.lights[s.index].name = name.toStdString();
	});
	refreshInspectorValues();
	QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
}

// The tools in one menu (the Arrange button, and a right-click on the list).
QMenu *SceneBuilderWidget::createArrangeMenu(QWidget *parent) {
	auto *menu = new QMenu(parent);
	const auto key = [](const QKeySequence &k) { return QLatin1Char('\t') + k.toString(QKeySequence::NativeText); };
	menu->addAction(tr("Search actions...") + key(QKeySequence(Qt::Key_F3)), this, [this]() { showActionSearch(); });
	menu->addSeparator();
	menu->addAction(tr("Rename...") + key(QKeySequence(Qt::Key_F2)), this, [this]() { renameSelected(); });
	menu->addAction(tr("Drop to floor") + key(QKeySequence(Qt::Key_End)), this, [this]() { dropSelectedToFloor(); });
	menu->addAction(tr("Frame selected") + key(QKeySequence(Qt::Key_F)), this, [this]() { frameSelected(); });
	menu->addSeparator();
	menu->addAction(tr("Hide in the views") + key(QKeySequence(Qt::Key_H)), this, [this]() { hideSelected(true); });
	menu->addAction(tr("Show everything hidden") + key(QKeySequence(Qt::ALT | Qt::Key_H)), this, [this]() { showAllHidden(); });
	menu->addAction(tr("Lock") + key(QKeySequence(Qt::Key_L)), this, [this]() { lockSelected(true); });
	menu->addAction(tr("Unlock"), this, [this]() { lockSelected(false); });
	menu->addAction(tr("Unlock everything locked") + key(QKeySequence(Qt::ALT | Qt::Key_L)), this, [this]() { unlockAllLocked(); });
	return menu;
}

// The keys, from anywhere in the tab except a text box (where they type).
void SceneBuilderWidget::createArrangeShortcuts() {
	const auto addKey = [this](const QKeySequence &key, const std::function<void()> &action) {
		auto *shortcut = new QShortcut(key, this);
		shortcut->setContext(Qt::WidgetWithChildrenShortcut);
		connect(shortcut, &QShortcut::activated, this, [action]() { if (!qobject_cast<QLineEdit *>(QApplication::focusWidget())) action(); });
	};
	addKey(QKeySequence(Qt::Key_H), [this]() { hideSelected(true); });
	addKey(QKeySequence(Qt::ALT | Qt::Key_H), [this]() { showAllHidden(); });
	addKey(QKeySequence(Qt::Key_L), [this]() { lockSelected(true); });
	addKey(QKeySequence(Qt::ALT | Qt::Key_L), [this]() { unlockAllLocked(); });
	addKey(QKeySequence(Qt::Key_F2), [this]() { renameSelected(); });
	addKey(QKeySequence(Qt::Key_End), [this]() { dropSelectedToFloor(); });
}
