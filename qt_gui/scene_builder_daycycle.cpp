// scene_builder_daycycle.cpp -- the Scene Builder's "Day cycle...": a video of a day, the sun going from sunrise to sunset over the scene while the camera stays where it is (a time-lapse of
// the light on the scene). Every frame is its own render with the Sun & sky set to that moment (src/shared/day_cycle.h says where the sun is); the frames are put together as an MP4 by the
// renderer (ray_tracer --assemble-video: AVFoundation on a Mac, ffmpeg elsewhere), the same way its own --video mode ends.
#include "scene_builder_widget.h"

#include "app_log.h"
#include "scene_builder_common.h"
#include "../src/shared/day_cycle.h"
#include "../src/shared/scene_sky.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

struct DayQuality {
	int width, samples;   // 0: the scene's own
};
const DayQuality kDayQualities[4] = {{480, 16}, {720, 64}, {960, 256}, {0, 0}};

// The first sky that is the generated Sun & sky (the only one whose sun can be moved): its index, or -1.
int generatedSkyIndex(const scene_doc::Document &doc) {
	for (size_t i = 0; i < doc.lights.size(); ++i)
		if (doc.lights[i].kind == scene_doc::LightKind::Infinite && doc.lights[i].physicalSky) return static_cast<int>(i);
	return -1;
}

}  // namespace

void SceneBuilderWidget::showDayCycleDialog() {
	const int skyIndex = generatedSkyIndex(m_doc);
	if (skyIndex < 0) {
		emit statusMessage(tr("A day cycle moves the sun of a Sun & sky light: add one first (Lighting, or a light of kind Sun & sky)."));
		return;
	}
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Day cycle video"));
	auto *root = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(tr("Makes a video of a day on the scene: the sun goes from sunrise to sunset (crossing where it is now at noon) and the sky and the light change with it, while the camera stays "
	                            "where it is. It renders every frame, so a long or large video takes a while; start with Draft."), &dialog);
	intro->setWordWrap(true);
	root->addWidget(intro);
	auto *form = new QFormLayout();
	root->addLayout(form);
	auto *seconds = new QSpinBox(&dialog);
	seconds->setRange(1, 120);
	seconds->setValue(6);
	seconds->setSuffix(tr(" s"));
	auto *fps = new QComboBox(&dialog);
	for (int f : {24, 30, 60}) fps->addItem(tr("%1 frames per second").arg(f), f);
	auto *quality = new QComboBox(&dialog);
	quality->addItem(tr("Draft: 480 pixels wide, 16 samples"));
	quality->addItem(tr("Good: 720 pixels wide, 64 samples"));
	quality->addItem(tr("Best: 960 pixels wide, 256 samples"));
	quality->addItem(tr("The scene's own size and samples (%1 x %2, %3)").arg(m_doc.render.width).arg(m_doc.render.height).arg(m_doc.render.samples));
	auto *noon = new QDoubleSpinBox(&dialog);
	noon->setRange(10.0, 90.0);
	noon->setDecimals(0);
	noon->setSuffix(tr(" degrees"));
	noon->setValue(std::clamp(std::round(m_doc.lights[static_cast<size_t>(skyIndex)].sky.sunElevation), 10.0, 90.0));
	noon->setToolTip(tr("How high the sun is at noon: about 65 in the summer in the middle latitudes, 25 in the winter."));
	auto *summary = new QLabel(&dialog);
	summary->setWordWrap(true);
	form->addRow(tr("Length:"), seconds);
	form->addRow(tr("Speed:"), fps);
	form->addRow(tr("Quality:"), quality);
	form->addRow(tr("Sun height at noon:"), noon);
	root->addWidget(summary);
	const auto refresh = [=]() { summary->setText(tr("%1 frames.").arg(seconds->value() * fps->currentData().toInt())); };
	connect(seconds, QOverload<int>::of(&QSpinBox::valueChanged), &dialog, refresh);
	connect(fps, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, refresh);
	refresh();
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Choose where to save..."));
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	root->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted) return;

	const QString start = (m_path.isEmpty() ? QDir::homePath() + "/daycycle" : QFileInfo(m_path).absolutePath() + "/" + QFileInfo(m_path).completeBaseName() + "-daycycle") + ".mp4";
	QString mp4 = QFileDialog::getSaveFileName(this, tr("Save the day cycle video"), start, tr("MP4 videos (*.mp4)"));
	if (mp4.isEmpty()) return;
	if (!mp4.endsWith(".mp4", Qt::CaseInsensitive)) mp4 += ".mp4";
	const DayQuality q = kDayQualities[std::clamp(quality->currentIndex(), 0, 3)];
	QString message;
	const bool ok = renderDayCycle(mp4, seconds->value() * fps->currentData().toInt(), fps->currentData().toInt(), q.width > 0 ? q.width : m_doc.render.width, q.samples > 0 ? q.samples : m_doc.render.samples,
	                               noon->value(), &message);
	if (!ok && !message.isEmpty()) QMessageBox::warning(this, tr("The day cycle failed"), message);
}

