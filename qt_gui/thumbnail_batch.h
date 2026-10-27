#pragma once
// thumbnail_batch.h -- what the main window keeps of a "Generate Thumbnails" run (mainwindow_thumbnails.cpp): its own progress bar and buttons, its counts and its clock.
// MainWindow::m_thumbnails.

#include <QDateTime>

class QProgressBar;
class QPushButton;
class ThumbnailGenerator;

struct ThumbnailBatch {
	// Hidden until the first "Generate Thumbnails" click, then shows live
	// progress ("Generating 3 of 12: Bouncing Spheres") via its own format
	// string (not the default "%p%") - see onThumbnailProgress(). Hidden
	// again once onThumbnailsAllDone() fires. Deliberately its own widget
	// rather than reusing m_progressBar (the real-render progress bar) -
	// see ThumbnailGenerator's own class comment for why the two must never
	// share chrome.
	QProgressBar *progressBar = nullptr;
	// Same show/hide lifecycle as progressBar - visible only
	// while a batch is running (onGenerateThumbnailsClicked()/
	// onThumbnailsAllDone()). Stop mirrors the real render's Stop button;
	// Pause toggles its own label between "Pause"/"Resume" the same way
	// m_pauseButton does, driven by onThumbnailPauseStateChanged() rather
	// than a locally tracked bool.
	QPushButton *pauseButton = nullptr;
	QPushButton *stopButton = nullptr;
	// Counts successes/failures across one "Generate Thumbnails" run, reset
	// at the start of onGenerateThumbnailsClicked() - onThumbnailsAllDone()
	// folds both into its finish summary (log line + status message).
	int succeeded = 0;
	int failed = 0;
	// Wall-clock start of one "Generate Thumbnails" run, set right before
	// ThumbnailGenerator::start() - onThumbnailsAllDone() logs the elapsed
	// time in its finish summary so a slow run (e.g. every scene falling
	// back to a software rasterizer) is visible without timestamp-
	// subtracting the log by hand. A QDateTime rather than a QElapsedTimer
	// so onThumbnailPauseStateChanged() can shift its origin forward by
	// however long a pause lasted, the same trick RenderController uses for
	// the totalTime it reports on a single render's completion - otherwise
	// a batch paused over lunch would report a multi-hour "elapsed" for
	// what was actually a few minutes of real work.
	QDateTime startTime;
	// Valid only while paused - see onThumbnailPauseStateChanged().
	QDateTime pauseStartedAt;
	// Lazily created (and reused across multiple "Generate Thumbnails"
	// clicks) by onGenerateThumbnailsClicked() the first time it's needed -
	// see that slot's own comment.
	ThumbnailGenerator *generator = nullptr;
};
