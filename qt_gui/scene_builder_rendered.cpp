// scene_builder_rendered.cpp - the Scene Builder's rendered viewport: the 3D view showing a progressive path-traced picture of the scene you are editing, kept up to date as you
// edit (Blender's "rendered" viewport). It is a Live Preview session (the GPU renderer behind the Live Preview tab, qt_gui/realtime_preview_session.h) running on the same
// "(live preview)" copy of the scene that "Preview live" writes, drawn by the 3D view behind its usual names, lights, camera and tool, so orbiting, picking and moving work as they
// always did. The decisions (what a change of the view or the document means for the renderer) are src/shared/scene_viewport.h.
//
// Only one renderer can be on the GPU's single in-process scene at a time, so starting the viewport stops a running Live Preview (renderedViewportStarting()), and starting Live
// Preview stops the viewport (stopRenderedViewport()). The viewport pauses while the Scene Builder tab is not showing, so it does not use the GPU for nothing.
#include "scene_builder_widget.h"

#include "app_log.h"
#include "scene_builder_common.h"
#include "../src/shared/scene_viewport.h"

#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>

#ifdef RT_GUI_HAVE_LIVE_PREVIEW
#include "realtime_preview_session.h"
#include "scene_metadata_client.h"
#include "../src/shared/oidn_runtime.h"
#endif

#include <algorithm>

namespace {
constexpr int kMaxViewportWidth = 800;   // the picture is at most this many pixels wide (a path tracer's cost is its pixel count); it is drawn scaled up
constexpr int kEditPauseMs = 300;        // an edit restarts the render once edits have paused this long (a drag is one pause at its end)
constexpr int kSyncMs = 60;              // how often the view's camera is compared with what the renderer was last told
}  // namespace

bool SceneBuilderWidget::renderedViewportAvailable() {
#ifdef RT_GUI_HAVE_LIVE_PREVIEW
	return RealtimePreviewSession::isAvailable();
#else
	return false;
#endif
}

#ifdef RT_GUI_HAVE_LIVE_PREVIEW

// Switches the rendered viewport on or off (the Rendered button).
void SceneBuilderWidget::setRenderedViewport(bool on) {
	if (m_renderedButton && m_renderedButton->isChecked() != on) {
		QSignalBlocker block(m_renderedButton);
		m_renderedButton->setChecked(on);
	}
	if (on == m_viewportOn) return;
	if (!on) {
		stopRenderedViewport();
		return;
	}
	if (!renderedViewportAvailable()) {
		if (m_renderedButton) {
			QSignalBlocker block(m_renderedButton);
			m_renderedButton->setChecked(false);
		}
		emit statusMessage(tr("The rendered viewport needs the Live Preview renderer, which was not found."));
		return;
	}
	m_viewportOn = true;
	show3dView(true);
	if (m_view3d->orthographic()) m_view3d->setOrthographic(false);   // (the renderer draws in perspective: the picture would not line up with the overlays)
	m_view3d->setOrthographicAllowed(false);                           // ...so it cannot be switched on again (the button, the View menu and the numpad 5 all end here) until the viewport is off
	if (m_orthoButton) m_orthoButton->setEnabled(false);
	emit renderedViewportStarting();
	if (!m_viewportSession) {
		m_viewportSession = new RealtimePreviewSession(this);
		connect(m_viewportSession, &RealtimePreviewSession::frameReady, this, [this](QImage image, int samples) {
			if (!m_viewportOn) return;
			++m_viewportFrames;
			// A frame of the scene just started has fewer samples than the last frame of the one before it (a session's count only grows), which is how it is told from a late frame of the
			// old one still on its way: until then the renderer has not built the new scene and would drop a move.
			if (m_viewportAwaitNew && (m_viewportSamplesAtRestart <= 1 || samples < m_viewportSamplesAtRestart)) m_viewportAwaitNew = false;
			m_viewportSamples = samples;
			m_viewportImage = image;
			m_view3d->setRenderedNote(QString());
			m_view3d->setRenderedImage(image, samples);
		});
		connect(m_viewportSession, &RealtimePreviewSession::statusChanged, this, [this](QString text) {
			if (!m_viewportOn) return;
			AppLog::warn(QStringLiteral("builder"), QStringLiteral("rendered viewport: %1").arg(text));
			m_view3d->setRenderedNote(text);
		});
	}
	if (!m_viewportTimer) {
		m_viewportTimer = new QTimer(this);
		m_viewportTimer->setInterval(kSyncMs);
		connect(m_viewportTimer, &QTimer::timeout, this, [this]() { syncRenderedViewport(); });
	}
	m_view3d->setRenderedMode(true);
	m_viewportRunning = false;
	m_viewportSignature.clear();
	m_viewportPending = false;
	m_viewportFrames = 0;
	m_viewportSamples = 0;
	m_viewportTimer->start();
	AppLog::info(QStringLiteral("builder"), QStringLiteral("rendered viewport: on"));
	syncRenderedViewport();
}