bool SceneBuilderWidget::renderDayCycle(const QString &outMp4, int frames, int fps, int width, int samples, double noonElevation, QString *message, std::vector<QImage> *framesOut) {
	const QString cat = QStringLiteral("builder-render");
	const auto fail = [&](const QString &msg, log_format::Level level = log_format::Level::Warn) {
		logLine(cat, QStringLiteral("day cycle: %1").arg(msg), level);
		m_previewStatus->setText(msg);
		if (message) *message = msg;
		return false;
	};
	flushEditLog();
	if (m_process || m_compositeRunning) return fail(tr("A render is already running."));
	if (frames < 1 || fps < 1 || width < 1) return fail(tr("A video needs at least one frame."));
	const int skyIndex = generatedSkyIndex(m_doc);
	if (skyIndex < 0) return fail(tr("A day cycle moves the sun of a Sun & sky light: add one first."));
	if (scene_doc::hasErrors(scene_doc::validate(m_doc))) return fail(tr("Fix the problems listed under the properties first."));
	if (!QFileInfo::exists(launcherPath())) return fail(tr("The renderer (%1) was not found next to the program.").arg(launcherPath()));

	const QString dir = workFolder() + "/daycycle";
	QDir(dir).removeRecursively();
	QDir().mkpath(dir);
	const double aspect = m_doc.render.width > 0 ? static_cast<double>(m_doc.render.height) / m_doc.render.width : 0.75;
	const int height = std::max(1, static_cast<int>(std::lround(width * aspect)));
	const double noonAzimuth = m_doc.lights[static_cast<size_t>(skyIndex)].sky.sunAzimuth;
	const bool gpu = m_gpuCheck && m_gpuCheck->isChecked();
	const bool denoise = m_denoiseCheck && m_denoiseCheck->isChecked();

	// Like Render into the photo: the Preview button reads Cancel, the status line says how far it is, and the window stays usable (the other render buttons are off).
	struct Running {
		SceneBuilderWidget *w;
		explicit Running(SceneBuilderWidget *widget) : w(widget) {
			w->m_compositeRunning = true;
			w->m_compositeCancel = false;
			w->m_previewButton->setText(SceneBuilderWidget::tr("Cancel"));
			w->updateActions();
			w->m_previewButton->setEnabled(true);
		}
		~Running() {
			w->m_compositeRunning = false;
			w->m_previewButton->setText(SceneBuilderWidget::tr("Preview"));
			w->updateActions();
		}
	} running(this);
	logLine(cat, QStringLiteral("day cycle: %1 frames at %2 fps, %3 x %4, %5 samples, sun %6 degrees high at noon, %7%8 -> %9")
	                 .arg(frames).arg(fps).arg(width).arg(height).arg(samples).arg(noonElevation, 0, 'f', 0).arg(gpu ? QStringLiteral("GPU") : QStringLiteral("CPU"), denoise ? QStringLiteral(", denoised") : QString(), outMp4));
	QElapsedTimer clock;
	clock.start();
	if (framesOut) framesOut->clear();
	for (int i = 0; i < frames; ++i) {
		if (m_compositeCancel) return fail(tr("The video was cancelled."), log_format::Level::Info);
		m_previewStatus->setText(tr("Day cycle: frame %1 of %2...").arg(i + 1).arg(frames));
		QCoreApplication::processEvents();
		// the scene at this moment: the sun of the Sun & sky moved, its sky picture and the Sun light made for it (into this video's own folder, which goes with it)
		scene_doc::Document frameDoc = m_doc;
		frameDoc.render.width = width;
		frameDoc.render.height = height;
		const day_cycle::Sun sun = day_cycle::sunOfFrame(i, frames, noonElevation, noonAzimuth);
		scene_doc::Light &sky = frameDoc.lights[static_cast<size_t>(skyIndex)];
		sky.sky.sunElevation = sun.elevation;
		sky.sky.sunAzimuth = sun.azimuth;
		std::string skyError;
		if (!scene_doc::sky::applySunAndSky(frameDoc.lights, static_cast<size_t>(skyIndex), dir.toStdString(), skyError)) return fail(tr("The sky for frame %1 could not be made: %2").arg(i + 1).arg(QString::fromStdString(skyError)));
		const QString scene = dir + "/scene.pbrt", ppm = dir + "/frame.ppm";
		QString writeError;
		if (!writeSceneFile(scene, &writeError, &frameDoc)) return fail(writeError);
		QFile::remove(ppm);
		QStringList args = {gpu ? "--gpu" : "--cpu"};
		if (denoise) args << "--denoise";
		args << "--output" << ppm << "--height" << QString::number(height) << QString::number(width) << QString::number(samples) << QString::number(m_doc.render.maxDepth) << scene;
		QProcess process;
		process.setProcessChannelMode(QProcess::MergedChannels);
		process.setWorkingDirectory(QCoreApplication::applicationDirPath());
		process.start(launcherPath(), args);
		QString tail;
		for (int waited = 0; process.state() != QProcess::NotRunning && waited < 20 * 60 * 1000 && !m_compositeCancel; waited += 50) {
			process.waitForFinished(50);
			tail = (tail + QString::fromLocal8Bit(process.readAll())).right(600);
			QCoreApplication::processEvents();
		}
		tail = (tail + QString::fromLocal8Bit(process.readAll())).right(600);
		if (process.state() != QProcess::NotRunning) {
			process.kill();
			process.waitForFinished(3000);
			return m_compositeCancel ? fail(tr("The video was cancelled."), log_format::Level::Info) : fail(tr("A frame took too long and was stopped."));
		}
		QImage picture(ppm);
		if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || picture.isNull())
			return fail(tr("The renderer could not make frame %1 (exit code %2). Its output:\n%3").arg(i + 1).arg(process.exitCode()).arg(tail.trimmed().right(400)), log_format::Level::Error);
		if (!picture.save(QStringLiteral("%1/enc_%2.png").arg(dir).arg(i, 4, 10, QLatin1Char('0')))) return fail(tr("Frame %1 could not be saved in %2.").arg(i + 1).arg(dir), log_format::Level::Error);
		if (framesOut) framesOut->push_back(picture);
		if (i == frames / 2) {   // the middle of the day, as a first look in the preview pane
			m_previewPixmap = QPixmap::fromImage(picture);
			updatePreviewPixmap();
		}
		logLine(cat, QStringLiteral("  frame %1 of %2: sun %3 degrees high, compass %4").arg(i + 1).arg(frames).arg(sun.elevation, 0, 'f', 1).arg(sun.azimuth, 0, 'f', 0));
	}
	// the frames, put together by the renderer
	m_previewStatus->setText(tr("Day cycle: putting the video together..."));
	QCoreApplication::processEvents();
	const QString made = dir + "/daycycle.mp4";
	QProcess assemble;
	assemble.setProcessChannelMode(QProcess::MergedChannels);
	assemble.setWorkingDirectory(QCoreApplication::applicationDirPath());
	assemble.start(launcherPath(), {"--assemble-video", dir, QString::number(frames), QString::number(fps), made});
	QString output;
	for (int waited = 0; assemble.state() != QProcess::NotRunning && waited < 10 * 60 * 1000 && !m_compositeCancel; waited += 50) {
		assemble.waitForFinished(50);
		output += QString::fromLocal8Bit(assemble.readAll());
		QCoreApplication::processEvents();
	}
	output += QString::fromLocal8Bit(assemble.readAll());
	if (assemble.state() != QProcess::NotRunning) {
		assemble.kill();
		assemble.waitForFinished(3000);
		return fail(m_compositeCancel ? tr("The video was cancelled.") : tr("Putting the video together took too long and was stopped."));
	}
	if (assemble.exitStatus() != QProcess::NormalExit || assemble.exitCode() != 0 || QFileInfo(made).size() == 0) {
		const bool noFfmpeg = output.contains(QStringLiteral("ffmpeg"), Qt::CaseInsensitive) && output.contains(QStringLiteral("Could not launch"));
		return fail(noFfmpeg ? tr("The frames were rendered, but putting them together needs the free program ffmpeg, which was not found. Install it (ffmpeg.org) and try again.")
		                     : tr("The frames were rendered but the video could not be put together (exit code %1).\n%2").arg(assemble.exitCode()).arg(output.trimmed().right(400)),
		            log_format::Level::Error);
	}
	QFile::remove(outMp4);
	if (!QFile::copy(made, outMp4)) return fail(tr("The video was made but could not be saved to %1.").arg(outMp4), log_format::Level::Error);
	QDir(dir).removeRecursively();   // the frames are large; the video has been copied out
	const QString done = tr("Day cycle done in %1 s. Saved %2.").arg(clock.elapsed() / 1000.0, 0, 'f', 1).arg(outMp4);
	logLine(cat, QStringLiteral("day cycle: %1").arg(done));
	m_previewStatus->setText(done);
	if (message) *message = done;
	return true;
}
