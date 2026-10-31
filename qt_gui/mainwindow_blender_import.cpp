// File > Import Blender Scene: a .blend turned into a scene of My Scenes, by the user's own Blender running tools/blender_import/blend_to_pbrt.py with --disable-autoexec (a .blend
// cannot run scripts of its own). The scene goes in a folder of its own in the scene-list folder; what the tool prints as `STEP ...` is shown as it goes.
#include "mainwindow.h"

#include "app_log.h"
#include "scene_builder_widget.h"
#include "scene_metadata_client.h"
#include "settings_keys.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>

#include <memory>

namespace {
const char *const kBlenderPathKey = "blenderImport/blenderPath";

// The script beside the program, or (running from a build's package folder) a folder up.
QString importScript() {
	const QString app = QCoreApplication::applicationDirPath();
	QStringList up{"", "../"};
	if (app.contains(".app/Contents/")) up << "../../../";
	for (const QString &u : up) {
		const QString candidate = app + "/" + u + "tools/blender_import/blend_to_pbrt.py";
		if (QFileInfo::exists(candidate)) return QDir::cleanPath(candidate);
	}
	return QString();
}

// Blender: where the user last pointed to, the PATH, the installer's and Steam's places.
QString findBlender() {
	const QString saved = QSettings(settings_keys::kOrg, settings_keys::kApp).value(kBlenderPathKey).toString();
	if (!saved.isEmpty() && QFileInfo::exists(saved)) return saved;
	const QString onPath = QStandardPaths::findExecutable(QStringLiteral("blender"));
	if (!onPath.isEmpty()) return onPath;
	QStringList candidates;
	const QDir foundation(QStringLiteral("C:/Program Files/Blender Foundation"));
	QStringList versions = foundation.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
	for (const QString &v : versions) candidates << foundation.filePath(v + "/blender.exe");
	candidates << QStringLiteral("C:/Program Files (x86)/Steam/steamapps/common/Blender/blender.exe") << QStringLiteral("C:/Program Files/Steam/steamapps/common/Blender/blender.exe")
			   << QStringLiteral("/Applications/Blender.app/Contents/MacOS/Blender") << QDir::homePath() + QStringLiteral("/Applications/Blender.app/Contents/MacOS/Blender");
	for (const QString &c : candidates)
		if (QFileInfo::exists(c)) return c;
	return QString();
}
}  // namespace

void MainWindow::onImportBlenderScene() {
	const QString blend = QFileDialog::getOpenFileName(this, tr("Import Blender Scene"), QString(), tr("Blender scenes (*.blend)"));
	if (!blend.isEmpty()) importBlendFile(blend);
}

// The import of one file. `done` (the self-test's) is told the scene file made, "" when there is none.
void MainWindow::importBlendFile(const QString &blend, const std::function<void(const QString &)> &done) {
	const QString script = importScript(), folder = SceneBuilderWidget::sceneListFolder();
	if (script.isEmpty() || folder.isEmpty()) {
		QMessageBox::warning(this, tr("Import Blender Scene"), script.isEmpty() ? tr("The import tool (tools/blender_import/blend_to_pbrt.py) was not found next to the program.")
																				: tr("There is no folder to keep imported scenes in."));
		if (done) done(QString());
		return;
	}
	QString blender = findBlender();
	if (blender.isEmpty()) {
		QMessageBox::information(this, tr("Import Blender Scene"), tr("Importing needs Blender, which was not found. Please show where it is (blender.exe, or Blender in Blender.app)."));
		blender = QFileDialog::getOpenFileName(this, tr("Where is Blender?"));
		if (blender.isEmpty()) {
			if (done) done(QString());
			return;
		}
		QSettings(settings_keys::kOrg, settings_keys::kApp).setValue(kBlenderPathKey, blender);
	}
	QString name = QFileInfo(blend).completeBaseName();
	for (QChar &c : name)
		if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_')) c = QLatin1Char('-');
	QString outDir = folder + "/" + name;
	for (int n = 2; QFileInfo::exists(outDir); ++n) outDir = folder + "/" + name + "-" + QString::number(n);
	const QString sceneName = QFileInfo(outDir).fileName();
	QDir().mkpath(outDir);

	auto *process = new QProcess(this);
	process->setProcessChannelMode(QProcess::MergedChannels);
	auto *progress = new QProgressDialog(tr("Reading %1 in Blender...").arg(QFileInfo(blend).fileName()), tr("Cancel"), 0, 0, this);
	progress->setWindowTitle(tr("Import Blender Scene"));
	progress->setWindowModality(Qt::WindowModal);
	progress->setMinimumDuration(0);
	auto missing = std::make_shared<QString>();
	auto log = std::make_shared<QString>();
	connect(process, &QProcess::readyReadStandardOutput, this, [process, progress, missing, log]() {
		while (process->canReadLine()) {
			const QString line = QString::fromUtf8(process->readLine()).trimmed();
			log->append(line + "\n");
			if (line.startsWith(QStringLiteral("STEP "))) progress->setLabelText(line.mid(5));
			else if (line.startsWith(QStringLiteral("MISSING "))) *missing = line.mid(8);
		}
	});
	connect(progress, &QProgressDialog::canceled, process, &QProcess::kill);
	connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [=](int code, QProcess::ExitStatus status) {
		progress->close();
		progress->deleteLater();
		process->deleteLater();
		const QString sceneFile = outDir + "/" + sceneName + ".pbrt";
		if (status != QProcess::NormalExit || code != 0 || !QFileInfo::exists(sceneFile)) {
			QDir(outDir).removeRecursively();
			if (!progress->wasCanceled()) {
				AppLog::error(QStringLiteral("scenes"), QStringLiteral("Blender import failed:\n") + *log);
				if (!done) QMessageBox::warning(this, tr("Import Blender Scene"), tr("The import did not finish. The log has what Blender said."));
			}
			if (done) done(QString());
			return;
		}
		SceneMetadataClient::refreshUserScenes();
		const QString id = SceneMetadataClient::sceneIdForFile(sceneFile);
		if (!id.isEmpty()) selectSceneById(id);
		statusBar()->showMessage(tr("Imported %1 into My Scenes").arg(sceneName), 8000);
		if (done) done(sceneFile);
		else if (!missing->isEmpty()) QMessageBox::information(this, tr("Import Blender Scene"), tr("Imported, but these pictures could not be found and were left out:\n%1").arg(*missing));
	});
	process->start(blender, {QStringLiteral("--background"), QStringLiteral("--factory-startup"), QStringLiteral("--disable-autoexec"), QStringLiteral("-noaudio"), blend, QStringLiteral("--python"), script,
							 QStringLiteral("--"), outDir, QStringLiteral("--name"), sceneName, QStringLiteral("--search"), QFileInfo(blend).absolutePath()});
}
