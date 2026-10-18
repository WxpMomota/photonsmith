// sample_photos.cpp -- the sample photos and the photo chooser (sample_photos.h).
#include "sample_photos.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QImageReader>
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

Picker::Picker(const QString &ownPhotoTitle, QWidget *parent) : QWidget(parent), m_ownTitle(ownPhotoTitle), m_samples(all()) {
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	auto *heading = new QLabel(QCoreApplication::translate("sample_photos", "Try a sample photo, or use one of your own."), this);
	heading->setWordWrap(true);
	layout->addWidget(heading);

	m_list = new QListWidget(this);
	m_list->setViewMode(QListView::IconMode);
	m_list->setFlow(QListView::LeftToRight);
	m_list->setWrapping(true);
	m_list->setMovement(QListView::Static);
	m_list->setResizeMode(QListView::Adjust);
	m_list->setIconSize(QSize(96, 60));
	m_list->setGridSize(QSize(110, 116));
	m_list->setSpacing(2);
	m_list->setFixedHeight(156);
	m_list->setWordWrap(true);   // (a title that does not fit goes on a second line)
	for (const Sample &s : m_samples) {
		auto *item = new QListWidgetItem(QIcon(QPixmap(s.resource).scaled(m_list->iconSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation)), s.title, m_list);
		item->setData(Qt::UserRole, s.resource);
		item->setToolTip(s.title);
		item->setTextAlignment(Qt::AlignHCenter);
	}
	m_list->setCurrentRow(0);
	layout->addWidget(m_list);

	m_credit = new QLabel(this);
	m_credit->setWordWrap(true);
	m_credit->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_credit->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	m_credit->setMinimumHeight(120);
	layout->addWidget(m_credit);

	auto *own = new QPushButton(QCoreApplication::translate("sample_photos", "Choose my own photo..."), this);
	own->setAutoDefault(false);
	auto *row = new QHBoxLayout();
	row->addWidget(own);
	row->addStretch(1);
	layout->addLayout(row);

	connect(m_list, &QListWidget::currentRowChanged, this, [this](int) {
		showCredit();
		emit photoChanged(photo());
		emit photoPicked(photo());
	});
	connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *) { emit photoPicked(photo()); });   // (a click on the one already selected too)
	connect(own, &QPushButton::clicked, this, [this]() {
		const QString file = QFileDialog::getOpenFileName(this, m_ownTitle, QString(), QCoreApplication::translate("sample_photos", "Images (*.png *.jpg *.jpeg *.bmp *.tga)"));
		if (!file.isEmpty()) useOwnPhoto(file);
	});
	showCredit();
}

QString Picker::photo() const { return m_list && m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString(); }

void Picker::showCredit() {
	const int row = m_list->currentRow();
	if (row >= 0 && row < m_samples.size()) m_credit->setText(m_samples[row].note + QStringLiteral("\n\n") + QCoreApplication::translate("sample_photos", "Photo: %1").arg(m_samples[row].credit));
	else if (m_list->currentItem() == m_own && m_own) m_credit->setText(QCoreApplication::translate("sample_photos", "Your photo: %1").arg(QDir::toNativeSeparators(m_own->data(Qt::UserRole).toString())));
	else m_credit->clear();
}

// A photo of the user's own goes in the row after the samples (one place, replaced by the next one chosen) and is selected.
void Picker::useOwnPhoto(const QString &file) {
	QImageReader reader(file);
	reader.setAutoTransform(true);
	const QSize size = reader.size();
	QPixmap picture;
	if (size.isValid()) {
		reader.setScaledSize(size.scaled(m_list->iconSize(), Qt::KeepAspectRatio));
		picture = QPixmap::fromImage(reader.read());
	}
	if (!m_own) {
		m_own = new QListWidgetItem(m_list);
		m_own->setTextAlignment(Qt::AlignHCenter);
	}
	m_own->setIcon(QIcon(picture));
	m_own->setText(QCoreApplication::translate("sample_photos", "My photo"));
	m_own->setData(Qt::UserRole, file);
	m_own->setToolTip(file);
	const bool wasSelected = m_list->currentItem() == m_own;
	m_list->setCurrentItem(m_own);   // (currentRowChanged shows its credit and says the photo changed)
	if (wasSelected) {               // (the same row, a different file: no row change to tell it)
		showCredit();
		emit photoChanged(file);
		emit photoPicked(file);
	}
}

}  // namespace sample_photos
