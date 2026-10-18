#pragma once
// scene_builder_widget.h - the GUI's "Scene Builder" tab: build a scene from shapes, materials and lights, move things around in a
// layout view, preview it, and save it as an ordinary .pbrt file.
//
// The model, the pbrt writer and the checks are in src/shared/scene_document.h (header-only, unit-tested without Qt); this file is
// only the editing surface. The widget owns one scene_doc::Document and never renders in-process: a preview runs the same
// ray_tracer launcher the Render tab runs, on a .pbrt file saved to a temporary folder.

#include <QElapsedTimer>
#include <QList>
#include <QPixmap>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <array>
#include <vector>

#include "../src/shared/log_format.h"
#include "../src/shared/photo_composite.h"
#include "../src/shared/scene_document.h"
#include "../src/shared/snapshot_history.h"
#include "../src/shared/scene_props.h"
#include "../src/shared/scene_blocks.h"
#include "../src/shared/scene_selection.h"
#include "../src/shared/scene_camera_views.h"
#include "../src/shared/scene_viewport.h"

class QButtonGroup;
class QCheckBox;
class QSplitter;
class QLineEdit;
class QComboBox;
class QFormLayout;
class QLabel;
class QSlider;
class QListWidget;
class QProcess;
class QImage;
class QListWidgetItem;
class RealtimePreviewSession;
class QMenu;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class Scene3DView;
class QTimer;

#include "scene_3d_view.h"
#include "scene_layout_view.h"


namespace scene_room { struct Import; }

class SceneBuilderWidget : public QWidget {
	Q_OBJECT
public:
	explicit SceneBuilderWidget(QWidget *parent = nullptr);
	~SceneBuilderWidget() override;

