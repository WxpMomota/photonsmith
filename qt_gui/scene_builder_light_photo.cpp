// scene_builder_light_photo.cpp -- the Scene Builder's "Match to a photo..." for a Sun & sky light (next to Sky gallery in the sky light's properties): a photo of an outdoor scene sets the
// sun's height and direction and the haze, so a rendered object is lit as the photographed scene was.
//
// The analysis is src/shared/light_from_photo.h (no model, nothing to install); this file loads the photo, turns the sun's direction in the photo into the Builder's compass (relative to
// where the Builder camera looks) and sets the light.
#include "scene_builder_widget.h"
#include "app_log.h"
#include "../src/shared/light_from_photo.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

bool SceneBuilderWidget::applyLightPhoto(int lightIndex, const QString &photo, double hfov, bool tiltGiven, double tilt, QString *error, QStringList *notes) {
	if (lightIndex < 0 || lightIndex >= static_cast<int>(m_doc.lights.size()) || m_doc.lights[static_cast<size_t>(lightIndex)].kind != scene_doc::LightKind::Infinite) {
		if (error) *error = tr("There is no sky light to set.");
		return false;
	}
	QImageReader reader(photo);
	reader.setAutoTransform(true);   // a phone photo stores its turn as a flag
	QImage image = reader.read();
	if (image.isNull()) {
		if (error) *error = tr("The photo %1 could not be read: %2").arg(photo, reader.errorString());
		return false;
	}
	image = image.convertToFormat(QImage::Format_RGB888);
	std::vector<unsigned char> rgb(static_cast<size_t>(image.width()) * image.height() * 3);
	for (int y = 0; y < image.height(); ++y) std::copy(image.constScanLine(y), image.constScanLine(y) + image.width() * 3, rgb.begin() + static_cast<std::ptrdiff_t>(y) * image.width() * 3);
	light_from_photo::Options options;
	options.hfovDegrees = hfov;
	options.tiltGiven = tiltGiven;
	options.tiltDegrees = tilt;
	const light_from_photo::Estimate found = light_from_photo::estimate(rgb.data(), image.width(), image.height(), options);
	if (!found.ok) {
		if (error) *error = QString::fromStdString(found.why);
		return false;
	}

	// The Builder's compass: azimuth 0 is +X, 90 is -Z, 180 is -X, 270 is +Z, growing counter-clockwise seen from above. The photo's sun is `found.sunAzimuth` degrees to the right of where
	// the photo looks; the Builder camera looks along its own heading, so the sun is that far to the right of it (to the right is clockwise seen from above: the compass number falls).
	const scene_doc::Camera &camera = m_doc.camera;
	const double forwardX = camera.target.x - camera.position.x, forwardZ = camera.target.z - camera.position.z;
	const double heading = (std::fabs(forwardX) + std::fabs(forwardZ) > 1e-9) ? std::atan2(-forwardZ, forwardX) * 180.0 / light_from_photo::kPi : 90.0;
	double azimuth = std::fmod(heading - found.sunAzimuth, 360.0);
	if (azimuth < 0.0) azimuth += 360.0;

	edit(QString(), [&]() {
		scene_doc::Light &sky = m_doc.lights[static_cast<size_t>(lightIndex)];
		if (!sky.physicalSky) {
			sky.physicalSky = true;
			sky.intensity = 1.0;   // (the generated sky is in real proportions: brightness 1 is a daylight scene)
		}
		sky.sky.sunElevation = std::round(found.sunElevation * 10.0) / 10.0;
		sky.sky.sunAzimuth = std::round(azimuth);
		sky.sky.turbidity = std::round(found.turbidity * 10.0) / 10.0;
	});
	QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
	AppLog::info(QStringLiteral("photo"), QStringLiteral("light from a photo: %1 (sun %2 deg high, %3 deg compass, haze %4, %5, confidence %6)")
	                                          .arg(photo)
	                                          .arg(found.sunElevation, 0, 'f', 1)
	                                          .arg(azimuth, 0, 'f', 0)
	                                          .arg(found.turbidity, 0, 'f', 1)
	                                          .arg(found.sunSeen ? QStringLiteral("sun seen") : found.flatSky ? QStringLiteral("overcast") : QStringLiteral("fitted"))
	                                          .arg(found.confidence, 0, 'f', 2));
	if (notes) {
		if (found.evenBlue) *notes << QString::fromStdString(found.why);
		else if (found.flatSky) *notes << tr("The sky is even (overcast), so the sun's direction cannot be told: it is set high and the haze to its most. Change Sun height and Sun direction by hand if you know them.");
		else if (found.sunSeen) *notes << tr("The sun is in the photo: its height and direction are read straight off it.");
		else *notes << tr("The sun is not in the photo: its height and direction are fitted to how the sky's brightness varies (confidence %1%).").arg(static_cast<int>(found.confidence * 100));
		if (found.horizonFound) *notes << tr("The camera was tilted %1 degrees (read off the horizon).").arg(found.tiltDegrees, 0, 'f', 1);
		if (!found.why.empty() && !found.flatSky) *notes << QString::fromStdString(found.why);
		*notes << tr("The sun's direction is set relative to where the Builder's camera looks now, as it was relative to where the photo looks. The photo's exposure is not known, so the sky's Brightness stays as it was.");
	}
	return true;
}

