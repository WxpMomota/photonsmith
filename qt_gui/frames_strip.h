#pragma once
// frames_strip.h -- the frames a video has rendered so far, as a strip of small pictures under the Progress tab's progress bar.
//
// A video render writes each finished frame as a numbered picture (<output folder>/frames/enc_0000.png, enc_0001.png ... : the launcher's PNG converter, whole or not at all) while the
// later ones are still rendering. The strip lists that folder as the render goes: it loads the new pictures a few at a time (so a resumed video with hundreds of finished frames does not
// freeze the window), puts each in its place by frame number, keeps only the latest `limit` (a film has thousands) and scrolls to the newest while you are looking at the end of the
// strip. Double-clicking a picture is for the window to act on (it opens the file).

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QListWidget>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QStringList>

class FramesStrip : public QListWidget {
  public:
	static constexpr int kThumbWidth = 112, kThumbHeight = 63;

	explicit FramesStrip(QWidget *parent = nullptr) : QListWidget(parent) {
		setObjectName("framesStrip");
		setViewMode(QListView::IconMode);
		setFlow(QListView::LeftToRight);
		setWrapping(false);
		setMovement(QListView::Static);
		setResizeMode(QListView::Adjust);
		setUniformItemSizes(true);
		setSelectionMode(QAbstractItemView::SingleSelection);
		setIconSize(QSize(kThumbWidth, kThumbHeight));
		setSpacing(4);
		setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		setFixedHeight(kThumbHeight + 56);
		hide();
	}

	// Starts listing `dir` (the frames folder of a render that is about to start or has started): clears the strip and shows it.
	void start(const QString &dir, int limit = 300) {
		clear();
		seen_.clear();
		dir_ = dir;
		limit_ = limit;
		pictures_ = 0;
		show();
	}

	// How many finished frames the folder holds (the whole count, not just the ones the strip shows).
	int pictures() const { return pictures_; }
	const QString &folder() const { return dir_; }

	// Loads up to `maxNew` pictures that are not in the strip yet; returns how many it loaded. Call it every second or so while the video renders, and once more when it is done (with a big
	// `maxNew`).
	int refresh(int maxNew = 12) {
		if (dir_.isEmpty()) return 0;
		static const QRegularExpression name("^enc_(\\d+)\\.png$");
		QStringList files;               // the finished frames in the folder, in order
		QHash<QString, qint64> written;  // and when each was written
		for (const QFileInfo &info : QDir(dir_).entryInfoList(QStringList() << "enc_*.png", QDir::Files, QDir::Name)) {
			if (!name.match(info.fileName()).hasMatch()) continue;   // (a picture being written has another name: enc_0007.png.tmp.png)
			files << info.fileName();
			written.insert(info.fileName(), info.lastModified().toMSecsSinceEpoch());
		}
		// A picture that is gone, or was written again since it was loaded, leaves the strip: a video that starts afresh over an older one's frames (other settings) has its folder
		// emptied by the renderer only after the strip has started looking, and its frame 1 is a new enc_0000.png under the old name. (The old frames stayed in the strip for the
		// whole render, 2026-10-28.)
		for (int row = count_() - 1; row >= 0; --row) {
			const QString file = QFileInfo(pathOf(item(row))).fileName();
			if (written.value(file, -1) != seen_.value(file, -2)) delete takeItem(row);
		}
		for (auto it = seen_.begin(); it != seen_.end();) it = written.value(it.key(), -1) == it.value() ? std::next(it) : seen_.erase(it);
		QStringList fresh;   // file names not seen yet, in order
		for (const QString &file : files)
			if (!seen_.contains(file)) fresh << file;
		pictures_ = files.size();
		// a resumed video can have hundreds of finished frames at once: only the newest `limit_` are shown
		while (fresh.size() > limit_) {
			const QString skipped = fresh.takeFirst();
			seen_.insert(skipped, written.value(skipped));
		}
		QScrollBar *bar = horizontalScrollBar();
		const bool atEnd = bar->value() >= bar->maximum() - 2;
		int loaded = 0;
		for (const QString &file : fresh) {
			if (loaded >= maxNew) break;
			const QString path = QDir(dir_).filePath(file);
			QImage image(path);
			if (image.isNull()) continue;   // (not readable yet: tried again next time)
			seen_.insert(file, written.value(file));
			const int index = name.match(file).captured(1).toInt();
			auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(image.scaled(kThumbWidth, kThumbHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation))), QString::number(index + 1));
			item->setData(Qt::UserRole, index);
			item->setData(Qt::UserRole + 1, path);
			item->setToolTip(tr("Frame %1 - double-click to open it").arg(index + 1));
			item->setTextAlignment(Qt::AlignHCenter);
			// in its place by frame number (a frame that was repeated after a failure can arrive late)
			int row = count_();
			while (row > 0 && item->data(Qt::UserRole).toInt() < this->item(row - 1)->data(Qt::UserRole).toInt()) --row;
			insertItem(row, item);
			++loaded;
		}
		while (count_() > limit_) delete takeItem(0);
		if (loaded > 0 && atEnd) scrollToItem(this->item(count_() - 1), QAbstractItemView::EnsureVisible);
		return loaded;
	}

	// The folder (beside the video's output file) a video's frames are in: `frames`, or for a cartoon `frames-<key>` (launcher/video_render.h), from the video's camera path.
	static QString folderNameFor(const QString &cameraPath) { return cameraPath.startsWith(QStringLiteral("cartoon:")) ? "frames-" + cameraPath.mid(8) : QStringLiteral("frames"); }

	static QString pathOf(const QListWidgetItem *item) { return item ? item->data(Qt::UserRole + 1).toString() : QString(); }

  private:
	int count_() const { return QListWidget::count(); }

	QString dir_;
	QHash<QString, qint64> seen_;   // the pictures loaded (or passed over for the limit), with when each was written
	int limit_ = 300;
	int pictures_ = 0;
};
