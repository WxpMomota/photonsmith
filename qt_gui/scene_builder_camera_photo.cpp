// scene_builder_camera_photo.cpp -- the Scene Builder's "Match to a photo..." for the camera (at the top of the camera's properties): the lens, the tilt and the roll of the camera that took a photo
// become the scene's camera, the picture takes the photo's shape, and the photo can be laid over the camera view to line the scene up against it.
//
// The analysis is src/shared/camera_from_photo.h (the lens from the photo's EXIF data, the tilt and roll from its horizon; no model, nothing to install); this file loads the photo, sets the
// camera and the picture's shape, keeps the photo for the camera view and (if asked) sets the Sun & sky from the same photo.
#include "scene_builder_widget.h"
#include "app_log.h"
#include "scene_3d_view.h"
#include "../src/shared/camera_from_photo.h"
#include "../src/shared/scene_arrange.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>

double SceneBuilderWidget::exifHorizontalFov(const QString &photo) {
	QImageReader reader(photo);
	reader.setAutoTransform(true);
	const QSize size = reader.size();   // (as it will be shown: turned upright)
	QFile file(photo);
	if (!size.isValid() || !file.open(QIODevice::ReadOnly)) return 0.0;
	const QByteArray head = file.read(1 << 18);   // (the EXIF block is at the start of the file)
	const double focal = camera_from_photo::exifFocalLength35mm(reinterpret_cast<const unsigned char *>(head.constData()), static_cast<size_t>(head.size()));
	return focal > 0.0 ? camera_from_photo::hfovFromFocal35(focal, size.width(), size.height()) : 0.0;
}

bool SceneBuilderWidget::applyCameraPhoto(const QString &photo, double hfov, double heightMetres, bool tiltGiven, double tilt, bool rollGiven, double roll, bool alsoSky, QString *error, QStringList *notes) {
	QImageReader reader(photo);
	reader.setAutoTransform(true);   // a phone photo stores its turn as a flag
	QImage image = reader.read();
	if (image.isNull()) {
		if (error) *error = tr("The photo %1 could not be read: %2").arg(photo, reader.errorString());
		return false;
	}
	image = image.convertToFormat(QImage::Format_RGB888);
	const int w = image.width(), h = image.height();
	bool fromExif = false;
	if (!(hfov > 0.0)) {
		hfov = exifHorizontalFov(photo);
		fromExif = hfov > 0.0;
		if (!fromExif) hfov = h > w ? 51.0 : 65.0;   // (a phone's 65 degrees along the long side)
	}
	std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
	for (int y = 0; y < h; ++y) std::copy(image.constScanLine(y), image.constScanLine(y) + w * 3, rgb.begin() + static_cast<std::ptrdiff_t>(y) * w * 3);
	camera_from_photo::Options options;
	options.hfovDegrees = hfov;
	options.tiltGiven = tiltGiven;
	options.tiltDegrees = tilt;
	options.rollGiven = rollGiven;
	options.rollDegrees = roll;
	const camera_from_photo::Estimate found = camera_from_photo::estimate(rgb.data(), w, h, options);
	if (!found.ok) {
		if (error) *error = QString::fromStdString(found.why);
		return false;
	}
	const scene_doc::Camera camera = camera_from_photo::makeCamera(m_doc.camera, hfov, w, h, found.tiltDegrees, found.rollDegrees, heightMetres);
	edit(QString(), [&]() {
		m_doc.camera = camera;
		m_doc.render.height = std::max(1, static_cast<int>(std::lround(m_doc.render.width * static_cast<double>(h) / w)));   // (the picture takes the photo's shape)
	});
	// the photo, kept for the camera view (at most 1600 pixels across: the view only needs it to line the scene up)
	m_cameraPhoto = std::max(w, h) > 1600 ? image.scaled(1600, 1600, Qt::KeepAspectRatio, Qt::SmoothTransformation) : image;
	m_cameraPhotoShown = true;
	updateCameraPhotoOverlay();
	show3dView(true);   // (the overlay is drawn by the 3D view, looking through the camera)
	lookThroughCamera(true);
	QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
	AppLog::info(QStringLiteral("photo"), QStringLiteral("camera from a photo: %1 (field of view %2 across%3, tilt %4, roll %5, %6)")
	                                          .arg(photo)
	                                          .arg(hfov, 0, 'f', 1)
	                                          .arg(fromExif ? QStringLiteral(" from EXIF") : QString())
	                                          .arg(found.tiltDegrees, 0, 'f', 1)
	                                          .arg(found.rollDegrees, 0, 'f', 1)
	                                          .arg(found.horizonFound ? QStringLiteral("horizon found") : QStringLiteral("horizon not used")));
	if (notes) {
		*notes << (fromExif ? tr("The lens was read from the photo's own data: %1 degrees across.").arg(hfov, 0, 'f', 1)
		                    : tr("The photo does not say what lens took it, so a phone's field of view was assumed (%1 degrees across); set it if you know it.").arg(hfov, 0, 'f', 0));
		if (found.horizonFound) *notes << tr("The horizon in the photo gave the camera's tilt (%1 degrees up) and roll (%2 degrees).").arg(found.tiltDegrees, 0, 'f', 1).arg(found.rollDegrees, 0, 'f', 1);
		if (!found.why.empty()) *notes << QString::fromStdString(found.why);
		*notes << tr("The camera keeps the direction it looked in, is %1 m above the ground, and the picture has the photo's shape. The photo is laid over the camera view (Camera section: Show the photo) to line your scene up against it.").arg(heightMetres, 0, 'f', 2);
	}
	if (alsoSky) {
		int sky = -1;
		for (size_t i = 0; i < m_doc.lights.size(); ++i)
			if (m_doc.lights[i].kind == scene_doc::LightKind::Infinite) { sky = static_cast<int>(i); break; }
		QString skyError;
		QStringList skyNotes;
		if (sky >= 0 && applyLightPhoto(sky, photo, hfov, true, found.tiltDegrees, &skyError, &skyNotes)) {
			if (notes) *notes << tr("The Sun & sky were set from the photo too: %1").arg(skyNotes.isEmpty() ? QString() : skyNotes.first());
		} else if (notes) {
			*notes << (sky < 0 ? tr("There is no sky light, so the sun was not set from the photo.") : tr("The sun was not set from the photo: %1").arg(skyError));
		}
	}
	return true;
}

