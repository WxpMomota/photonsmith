// scene_builder_environment.cpp -- the Scene Builder's environment library (Add > Environment library..., and the "Photographic sky (HDRI)..." button of a sky light): photographic skies and rooms from the
// environments/ folder (src/shared/environment_library.h) become the sky light's picture, so the sun, clouds, lamps and windows of a real place light the scene and show behind it.
#include "scene_builder_widget.h"

#include "app_log.h"
#include "scene_builder_common.h"

#include "../src/shared/environment_library.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// The folder the pictures are in: next to the installed app, or the source tree's environments/ folder when running from a build inside it (the one with the most of the library's files).
QString environmentsFolder() {
	const QString app = QCoreApplication::applicationDirPath();
	const QStringList candidates = {app + "/environments", app + "/../environments", app + "/../../environments", QDir::currentPath() + "/environments", QDir::currentPath() + "/../environments"};
	QString best;
	int bestCount = 0;
	for (const QString &c : candidates) {
		int count = 0;
		for (const environment_library::Entry &e : environment_library::catalog())
			if (QFileInfo(c + "/" + QString::fromStdString(environment_library::fileOf(e))).exists()) ++count;
		if (count > bestCount) { bestCount = count; best = QDir(c).absolutePath(); }
	}
	return best;
}

}  // namespace

bool SceneBuilderWidget::applyEnvironment(int lightIndex, const QString &stem) {
	const environment_library::Entry *entry = environment_library::findEntry(stem.toStdString());
	const QString folder = environmentsFolder();
	if (!entry || folder.isEmpty()) return false;
	const QString path = folder + "/" + QString::fromStdString(environment_library::fileOf(*entry));
	if (!QFileInfo::exists(path)) return false;
	int sky = -1;
	edit(QString(), [&]() {
		// the sky light: the one asked for, else the first, else a new one
		if (lightIndex >= 0 && lightIndex < static_cast<int>(m_doc.lights.size()) && m_doc.lights[static_cast<size_t>(lightIndex)].kind == scene_doc::LightKind::Infinite) sky = lightIndex;
		for (size_t i = 0; sky < 0 && i < m_doc.lights.size(); ++i)
			if (m_doc.lights[i].kind == scene_doc::LightKind::Infinite) sky = static_cast<int>(i);
		if (sky < 0) {
			scene_doc::Light made;
			made.kind = scene_doc::LightKind::Infinite;
			made.name = "Sky";
			m_doc.lights.push_back(made);
			sky = static_cast<int>(m_doc.lights.size()) - 1;
		}
		scene_doc::Light &light = m_doc.lights[static_cast<size_t>(sky)];
		light.physicalSky = false;   // a picture of its own, not the generated sky
		light.imageFile = QDir::toNativeSeparators(path).toStdString();
		light.intensity = 1.0;
		light.skySpin = 0.0;
		// the picture has its own sun: the Sun light the generated sky made would light everything twice
		const scene_doc::Light kept = light;
		m_doc.lights.erase(std::remove_if(m_doc.lights.begin(), m_doc.lights.end(), [](const scene_doc::Light &l) { return l.kind == scene_doc::LightKind::Distant && l.name == "Sun"; }), m_doc.lights.end());
		for (size_t i = 0; i < m_doc.lights.size(); ++i)
			if (m_doc.lights[i].kind == scene_doc::LightKind::Infinite && m_doc.lights[i].imageFile == kept.imageFile) sky = static_cast<int>(i);
		m_sel = {SelKind::Light, sky};
	});
	rebuildList();
	setSelection(m_sel);
	emit statusMessage(tr("Environment: %1. It is the sky light's picture now: the sun, clouds and lamps in it light the scene. Rotate sky turns it; Brightness changes its strength.").arg(QString::fromUtf8(entry->name)));
	return true;
}

void SceneBuilderWidget::showEnvironmentLibrary(int lightIndex) {
	const QString folder = environmentsFolder();
	if (folder.isEmpty()) {
		QMessageBox::information(this, tr("Environment library"), tr("No environment pictures were found next to the app (an environments folder)."));
		return;
	}
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Environment library"));
	dialog.resize(880, 600);
	auto *layout = new QVBoxLayout(&dialog);
	auto *kind = new QComboBox(&dialog);
	kind->addItem(tr("All kinds"), QString());
	for (const std::string &c : environment_library::categories()) kind->addItem(QCoreApplication::translate("EnvironmentLibrary", c.c_str()), QString::fromStdString(c));
	layout->addWidget(kind);
	auto *list = new QListWidget(&dialog);
	list->setViewMode(QListView::IconMode);
	list->setIconSize(QSize(192, 96));
	list->setResizeMode(QListView::Adjust);
	list->setMovement(QListView::Static);
	list->setSpacing(10);
	list->setWordWrap(true);
	list->setUniformItemSizes(true);
	list->setGridSize(QSize(214, 138));
	layout->addWidget(list, 1);
	int available = 0;
	for (const environment_library::Entry &e : environment_library::catalog()) {
		if (!QFileInfo(folder + "/" + QString::fromStdString(environment_library::fileOf(e))).exists()) continue;
		auto *item = new QListWidgetItem(QString::fromUtf8(e.name), list);
		const QString thumb = folder + "/thumbnails/" + QString::fromUtf8(e.stem) + ".png";
		if (QFileInfo::exists(thumb)) item->setIcon(QIcon(QPixmap(thumb).scaled(192, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
		item->setData(Qt::UserRole, QString::fromUtf8(e.stem));
		item->setData(Qt::UserRole + 1, QString::fromUtf8(e.category));
		item->setToolTip(tr("%1\n%2\nFrom: %3").arg(QString::fromUtf8(e.name), QCoreApplication::translate("EnvironmentLibrary", e.category), QString::fromUtf8(e.source)));
		++available;
	}
	auto *note = new QLabel(tr("%1 photographic skies and rooms (Poly Haven, CC0). One becomes the sky light's picture: the sun, clouds, lamps and windows in it light the scene and show behind it. "
	                           "The Sun light made by the generated sky is removed, because the picture has its own.").arg(available), &dialog);
	note->setWordWrap(true);
	layout->addWidget(note);
	auto *buttons = new QDialogButtonBox(&dialog);
	QPushButton *use = buttons->addButton(tr("Use this sky"), QDialogButtonBox::AcceptRole);
	buttons->addButton(QDialogButtonBox::Close);
	layout->addWidget(buttons);
	use->setEnabled(false);
	use->setDefault(true);
	connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [list, kind]() {
		const QString want = kind->currentData().toString();
		for (int i = 0; i < list->count(); ++i) list->item(i)->setHidden(!want.isEmpty() && list->item(i)->data(Qt::UserRole + 1).toString() != want);
	});
	connect(list, &QListWidget::itemSelectionChanged, &dialog, [list, use]() { use->setEnabled(!list->selectedItems().isEmpty()); });
	const auto choose = [&]() {
		const QList<QListWidgetItem *> sel = list->selectedItems();
		if (sel.isEmpty()) return;
		if (applyEnvironment(lightIndex, sel.first()->data(Qt::UserRole).toString())) dialog.accept();
		else QMessageBox::warning(&dialog, tr("Environment library"), tr("That picture could not be used."));
	};
	connect(use, &QPushButton::clicked, &dialog, choose);
	connect(list, &QListWidget::itemDoubleClicked, &dialog, [&](QListWidgetItem *) { choose(); });
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialog.exec();
}
