// Saved camera views in the Scene Builder: look through the scene's camera, set the camera from the 3D view, and name cameras to come back to. The operations on the
// document are src/shared/scene_camera_views.h (one call = one undo step); this file is how the 3D view's bar and the camera's properties reach them.
#include "scene_builder_widget.h"

#include "app_log.h"
#include "flow_layout.h"
#include "scene_3d_view.h"
#include "scene_builder_common.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QImage>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <cmath>

using namespace scene_builder_ui;

using scene_doc::Camera;
using scene_doc::Float3;

namespace {

Float3 toFloat3(const scene_view::V3 &v) { return {v.x, v.y, v.z}; }

}  // namespace

// The bar under the Move / Rotate / Scale tools of the 3D view: look through the camera, take the camera from the view, and the saved views.
void SceneBuilderWidget::createCameraBar(QWidget *layoutBox) {
	m_cameraBar = new QWidget(layoutBox);
	auto *flow = new FlowLayout(m_cameraBar, 0, 6, 6);
	m_throughButton = new QPushButton(tr("Through camera"), m_cameraBar);
	m_throughButton->setCheckable(true);
	m_throughButton->setToolTip(tr("Look at the scene from the camera, with the picture's frame drawn over it. Orbit, pan or zoom to leave."));
	auto *fromView = new QPushButton(tr("Camera from view"), m_cameraBar);
	fromView->setToolTip(tr("Put the scene's camera where the 3D view is now, looking the same way"));
	m_viewCombo = new QComboBox(m_cameraBar);
	m_viewCombo->setToolTip(tr("Saved camera views: pick one to put the camera there"));
	m_viewCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	auto *saveView = new QPushButton(tr("Save view..."), m_cameraBar);
	saveView->setToolTip(tr("Remember what you are looking at now (the 3D view, or the camera while looking through it) under a name"));
	for (QPushButton *b : {m_throughButton, fromView, saveView}) {
		b->setAutoDefault(false);
		compactStyle(b);
		flow->addWidget(b);
	}
	flow->addWidget(m_viewCombo);
	m_pathCheck = new QCheckBox(tr("Camera path"), m_cameraBar);
	m_pathCheck->setChecked(true);
	m_pathCheck->setToolTip(tr("With two or more saved views, draw the path a flythrough would take through them (the orange curve), with a marker at each view. Drag a marker to move that view (Shift: up and down); press one to see where it looks, and drag the ring at its target too."));
	flow->addWidget(m_pathCheck);
	auto *scrubLabel = new QLabel(tr("Flythrough:"), m_cameraBar);
	m_scrubSlider = new QSlider(Qt::Horizontal, m_cameraBar);
	m_scrubSlider->setRange(0, 1000);
	m_scrubSlider->setMinimumWidth(140);
	m_scrubSlider->setEnabled(false);
	m_scrubSlider->setToolTip(tr("Slide along the path through the saved views to see what the camera sees on the way, at the pace of a flythrough video (pauses included). Needs two or more saved views. Orbit, pan or zoom to leave."));
	scrubLabel->setBuddy(m_scrubSlider);
	flow->addWidget(scrubLabel);
	flow->addWidget(m_scrubSlider);
	m_cameraBar->setVisible(false);   // shown with the 3D view; createViews() puts it in the column

	connect(m_throughButton, &QPushButton::toggled, this, [this](bool on) { m_view3d->setThroughCamera(on); });
	connect(m_scrubSlider, &QSlider::valueChanged, this, [this](int value) { scrubFlythrough(value / 1000.0); });
	connect(m_view3d, &Scene3DView::cameraViewLeft, this, [this]() {
		QSignalBlocker block(m_throughButton);
		m_throughButton->setChecked(false);
		QSignalBlocker blockScrub(m_scrubSlider);
		m_scrubSlider->setValue(0);   // (leaving the preview, by orbiting or by any other way, puts the slider back at the start)
	});
	connect(m_pathCheck, &QCheckBox::toggled, this, [this](bool on) { m_view3d->setShowCameraPath(on); });
	connect(fromView, &QPushButton::clicked, this, [this]() { cameraFromView(); });
	connect(saveView, &QPushButton::clicked, this, [this]() {
		bool ok = false;
		const QString name = QInputDialog::getText(this, tr("Save view"), tr("Name of the view:"), QLineEdit::Normal, QString::fromStdString(scene_doc::uniqueViewName(m_doc, "View")), &ok);
		if (ok) saveCameraViewNamed(name);
	});
	connect(m_viewCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int row) {
		if (row >= 1) useCameraView(row - 1);   // row 0 is the heading
	});
	refreshViewCombo();
}

// The 3D view looks from `progress` (0..1) of the way along the flythrough. It is not the scene's camera, so Through camera is not lit.
void SceneBuilderWidget::scrubFlythrough(double progress) {
	if (m_doc.cameraViews.size() < 2) return;
	if (m_viewStack->currentWidget() != m_view3d) return;
	m_view3d->setFlythroughPreview(scene_doc::cameraAtFlythrough(m_doc, progress), progress);
	if (m_throughButton) {
		QSignalBlocker block(m_throughButton);
		m_throughButton->setChecked(false);
	}
}

