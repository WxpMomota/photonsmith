#pragma once
// photo_dialog.h -- the shape of the photo features' dialogs (Match to a photo for the camera and for the Sun & sky): the settings on the left and, when Ok has done its work, the result beside them in
// the same window, so the settings are still there to change and apply again. No second pop-up to close on top of the first.
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace photo_dialog {

// `dialog` has a vertical layout with `page` (the settings) above `buttons` (Ok and Cancel). The settings go into a scroll area (so a short screen can still reach all of them) and the window is
// sized to the screen. Ok calls `apply`; when that returns true the text it gave in `result` appears in a panel beside the settings, Ok now reads "Apply again" (the settings are still there to
// change) and Cancel reads "Close"; when it returns false (it has already said why) nothing else changes. Cancel and Close close the window.
inline void run(QDialog &dialog, QWidget *page, QDialogButtonBox *buttons, const std::function<bool(QString *result)> &apply) {
	auto *layout = qobject_cast<QVBoxLayout *>(dialog.layout());
	if (!layout) return;
	// the settings in a scroll area, the result panel beside it (hidden until there is a result)
	const int slot = layout->indexOf(page);
	layout->removeWidget(page);
	auto *settings = new QScrollArea(&dialog);
	settings->setWidgetResizable(true);
	settings->setFrameShape(QFrame::NoFrame);
	settings->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);   // (as big as the settings are, up to the screen: see below)
	settings->setWidget(page);
	auto *panel = new QScrollArea(&dialog);
	panel->setWidgetResizable(true);
	panel->setFrameShape(QFrame::NoFrame);
	panel->setFixedWidth(420);
	panel->hide();
	auto *text = new QLabel(panel);
	text->setWordWrap(true);
	text->setTextInteractionFlags(Qt::TextSelectableByMouse);
	text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	text->setContentsMargins(10, 4, 4, 4);
	panel->setWidget(text);
	auto *row = new QHBoxLayout();
	row->addWidget(settings, 1);
	row->addWidget(panel);
	layout->insertLayout(slot, row, 1);

	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, buttons, panel, text, apply]() {
		QString result;
		if (!apply(&result)) return;   // (it said why: the settings stay, to be changed)
		text->setText(result);
		const bool first = !panel->isVisible();
		panel->show();
		if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) ok->setText(QCoreApplication::translate("photo_dialog", "Apply again"));
		if (QPushButton *close = buttons->button(QDialogButtonBox::Cancel)) close->setText(QCoreApplication::translate("photo_dialog", "Close"));
		if (first) dialog.resize(dialog.width() + panel->width() + 12, dialog.height());   // (the panel appears beside the settings, which keep their size)
	});

	layout->activate();
	QSize want = dialog.sizeHint().expandedTo(dialog.size());
	if (const QScreen *screen = QGuiApplication::primaryScreen()) {   // (a window taller than the screen cannot be used: the settings scroll instead)
		const QRect free = screen->availableGeometry();
		want = want.boundedTo(QSize(free.width() * 95 / 100, free.height() * 90 / 100));
	}
	dialog.resize(want);
	dialog.exec();
}

}  // namespace photo_dialog