	// For the automated self-test (mainwindow_selftest.cpp) and tests: drive the editor without a mouse.
	const scene_doc::Document &document() const { return m_doc; }
	void newScene();
	bool openFile(const QString &path, QString *error = nullptr);
	bool saveFile(const QString &path);
	void selectObject(int index);
	void selectLight(int index);   // (the self-test picks the sky)
	void setSkyImageForTest(int light, const QString &path);   // a light's sky picture chosen as the Browse button would leave it, and the properties rebuilt
	QString pbrtTextForTest() const { return QString::fromStdString(scene_doc::toPbrt(m_doc)); }
	bool writeSavedCopyForTest(const QString &path) { return writeSceneCopy(m_doc, path); }   // what Save writes (a sky picture as its equal-area copy), without changing which file the scene is
	// Several items at once (scene_builder_multi.cpp): Ctrl- or Shift-click and a box in the views, Ctrl/Shift-click in the list, and groups. Everything acts on all of them.
	void pickObjects(const std::vector<int> &objects, const std::vector<int> &lights = {});   // replaces the selection; the first one given is the main one
	scene_doc::ItemSet pickedItems() const;   // the main item and the others picked along with it (never the camera)
	void selectAll();                          // every object and light (Ctrl+A)
	void groupSelected();                      // the picked objects become one group (Ctrl+G); needs two or more
	void ungroupSelected();                    // dissolves the groups of the picked objects (Ctrl+Shift+G)
	void moveSelectedBy(const scene_doc::Float3 &delta);   // the picked items, together, one undo step
	// The arrow keys in a view: the picked items (or the camera, with its target) move by `delta`; a run of key presses is one undo step.
	void nudgeSelectedBy(const scene_doc::Float3 &delta);
	void sendKeyForTest(bool in3d, int key, Qt::KeyboardModifiers mods = Qt::NoModifier);   // a real key press in the 2D or the 3D view
	// Turn or scale the picked items as one about the middle of them (scene_selection.h). One undo step each; while a ring or handle is dragged (transformPicked) the
	// whole drag is one step and every update is worked out from the items as they were when the drag began.
	void turnSelectedBy(int axis, double degrees);
	void scaleSelectedBy(double factor);
	void useLookOfMainObject();                // the other picked objects get the main one's material
	// "Snap to objects" (scene_snap.h): while an object (or a picked set) is dragged, its edges and middle line up with the other objects', and it rests on them or the floor.
	void setSnapToObjects(bool on);
	bool snapToObjects() const { return m_snapObjects; }
	// Saved camera views (scene_builder_camera_views.cpp). Each change is one undo step.
	int saveCameraViewNamed(const QString &name, bool sceneCamera = false);   // the 3D view's place (or the camera, looking through it; or always the camera if `sceneCamera`); the new index, -1 if full
	void useCameraView(int index);            // the scene's camera goes to the saved view
	void updateCameraViewFromNow(int index, bool sceneCamera = false);
	void deleteCameraView(int index);
	void renameCameraView(int index, const QString &name);
	void setCameraViewHold(int index, double seconds);   // how long a flythrough stays at the view (seconds)
	void cameraFromView();                    // the scene's camera goes where the 3D view is
	void lookThroughCamera(bool on);          // the 3D view looks through the scene's camera
	bool lookingThroughCameraForTest() const { return m_view3d && m_view3d->throughCamera(); }
	bool view3dShownForTest() const;   // the 3D view is the one showing (not Top, Front or Side)
	scene_doc::Float3 viewEyeForTest() const {   // where the 3D view's eye is now
		const scene_view::V3 e = m_view3d->currentPose().eye();
		return {e.x, e.y, e.z};
	}
	// How many pixels of the 3D view's picture are the flythrough path's orange, with the path drawn or not (the setting is put back).
	int cameraPathPixelsForTest(bool shown);
	bool playingForTest() const;
	void playForTest(bool on) { if (on) startPlay(); else stopPlay(); }
	void advancePlayForTest(double seconds) { advancePlay(seconds); }   // what the timer does, with a made-up time step
	double playProgressForTest() const { return m_playProgress; }
	bool scrubForTest(double progress);   // moves the Flythrough slider; true if the 3D view is now looking from there
	int viewComboCountForTest() const;
	bool dragPickedForTest(int index, const QPointF &deltaPx);
	// A drag left unfinished (the mouse still down) in the 2D or the 3D view, and its release: for a picture of what a drag shows while it is going on.
	void holdDragForTest(int index, const QPointF &deltaPx, bool in3d);
	void releaseDragForTest(bool in3d);   // a real drag in the layout view of one picked object (moves them all)
	void togglePickedForTest(const BuilderSelection &s) { togglePicked(s); }
	void clickItemForTest(const BuilderSelection &s, Qt::KeyboardModifiers mods = Qt::NoModifier);   // a real click on an item in the layout view
	QString listTextForTest(int row) const;
	int pickedRowsForTest() const;   // how many rows of the list are highlighted
	QPointF itemScreenPosForTest(const BuilderSelection &s) const;   // where the layout view draws an item
	// A box dragged in the layout view with Ctrl held, from one pixel position to another (real mouse events); the layout view must be the one showing.
	void boxSelectForTest(const QPointF &from, const QPointF &to);
	bool dragObjectForTest(int index, const QPointF &deltaPx);
	// The 3D view, for the self-test: show it (or go back to the 2D views), drag an object on the floor, or along one axis arrow of the selected item.
	void show3dView(bool on);
	void resetPaneSizes();   // View > Reset Window Layout
	bool dragObject3dForTest(int index, const QPointF &deltaPx, Qt::KeyboardModifiers mods = Qt::NoModifier);
	QPointF shiftPanBackground3dForTest(int index, const QPointF &deltaPx);
	bool dragAxis3dForTest(int index, int axis, double pixels);
	// Selects the object, presses on the inner part of its `axis` arrow (a `fraction` of the way to the tip: that is the object itself, not the arrow) and drags by
	// `deltaPx`; true if it moved in both X and Z, on the floor.
	bool dragInnerArrow3dForTest(int index, int axis, double fraction, const QPointF &deltaPx);
	bool dragRotate3dForTest(int index, int axis, double degrees);
	// With several picked (the picking is left alone): drag the Rotate ring of world axis `axis` by `degrees`, or the Scale handle of `axis` to `ratio` times its distance
	// from the middle of them, with real mouse events; true if the picked items changed.
	bool dragPickedRing3dForTest(int axis, double degrees);
	bool dragPickedScale3dForTest(int axis, double ratio);
	void setGizmoToolForTest(int tool) { m_view3d->setGizmoMode(static_cast<Scene3DView::GizmoMode>(tool)); }   // 0 Move, 1 Rotate, 2 Scale
	bool dragScale3dForTest(int index, int axis, double ratio);
	BuilderSelection selectionForTest() const { return m_sel; }
	void setMaterialForTest(int index, const scene_doc::Material &m);   // an object's material set as the user would leave it, and the properties rebuilt
	void setObjectTextForTest(int index, const std::string &text);      // a text object's words set, and the properties rebuilt
	bool dragViewMarker3dForTest(int viewIndex, int which, const QPointF &deltaPx, Qt::KeyboardModifiers mods = Qt::NoModifier);   // a saved view's marker in the 3D view
	void orbit3dForTest(double yawDeg, double pitchDeg);
	void frame3dForTest() { m_view3d->frameAll(); }
	Scene3DView *view3dForTest() { return m_view3d; }
	bool clickPickFocusForTest(int objectIndex);   // Pick focus, then a click on that object in the 3D view; true if the focus distance changed
	int wireframeDifferenceForTest();   // how many pixels of the 3D view differ between solid and wireframe
	scene_doc::Float3 dropPointForTest() const { return dropPoint(); }
	bool meshReadyForTest(const QString &path) const;
	bool gizmoButtonEnabled(int tool) const;   // 0 Move, 1 Rotate, 2 Scale
	int gizmoButtonChecked() const;
	QCheckBox *denoiseCheckForTest() const { return m_denoiseCheck; }
	void selectCameraForTest() { setSelection({SelKind::Camera, 0}); }
	QWidget *inspectorContentForTest() const;   // the whole properties page (what the scroll area scrolls over), for taking its picture
	void addObject(scene_doc::ShapeKind shape);
	void addProp(scene_doc::PropKind kind);   // a few ordinary objects at once (a table, a tree...), one undo step
	void addBlocky(scene_doc::BlockyKind kind);   // a block, creature or thing made of boxes (scene_blocks.h), one undo step
	void duplicateSelected();                                  // a numbered copy beside the selected object or light (the Duplicate button, Ctrl+D)
	void addCopies(const std::vector<scene_doc::Object> &copies);   // objects made from another one (an array, a scatter): one undo step, the first selected
	void showArrayDialog();                                    // the Array... button: a grid, ring or scatter of copies of the selected object
	// The objects go in together at the drop point, named apart from what is there, as a group of that name (two or more) and all picked.
	void addParts(std::vector<scene_doc::Object> parts, const QString &groupName = QString());
	void addVolume(scene_doc::VolumeKind kind);   // fog, smoke or a cloud
	void addLight(scene_doc::LightKind kind);
	void applyLightingPreset(scene_doc::lighting::Preset preset);   // the lights become a ready-made lighting (one undo step)
	void addObjectFromPhoto();  // scene_builder_photo.cpp: needs the optional photo helper
	void addRoomFromPhoto();    // scene_builder_photo.cpp: a photo of a room becomes a room (the same helper environment, plus a depth model)
	void showModelLibrary();    // scene_builder_models.cpp: the dialog behind Add > Model library...
	// Adds one library model (the stem of models/<stem>.obj) at a handy size, standing on the floor under the drop point, as one undo step. False when it is not found.
	bool addLibraryModel(const QString &stem);
	// Runs the helper on one photo (progress dialog) and adds the mesh. False with `error` set (empty when cancelled) if nothing was added.
	bool importPhoto(const QString &photo, QString *error);
	// Runs the room helper on one photo of a room (progress dialog) and REPLACES the scene with the room it finds (one undo step). Camera height in metres and the photo's horizontal
	// field of view in degrees, 0 to let the helper decide; `furniture` is how many pieces of furniture to find and make 3D objects of (0: an empty room). False with `error` set (empty
	// when cancelled) if the scene was left alone; `notes` gets what the helper had to assume.
	bool importRoom(const QString &photo, double cameraHeight, double fov, int furniture, QString *error, QStringList *notes = nullptr, bool plainBox = false);
	// Sets the room's lights so that a small draft render of it is as bright as the photo (the helper's own light level is a guess that can be several times off); returns the factor
	// it applied (1 when the file has no brightness or the renderer is not there). `folder` is where the draft's scratch files go.
	double calibrateRoomLight(scene_room::Import &room, const QString &folder);
	// Material from a photo (scene_builder_material_photo.cpp): the "Make from a photo..." button of a material. materialFromPhoto asks for the photo and the kind of surface and applies it
	// (one undo step); applyMaterialPhoto is that without the dialogs: the photo becomes a seamless colour picture and a height map saved with the Builder's pictures, and the object's
	// material takes them (a coated or plain diffuse by `surfaceKind`, an index into material_from_photo::surfaceKinds()), with the picture repeated as often as `patchCm`, the width in
	// centimetres of the photographed patch, says (0: repeats left as they were). False with `error` set when it cannot; `notes` gets what the user should know.
	void materialFromPhoto(int objectIndex);
	bool applyMaterialPhoto(int objectIndex, const QString &photo, int surfaceKind, double patchCm, QString *error, QStringList *notes = nullptr);
	// Light from a photo (scene_builder_light_photo.cpp): the "Match to a photo..." button of a Sun & sky light. lightFromPhoto asks for the photo and applies it; applyLightPhoto is that
	// without the dialogs: the sky in the photo (src/shared/light_from_photo.h) sets lights[lightIndex] (an Infinite light, made a Sun & sky if it was not) to the sun height, direction
	// (relative to where the Builder camera looks) and haze it shows, one undo step. `hfov` is the photo's horizontal field of view in degrees, `tilt` how far the camera looked up when
	// `tiltGiven` (else it is read off the horizon). False with `error` set when the photo cannot be used; `notes` gets what the user should know.
	void lightFromPhoto(int lightIndex);
	bool applyLightPhoto(int lightIndex, const QString &photo, double hfov, bool tiltGiven, double tilt, QString *error, QStringList *notes = nullptr);
	// Camera from a photo (scene_builder_camera_photo.cpp): the "Match to a photo..." button of the camera. cameraFromPhoto asks for the photo and the settings and applies it; applyCameraPhoto is
	// that without the dialogs: the photo's lens (`hfov`, its horizontal field of view in degrees; 0: from the photo's EXIF data, else a phone's), tilt and roll (read off the horizon unless
	// given) become the scene's camera, which keeps the heading it has and is put `heightMetres` above the ground; the picture takes the photo's shape; the photo is laid over the camera view;
	// and with `alsoSky` the Sun & sky are set from the photo too. One undo step for the camera. False with `error` set when the photo cannot be read; `notes` gets what the user should know.
	void cameraFromPhoto();
	bool applyCameraPhoto(const QString &photo, double hfov, double heightMetres, bool tiltGiven, double tilt, bool rollGiven, double roll, bool alsoSky, QString *error, QStringList *notes = nullptr);
	// The photo as the environment (src/shared/environment_from_photo.h): the scene's first Infinite light gets, as its picture, the photo placed where the camera looks (the camera must be matched
	// to a photo, and is where the photo was taken from) with the rest of the sphere continued from the photo's own colours. The Sun light stays. One undo step. False with `error` set when it cannot.
	bool applyEnvironmentPhoto(QString *error, QStringList *notes = nullptr);
	static double exifHorizontalFov(const QString &photo);   // the photo's horizontal field of view from its EXIF lens data, 0 when it has none
	// Render into the photo (scene_builder_composite.cpp, src/shared/photo_composite.h): needs the photo the camera was matched to. The scene is rendered three times (as it is, without the
	// inserted objects, and as a mask of them) and put into the photo with its shadows. `groundObjects` are the indices of the objects that are the ground the photo already has (not drawn,
	// only their shadows count); lights stay lights; the rest is inserted. `exposureStops` brightens or darkens the inserted objects. Saves a PNG; false with `error` set when it cannot.
	void renderIntoPhotoDialog();
	// `matchToPhoto`: the objects' exposure and colour cast are read from the photo's sky (photo_composite::matchToPhoto; needs the Sun & sky set from the same photo) and `exposureStops` is added to it.
	bool renderIntoPhoto(const QString &outPng, const std::vector<int> &groundObjects, double exposureStops, int samples, bool matchToPhoto, QString *error, QStringList *notes = nullptr);
	// The same, giving the picture itself (at most `maxWidth` pixels across) instead of a file: the Builder shows it in its preview pane (showInPreview).
	bool renderIntoPhotoImage(QImage *result, const std::vector<int> &groundObjects, double exposureStops, int samples, int maxWidth, bool matchToPhoto, QString *error, QStringList *notes = nullptr);
	// "Preview in the photo": the same at the Quality setting of the Preview, with the ground, exposure and matching last used (the first time: the floor is the ground, the sky matches when there is one).
	bool renderIntoPhotoRoles(QImage *result, const std::vector<photo_composite::Role> &roles, double exposureStops, int samples, int maxWidth, bool matchToPhoto, QString *error, QStringList *notes = nullptr);
	std::vector<photo_composite::Role> compositeRoles() const;   // what each object is for the picture: as last said in the dialog, else guessed
	void previewIntoPhoto();
	void previewIntoPhotoWith(int maxWidth, int samples, bool live);   // previewIntoPhoto at a chosen size; `live`: the quiet, small refresh of the Live tick
	void scheduleLiveComposite();   // the scene changed: with Live on, a picture in the photo is made when it has been still for a moment (and one in progress is stopped: it is out of date)
	void runLiveComposite();
	void showInPreview(const QPixmap &picture, const QString &status);   // the preview pane shows this picture (and "Save picture..." can save it)
	void onSavePictureClicked();                                           // "Save picture...": the picture in the preview pane, as a PNG
	bool previewHasPictureForTest() const { return !m_previewPixmap.isNull(); }
	bool denoiseTickedForTest() const;
	bool compositeRunningForTest() const { return m_compositeRunning; }
	void setLiveForTest(bool on);
	int liveRefreshesForTest() const { return m_liveRefreshes; }
	int compositeCacheHitsForTest() const { return m_compositeCacheHits; }
	void setPhotoExposureForTest(double v) { m_doc.photo.exposureStops = v; m_doc.photo.roles = {{"Floor", 0}}; documentChanged(); }   // what the Render into the photo dialog keeps in the document
	void removePhotoForTest() { m_doc.photo = scene_doc::PhotoBackdrop(); documentChanged(); }   // what the Remove the photo button does
	QString launcherPathForTest() const { return launcherPath(); }
	QString previewButtonTextForTest() const;
	QString previewStatusForTest() const;
	void pressPreviewForTest();   // the Preview button, which reads Cancel while a render runs
	void setDenoiseForTest(bool on);   // the Denoise tick next to Preview, which Render into the photo and Preview in the photo follow
	QSize previewPictureSizeForTest() const { return m_previewPixmap.size(); }
	void updateCameraPhotoOverlay();                         // shows (or takes away) the photo over the camera view
	// The photo belongs to the scene (Document::photo: its file, whether it is shown, how strongly, and what Render into the photo was told), so saving and opening a scene keeps it. The members below
	// are what the window works from, brought in line with the document whenever it changes (documentChanged, an open, a new scene, an undo): this loads the photo when its file is another one.
	void syncCameraPhoto();
	// What is in front of the objects in the photo (Paint what is in front...): a mask painted on the photo, kept in the document (photo.occluder) and used by the composite so that the photo stays in front
	// of the objects there. occluderBytes is the mask at a picture's size (one byte a pixel; empty when there is none).
	void paintOccluderDialog();
	static QImage occluderFromText(const QString &text);
	std::vector<unsigned char> occluderBytes(int w, int h) const;
	bool occluderPresentForTest() const { return !m_occluder.isNull(); }
	void setOccluderForTest(const QImage &mask);   // a mask in the document, as the painting window would leave it
	static QImage loadCameraPhotoImage(const QString &file);   // the photo as the camera view needs it: turned upright, RGB, at most 1600 pixels across; null when it cannot be read
	// Place on the photo's ground (scene_builder_camera_photo.cpp): placeByClicking looks through the camera and waits for clicks on the ground in the picture; each click stands the picked items
	// there (placePickedAt, one undo step), on the ground y = 0 (the camera is `height` above it). setPickedHeight makes the picked items `metres` tall about the middle of their base.
	void placeByClicking();
	bool placePickedAt(const scene_doc::Float3 &where);
	bool setPickedHeight(double metres);
	void setPickedHeightDialog();
	Scene3DView *view3dForTest() const { return m_view3d; }
	bool cameraPhotoShownForTest() const { return m_cameraPhotoShown && !m_cameraPhoto.isNull(); }
	// See scene_builder_inspector.cpp: an upright copy of a picture whose EXIF tag asks for a rotation (the renderer ignores that tag).
	static QString uprightPictureCopy(const QString &path, QSize *size);
	// Arrange (scene_builder_arrange.cpp): hide and lock what is picked, put it on the floor, rename it, frame it.
	void hideSelected(bool hide);
	void lockSelected(bool lock);
	void showAllHidden();
	void unlockAllLocked();
	void dropSelectedToFloor();
	void frameSelected();
	void renameSelected();
	void renameItem(const BuilderSelection &s, const QString &wanted);
	// The rendered viewport (scene_builder_rendered.cpp): the 3D view shows a progressive path-traced picture of the scene, kept up to date as you edit. Needs the Live Preview
	// renderer; the main window stops a running Live Preview when this starts, and calls stopRenderedViewport() when Live Preview starts (one renderer at a time).
	static bool renderedViewportAvailable();
	void setRenderedViewport(bool on);
	bool renderedViewport() const { return m_viewportOn; }
	void stopRenderedViewport();
	int renderedFramesForTest() const { return m_viewportFrames; }
	int renderedRestartsForTest() const { return m_viewportRestarts; }   // how many times the renderer was started on a new copy of the scene
	int renderedSamplesForTest() const { return m_viewportSamples; }
	QImage renderedImageForTest() const { return m_viewportImage; }
	// "Search actions" (F3, Ctrl+K): type what to do and run it (scene_builder_actions.cpp; the matching is src/shared/action_search.h).
	void showActionSearch();
	QStringList actionNamesForTest();
	bool runActionForTest(const QString &words);   // runs the best match for the words, as if picked in the box
	void deleteSelected();
	bool undo();
	std::size_t undoDepth() const { return m_history.serial(); }   // (not undoSize(): that stops growing when the list is full)
	void undoTo(std::size_t depth) { while (m_history.serial() > depth && undo()) {} }   // (the photo windows take back their own last apply before the next, so they leave one apply behind)
	bool redo();
	// A video of the scene from a camera that goes once round the point the camera looks at (scene_builder_turntable.cpp). The window asks for the length, speed and
	// quality and where to save the MP4; startTurntable() renders `frames` pictures `width` pixels wide (the height follows the scene's picture shape) and puts them together
	// at `fps`, then copies the video to `outMp4` and calls `done`.
	void showTurntableDialog();
	void startTurntable(int frames, int fps, int width, int samples, const QString &outMp4, const std::function<void(bool ok, const QString &message)> &done = nullptr);
	// A video of the scene from a camera that flies through the saved camera views, in the order they were saved (and back to the first at the end if `returnToStart`); `ease`
	// starts and ends it gently. Needs two or more views. The lens is the first view's.
	void showFlythroughDialog();
	void startFlythrough(int frames, int fps, int width, int samples, const QString &outMp4, bool ease, bool returnToStart, const std::function<void(bool ok, const QString &message)> &done = nullptr);
	// A video of a day (scene_builder_daycycle.cpp, src/shared/day_cycle.h): the sun of the scene's Sun & sky goes from sunrise to sunset (crossing where it is now at noon, `noonElevation` degrees high)
	// over `frames` frames while the camera stays; every frame is a render, and the renderer puts them together at `fps` (--assemble-video). Copies the MP4 to `outMp4`; false with `message` set when it
	// cannot. `framesOut`, when given, gets the pictures (a test looks at them). The Preview button reads Cancel while it runs.
	void showDayCycleDialog();
	bool renderDayCycle(const QString &outMp4, int frames, int fps, int width, int samples, double noonElevation, QString *message, std::vector<QImage> *framesOut = nullptr);
	// Starts a preview render (no file dialog) and calls `done` when the picture is loaded or it failed.
	void startPreview(const std::function<void(bool ok, const QString &message)> &done = nullptr);
	QString previewImagePath() const { return m_previewPng; }
	// The "Use the GPU" checkbox (OptiX on Windows, Metal on a Mac); the self-test drives it.
	void setUseGpu(bool on);
	bool isDirty() const { return m_dirty; }
	QString problemsText() const;
	// Where a scene is saved so the scene list finds it (empty if no scene folder exists); see saveToSceneList().
	static QString sceneListFolder();
	static bool hasAutosave();           // an unsaved scene from the last run is waiting to be restored
	static QString setAsideAutosave();   // renames it (never deletes it); returns the new path, "" if there was none
	// Saves a copy of the scene into the scene-list folder (see sceneListFolder()); no dialogs. Returns the path, or "" with `error` set. An existing file
	// of that name is replaced.
	// With `update` false (the default) a scene of that name already in the folder is never touched: the copy gets the next free name ("my-scene-2").
	// With `update` true and this document already listed (listedPath()), that listing is overwritten.
	QString addToSceneList(QString *error, bool update = false);
	// The scene-list file this document was last added as, or opened from (empty if none).
	QString listedPath() const { return m_listedPath; }
	// Shows or hides the "Preview live" button (the main window knows whether a Live Preview library is there).
	void setLivePreviewAvailable(bool available);
	// Opens a scene file the Scene Builder saved, asking first what to do with unsaved changes; false when the user cancels or the file cannot be opened (then a message says why).
	bool openForEditing(const QString &path);
	// The scene's name (its title), as typed in the toolbar; setSceneName() is an undoable edit like typing there.
	QString sceneName() const { return QString::fromStdString(m_doc.title); }
	void setSceneName(const QString &name);

signals:
	void renderedViewportStarting();   // the main window stops a running Live Preview: only one renderer can have the GPU
	void statusMessage(const QString &text);
	// A line for the Log Output pane (the main window shows it with a time, and keeps it in the log file): what the Scene Builder is doing while it works, as the renders and the photo features report it.
	void logMessage(const QString &line);
	// A copy of the scene was saved into the scene-list folder ("Add to scene list"); the main window lists it without a restart.
	void sceneListed(const QString &path);
	// The Denoise tick was ticked and the CPU denoiser (Intel Open Image Denoise) is not installed: the main window offers to download it (its own prompt, shared with the Render Options tab).
	// `onDeclined` runs when it was declined, cancelled or failed (the tick goes off again); `onInstalled` when the library is there.
	void denoiserInstallRequested(const std::function<void()> &onDeclined, const std::function<void()> &onInstalled);
	// "Preview live": the scene was saved as a listing of its own (livePreviewCopy()) and should be opened in Live Preview; the main window does that.
	void livePreviewRequested(const QString &path);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void showEvent(QShowEvent *e) override;
	void hideEvent(QHideEvent *e) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
	void onListSelectionChanged();
	void onOpenClicked();
	void onSaveClicked();
	void onSaveAsClicked();
	void onSaveToSceneListClicked();
	void onPreviewLiveClicked();
	// Saves the scene, titled "<name> (live preview)", as <name>-live-preview.pbrt in the scene-list folder (replaced each time: one preview copy per scene name), so
	// Live Preview can open it by a scene id without touching the scene's own listing or file. Returns the path, or "" with `error` set.
	QString writeLivePreviewCopy(QString *error);
	void onRenderFinalClicked();
	void onTurntableFinished(int exitCode);
	void onPreviewFinished(int exitCode);

private:
	using SelKind = BuilderSelection::Kind;