// Off: the renderer stops and the 3D view draws its faces again. Also called by the main window when Live Preview starts, because they cannot share the GPU.
void SceneBuilderWidget::stopRenderedViewport() {
	const bool was = m_viewportOn;
	m_viewportOn = false;
	m_viewportRunning = false;
	m_viewportPending = false;
	if (m_viewportTimer) m_viewportTimer->stop();
	if (m_viewportSession) m_viewportSession->stop();
	m_view3d->setRenderedMode(false);
	m_view3d->setRenderedNote(QString());
	m_view3d->setOrthographicAllowed(true);
	if (m_orthoButton) m_orthoButton->setEnabled(true);
	if (m_renderedButton && m_renderedButton->isChecked()) {
		QSignalBlocker block(m_renderedButton);
		m_renderedButton->setChecked(false);
	}
	if (was) AppLog::info(QStringLiteral("builder"), QStringLiteral("rendered viewport: off"));
}

// The pose the picture must be rendered for: the 3D view's own eye, target and field of view, and its shape at a size the renderer can do many samples of.
scene_viewport::Pose SceneBuilderWidget::viewportPose() const {
	const scene_view::OrbitCamera c = m_view3d->currentPose();
	const scene_view::V3 eye = c.eye();
	scene_viewport::Pose p;
	p.eye[0] = eye.x; p.eye[1] = eye.y; p.eye[2] = eye.z;
	p.target[0] = c.target.x; p.target[1] = c.target.y; p.target[2] = c.target.z;
	p.fovDeg = c.fovDeg;
	scene_viewport::renderSize(m_view3d->width(), m_view3d->height(), kMaxViewportWidth, p.width, p.height);
	if (m_view3d->throughCamera() && m_doc.camera.lensRadius > 0.0) {   // (depth of field only through the scene's own camera: that is the picture a render makes)
		p.lensRadius = m_doc.camera.lensRadius;
		p.focusDistance = m_doc.camera.focusDistance;
	}
	return p;
}

// Every tick: a changed document restarts the render once edits have paused; a moved camera is passed on; a new size or field of view restarts it.
void SceneBuilderWidget::syncRenderedViewport() {
	if (!m_viewportOn || !isVisible() || !m_viewportSession) return;
	const scene_viewport::Pose pose = viewportPose();
	if (m_viewportDocChanged || m_viewportSignature.empty()) {
		m_viewportDocChanged = false;
		const std::string signature = scene_viewport::sceneSignature(m_doc);
		if (signature != m_viewportSignature) {
			m_viewportSignature = signature;
			if (!m_viewportPending && !moveViewportObjects()) {
				m_viewportPending = true;          // (the first time too)
				m_viewportEditClock.restart();
			} else if (m_viewportPending) {
				m_viewportEditClock.restart();
			}
		}
	}
	if (m_viewportPending && !m_viewportEditClock.hasExpired(kEditPauseMs) && m_viewportRunning) return;   // still being edited: the old picture stays until the pause
	if (!m_viewportRunning || m_viewportPending || scene_viewport::classify(m_viewportSent, pose) == scene_viewport::Change::Restart) {
		m_viewportPending = false;
		startViewportSession(pose);
		return;
	}
	if (scene_viewport::classify(m_viewportSent, pose) == scene_viewport::Change::Camera) {
		m_viewportSession->setCamera(pose.eye[0], pose.eye[1], pose.eye[2], pose.target[0], pose.target[1], pose.target[2]);
		m_viewportSent = pose;
	}
}

