#pragma once
// photo_dialog.h -- the shape of the photo features' dialogs (Match to a photo for the camera and for the Sun & sky): the photo and the settings above, the result under them, in one window, and
// no Ok button: whatever is clicked or changed is applied at once (after a short pause, so typing a number is one apply), and the result is the panel's text.
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace photo_dialog {

// `dialog` has a vertical layout with `page` (the photo chooser and the settings) above `buttons` (a Close button). The settings go into a scroll area (so a short screen can still reach all of them), the
// result panel stands under it and the window is sized to the screen. `wire(schedule)` connects every widget whose change should apply (the photo row, each setting) to `schedule`; a pause of
// `delayMs` after the last call to it, `apply` runs and the text it gives in `result` (the result, or why it could not be done) is shown in the panel. Nothing is applied before the first call.
inline void run(QDialog &dialog, QWidget *page, QDialogButtonBox *buttons, const QString &hint, const std::function<void(const std::function<void()> &schedule)> &wire,
                const std::function<bool(QString *result)> &apply, int delayMs = 350) {
	auto *layout = qobject_cast<QVBoxLayout *>(dialog.layout());
	if (!layout) return;
	const int slot = layout->indexOf(page);
	layout->removeWidget(page);
	auto *settings = new QScrollArea(&dialog);
	settings->setWidgetResizable(true);
	settings->setFrameShape(QFrame::NoFrame);
	settings->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // (the settings wrap to the width they have)
	settings->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);   // (as big as the settings are, up to the screen: see below)
	settings->setWidget(page);
	auto *panel = new QScrollArea(&dialog);
	panel->setWidgetResizable(true);
	panel->setFrameShape(QFrame::NoFrame);
	panel->setMinimumHeight(170);
	panel->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto *text = new QLabel(hint, panel);
	text->setTextFormat(Qt::PlainText);   // (a photo's file name is in the result: text, never markup)
	text->setWordWrap(true);
	text->setTextInteractionFlags(Qt::TextSelectableByMouse);
	text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	text->setContentsMargins(10, 4, 4, 4);
	panel->setWidget(text);
	auto *row = new QVBoxLayout();
	row->addWidget(settings, 3);
	row->addWidget(panel, 2);   // (the result gets two fifths of whatever height the window has)
	layout->insertLayout(slot, row, 1);

	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto *pause = new QTimer(&dialog);
	pause->setSingleShot(true);
	pause->setInterval(delayMs);
	QObject::connect(pause, &QTimer::timeout, &dialog, [text, apply]() {
		QString result;
		apply(&result);
		text->setText(result);
	});
	wire([pause]() { pause->start(); });

	layout->activate();
	QSize want = dialog.sizeHint().expandedTo(dialog.size());
	if (const QScreen *screen = dialog.screen() ? dialog.screen() : QGuiApplication::primaryScreen()) {   // (the screen the window is on)   // (a window taller than the screen cannot be used: the settings scroll instead)
		const QRect free = screen->availableGeometry();
		want = want.expandedTo(QSize(free.width() * 66 / 100, free.height() * 92 / 100)).boundedTo(QSize(free.width() * 96 / 100, free.height() * 94 / 100));   // (tall: most of the screen's height)
		dialog.resize(want);
		dialog.move(free.center() - QPoint(want.width() / 2, want.height() / 2));
	} else {
		dialog.resize(want);
	}
	dialog.setSizeGripEnabled(true);
	dialog.exec();
}

}  // namespace photo_dialog
