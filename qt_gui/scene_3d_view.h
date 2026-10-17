#pragma once
// scene_3d_view.h - the Scene Builder's 3D view: the scene seen from an orbiting camera, where things are picked and moved, turned and resized. It is
// drawn with QPainter (flat-shaded faces sorted back to front), so it needs no OpenGL and no extra libraries; its geometry is
// src/shared/scene_view_math.h, the tools' decisions are src/shared/scene_gizmo.h and a mesh file's bounds and vertex sample come from
// src/shared/mesh_preview.h (read on a worker thread, never while painting).
//
//   drag the background  orbit        right- or middle-drag  pan        wheel  zoom
//   click an item        select it    W / E / R  (or the buttons)  the Move / Rotate / Scale tool
//   Move:   drag an item to move it on the floor (Shift: up and down), or drag a coloured arrow to move along that axis only (X red, Y green, Z blue)
//   Rotate: drag a coloured ring to turn the object about that world axis
//   Scale:  drag a square handle to stretch the object along that one of its own axes (a sphere, disk or mesh scales all round)
//
// Moves use the same signals as the 2D layout view, so the Scene Builder treats them the same whichever view they came from; turns and stretches
// arrive as a whole edited object.

#include <QElapsedTimer>
#include <QImage>
#include <QPointF>
#include <QWidget>

#include <map>
#include <string>
#include <vector>

#include "../src/shared/mesh_preview.h"
#include "../src/shared/scene_document.h"
#include "../src/shared/scene_gizmo.h"
#include "../src/shared/scene_view_math.h"
#include "scene_layout_view.h"

class Scene3DView : public QWidget {
	Q_OBJECT
public:
	enum class GizmoMode { Move, Rotate, Scale };

	explicit Scene3DView(QWidget *parent = nullptr);
	~Scene3DView() override;  // out of line: Face is only defined in the .cpp

