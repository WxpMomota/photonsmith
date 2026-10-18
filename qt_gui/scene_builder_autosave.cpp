// scene_builder_autosave.cpp - where the scene being edited is kept so a crash does not lose it (see scene_builder_widget.h).
#include "scene_builder_widget.h"

#include "scene_builder_common.h"
#include "app_log.h"
#include "window_geometry.h"
#include "atomic_file.h"
#include "../src/shared/scene_doc_diff.h"
#include "../src/shared/pbrt_asset_check.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QRegularExpression>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using scene_doc::Document;
using scene_doc::Float3;
using scene_doc::Light;
using scene_doc::LightKind;
using scene_doc::MaterialKind;
using scene_doc::Object;
using scene_doc::Rgb;
using scene_doc::ShapeKind;

using namespace scene_builder_ui;

// Where the unsaved scene is kept while it is being edited (RAY_TRACER_STATE_DIR names another folder, for tests).
static QString autosavePath() {
	const QString env = qEnvironmentVariable("RAY_TRACER_STATE_DIR");
	const QString dir = env.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : env;
	QDir().mkpath(dir);
	return dir + "/scene_builder_autosave.pbrt";
}

bool SceneBuilderWidget::hasAutosave() { return QFileInfo::exists(autosavePath()); }

// "Start fresh" after a crash: the autosave is renamed, not deleted - if it was the scene that crashed the program, the user can still open it by hand.
QString SceneBuilderWidget::setAsideAutosave() {
	const QString path = autosavePath();
	if (!QFileInfo::exists(path)) return QString();
	const QString aside = QFileInfo(path).absolutePath() + "/scene_builder_autosave.set-aside-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".pbrt";
	return QFile::rename(path, aside) ? aside : QString();
}

void SceneBuilderWidget::scheduleAutosave() {
	if (m_autosaveEnabled) m_autosaveTimer->start();
}

void SceneBuilderWidget::writeAutosave() {
	if (!m_autosaveEnabled || !m_dirty) return;
	const std::string text = scene_doc::toPbrt(m_doc);
	QString error;
	const bool ok = writeFileAtomically(autosavePath(), QByteArray::fromRawData(text.data(), static_cast<qsizetype>(text.size())), &error);
	// Said once when it starts failing (and once when it works again), not every 1.5 seconds.
	static bool failing = false;
	if (!ok && !failing) AppLog::warn(QStringLiteral("builder"), QStringLiteral("could not write the unsaved-scene backup %1: %2").arg(autosavePath(), error));
	if (ok && failing) AppLog::info(QStringLiteral("builder"), QStringLiteral("the unsaved-scene backup is being written again"));
	failing = !ok;
}

void SceneBuilderWidget::clearAutosave() {
	if (m_autosaveEnabled) QFile::remove(autosavePath());
}

// A scene that was being edited when the program closed comes back, marked unsaved.
bool SceneBuilderWidget::loadAutosave() {
	QFile f(autosavePath());
	if (!f.exists()) return false;
	if (!f.open(QIODevice::ReadOnly)) {
		AppLog::warn(QStringLiteral("builder"), QStringLiteral("unsaved scene %1 exists but cannot be opened (%2): starting with the starter scene").arg(f.fileName(), f.errorString()));
		return false;
	}
	Document d;
	std::string err;
	if (!scene_doc::fromPbrt(f.readAll().toStdString(), d, err)) {
		AppLog::warn(QStringLiteral("builder"), QStringLiteral("unsaved scene %1 could not be read (%2): starting with the starter scene").arg(f.fileName(), QString::fromStdString(err)));
		return false;
	}
	AppLog::info(QStringLiteral("builder"), QStringLiteral("restored the unsaved scene from %1: \"%2\", %3 objects, %4 lights")
	                                            .arg(f.fileName(), QString::fromStdString(d.title)).arg(d.objects.size()).arg(d.lights.size()));
	m_doc = std::move(d);
	syncCameraPhoto();
	m_path.clear();
	m_dirty = true;
	m_history.clear();
	m_lastEditKey.clear();
	QElapsedTimer step;   // (what the restore spends its time on, for the startup profile)
	step.start();
	auto took = [&step](const char *what) { AppLog::debug(QStringLiteral("startup"), QStringLiteral("restoring the unsaved scene: %1 took %2 ms").arg(QLatin1String(what)).arg(step.restart())); };
	rebuildList();
	took("the list");
	setSelection({SelKind::None, 0});
	took("the selection");
	frameViews();
	took("framing the views");
	refreshProblems();
	took("checking the scene");
	updateTitle();
	updateActions();
	took("title and buttons");
	return true;
}
