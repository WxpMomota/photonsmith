#pragma once
// sample_photos.h -- the photos built into the program for trying the photo features (Match to a photo for the camera and for the Sun & sky, Render into the photo) without a photo of one's own.
//
// They are free-licence photos from Wikimedia Commons, scaled to 1280 pixels across (a change the licences ask to be said: see docs/PHOTO_TO_SCENE.md, "Sample photos"), and live in the program
// (qt_gui/sample_photos/, resources.qrc) so there is nothing to find or download and they work the same on every system. choose() is the picker both features open instead of a bare file dialog.
#include <QList>
#include <QString>
#include <QWidget>

namespace sample_photos {

struct Sample {
	QString resource;   // ":/sample_photos/beach.jpg": a path every reader here (QImageReader, QFile, QImage) opens as it opens a file
	QString title;
	QString note;       // what the photo shows and what it is good for
	QString credit;     // author, licence and source, as the licences ask them to be given
};

QList<Sample> all();   // (built on each call, so the titles and notes follow the language)

// Asks which photo to use: one of the samples (with a picture of each and its credit) or "Choose my own photo...", which opens a file dialog. Returns the sample's resource path or the file the user
// chose; empty when cancelled. `title` is the window's title and what the file dialog is headed.
QString choose(QWidget *parent, const QString &title);

}  // namespace sample_photos