	void setDocument(const scene_doc::Document *doc) { m_doc = doc; update(); }
	void setSelection(const BuilderSelection &s) { m_sel = s; update(); }
	// The other items picked along with the main one (Ctrl- or Shift-click, a box in the 2D view, a group): drawn like it, and dragged with it. With any, the tool
	// is drawn at the middle of them all and turns or scales them as one (groupTurned, groupScaled), about that point.
	void setExtraSelection(const std::vector<BuilderSelection> &extra) { m_extra = extra; update(); }
	void setSnap(bool on) { m_snap = on; }
	// What a drag has just snapped to (scene_snap.h): an outline round each object matched and a note; gone when the mouse is released.
	void setSnapGuides(const std::vector<scene_doc::SnapGuide> &guides) { m_guides = guides; update(); }
	double snapTolerance(const scene_doc::Float3 &at) const;   // about 8 pixels at that point, in world units
	// Looks through the scene's camera: the view takes the camera's place, direction and field of view, with the picture's frame drawn over it (everything outside
	// the frame is dimmed), and follows the camera while it is edited. Orbiting, panning, zooming or Frame all leaves it (cameraViewLeft()).
	void setThroughCamera(bool on);
	bool throughCamera() const { return m_throughCamera; }
	// A photo laid over the picture frame while looking through the camera, `opacity` thick, to line the scene up against it (nothing in the document; a null image takes it away).
	void setPhotoOverlay(const QImage &photo, double opacity) { m_photoOverlay = photo; m_photoOpacity = opacity; update(); }
	// The flythrough scrub: looks through `camera` (a point along the saved views' path, `progress` 0..1 of the way) instead of the scene's camera, with the picture's frame over it
	// like Through camera. Ended by setThroughCamera(), or by orbiting, panning or zooming (cameraViewLeft(), as for Through camera). It changes nothing in the document.
	void setFlythroughPreview(const scene_doc::Camera &camera, double progress);
	bool previewingFlythrough() const { return m_previewing; }
	const scene_doc::Camera &previewCamera() const { return m_previewCamera; }
	// The flythrough's path: with two or more saved camera views, the curve the camera would follow through them (the renderer's own Catmull-Rom curve, camera_keyframes.h), a
	// marker and the name at each view, and a short line along where it looks. On by default; not drawn while looking through the camera.
	void setShowCameraPath(bool on) { m_showPath = on; update(); }
	bool showCameraPath() const { return m_showPath; }
	// A marker of that path can be dragged: the eye (which 0) of any saved view, and the target (which 1, a ring) of the one pressed last. It moves on the floor plane (Shift: up
	// and down; Ctrl: freely), snapped to the 0.25 grid like an object; the owner changes the view (viewMarkerDragged).
	int activeView() const { return m_activeView; }
	QPointF viewMarkerPoint(int viewIndex, int which) const;   // where a marker is drawn, in this widget's pixels (null if hidden or behind the camera), for tests
	// Where the eye is and what it looks at now: the 3D view's own orbit, or the scene's camera while looking through it.
	scene_view::OrbitCamera currentPose() const { return view().cam; }
	// Display modes. Wireframe draws only the edges of every face (nothing hides anything), to see through and into things. Orthographic has no perspective: things keep their size
	// with distance and parallel lines stay parallel (the same size at the target, so switching does not zoom). A standard view (Front, Back, Right, Left, Top, Bottom) puts the
	// camera on that axis, looking at the same target from the same distance; orbiting leaves it. The numpad does these too (see keyPressEvent).
	void setWireframe(bool on) { m_wireframe = on; update(); emit displayModeChanged(); }
	bool wireframe() const { return m_wireframe; }
	void setOrthographic(bool on);
	void setOrthographicAllowed(bool ok) { m_orthoAllowed = ok; }   // false while a rendered picture (perspective only) is under the view: switching orthographic on is refused
	bool orthographic() const { return m_cam.ortho; }
	void setStandardView(scene_view::StandardView v);
	QString viewName() const { return m_viewName; }   // "Front", "Top"...: empty once the view is turned by hand
	// Pick focus: the next click puts what is under it in focus. The view takes the click (nothing is selected or dragged), the cursor is a cross, and the point the click found on a
	// surface (or on the floor if it hit none) is reported with focusPointPicked(); Escape, or the first click that finds nothing at all, ends it.
	void beginPickFocus();
	bool pickingFocus() const { return m_pickFocus; }
	bool surfaceAt(const QPointF &px, scene_doc::Float3 &where) const;   // the point of the nearest face under that pixel (else of the floor); false if neither
	// Rendered mode (the Scene Builder's rendered viewport): a path-traced picture of the scene is drawn behind the usual names, lights, camera and tool, instead of the flat-shaded
	// faces, so everything (orbiting, picking, moving) works as before with the real picture under it. The picture is rendered by the owner for exactly this view (same eye, target,
	// field of view and shape), so what is drawn on top lines up. Until the first picture arrives the faces are drawn as usual. With Wireframe on, the edges are drawn over the picture.
	void setRenderedMode(bool on) { m_renderedMode = on; if (!on) m_rendered = QImage(); update(); }
	bool renderedMode() const { return m_renderedMode; }
	void setRenderedImage(const QImage &image, int samples) { m_rendered = image; m_renderedSamples = samples; update(); }
	void setRenderedNote(const QString &text) { m_renderedNote = text; update(); }   // what to say in the corner instead of the sample count (an error), or empty
	bool hasRenderedImage() const { return !m_rendered.isNull(); }
	void setGizmoMode(GizmoMode m);
	GizmoMode gizmoMode() const { return m_gizmo; }
	// Where a new object is dropped: the floor (y = 0) point under the middle of the view when that is near what the camera looks at, else the floor under
	// the point it looks at (a level camera would otherwise drop it near the horizon, far from anything visible).
	scene_doc::Float3 centerInWorld() const;
	void frameAll();
	void frameBox(const scene_doc::Float3 &lo, const scene_doc::Float3 &hi);   // Frame selected: look at the middle of this box from far enough to hold it
	QPointF itemScreenPos(const BuilderSelection &s) const;        // where an item is drawn, in this widget's pixels (null if behind the camera)
	// Points on the selected object's tool, for tests: along a move arrow, on a rotate ring (angle from its reference direction), at a scale handle.
	QPointF axisArrowPoint(int axis, double fraction) const;
	QPointF ringPoint(int axis, double deg) const;
	QPointF scaleHandlePoint(int axis, double fraction) const;
	// For tests: set the camera's angles, and whether a mesh file's preview has been read yet.
	void orbitForTest(double yawDeg, double pitchDeg) { leaveCameraView(); m_cam.yawDeg = yawDeg; m_cam.pitchDeg = pitchDeg; m_userView = true; m_viewName.clear(); update(); }
	bool meshPreviewReady(const std::string &path) const { return meshPreview(path) != nullptr; }

