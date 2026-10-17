// sample_photos.cpp -- the sample photos and the picker (sample_photos.h).
#include "sample_photos.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace sample_photos {

QList<Sample> all() {
	// (the credits are not translated: they are names and licences)
	return {
	    {QStringLiteral(":/sample_photos/beach.jpg"), QCoreApplication::translate("sample_photos", "Beach, blue sky"),
	     QCoreApplication::translate("sample_photos", "A clean horizon and a clear sky: the camera, the sun and the exposure all read well from it. The best one to start with."),
	     QStringLiteral("Dietmar Rabich, \"Juist, Strand -- 2014 -- 3658\", CC BY-SA 4.0, commons.wikimedia.org/wiki/File:Juist,_Strand_--_2014_--_3658.jpg; scaled to 1280 pixels across")},
	    {QStringLiteral(":/sample_photos/north-sea.jpg"), QCoreApplication::translate("sample_photos", "North Sea beach"),
	     QCoreApplication::translate("sample_photos", "A beach under a blue sky with the sea behind it: a straight horizon."),
	     QStringLiteral("Dietmar Rabich, \"Norderney, Nordstrand -- 2016 -- 5172\", CC BY-SA 4.0, commons.wikimedia.org/wiki/File:Norderney,_Nordstrand_--_2016_--_5172.jpg; scaled to 1280 pixels across")},
	    {QStringLiteral(":/sample_photos/heath.jpg"), QCoreApplication::translate("sample_photos", "Heath under clouds"),
	     QCoreApplication::translate("sample_photos", "Open ground under a cloudy sky: the sky is mostly cloud, so the objects' exposure is left for you to set."),
	     QStringLiteral("Dietmar Rabich, \"Vreden, Zwillbrocker Venn -- 2016 -- 4122\", CC BY-SA 4.0, commons.wikimedia.org/wiki/File:Vreden,_Zwillbrocker_Venn_--_2016_--_4122.jpg; scaled to 1280 pixels across")},
	    {QStringLiteral(":/sample_photos/meadow.jpg"), QCoreApplication::translate("sample_photos", "Meadow, partly cloudy"),
	     QCoreApplication::translate("sample_photos", "A meadow with hills on the horizon: the sky's lower edge is not a clean line, so check the camera's tilt by eye."),
	     QStringLiteral("vastateparksstaff, \"Touch the Sky at Sky Meadows\", CC BY 2.0, commons.wikimedia.org/wiki/File:Touch_the_Sky_at_Sky_Meadows_(32708244180).jpg; scaled to 1280 pixels across")},
	    {QStringLiteral(":/sample_photos/overcast.jpg"), QCoreApplication::translate("sample_photos", "Overcast hills"),
	     QCoreApplication::translate("sample_photos", "A grey sky with no sun: the light comes out as an overcast sky."),
	     QStringLiteral("paul morris oldskool2016, \"Overcast sky over Snowdonia\", CC0 (public domain), commons.wikimedia.org/wiki/File:Overcast_sky_over_Snowdonia_(Unsplash).jpg; scaled to 1280 pixels across")},
	};
}

QString choose(QWidget *parent, const QString &title) {
	QDialog dialog(parent);
	dialog.setWindowTitle(title);
	auto *layout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(QCoreApplication::translate("sample_photos", "Try a sample photo, or use one of your own."), &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *list = new QListWidget(&dialog);
	list->setIconSize(QSize(160, 96));
	list->setMinimumSize(520, 330);
	const QList<Sample> samples = all();
	for (const Sample &s : samples) {
		auto *item = new QListWidgetItem(QIcon(QPixmap(s.resource).scaled(list->iconSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation)), s.title, list);
		item->setData(Qt::UserRole, s.resource);
		item->setSizeHint(QSize(480, 108));
		QFont big = list->font();
		big.setPointSize(big.pointSize() + 2);
		item->setFont(big);
	}
	list->setCurrentRow(0);
	layout->addWidget(list);
	auto *credit = new QLabel(&dialog);
	credit->setWordWrap(true);
	credit->setTextInteractionFlags(Qt::TextSelectableByMouse);
	credit->setMinimumHeight(120);
	credit->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	layout->addWidget(credit);
	auto showCredit = [&]() {
		const int row = list->currentRow();
		credit->setText(row >= 0 && row < samples.size() ? samples[row].note + QStringLiteral("\n\n") + QCoreApplication::translate("sample_photos", "Photo: %1").arg(samples[row].credit) : QString());
	};
	QObject::connect(list, &QListWidget::currentRowChanged, &dialog, showCredit);
	showCredit();
	auto *buttons = new QDialogButtonBox(&dialog);
	auto *use = buttons->addButton(QCoreApplication::translate("sample_photos", "Use this photo"), QDialogButtonBox::AcceptRole);
	auto *own = buttons->addButton(QCoreApplication::translate("sample_photos", "Choose my own photo..."), QDialogButtonBox::ActionRole);
	buttons->addButton(QDialogButtonBox::Cancel);
	use->setDefault(true);
	layout->addWidget(buttons);
	QString result;
	QObject::connect(use, &QPushButton::clicked, &dialog, [&]() {
		if (list->currentItem()) result = list->currentItem()->data(Qt::UserRole).toString();
		dialog.accept();
	});
	QObject::connect(list, &QListWidget::itemDoubleClicked, use, &QPushButton::click);
	QObject::connect(own, &QPushButton::clicked, &dialog, [&]() {
		const QString file = QFileDialog::getOpenFileName(&dialog, title, QString(), QCoreApplication::translate("sample_photos", "Images (*.png *.jpg *.jpeg *.bmp *.tga)"));
		if (file.isEmpty()) return;
		result = file;
		dialog.accept();
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialog.exec();
	return result;
}

}  // namespace sample_photos