bool SceneBuilderWidget::view3dShownForTest() const { return m_viewStack && m_view3d && m_viewStack->currentWidget() == m_view3d; }

void SceneBuilderWidget::updateCameraPhotoOverlay() {
	if (m_view3d) m_view3d->setPhotoOverlay(m_cameraPhotoShown ? m_cameraPhoto : QImage(), m_cameraPhotoOpacity);
}

void SceneBuilderWidget::cameraFromPhoto() {
	const QString photo = QFileDialog::getOpenFileName(this, tr("Choose the photo to match the camera to"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp *.tga)"));
	if (photo.isEmpty()) return;
	QImageReader shape(photo);
	shape.setAutoTransform(true);
	const QSize shown = shape.size();
	const double exif = exifHorizontalFov(photo);

	QDialog dialog(this);
	dialog.setWindowTitle(tr("Camera from a photo"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(tr("The camera takes the photo's lens (from the photo's own data when it has it), tilt and roll (read off the horizon: the lower edge of a blue or white sky), and the picture "
	                            "takes the photo's shape. The photo can then be laid over the camera view. All the settings below are optional."),
	                         &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *form = new QFormLayout();
	auto *fov = new QDoubleSpinBox(&dialog);
	fov->setRange(0.0, 140.0);
	fov->setDecimals(0);
	fov->setSuffix(tr(" degrees"));
	fov->setSpecialValueText(exif > 0.0 ? tr("from the photo (%1)").arg(exif, 0, 'f', 0) : tr("from the photo (not in it: a phone's)"));
	fov->setValue(0.0);
	fov->setToolTip(tr("How wide the photo sees, left to right. Left at the lowest setting it is read from the photo's own data when it has it, else a phone's is assumed."));
	form->addRow(tr("Photo's field of view (horizontal)"), fov);
	auto *height = new QDoubleSpinBox(&dialog);
	height->setRange(0.1, 100.0);
	height->setDecimals(2);
	height->setSingleStep(0.1);
	height->setSuffix(tr(" m"));
	height->setValue(shown.isValid() && std::fabs(m_doc.camera.position.y) > 0.1 ? std::clamp(m_doc.camera.position.y, 0.1, 100.0) : 1.6);
	height->setToolTip(tr("How high the camera was above the ground. Standing at eye level is about 1.6 m."));
	form->addRow(tr("Camera height above the ground"), height);
	auto *tilt = new QDoubleSpinBox(&dialog);
	tilt->setRange(-60.0, 60.0);
	tilt->setDecimals(1);
	tilt->setSpecialValueText(tr("from the horizon"));
	tilt->setValue(-60.0);
	tilt->setToolTip(tr("How far the camera looked up (+) or down (-). Left at the lowest setting it is read off the horizon."));
	form->addRow(tr("Camera tilt"), tilt);
	auto *rollSpin = new QDoubleSpinBox(&dialog);
	rollSpin->setRange(-45.0, 45.0);
	rollSpin->setDecimals(1);
	rollSpin->setSpecialValueText(tr("from the horizon"));
	rollSpin->setValue(-45.0);
	rollSpin->setToolTip(tr("How far the camera was rolled (+ counter-clockwise seen from behind it: the horizon then runs down to the right). Left at the lowest setting it is read off the horizon."));
	form->addRow(tr("Camera roll"), rollSpin);
	bool hasSky = false;
	for (const scene_doc::Light &l : m_doc.lights) hasSky = hasSky || l.kind == scene_doc::LightKind::Infinite;
	auto *sun = new QCheckBox(tr("Also set the sun and sky from the photo"), &dialog);
	sun->setChecked(hasSky);
	sun->setEnabled(hasSky);
	form->addRow(QString(), sun);
	layout->addLayout(form);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted) return;

	QString error;
	QStringList notes;
	if (!applyCameraPhoto(photo, fov->value(), height->value(), tilt->value() > tilt->minimum(), tilt->value(), rollSpin->value() > rollSpin->minimum(), rollSpin->value(), sun->isChecked(), &error, &notes)) {
		QMessageBox::warning(this, tr("Could not read the camera"), error);
		return;
	}
	QMessageBox::information(this, tr("Camera from a photo"), tr("The camera was set from %1:\n\n- %2").arg(QFileInfo(photo).fileName(), notes.join("\n- ")));
}

// ---- Place on the photo's ground ---------------------------------------------------------------------------------------------------------------------------------------------------------

void SceneBuilderWidget::placeByClicking() {
	if (pickedItems().empty()) {
		emit statusMessage(tr("Pick an object first (click it in the list or the view), then Place by clicking."));
		return;
	}
	show3dView(true);
	lookThroughCamera(true);   // (the click is on the picture the camera sees, which is the photo's when it was matched to one)
	m_view3d->beginPickPlace();
	emit statusMessage(tr("Click the ground in the picture to stand the picked object there. Click again to move it; Esc stops."));
}

bool SceneBuilderWidget::placePickedAt(const scene_doc::Float3 &where) {
	const scene_doc::ItemSet items = pickedItems();
	if (items.empty()) return false;
	bool placed = false;
	edit(QString(), [&]() { placed = scene_doc::placeOnGround(m_doc, items, where); });
	if (!placed) return false;
	refreshInspectorValues();
	const double tall = scene_doc::heightOfItems(m_doc, items);
	emit statusMessage(tr("Placed on the ground. It is %1 m tall; the camera is %2 m above the ground. Click again to move it, Esc stops, or set its real height.").arg(tall, 0, 'f', 2).arg(m_doc.camera.position.y, 0, 'f', 2));
	AppLog::info(QStringLiteral("photo"), QStringLiteral("placed on the ground at (%1, %2): %3 m tall").arg(where.x, 0, 'f', 2).arg(where.z, 0, 'f', 2).arg(tall, 0, 'f', 2));
	return true;
}

bool SceneBuilderWidget::setPickedHeight(double metres) {
	const scene_doc::ItemSet items = pickedItems();
	if (items.empty()) return false;
	bool done = false;
	edit(QString(), [&]() { done = scene_doc::setHeightOfItems(m_doc, items, metres); });
	if (done) refreshInspectorValues();
	return done;
}

void SceneBuilderWidget::setPickedHeightDialog() {
	const scene_doc::ItemSet items = pickedItems();
	const double now = scene_doc::heightOfItems(m_doc, items);
	if (items.empty() || !(now > 1e-9)) {
		emit statusMessage(tr("Pick an object first: its height is made what you say, about the middle of its base."));
		return;
	}
	bool ok = false;
	const double metres = QInputDialog::getDouble(this, tr("Real height"), tr("How tall is it in the photo (metres)? The picked object is scaled about the middle of its base."), now, 0.01, 1000.0, 2, &ok);
	if (!ok) return;
	if (!setPickedHeight(metres)) QMessageBox::warning(this, tr("Real height"), tr("It cannot be made that tall (the size may change by up to 100 times)."));
}