	QSize sizeHint() const override { return QSize(520, 380); }

signals:
	void selectionRequested(const BuilderSelection &s);
	// A Ctrl- or Shift-click (no drag) on an item: add it to the picked items, or take it out. (A Shift- or Ctrl-drag still lifts or moves freely.)
	void selectionToggled(const BuilderSelection &s);
	void dragBegan();
	void positionDragged(const BuilderSelection &s, int which, const scene_doc::Float3 &world);
	// A turn or a stretch: the object as it now is (the owner takes its rotation, radius, height, size and meshScale).
	void objectEdited(const BuilderSelection &s, const scene_doc::Object &updated);
	void gizmoModeChanged(int mode);
	// A saved view's marker dragged (the owner takes the view's eye or target to `world`); dragBegan() came first, as for everything else that is dragged.
	void viewMarkerDragged(int viewIndex, int which, const scene_doc::Float3 &world);
	void focusPointPicked(const scene_doc::Float3 &world);   // a click found this point (Pick focus)
	void frameSelectedRequested();   // the F key
	void displayModeChanged();       // wireframe or orthographic changed (the numpad 5 key changes the latter)
	void cameraViewRequested();      // the numpad 0 key: look through the scene's camera
	void cameraViewLeft();   // the user orbited, panned or zoomed away from looking through the camera
	// An arrow key, Page Up or Page Down: move the picked items by this much (scene units): the arrows along the horizontal world axes nearest the screen's right and away,
	// Page Up and Page Down up and down.
	void nudged(const scene_doc::Float3 &delta);
	// With several items picked, a Rotate ring or Scale handle dragged: the TOTAL turn (degrees, about world axis 0 X, 1 Y, 2 Z) or factor since the press, which the
	// owner applies to the items as they were then (about the middle of them, scene_selection.h centroidOf()).
	void groupTurned(int axis, double degrees);
	void groupScaled(double factor);

protected:
	void paintEvent(QPaintEvent *) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	struct Face;
	struct Ctx;
	struct Hit {
		enum class Kind { None, Item, Axis, Ring, ScaleHandle, FreeHandle, ViewMarker } kind = Kind::None;
		BuilderSelection sel;   // (a ViewMarker keeps the saved view's index in sel.index)
		int which = 0;   // 0: the item's position, 1: its target
		int axis = 0;    // for an arrow, ring or handle
	};
	enum class Mode { None, Orbit, Pan, Ground, Vertical, Free, Axis, Rotate, Scale, MaybeToggle };

	scene_view::View view() const;
	// The shapes as world-space polygons. Tessellating them is the expensive part of a repaint, so they are kept until the document changes (found by
	// comparing a signature of everything that shapes them), and only projected again for each frame.
	std::vector<Face> &faces() const;
	void projectFaces(const scene_view::View &v) const;
	std::uint64_t geometrySignature() const;
	const scene_doc::Float3 *handle(const BuilderSelection &s, int which) const;
	const scene_doc::Float3 *viewHandle(int viewIndex, int which) const;   // a saved view's eye or target in the document, or null
	bool pathShown() const { return m_showPath && m_doc && m_doc->cameraViews.size() >= 2 && !m_throughCamera && !m_previewing; }   // (it is not drawn while looking through the camera, so it is not there to be dragged either)
	const scene_doc::Object *selectedObject() const;
	GizmoMode effectiveGizmo() const;   // Rotate and Scale only apply to objects; for a light or the camera the Move tool shows
	double gizmoLength(const scene_view::View &v, const scene_view::V3 &at) const;
	scene_view::V3 localAxis(const scene_doc::Object &o, int axis) const;   // the object's own axis, in the world
	// A mesh file's bounds and vertex sample, or null while it is still being read (on a worker thread) or if it cannot be.
	const mesh_preview::MeshPreview *meshPreview(const std::string &path) const;
	void startMeshLoad(const std::string &path, qint64 modified, qint64 size) const;
	void meshLoaded(const std::string &path, mesh_preview::MeshPreview preview, qint64 modified, qint64 size);
	Hit hitTest(const QPointF &px) const;
	void applyDrag(const QPointF &px, Qt::KeyboardModifiers mods);
	void beginHit(const Hit &h, const QPointF &px, Qt::KeyboardModifiers mods);   // what a press on an item or a tool handle starts
	bool isPicked(const BuilderSelection &s) const;   // the main item or one of the extra ones
	bool multi() const { return !m_extra.empty(); }
	// The main selected item is locked or hidden, so no tool is drawn for it and it is not picked or moved by dragging in this view.
	bool selectionLocked() const {
		if (!m_doc) return false;
		if (m_sel.kind == BuilderSelection::Kind::Object) return m_sel.index >= 0 && m_sel.index < static_cast<int>(m_doc->objects.size()) && (m_doc->objects[m_sel.index].locked || m_doc->objects[m_sel.index].hidden);
		if (m_sel.kind == BuilderSelection::Kind::Light) return m_sel.index >= 0 && m_sel.index < static_cast<int>(m_doc->lights.size()) && (m_doc->lights[m_sel.index].locked || m_doc->lights[m_sel.index].hidden);
		return false;
	}
	bool gizmoOrigin(scene_view::V3 &out) const;   // where the tool is drawn: the item's place, or the middle of everything picked

