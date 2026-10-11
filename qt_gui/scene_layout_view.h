#pragma once
// scene_layout_view.h - the Scene Builder's 2D layout view: the scene seen from above, the front or the side, where things are picked and dragged.

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <vector>

#include "../src/shared/scene_document.h"
#include "../src/shared/scene_snap.h"

// What is selected in the list and the layout view.
struct BuilderSelection {
	enum class Kind { None, Camera, Object, Light };
	Kind kind = Kind::None;
	int index = 0;
	bool operator==(const BuilderSelection &o) const { return kind == o.kind && index == o.index; }
};

// The position (which = 0) or target (which = 1) of the selected camera, object or light that can be dragged, or null (shared by the 2D and 3D views).
const scene_doc::Float3 *builderHandle(const scene_doc::Document *doc, const BuilderSelection &s, int which);

// A 2D orthographic view of the scene (from above, the front or the side) where things are picked and dragged.
class SceneLayoutView : public QWidget {
	Q_OBJECT
public:
	enum class Plane { Top, Front, Side };

	explicit SceneLayoutView(QWidget *parent = nullptr);

	void setDocument(const scene_doc::Document *doc) { m_doc = doc; update(); }
	void setSelection(const BuilderSelection &s) { m_sel = s; update(); }
	// The other items picked along with the main one (Ctrl- or Shift-click, a box, a group): drawn like it, and dragged with it.
	void setExtraSelection(const std::vector<BuilderSelection> &extra) { m_extra = extra; update(); }
	// What a drag has just snapped to (scene_snap.h): drawn as lines through the shared coordinates and an outline round each object matched; gone when the mouse is released.
	void setSnapGuides(const std::vector<scene_doc::SnapGuide> &guides) { m_guides = guides; update(); }
	double snapTolerance() const { return 8.0 / m_scale; }   // about 8 pixels, in world units
	void setPlane(Plane p);
	void setSnap(bool on) { m_snap = on; }
	// The point in world space the view is centred on, for the plane's two axes (the third is 0): where a new object is dropped.
	scene_doc::Float3 centerInWorld() const;
	void frameAll();
	void frameBox(const scene_doc::Float3 &lo, const scene_doc::Float3 &hi);   // Frame selected: the view shows this box (and stays there when the window is resized)
	QPointF itemScreenPos(const BuilderSelection &s) const;  // where an item is drawn, in this widget's pixels

	QSize sizeHint() const override { return QSize(520, 380); }

signals:
	void selectionRequested(const BuilderSelection &s);
	// Ctrl- or Shift-click on an item: add it to the picked items, or take it out.
	void selectionToggled(const BuilderSelection &s);
	// A box dragged round items (Ctrl or Shift held on the background): every object and light whose centre is inside; `additive` adds to the picked items.
	void boxSelected(const QList<BuilderSelection> &items, bool additive);
	// An arrow key, Page Up or Page Down: move the picked items by this much (scene units; the view works out which way the arrow points in its plane).
	void frameSelectedRequested();   // the F key
	void nudged(const scene_doc::Float3 &delta);
	// A drag moved something: `which` is 0 for the item's position, 1 for its target (camera, spot and distant lights). dragBegan is sent once at the
	// start so the owner can take an undo snapshot.
	void dragBegan();
	void positionDragged(const BuilderSelection &s, int which, const scene_doc::Float3 &world);

protected:
	void paintEvent(QPaintEvent *) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	struct Hit {
		BuilderSelection sel;
		int which = 0;
		bool valid = false;
	};

	QPointF toScreen(const scene_doc::Float3 &p) const;
	QPointF toUV(const scene_doc::Float3 &p) const;
	scene_doc::Float3 fromScreen(const QPointF &px, const scene_doc::Float3 &keep) const;
	QList<QPointF> silhouette(const scene_doc::Object &o) const;
	Hit hitTest(const QPointF &px) const;
	const scene_doc::Float3 *handlePosition(const BuilderSelection &s, int which) const;
	bool isPicked(const BuilderSelection &s) const;   // the main item or one of the extra ones
	// The main selected item is locked or hidden, so its handles are not offered (it is not moved by dragging in a view).
	bool selectionLocked() const {
		if (!m_doc) return false;
		if (m_sel.kind == BuilderSelection::Kind::Object) return m_sel.index >= 0 && m_sel.index < static_cast<int>(m_doc->objects.size()) && (m_doc->objects[m_sel.index].locked || m_doc->objects[m_sel.index].hidden);
		if (m_sel.kind == BuilderSelection::Kind::Light) return m_sel.index >= 0 && m_sel.index < static_cast<int>(m_doc->lights.size()) && (m_doc->lights[m_sel.index].locked || m_doc->lights[m_sel.index].hidden);
		return false;
	}
	void drawSnapGuides(QPainter &p) const;
	std::vector<scene_doc::SnapGuide> m_guides;

	const scene_doc::Document *m_doc = nullptr;
	BuilderSelection m_sel;
	std::vector<BuilderSelection> m_extra;
	bool m_maybeToggle = false;      // Ctrl or Shift pressed: a click toggles m_toggleCandidate, a drag becomes a box
	BuilderSelection m_toggleCandidate;
	bool m_banding = false;          // a box is being dragged out
	bool m_bandAdditive = false;
	QPointF m_bandStart, m_bandEnd;
	BuilderSelection m_collapseTo;   // pressed on one of several picked items: if the mouse does not move, that one alone becomes the selection
	bool m_collapsePending = false;
	Plane m_plane = Plane::Top;
	double m_scale = 40.0;           // pixels per world unit
	double m_cu = 0.0, m_cv = 2.0;   // the (u, v) point at the middle of the widget
	bool m_snap = true;
	bool m_panning = false;
	bool m_userView = false;         // the user has zoomed or panned, so a resize keeps the view instead of re-framing the scene
	bool m_dragging = false;
	Hit m_drag;
	QPointF m_dragOffsetUV;          // from the pointer to the dragged item's position, in view units
	QPointF m_lastPan;
};
