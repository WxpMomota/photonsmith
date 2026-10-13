#include "gpu_warmup.h"

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QStringList>
#include <QThread>
#include <QTimer>

#include "app_log.h"
#include "realtime_preview_session.h"

GpuWarmup::GpuWarmup(QObject *parent) : QObject(parent) {}

// Closing the window while a build runs: the niced child is stopped; the in-process thread cannot be stopped in the middle of Metal's compile, so it is let go (it frees itself when it
// finishes, and the process ends it at exit) instead of making the window wait up to two minutes.
GpuWarmup::~GpuWarmup() {
	if (m_process) {
		m_process->disconnect(this);
		if (m_process->state() != QProcess::NotRunning) {
			m_process->kill();
			m_process->waitForFinished(500);
		}
	}
	if (m_thread && m_thread->isRunning()) {
		m_thread->disconnect(this);
		m_thread->setParent(nullptr);
	}
}

void GpuWarmup::start() {
#ifdef Q_OS_MAC
	// (the "quitwarm" self-test is the one that wants it: it quits the window once the warm-up is over, which is when ~GpuWarmup once crashed)
	if (m_pending > 0 || (qEnvironmentVariableIsSet("RT_GUI_SELFTEST") && qEnvironmentVariable("RT_GUI_SELFTEST") != "quitwarm") || qEnvironmentVariableIsSet("RT_NO_GPU_WARMUP")) return;
	m_pending = 0;
	// 1. This process: the library Live Preview calls.
	if (RealtimePreviewSession::isAvailable()) {
		++m_pending;
		m_thread = QThread::create([] { RealtimePreviewSession::warmUp(); });
		m_thread->setParent(this);
		connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
		connect(m_thread, &QThread::finished, this, [this] { oneFinished(true); });
		m_thread->start(QThread::LowPriority);
	}
	// 2. The command line renderer the Render button starts, which has a cache of its own. Niced, so a CPU render started meanwhile loses almost nothing.
	const QString exe = QCoreApplication::applicationDirPath() + "/ray_tracer";
	if (QFile::exists(exe) && QFile::exists("/usr/bin/nice")) {
		++m_pending;
		m_process = new QProcess(this);
		connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
		        [this](int code, QProcess::ExitStatus status) { oneFinished(status == QProcess::NormalExit && code == 0); });
		connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart) oneFinished(false);
		});
		m_process->start("/usr/bin/nice", QStringList() << "-n" << "10" << exe << "--prepare-gpu");
	}
	if (m_pending == 0) return;
	QTimer::singleShot(2500, this, [this] {
		if (m_pending > 0 && !m_slow) {
			m_slow = true;
			emit slow();
		}
	});
#endif
}

void GpuWarmup::oneFinished(bool ok) {
	if (m_pending <= 0) return;
	m_ok = m_ok && ok;
	AppLog::info(QStringLiteral("gpu"), QStringLiteral("GPU renderer warm-up: one build %1 (%2 left)").arg(ok ? "finished" : "failed").arg(m_pending - 1));
	if (--m_pending == 0) emit done(m_ok, m_slow);
}
