#pragma once
// photo_dialog.h -- the shape of the photo features' dialogs (Match to a photo for the camera and for the Sun & sky): settings first, and when Ok has done its work the result, in the same window,
// instead of a second pop-up that has to be closed on top of the first.
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace photo_dialog {

// `dialog` has a vertical layout with `page` (the settings) above `buttons` (Ok and Cancel). Ok calls `apply`; when that returns true the page is replaced, in this same window, by the text it
// gave in `result` (scrolling when it is long) and the buttons by a single Close; when it returns false (it has already said why) the settings stay as they were. Cancel closes the window.
inline void run(QDialog &dialog, QWidget *page, QDialogButtonBox *buttons, const std::function<bool(QString *result)> &apply) {
	auto *layout = qobject_cast<QVBoxLayout *>(dialog.layout());
	if (!layout) return;
	auto *scroll = new QScrollArea(&dialog);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->hide();
	auto *text = new QLabel(scroll);
	text->setWordWrap(true);
	text->setTextInteractionFlags(Qt::TextSelectableByMouse);
	text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	scroll->setWidget(text);
	layout->insertWidget(layout->indexOf(buttons), scroll, 1);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, page, buttons, scroll, text, apply]() {
		QString result;
		if (!apply(&result)) return;   // (it said why: the settings stay, to be changed)
		page->hide();
		text->setText(result);
		const int width = std::max(dialog.width(), 520) - 40;
		scroll->setFixedHeight(std::min(text->heightForWidth(width) + 12, 460));
		scroll->show();
		buttons->clear();
		auto *close = buttons->addButton(QDialogButtonBox::Close);
		close->setDefault(true);
		dialog.setMinimumWidth(std::max(dialog.width(), 520));
		dialog.adjustSize();
	});
	layout->activate();
	dialog.resize(dialog.sizeHint().expandedTo(dialog.size()));   // (sized for the wrapped texts before it is shown, or the rows overlap)
	dialog.exec();
}

}  // namespace photo_dialog