	void buildUi();
	void rebuildList();
	void refreshListLabels();
	void rebuildInspector();
	// Says what is being done: a line in the Log Output pane when the main window listens (it also writes the log file), else straight into the log file.
	void logLine(const QString &category, const QString &text, log_format::Level level = log_format::Level::Info);
	void logLine(log_format::Level level, const QString &category, const QString &text) { logLine(category, text, level); }   // (the order AppLog::write takes)
	void refreshInspectorValues();
	void updatePreviewPixmap();
	void setSelection(const BuilderSelection &s, bool fromList = false);   // one item (or none): the others are let go
	void applySelection(const BuilderSelection &s, bool fromList);      // m_sel is the main item; m_extra the others, as they are
	void syncListSelection();                                           // highlights the picked items' rows
	// scene_builder_multi.cpp
	void pickItems(const scene_doc::ItemSet &items, const BuilderSelection &mainItem = BuilderSelection{});
	void selectWithGroup(const BuilderSelection &s);   // a click in a view: the whole group when the object is in one
	void togglePicked(const BuilderSelection &s);      // Ctrl- or Shift-click: add or remove (a group goes in or out whole)
	void boxPicked(const QList<BuilderSelection> &items, bool additive);
	void inspectMultiple(QFormLayout *f);
	scene_doc::Float3 snapDragTarget(const BuilderSelection &s, const scene_doc::Float3 &wanted);   // where a drag of `s` to `wanted` ends up, with what it snapped to shown
	void transformPicked(bool turn, int axis, double amount);   // a drag in the 3D view: the total turn or factor since it began
	scene_doc::Document m_groupBase;                            // the document when the current group turn or stretch began
	int m_groupBaseDrag = -1;                                   // m_editCounter of that drag
	void inspectCameraViews(QFormLayout *f);
	void showVideoDialog(bool flythrough);   // scene_builder_turntable.cpp: the Turntable and Flythrough windows
	void startCameraVideo(int frames, int fps, int width, int samples, const QString &outMp4, const QString &keyframes, const scene_doc::Document *sceneDoc,
	                      const std::function<void(bool, const QString &)> &done);
	void createCameraBar(QWidget *layoutBox);
	void refreshViewCombo();
	void refreshScrub();                      // the scrub slider follows the saved views: off with fewer than two, and the picture they show is kept current
	void scrubFlythrough(double progress);
	void startPlay();                         // the Flythrough preview plays from where the slider is (from the start if it is at the end)
	void stopPlay();
	void advancePlay(double seconds);         // the playing preview moves on by that much video time; stops at the end    // the 3D view looks from that point on the flythrough
	scene_doc::Camera cameraNow() const;
	void logSelection(const BuilderSelection &s, const char *where);   // what the user picked, and where (the list, the layout view, the 3D view)
	void documentChanged();
	void refreshProblems();
	void updateTitle();
	void updateActions();