	// paintEvent's parts
	void drawGrid(Ctx &c) const;
	void drawFaces(Ctx &c) const;
	void drawMeshPoints(Ctx &c, int objectIndex) const;
	void drawNames(Ctx &c) const;
	void drawLights(Ctx &c) const;
	void drawProjector(Ctx &c, const scene_doc::Light &l, bool selected) const;   // the frustum a projector light shines through
	double slideAspect(const std::string &file) const;   // width / height of a slide picture (1 until it is found)
	void drawCamera(Ctx &c) const;
	void drawFlythroughPath(Ctx &c) const;
	const scene_doc::Camera &lookCamera() const { return m_previewing ? m_previewCamera : m_doc->camera; }   // what the view looks through when it is the camera's
	void drawTool(Ctx &c) const;
	void drawCameraFrame(QPainter &p) const;
	void drawSnapGuides(Ctx &c) const;
	std::vector<scene_doc::SnapGuide> m_guides;
	scene_view::OrbitCamera cameraPose() const;   // the scene's camera as an orbit (with the field of view that makes the frame show what it sees)
	void leaveCameraView();                        // out of looking through the camera, the view staying where it is

	const scene_doc::Document *m_doc = nullptr;
	BuilderSelection m_sel;
	std::vector<BuilderSelection> m_extra;
	Hit m_pending;                    // a Ctrl- or Shift-press on an item: a click toggles it, a drag past a few pixels is the usual drag of it
	QPointF m_pendingPress;
	Qt::KeyboardModifiers m_pendingMods;
	BuilderSelection m_collapseTo;    // pressed on one of several picked items: if the mouse does not move, that one alone becomes the selection
	bool m_collapsePending = false;
	scene_view::OrbitCamera m_cam;
	bool m_snap = true;
	GizmoMode m_gizmo = GizmoMode::Move;
	bool m_userView = false;   // the user has orbited, panned or zoomed, so a resize keeps the view instead of re-framing the scene
	bool m_previewing = false;      // the view is a point on the flythrough (setFlythroughPreview), not the scene's camera
	scene_doc::Camera m_previewCamera;
	double m_previewProgress = 0.0;
	int m_activeView = -1;          // the saved view whose marker was pressed last: its target is drawn and can be dragged too
	bool m_pickFocus = false;       // the next click picks the focus point
	mutable std::map<std::string, double> m_slideAspects;
	bool m_orthoAllowed = true;
	bool m_renderedMode = false;
	QImage m_rendered;
	int m_renderedSamples = 0;
	QString m_renderedNote;
	bool m_wireframe = false;
	QString m_viewName;             // the standard view the camera is in, until it is turned by hand
	bool numpadKey(QKeyEvent *e);   // a numpad view key; true if it was one
	bool m_showPath = true;         // draw the flythrough's camera path (setShowCameraPath)
	QImage m_photoOverlay;          // the photo laid over the camera view (setPhotoOverlay)
	double m_photoOpacity = 0.5;
	bool m_throughCamera = false;   // the view is the scene's camera (m_cam is kept for when it is switched off)

	Mode m_mode = Mode::None;
	Hit m_drag;
	QPointF m_last;
	scene_view::V3 m_dragStart;     // the item's position when the drag began
	scene_view::V3 m_grabOffset;    // from where the mouse met the floor to the item (ground drags)
	double m_axisT0 = 0.0;          // where along the axis the mouse was when the drag began
	scene_doc::Object m_dragObject; // the object as it was when a turn or stretch began
	double m_angle0 = 0.0;          // the mouse's angle round the ring when a turn began

	struct CachedMesh {
		mesh_preview::MeshPreview preview;
		qint64 modified = 0, size = 0;
		qint64 checkedAt = 0;   // when the file's date was last looked at (ms on m_clock), so a repaint does not stat it every time
		bool pending = false;   // being read now
	};
	mutable std::map<std::string, CachedMesh> m_meshes;
	mutable int m_meshVersion = 0;   // counts finished mesh reads, so the cached faces are rebuilt when one lands
	QElapsedTimer m_clock;

	mutable std::vector<Face> m_faces;
	mutable std::uint64_t m_facesSignature = 0;
	mutable bool m_facesValid = false;
};
