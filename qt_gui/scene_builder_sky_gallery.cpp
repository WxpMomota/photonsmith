// scene_builder_sky_gallery.cpp -- see the header.
#include "scene_builder_sky_gallery.h"
#include "../src/shared/scene_sky.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QImage>
#include <QListWidget>
#include <QPixmap>
#include <QVBoxLayout>

namespace {

constexpr int kThumbW = 192, kThumbH = 96;

// One thumbnail per preset, made once (the Hosek sky at 192 x 96 is quick, but the window opens often).
QImage thumbnailOf(size_t index) {
	static std::vector<QImage> cache(scene_doc::sky::presets().size());
	if (cache[index].isNull()) {
		const scene_doc::sky::SkyPreset &preset = scene_doc::sky::presets()[index];
		scene_doc::SkyParams p;
		scene_doc::sky::applyPreset(p, preset);
		const std::vector<unsigned char> px = scene_doc::sky::thumbnail(p, kThumbW, kThumbH);
		QImage img(px.data(), kThumbW, kThumbH, kThumbW * 3, QImage::Format_RGB888);
		cache[index] = img.copy();
	}
	return cache[index];
}

}  // namespace

int chooseSkyPreset(QWidget *parent, int current) {
	QDialog dialog(parent);
	dialog.setWindowTitle(QCoreApplication::translate("SkyGallery", "Sky gallery"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *list = new QListWidget;
	list->setViewMode(QListView::IconMode);
	list->setIconSize(QSize(kThumbW, kThumbH));
	list->setResizeMode(QListView::Adjust);
	list->setMovement(QListView::Static);
	list->setWordWrap(true);
	list->setSpacing(8);
	list->setMinimumSize(QSize(3 * (kThumbW + 28) + 24, 3 * (kThumbH + 52)));
	const auto &presets = scene_doc::sky::presets();
	for (size_t i = 0; i < presets.size(); ++i) {
		auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(thumbnailOf(i))), QCoreApplication::translate("SkyGallery", presets[i].name));
		item->setData(Qt::UserRole, static_cast<int>(i));
		item->setSizeHint(QSize(kThumbW + 20, kThumbH + 44));
		list->addItem(item);
	}
	if (current >= 0 && current < list->count()) list->setCurrentRow(current);
	layout->addWidget(list);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted || !list->currentItem()) return -1;
	return list->currentItem()->data(Qt::UserRole).toInt();
}