	// An edit: takes an undo snapshot (merging a run of edits with the same key, such as a spin box being dragged), applies `mutate`, and refreshes.
	void edit(const QString &key, const std::function<void()> &mutate);
	// Sun & sky (src/shared/scene_sky.h): while an Infinite light has it on, its picture and the scene's Sun follow its sun settings. The sun is brought into line
	// right after an edit that changed those settings (skySignature() differs from the one the document had before), and only then, so a Sun the user moved by
	// hand stays where it is until the sky is changed again.
	std::string skySignature() const;
	void syncSunAndSky();
	static QString defaultSlidePicture();   // the picture a new projector light starts with (a window): made once, in <user assets>/slides
	static QString skyImageFolder();   // where the generated sky pictures go: <user assets>/skies
	void pushUndo();
	bool restore(const std::string &json);   // false (and nothing changed) if the snapshot cannot be read
	void scheduleAutosave();
	void writeAutosave();
	void clearAutosave();
	bool loadAutosave();
	bool confirmDiscard();

	// Inspector building blocks. Each binds to the live value through a pointer-returning function, so a rebuilt document is never dangling.
	void addNum(QFormLayout *f, const QString &label, const std::function<double *()> &ref, double lo, double hi, double step, int decimals = 3,
	            const QString &suffix = QString());
	void addInt(QFormLayout *f, const QString &label, const std::function<int *()> &ref, int lo, int hi);
	// A number the document does not store but works out from what it does (a focal length from the field of view): `get` reads it, `set` makes the edit it means. `special`, if
	// not empty, is shown for the lowest value (0).
	void addDerivedNum(QFormLayout *f, const QString &label, const std::function<double()> &get, const std::function<void(double)> &set, double lo, double hi, double step, int decimals,
	                   const QString &suffix = QString(), const QString &special = QString());
	void beginPickFocus();   // the next click in the 3D view puts what is under it in focus
	void addVec3(QFormLayout *f, const QString &label, const std::function<scene_doc::Float3 *()> &ref, double step);
	void addColor(QFormLayout *f, const QString &label, const std::function<scene_doc::Rgb *()> &ref);
	void addBool(QFormLayout *f, const QString &label, const std::function<bool *()> &ref, bool rebuildAfter = false);
	void addText(QFormLayout *f, const QString &label, const std::function<std::string *()> &ref);
	void addMultilineText(QFormLayout *f, const QString &label, const std::function<std::string *()> &ref);   // (a text with line breaks, in a box of its own)
	// A file row: read-only path, Browse (and Clear, when `clearable`). `alsoApply`, if given, runs inside the same undo step as the choice.
	void addFile(QFormLayout *f, const QString &label, const std::function<std::string *()> &ref, const QString &filter, bool clearable = false,
	             const std::function<void(const QString &)> &alsoApply = nullptr);
	void inspectCamera(QFormLayout *f);
	scene_doc::ItemSet unlockedPicked() const;   // the picked items a drag in a view may move (not the locked or hidden ones)
	bool isLockedItem(const BuilderSelection &s) const;
	QMenu *createArrangeMenu(QWidget *parent);
	void createArrangeShortcuts();
	void syncRenderedViewport();
	bool moveViewportObjects();
	void startViewportSession(const scene_viewport::Pose &pose);
	void pauseViewportIfHidden(bool hidden);
	scene_viewport::Pose viewportPose() const;
	QString livePreviewCopyPath(QString *error);       // where the "(live preview)" copy of the scene goes
	bool writeSceneCopy(const scene_doc::Document &doc, const QString &path);
	void createActionSearchShortcuts();
	struct Command { QString label; std::function<void()> run; };   // one thing the search box can do
	std::vector<Command> buildCommands();
	void addVisibilityRows(QFormLayout *f, const std::function<bool *()> &hidden, const std::function<bool *()> &locked);
	void decorateListItem(QListWidgetItem *it, bool hidden, bool locked);
	QPushButton *m_renderedButton = nullptr;  // Rendered: the 3D view shows a rendered picture of the scene
	RealtimePreviewSession *m_viewportSession = nullptr;
	QTimer *m_viewportTimer = nullptr;
	bool m_viewportOn = false;
	bool m_viewportRunning = false;           // the renderer has been started on the scene as it is (or was, until an edit)
	bool m_viewportPending = false;           // the scene changed: restart once the edits have paused
	bool m_viewportDocChanged = false;        // documentChanged() ran since the signature was last worked out
	QElapsedTimer m_viewportEditClock;
	std::string m_viewportSignature;
	scene_viewport::Pose m_viewportSent;
	scene_doc::Document m_viewportBase;                      // the document the renderer's copy was written from
	std::vector<std::array<double, 3>> m_viewportOffsets;    // where the renderer has been told each object is, from the copy (empty: none)
	int m_viewportRestarts = 0;
	bool m_viewportFailed = false;                           // the last attempt to start the renderer failed: wait before another
	QElapsedTimer m_viewportRetryClock;
	bool m_viewportAwaitNew = false;                         // the copy was just started and no frame of it has arrived: a move would not be known to the renderer yet
	int m_viewportSamplesAtRestart = 0;                      // the samples of the last frame of the render before (a new render's frames have fewer)
	std::string m_viewportBaseSignature;                     // scene_viewport::sceneSignature of m_viewportBase
	QString m_viewportSceneId;
	int m_viewportFrames = 0;
	int m_viewportSamples = 0;
	QImage m_viewportImage;
	QPushButton *m_wireButton = nullptr;      // the 3D view's Wireframe and Orthographic switches
	QPushButton *m_orthoButton = nullptr;
	QPushButton *m_arrangeButton = nullptr;   // the Arrange menu: rename, drop to floor, frame, hide, lock
	void inspectVolume(QFormLayout *f, int i);
	void inspectObject(QFormLayout *f, int i);
	void inspectLight(QFormLayout *f, int i);
	void inspectMaterial(QFormLayout *f, int i);
	void addMixRows(QFormLayout *f, int i);   // the last of a material's rows: mix it with a second material (scene_builder_mix.cpp)