void SceneBuilderWidget::refreshScrub() {
	if (!m_scrubSlider) return;
	const bool enough = m_doc.cameraViews.size() >= 2;
	m_scrubSlider->setEnabled(enough);
	if (!enough) {
		if (m_view3d->previewingFlythrough()) m_view3d->setThroughCamera(false);
		QSignalBlocker block(m_scrubSlider);
		m_scrubSlider->setValue(0);
	} else if (m_view3d->previewingFlythrough()) {
		scrubFlythrough(m_scrubSlider->value() / 1000.0);   // a view was edited: the picture follows
	}
}

bool SceneBuilderWidget::scrubForTest(double progress) {
	if (!m_scrubSlider || !m_scrubSlider->isEnabled()) return false;
	m_scrubSlider->setValue(qRound(progress * 1000.0));
	if (progress == 0.0) scrubFlythrough(0.0);   // (the slider starts at 0, so no change was signalled)
	return m_view3d->previewingFlythrough();
}

void SceneBuilderWidget::refreshViewCombo() {
	if (!m_viewCombo) return;
	QSignalBlocker block(m_viewCombo);
	m_viewCombo->clear();
	m_viewCombo->addItem(m_doc.cameraViews.empty() ? tr("No saved views") : tr("Saved views"));
	for (const scene_doc::CameraView &v : m_doc.cameraViews) m_viewCombo->addItem(QString::fromStdString(v.name));
	m_viewCombo->setCurrentIndex(0);
	refreshScrub();
}

// What the user is looking at as a camera: the scene's camera while looking through it, else the 3D view's place and direction with the scene camera's lens.
Camera SceneBuilderWidget::cameraNow() const {
	if (m_view3d->previewingFlythrough()) return m_view3d->previewCamera();   // the point on the flythrough being looked from
	if (m_view3d->throughCamera()) return m_doc.camera;
	const scene_view::OrbitCamera pose = m_view3d->currentPose();
	return scene_doc::cameraLookingFrom(m_doc.camera, toFloat3(pose.eye()), toFloat3(pose.target));
}

void SceneBuilderWidget::cameraFromView() {
	if (m_view3d->throughCamera() && !m_view3d->previewingFlythrough()) return;   // already the camera
	const Camera camera = cameraNow();
	edit(QString(), [&]() { m_doc.camera = camera; });
	refreshInspectorValues();
	emit statusMessage(tr("The camera is where the view is."));
}

int SceneBuilderWidget::saveCameraViewNamed(const QString &name, bool sceneCamera) {
	const Camera camera = sceneCamera ? m_doc.camera : cameraNow();
	int index = -1;
	edit(QString(), [&]() { index = scene_doc::saveCameraView(m_doc, name.toStdString(), camera); });
	if (index < 0) {
		emit statusMessage(tr("There are already %1 saved views: delete one first.").arg(static_cast<int>(scene_doc::kMaxCameraViews)));
		return -1;
	}
	AppLog::info(QStringLiteral("builder"), QStringLiteral("camera view: saved '%1'").arg(QString::fromStdString(m_doc.cameraViews[static_cast<size_t>(index)].name)));
	emit statusMessage(tr("Saved the view \"%1\".").arg(QString::fromStdString(m_doc.cameraViews[static_cast<size_t>(index)].name)));
	if (m_sel.kind == SelKind::Camera) QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
	return index;
}

// The scene's camera goes to the saved view; with the 3D view showing, it looks through the camera so the change is seen.
void SceneBuilderWidget::useCameraView(int index) {
	if (index < 0 || index >= static_cast<int>(m_doc.cameraViews.size())) return;
	edit(QString(), [&]() { scene_doc::applyCameraView(m_doc, index); });
	refreshInspectorValues();
	if (m_viewStack->currentWidget() == m_view3d && m_throughButton) m_throughButton->setChecked(true);
	AppLog::info(QStringLiteral("builder"), QStringLiteral("camera view: used '%1'").arg(QString::fromStdString(m_doc.cameraViews[static_cast<size_t>(index)].name)));
}

void SceneBuilderWidget::updateCameraViewFromNow(int index, bool sceneCamera) {
	if (index < 0 || index >= static_cast<int>(m_doc.cameraViews.size())) return;
	const Camera camera = sceneCamera ? m_doc.camera : cameraNow();
	edit(QString(), [&]() { scene_doc::updateCameraView(m_doc, index, camera); });
}

void SceneBuilderWidget::deleteCameraView(int index) {
	if (index < 0 || index >= static_cast<int>(m_doc.cameraViews.size())) return;
	edit(QString(), [&]() { scene_doc::removeCameraView(m_doc, index); });
	if (m_sel.kind == SelKind::Camera) QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
}