void SceneBuilderWidget::lightFromPhoto(int lightIndex) {
	if (lightIndex < 0 || lightIndex >= static_cast<int>(m_doc.lights.size())) return;
	const QString photo = QFileDialog::getOpenFileName(this, tr("Choose a photo with the sky in it"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp *.tga)"));
	if (photo.isEmpty()) return;

	QDialog dialog(this);
	dialog.setWindowTitle(tr("Light from a photo"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(tr("The sun's height and direction and the haze are read from the sky in the photo (a blue or white sky that reaches the top of the picture), and the Sun && sky "
	                            "settings are set to match. The photo should show some sky and, for the height, the horizon. Both settings below are optional."),
	                         &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *form = new QFormLayout();
	auto *fov = new QDoubleSpinBox(&dialog);
	fov->setRange(20.0, 140.0);
	fov->setDecimals(0);
	QImageReader shape(photo);
	shape.setAutoTransform(true);
	const QSize shown = shape.size();
	fov->setValue(shown.isValid() && shown.height() > shown.width() ? 51.0 : 65.0);   // (65 degrees is along a phone photo's long side: across a portrait one it is about 51)
	fov->setSuffix(tr(" degrees"));
	fov->setToolTip(tr("How wide the photo sees, left to right. A phone's main camera is about 65 to 70 degrees; a wide-angle one 100 or more."));
	form->addRow(tr("Photo's field of view (horizontal)"), fov);
	auto *tilt = new QDoubleSpinBox(&dialog);
	tilt->setRange(-60.0, 60.0);
	tilt->setDecimals(0);
	tilt->setSpecialValueText(tr("from the horizon"));
	tilt->setValue(-60.0);
	tilt->setToolTip(tr("How far the camera looked up (+) or down (-). Left at the lowest setting it is read off where the sky ends, which works when the horizon is in the photo and "
	                    "not hidden by buildings or trees."));
	form->addRow(tr("Camera tilt"), tilt);
	layout->addLayout(form);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted) return;

	QString error;
	QStringList notes;
	const bool tiltGiven = tilt->value() > tilt->minimum();
	if (!applyLightPhoto(lightIndex, photo, fov->value(), tiltGiven, tilt->value(), &error, &notes)) {
		QMessageBox::warning(this, tr("Could not read the light"), error);
		return;
	}
	QMessageBox::information(this, tr("Light from a photo"), tr("The sky was set from %1:\n\n- %2").arg(QFileInfo(photo).fileName(), notes.join("\n- ")));
}