	QString workFolder() const;
	bool writeSceneFile(const QString &path, QString *error, const scene_doc::Document *doc = nullptr) const;   // the scene (or `doc`) as pbrt text, for a renderer to read
	QString launcherPath() const;
	void runRender(int width, int height, int samples, bool toFinalFile, const QString &finalPng,
	               const std::function<void(bool, const QString &)> &done);

	scene_doc::Document m_doc;
	BuilderSelection m_sel;
	std::vector<BuilderSelection> m_extra;   // the other items picked along with m_sel
	QString m_path;                  // the .pbrt file this scene was opened from or saved to; empty for an unsaved one
	bool m_dirty = false;
	bool m_loading = false;          // true while the inspector is being filled, so setting a value does not count as an edit
	SnapshotHistory m_history;   // undo and redo
	QStringList m_loggedProblems;   // the problems already written to the log, so each is said once when it appears
	std::vector<scene_doc::Problem> m_problems;   // validate() of the current document, refreshed by refreshProblems()
	QString m_lastEditKey;
	std::string m_skySignature;
	// What changed since the last undo step began, for the log file: one line per step ("object 'Ball': position 0,1,0 -> 2,1,0"), not one per drag movement.
	scene_doc::Document m_logBase;
	bool m_logPending = false;
	QTimer *m_logTimer = nullptr;
	void flushEditLog();
	QElapsedTimer m_editClock;
	int m_editCounter = 0;
	bool m_autosaveEnabled = true;
	QTimer *m_autosaveTimer = nullptr;

