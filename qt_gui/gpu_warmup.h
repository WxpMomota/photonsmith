#ifndef GPU_WARMUP_H
#define GPU_WARMUP_H
// gpu_warmup.h -- builds the GPU renderer in the background when the GUI starts: Metal's on a Mac, the default OptiX renderer's shaders on Windows (nothing happens elsewhere).
//
// Windows: OptiX compiles the default renderer's shaders the first time (about 20 minutes on a cold cache, after the first run on a computer or an update of the shaders) and keeps them in its
// cache; `ray_tracer.exe --prepare-gpu` at a low priority does that compile at the start of the GUI instead of at the first render, and a warm start finishes in a second, silently.
//
// Metal compiles the whole path tracer into machine code for this GPU the first time an app asks, about a minute and a half on an M2, and keeps the result for that app. Until then the
// first GPU render or Live Preview waits with nothing on screen to say why. This does the wait early and out of sight: a low-priority thread builds the pipeline in this process (what
// Live Preview, which runs in the GUI's own process, will reuse) and `ray_tracer --prepare-gpu` does the same in its own process (what the GUI's Render button starts). When it takes
// longer than a moment, `slow()` says so, and `done()` says it is over. Every later start finds the cache full and finishes in about a second, silently.
//
// RT_NO_GPU_WARMUP=1 turns it off, and the GUI self-test (RT_GUI_SELFTEST) never starts it.

#include <QObject>
#include <QPointer>
#include <QThread>

class QProcess;

class GpuWarmup : public QObject {
	Q_OBJECT
public:
	explicit GpuWarmup(QObject *parent = nullptr);
	~GpuWarmup() override;
	void start();
	bool running() const { return m_pending > 0; }

signals:
	void slow();                       // still building after a moment: the first run on this Mac
	void done(bool ok, bool wasSlow);  // both builds finished

private:
	void oneFinished(bool ok);

	int m_pending = 0;
	bool m_ok = true;
	bool m_slow = false;
	QProcess *m_process = nullptr;
	QPointer<QThread> m_thread;   // a guarded pointer: the thread deletes itself when it finishes, long before this object goes
};

#endif  // GPU_WARMUP_H