void SceneBuilderWidget::setCameraViewHold(int index, double seconds) {
	if (index < 0 || index >= static_cast<int>(m_doc.cameraViews.size()) || m_doc.cameraViews[static_cast<size_t>(index)].hold == seconds) return;
	edit(QStringLiteral("viewhold:%1").arg(index), [&]() { scene_doc::setCameraViewHold(m_doc, index, seconds); });
}

void SceneBuilderWidget::renameCameraView(int index, const QString &name) {
	if (index < 0 || index >= static_cast<int>(m_doc.cameraViews.size())) return;
	if (name.trimmed().isEmpty() || name.toStdString() == m_doc.cameraViews[static_cast<size_t>(index)].name) return;
	edit(QString(), [&]() { scene_doc::renameCameraView(m_doc, index, name.toStdString()); });
	if (m_sel.kind == SelKind::Camera) QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
}

int SceneBuilderWidget::cameraPathPixelsForTest(bool shown) {
	const bool was = m_view3d->showCameraPath();
	m_view3d->setShowCameraPath(shown);
	const QImage img = m_view3d->grab().toImage();
	if (shown && !qEnvironmentVariable("RT_GUI_SELFTEST_PATH_PNG").isEmpty()) img.save(qEnvironmentVariable("RT_GUI_SELFTEST_PATH_PNG"));
	m_view3d->setShowCameraPath(was);
	int n = 0;
	for (int y = 0; y < img.height(); ++y)
		for (int x = 0; x < img.width(); ++x) {
			const QColor c = img.pixelColor(x, y);
			if (c.red() - c.blue() > 70 && c.red() > c.green() && c.green() > c.blue()) ++n;   // orange, or a line of it blended into the background
		}
	return n;
}

int SceneBuilderWidget::viewComboCountForTest() const { return m_viewCombo ? m_viewCombo->count() : 0; }

void SceneBuilderWidget::lookThroughCamera(bool on) {
	if (m_throughButton) m_throughButton->setChecked(on);
	else m_view3d->setThroughCamera(on);
}

// The camera's properties: the saved views, each with its name and what to do with it.
void SceneBuilderWidget::inspectCameraViews(QFormLayout *f) {
	auto *heading = new QLabel(tr("<b>Saved views</b>"));
	heading->setContentsMargins(0, 8, 0, 0);
	f->addRow(heading);
	if (m_doc.cameraViews.empty()) {
		auto *none = new QLabel(tr("None yet. Save the camera as a view to come back to it: a front view, a close-up, a bird's-eye shot."));
		none->setWordWrap(true);
		f->addRow(none);
	}
	for (int i = 0; i < static_cast<int>(m_doc.cameraViews.size()); ++i) {
		auto *name = new QLineEdit(QString::fromStdString(m_doc.cameraViews[static_cast<size_t>(i)].name));
		connect(name, &QLineEdit::editingFinished, this, [this, i, name]() { renameCameraView(i, name->text()); });
		f->addRow(name);
		auto *row = new QWidget;
		auto *layout = new QHBoxLayout(row);
		layout->setContentsMargins(0, 0, 0, 0);
		const auto add = [&](const QString &text, const QString &tip, const std::function<void()> &action) {
			auto *b = new QPushButton(text);
			b->setToolTip(tip);
			b->setAutoDefault(false);
			compactStyle(b);
			layout->addWidget(b);
			connect(b, &QPushButton::clicked, this, action);
		};
		const double held = m_doc.cameraViews[static_cast<size_t>(i)].hold;
		auto *hold = new QDoubleSpinBox;
		hold->setRange(0.0, 600.0);
		hold->setDecimals(1);
		hold->setSingleStep(0.5);
		hold->setSuffix(tr(" s"));
		hold->setValue(held);
		hold->setKeyboardTracking(false);
		hold->setToolTip(tr("How long a flythrough stays at this view before flying on (0: it does not stop)"));
		f->addRow(tr("Hold:"), hold);
		connect(hold, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, i](double seconds) { setCameraViewHold(i, seconds); });
		add(tr("Use"), tr("Put the camera at this view"), [this, i]() { useCameraView(i); });
		add(tr("Update"), tr("Save the camera as it is now over this view"), [this, i]() { updateCameraViewFromNow(i, true); });
		add(tr("Delete"), tr("Remove this saved view"), [this, i]() { deleteCameraView(i); });
		layout->addStretch(1);
		f->addRow(row);
	}
	auto *save = new QPushButton(tr("Save camera as a view"));
	save->setToolTip(tr("Remember the camera as it is now under a name (a saved view can be used from the 3D view's list too)"));
	save->setAutoDefault(false);
	f->addRow(save);
	connect(save, &QPushButton::clicked, this, [this]() { saveCameraViewNamed(QString::fromStdString(scene_doc::uniqueViewName(m_doc, "View")), true); });
}
