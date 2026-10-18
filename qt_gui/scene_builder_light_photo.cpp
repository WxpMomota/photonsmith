// scene_builder_light_photo.cpp -- the Scene Builder's "Match to a photo..." for a Sun & sky light (next to Sky gallery in the sky light's properties): a photo of an outdoor scene sets the
// sun's height and direction and the haze, so a rendered object is lit as the photographed scene was.
//
// The analysis is src/shared/light_from_photo.h (no model, nothing to install); this file loads the photo, turns the sun's direction in the photo into the Builder's compass (relative to
// where the Builder camera looks) and sets the light.
#include "scene_builder_widget.h"
#include "app_log.h"
#include "photo_dialog.h"
#include "sample_photos.h"
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
	logLine(QStringLiteral("photo"), QStringLiteral("light from a photo: %1 (sun %2 deg high, %3 deg compass, haze %4, %5, confidence %6)")
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
	// One window for the whole thing: the photo to use (a sample or one's own), the settings, and then (photo_dialog::run) the result.
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Light from a photo"));
	dialog.setMinimumWidth(720);
	auto *layout = new QVBoxLayout(&dialog);
	auto *page = new QWidget(&dialog);
	auto *pageLayout = new QVBoxLayout(page);
	pageLayout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(page);
	auto *picker = new sample_photos::Picker(tr("Choose a photo with the sky in it"), &dialog);
	pageLayout->addWidget(picker);
	const QString intro = tr("The sun's height and direction and the haze are read from the sky in the photo (a blue or white sky that reaches the top of the picture), and the Sun && sky "
	                            "settings are set to match. The photo should show some sky and, for the height, the horizon. Both settings below are optional.").replace(QStringLiteral("&&"), QStringLiteral("&"));   // (shown in the result panel until there is a result: the settings get the room)
	auto *form = new QFormLayout();
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	auto *fov = new QDoubleSpinBox(&dialog);
	fov->setRange(20.0, 140.0);
	fov->setDecimals(0);
	// the field of view starts at the photo's own lens data when it has it, else a phone's (65 degrees along the long side of a phone photo, about 51 across a portrait one); each photo chosen sets it again
	const auto lensOfPhoto = [fov](const QString &chosen) {
		QImageReader shape(chosen);
		shape.setAutoTransform(true);
		const QSize shown = shape.size();
		const double fromExif = exifHorizontalFov(chosen);
		fov->setValue(fromExif > 0.0 ? std::clamp(std::round(fromExif), 20.0, 140.0) : shown.isValid() && shown.height() > shown.width() ? 51.0 : 65.0);
	};
	lensOfPhoto(picker->photo());
	connect(picker, &sample_photos::Picker::photoChanged, &dialog, lensOfPhoto);
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
	pageLayout->addLayout(form);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	layout->addWidget(buttons);
	const std::size_t baseline = undoDepth();   // (every apply takes back the last one first, so the window leaves one apply behind, not one for every click)
	photo_dialog::run(
	    dialog, page, buttons, intro + QStringLiteral("\n\n") + tr("Click a photo to use it: the sky is set from it at once, and again whenever you change a setting."),
	    [&](const std::function<void()> &schedule) {
		    connect(picker, &sample_photos::Picker::photoPicked, &dialog, schedule);
		    for (QDoubleSpinBox *spin : {fov, tilt}) connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &dialog, schedule);
	    },
	    [&](QString *result) {
		    undoTo(baseline);
		    const QString photo = picker->photo();
		    QString error;
		    QStringList notes;
		    const bool tiltGiven = tilt->value() > tilt->minimum();
		    if (!applyLightPhoto(lightIndex, photo, fov->value(), tiltGiven, tilt->value(), &error, &notes)) {
			    logLine(QStringLiteral("photo"), QStringLiteral("light from a photo failed: %1").arg(error), log_format::Level::Warn);
			    *result = tr("The sky could not be set from this photo: %1").arg(error);
			    return false;
		    }
		    for (const QString &note : notes) logLine(QStringLiteral("photo"), QStringLiteral("  %1").arg(note));
		    *result = tr("The sky was set from %1:\n\n- %2").arg(QFileInfo(photo).fileName(), notes.join("\n- "));
		    return true;
	    });
}