	// UI
	QLabel *m_titleLabel = nullptr;
	QLineEdit *m_titleEdit = nullptr;   // the scene's name, editable in the toolbar
	QString m_listedPath;
	QListWidget *m_list = nullptr;
	SceneLayoutView *m_view = nullptr;
	Scene3DView *m_view3d = nullptr;
	QImage m_occluder;                     // photo.occluder as a picture (grayscale, the photo's size); null when there is none
	QString m_occluderText;                // the text it was decoded from
	QString m_cameraPhotoFile;             // the file m_cameraPhoto was loaded from (Document::photo.file)
	QImage m_cameraPhoto;                  // the photo the camera was matched to, laid over the camera view while m_cameraPhotoShown
	bool m_cameraPhotoShown = false;
	double m_cameraPhotoOpacity = 0.5;
	QStackedWidget *m_viewStack = nullptr;  // m_view (Top / Front / Side) or m_view3d, one at a time
	QLabel *m_viewHint = nullptr;
	QWidget *m_gizmoBar = nullptr;           // the 3D view's Move / Rotate / Scale buttons
	bool m_snapObjects = true;
	QPointF m_holdEnd;   // where holdDragForTest() left the mouse
	QCheckBox *m_snapObjectsCheck = nullptr;
	QPushButton *m_playButton = nullptr;     // Play / Pause: the scrub slider runs by itself, at the video's pace
	QTimer *m_playTimer = nullptr;
	QElapsedTimer m_playClock;
	double m_playProgress = 0.0;             // where the playing preview is, 0..1 (the slider is only 1000 steps)
	QLabel *m_scrubLabel = nullptr;          // the "Flythrough:" label beside it (hidden with it)
	QSlider *m_scrubSlider = nullptr;        // Flythrough: scrubs the 3D view along the saved views' path (0..1000)
	QCheckBox *m_pathCheck = nullptr;        // Camera path: draw the flythrough's path in the 3D view
	QWidget *m_cameraBar = nullptr;          // the 3D view's camera buttons: Through camera, Camera from view, the saved views
	QPushButton *m_throughButton = nullptr;
	QComboBox *m_viewCombo = nullptr;
	// scene_builder_views.cpp: building the views, and doing one thing to both (they follow the same document and selection).
	void createViews(QWidget *layoutBox, QVBoxLayout *layoutLayout);
	void updateViews();
	void selectInViews(const BuilderSelection &s);
	void updateGizmoButtons(const BuilderSelection &s);
	QButtonGroup *m_gizmoGroup = nullptr;  // the Move / Rotate / Scale buttons
	void frameViews();
	scene_doc::Float3 dropPoint() const;  // where a new object goes: the middle of the view on show
	QWidget *m_inspectorHost = nullptr;
	QScrollArea *m_inspectorScroll = nullptr;
	QLabel *m_problemsLabel = nullptr;
	QLabel *m_previewLabel = nullptr;
	QLabel *m_previewStatus = nullptr;
	QComboBox *m_qualityCombo = nullptr;
	QCheckBox *m_gpuCheck = nullptr;
	void createDenoiseCheck(QWidget *parent);   // scene_builder_render.cpp: the Preview row's Denoise tick
	QCheckBox *m_denoiseCheck = nullptr;     // Denoise: the Preview picture is cleaned by the AI denoiser (OIDN on the CPU, OptiX's own with the GPU on Windows)
	QPushButton *m_previewButton = nullptr;
	QPushButton *m_inPhotoButton = nullptr;   // "Preview in the photo"
	QPushButton *m_saveButton = nullptr;      // "Save picture..." (the picture in the preview pane)
	std::vector<std::pair<std::string, int>> m_compositeRoles;   // what each object (by name) was last said to be in Render into the photo: a photo_composite::Role
	double m_compositeExposure = 0.0;            // the exposure last given there
	QPushButton *m_finalButton = nullptr;
	QPushButton *m_turntableButton = nullptr;
	QPushButton *m_flythroughButton = nullptr;
	QPushButton *m_dayCycleButton = nullptr;
	QString m_turntableOut;
	int m_turntableFrames = 0;
	QPushButton *m_deleteButton = nullptr;
	QPushButton *m_duplicateButton = nullptr;
	QPushButton *m_arrayButton = nullptr;
	QPushButton *m_addButton = nullptr;
	QWidget *m_inspectorPanel = nullptr;  // the properties column
	QSplitter *m_mainSplit = nullptr;     // list | view and preview | properties; its sizes are remembered between runs (window_geometry.h)
	QSplitter *m_centreSplit = nullptr;   // view above preview
	QScrollArea *m_centreScroll = nullptr;   // the scrolling column that holds them (the preview is below the fold in a short window)
	QWidget *m_leftPanel = nullptr;  // the list column; widened to fit its buttons when shown
	QPushButton *m_liveButton = nullptr;   // "Preview live"; hidden until the main window says Live Preview is there
	QPushButton *m_undoButton = nullptr;
	QPushButton *m_redoButton = nullptr;

