#pragma once
// sample_photos.h -- the photos built into the program for trying the photo features (Match to a photo for the camera and for the Sun & sky, Render into the photo) without a photo of one's own.
//
// They are free-licence photos from Wikimedia Commons, scaled to 1280 pixels across (a change the licences ask to be said: see docs/PHOTO_TO_SCENE.md, "Sample photos"), and live in the program
// (qt_gui/sample_photos/, resources.qrc) so there is nothing to find or download and they work the same on every system. Picker is the part of those features' windows that chooses the photo:
// a picture of each sample (and of one's own photo, once chosen), what it is good for and its credit.
#include <QList>
#include <QString>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;

namespace sample_photos {

struct Sample {
	QString resource;   // ":/sample_photos/beach.jpg": a path every reader here (QImageReader, QFile, QImage) opens as it opens a file
	QString title;
	QString note;       // what the photo shows and what it is good for
	QString credit;     // author, licence and source, as the licences ask them to be given
};

QList<Sample> all();   // (built on each call, so the titles and notes follow the language)

// The photo chooser that sits inside a feature's own window (so choosing the photo, setting the options and seeing the result are one window): a row of pictures, the first sample selected, and
// "Choose my own photo..." for a file. photo() is the selected sample's resource path or the file chosen.
class Picker : public QWidget {
	Q_OBJECT
public:
	explicit Picker(const QString &ownPhotoTitle, QWidget *parent = nullptr);   // `ownPhotoTitle` heads the file dialog
	QString photo() const;
	void chooseOwnPhotoForTest(const QString &file) { useOwnPhoto(file); }

signals:
	void photoChanged(const QString &photo);   // the selected photo is another one (also set from code)
	void photoPicked(const QString &photo);    // the user chose a photo: a click (the selected one again too), a key, or one's own file

private:
	void showCredit();
	void useOwnPhoto(const QString &file);

	QString m_ownTitle;
	QList<Sample> m_samples;
	QListWidget *m_list = nullptr;
	QLabel *m_credit = nullptr;
	QListWidgetItem *m_own = nullptr;
};

}  // namespace sample_photos
