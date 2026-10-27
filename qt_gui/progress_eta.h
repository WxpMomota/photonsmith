#pragma once
// progress_eta.h -- the status line's "elapsed / rate / ETA" text during a render, from the progress samples of the last few seconds (MainWindow::onElapsedTick, once a second).
//
// Modelled on HandBrake's UpdateState() (libhb/sync.c), which is the
// most carefully-built ETA of the render/encode tools surveyed. Two
// separate rates are derived from the same samples:
//
//   * an INSTANTANEOUS rate over the short sliding window, which is
//     responsive enough to be worth showing, but far too noisy to
//     divide by; and
//   * a CUMULATIVE rate over the whole render, whose sensitivity to
//     new noise keeps shrinking, so the ETA drifts smoothly instead
//     of oscillating every tick.
//
// The ETA uses the cumulative rate for exactly that reason. Nothing is
// shown at all until kEtaWarmupMs has passed - an early estimate from
// two samples is worse than admitting we don't know yet, so the label
// reads "--:--" rather than a wild (or zero) guess.
//
// (The words are MainWindow's for translation - QCoreApplication::translate("MainWindow", ...) - as they were when this was part of it.)

#include <QChar>
#include <QCoreApplication>
#include <QString>

class ProgressEta {
  public:
	static constexpr int kProgressSamples = 4;     // ring depth (~3s window at 1Hz)
	static constexpr qint64 kEtaWarmupMs = 4000;   // no estimate before this

	void reset() {
		m_count = 0;
		for (Sample &sample : m_ring) sample = Sample{};
	}

	// Appends a sample and returns the formatted "elapsed / ETA" status text for the current tick.
	QString status(qint64 elapsedMs, int percent) {
		// Append this tick's sample, shifting the ring when it's full.
		if (m_count < kProgressSamples) {
			m_ring[m_count++] = Sample{elapsedMs, percent};
		} else {
			for (int i = 0; i < kProgressSamples - 1; ++i)
				m_ring[i] = m_ring[i + 1];
			m_ring[kProgressSamples - 1] = Sample{elapsedMs, percent};
		}

		QString text = QCoreApplication::translate("MainWindow", "Rendering  ·  %1%  ·  elapsed %2")
			.arg(percent, 3)
			.arg(formatDuration(elapsedMs / 1000));

		// Instantaneous rate across the ring - shown, never divided by.
		const Sample &oldest = m_ring[0];
		const qint64 windowMs = elapsedMs - oldest.elapsedMs;
		if (m_count == kProgressSamples && windowMs > 0) {
			const double pctPerSec = 1000.0 * (percent - oldest.percent) / windowMs;
			if (pctPerSec > 0.0)
				text += QCoreApplication::translate("MainWindow", "  ·  %1 %/s").arg(pctPerSec, 0, 'f', 1);
		}

		// ETA from the cumulative rate, suppressed during warm-up.
		if (elapsedMs >= kEtaWarmupMs && percent > 0 && percent < 100) {
			const double pctPerMs = double(percent) / double(elapsedMs);
			if (pctPerMs > 0.0) {
				const qint64 remainingMs = qint64((100.0 - percent) / pctPerMs);
				text += QCoreApplication::translate("MainWindow", "  ·  ETA %1").arg(formatDuration(remainingMs / 1000));
			}
		} else if (percent < 100) {
			text += QCoreApplication::translate("MainWindow", "  ·  ETA --:--");
		}

		return text;
	}

	// H:MM:SS once past an hour, else M:SS. Always at least M:SS so the field
	// width stays stable and the status line doesn't jitter as digits change -
	// the same reason HandBrake's status string uses fixed-width specifiers.
	static QString formatDuration(qint64 seconds) {
		if (seconds < 0) return QStringLiteral("--:--");
		const qint64 h = seconds / 3600;
		const qint64 m = (seconds % 3600) / 60;
		const qint64 s = seconds % 60;
		if (h > 0)
			return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
		return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
	}

  private:
	struct Sample {
		qint64 elapsedMs = 0;
		int percent = 0;
	};
	Sample m_ring[kProgressSamples];
	int m_count = 0;   // how many slots are actually filled
};