	// A running render
	QProcess *m_process = nullptr;
	QElapsedTimer m_renderClock;
	QString m_previewPng;
	QString m_pendingFinalPng;
	std::function<void(bool, const QString &)> m_pendingDone;
	QString m_renderLog;
	bool m_cancelRequested = false;
	bool m_compositeRunning = false;   // Render into the photo / Preview in the photo is rendering (its three renders run from renderIntoPhotoRoles, with the window still usable)
	bool m_compositeCancel = false;    // the Preview button (Cancel while it runs) was pressed
	bool m_compositeLive = false;      // the running composite is a Live refresh: quiet (no log lines, no first look, no scrolling to the picture)
	QCheckBox *m_liveCheck = nullptr;  // "Live": the picture in the photo follows the scene
	QTimer *m_liveTimer = nullptr;
	int m_liveRefreshes = 0, m_compositeCacheHits = 0;
	// The last render of each of the three passes (as it is, without the inserted objects, mask), kept with what made it (the scene text and the renderer's arguments): when nothing that a pass
	// depends on has changed (moving an inserted object leaves the ground pass as it was) the pass is not rendered again.
	struct CompositePass {
		QByteArray key;
		std::vector<float> rgb;
		int w = 0, h = 0;
	} m_compositeCache[3];
	QPixmap m_previewPixmap;
	std::vector<std::function<void()>> m_refreshers;  // re-read each inspector field from the document (after a drag moves something)
};