// A drag only moved objects: tell the renderer where each one is now, from where its copy has it, and it redraws at once without a restart. False when the change is more than that
// (or the renderer has not drawn the new copy yet, when it would not know the objects and would drop the move), and the copy is written again.
bool SceneBuilderWidget::moveViewportObjects() {
	if (!m_viewportRunning || m_viewportAwaitNew || !RealtimePreviewSession::objectEditingAvailable()) return false;
	const std::vector<std::array<double, 3>> offsets = scene_viewport::translationOnly(m_viewportBase, m_viewportBaseSignature, m_doc);
	if (offsets.empty()) return false;
	if (m_viewportOffsets.size() != offsets.size()) m_viewportOffsets.assign(offsets.size(), {0.0, 0.0, 0.0});
	for (std::size_t i = 0; i < offsets.size(); ++i) {
		if (offsets[i] == m_viewportOffsets[i]) continue;
		m_viewportSession->setObjectOffset(static_cast<int>(i), offsets[i][0], offsets[i][1], offsets[i][2]);
		m_viewportOffsets[i] = offsets[i];
	}
	return true;
}

// Writes the scene as the viewport sees it (the view's camera, the view's shape) over the "(live preview)" copy and starts the renderer on it.
void SceneBuilderWidget::startViewportSession(const scene_viewport::Pose &pose) {
	QString error;
	const QString path = livePreviewCopyPath(&error);
	if (path.isEmpty()) {
		m_view3d->setRenderedNote(error);
		return;
	}
	const bool lens = pose.lensRadius > 0.0;
	scene_doc::Document copy = m_doc;
	copy.title += " (live preview)";
	copy.camera.position = {pose.eye[0], pose.eye[1], pose.eye[2]};
	copy.camera.target = {pose.target[0], pose.target[1], pose.target[2]};
	copy.camera.up = {0.0, 1.0, 0.0};
	copy.camera.fov = pose.fovDeg;
	copy.camera.lensRadius = pose.lensRadius;
	copy.camera.focusDistance = pose.focusDistance;
	copy.render.width = pose.width;
	copy.render.height = pose.height;
	if (!writeSceneCopy(copy, path)) {
		m_view3d->setRenderedNote(tr("Could not write %1.").arg(path));
		return;
	}
	QString id = SceneMetadataClient::sceneIdForFile(path);
	if (id.isEmpty()) {   // the first time: the copy is not in the scene list yet
		SceneMetadataClient::refreshUserScenes();
		id = SceneMetadataClient::sceneIdForFile(path);
	}
	if (id.isEmpty()) {
		m_view3d->setRenderedNote(tr("The scene list could not list the preview copy."));
		return;
	}
	m_viewportSession->setExposure(1.0);
	m_viewportSession->setSmoothLowSample(true);
	m_viewportSession->setAiDenoise(oidn_runtime::available());
	m_viewportSession->start(id, pose.width, pose.height, pose.eye[0], pose.eye[1], pose.eye[2], pose.target[0], pose.target[1], pose.target[2],
	                         /*denoise*/ false, 0.0, false, /*svgf*/ false, /*restirGi*/ true, /*restirDi*/ true, /*probeCache*/ false, /*pathGuiding*/ false,
	                         /*spp*/ 1, std::min(m_doc.render.maxDepth, 8), /*fireflyClamp*/ 50.0, /*temporalUpscale*/ false, 1, /*nrc*/ false, /*neuralUpscale*/ false,
	                         lens, pose.lensRadius, pose.focusDistance);
	m_viewportSent = pose;
	m_viewportBase = m_doc;
	m_viewportBaseSignature = m_viewportSignature;   // (the signature of the document as it is now: syncRenderedViewport worked it out just before starting)
	m_viewportOffsets.clear();
	m_viewportAwaitNew = true;
	m_viewportSamplesAtRestart = m_viewportSamples;
	++m_viewportRestarts;
	m_viewportRunning = true;
	m_viewportSceneId = id;
}

// The Scene Builder tab was hidden or shown: the renderer rests while it is hidden.
void SceneBuilderWidget::pauseViewportIfHidden(bool hidden) {
	if (!m_viewportOn || !m_viewportSession) return;
	if (hidden) {
		m_viewportSession->stop();
		m_viewportRunning = false;
	} else {
		m_viewportPending = true;   // (started again from the document as it is now)
		m_viewportEditClock.restart();
	}
}

#else  // no Live Preview in this build: the button is never offered

void SceneBuilderWidget::setRenderedViewport(bool) {}
void SceneBuilderWidget::stopRenderedViewport() {}
void SceneBuilderWidget::syncRenderedViewport() {}
void SceneBuilderWidget::pauseViewportIfHidden(bool) {}

#endif
