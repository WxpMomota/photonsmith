// scene_builder_inspector.cpp - the Scene Builder's property panel (SceneBuilderWidget::rebuildInspector and the inspect*/add* builders).
#include "scene_builder_widget.h"

#include "scene_builder_common.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QIcon>
#include <QImage>
#include <QDir>
#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QImageReader>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QRegularExpression>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using scene_doc::Document;
using scene_doc::Float3;
using scene_doc::Light;
using scene_doc::LightKind;
using scene_doc::MaterialKind;
using scene_doc::Object;
using scene_doc::Rgb;
using scene_doc::ShapeKind;

using namespace scene_builder_ui;

void SceneBuilderWidget::rebuildInspector() {
	m_refreshers.clear();
	auto *host = new QWidget;
	// The application stylesheet pads spin boxes generously; three of them in a row need less.
	host->setStyleSheet("QAbstractSpinBox { padding: 3px 3px; min-width: 40px; }");
	auto *form = new QFormLayout(host);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);  // in a narrow panel a label moves above its field instead of pushing the field off the edge
	m_loading = true;
	if (!m_extra.empty()) {   // several items picked
		inspectMultiple(form);
		m_loading = false;
		m_inspectorScroll->setWidget(host);
		if (m_inspectorPanel) m_inspectorPanel->setMinimumWidth(host->minimumSizeHint().width() + m_inspectorScroll->verticalScrollBar()->sizeHint().width() + 6);
		return;
	}
	switch (m_sel.kind) {
		case SelKind::Camera: inspectCamera(form); break;
		case SelKind::Object:
			if (m_sel.index < static_cast<int>(m_doc.objects.size())) inspectObject(form, m_sel.index);
			break;
		case SelKind::Light:
			if (m_sel.index < static_cast<int>(m_doc.lights.size())) inspectLight(form, m_sel.index);
			break;
		case SelKind::None: {
			auto *help = new QLabel(tr("Pick something in the list or the layout view to edit it.\n\n"
			                           "Add shapes and lights with the Add button. Drag them in the layout view, then press Preview to see the picture. "
			                           "Save writes an ordinary .pbrt file that the renderer (and this tab) can open."),
			                        host);
			help->setWordWrap(true);
			form->addRow(help);
			break;
		}
	}
	m_loading = false;
	m_inspectorScroll->setWidget(host);  // the scroll area deletes the previous page
	// The panel can be dragged as narrow as this page's widest row (with labels wrapped above their fields) plus the scroll bar, so nothing is cut off.
	if (m_inspectorPanel) m_inspectorPanel->setMinimumWidth(host->minimumSizeHint().width() + m_inspectorScroll->verticalScrollBar()->sizeHint().width() + 6);
}

void SceneBuilderWidget::refreshInspectorValues() {
	m_loading = true;
	for (const auto &r : m_refreshers) r();
	m_loading = false;
}

static void addHeading(QFormLayout *f, const QString &text) {
	auto *l = new QLabel("<b>" + text.toHtmlEscaped() + "</b>");
	l->setContentsMargins(0, 8, 0, 0);
	f->addRow(l);
}

void SceneBuilderWidget::addNum(QFormLayout *f, const QString &label, const std::function<double *()> &ref, double lo, double hi, double step, int decimals,
                                const QString &suffix) {
	auto *box = new QDoubleSpinBox;
	box->setRange(lo, hi);
	box->setDecimals(decimals);
	box->setSingleStep(step);
	box->setKeyboardTracking(false);
	if (!suffix.isEmpty()) box->setSuffix(suffix);
	if (double *v = ref()) box->setValue(*v);
	const QString key = QString("%1:%2:%3").arg(static_cast<int>(m_sel.kind)).arg(m_sel.index).arg(label);
	connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, ref, key](double x) {
		if (m_loading) return;
		edit(key, [&]() { if (double *v = ref()) *v = x; });
	});
	m_refreshers.push_back([box, ref]() {
		QSignalBlocker b(box);
		if (double *v = ref()) box->setValue(*v);
	});
	f->addRow(label, box);
}

void SceneBuilderWidget::addDerivedNum(QFormLayout *f, const QString &label, const std::function<double()> &get, const std::function<void(double)> &set, double lo, double hi, double step,
                                       int decimals, const QString &suffix, const QString &special) {
	auto *box = new QDoubleSpinBox;
	box->setRange(lo, hi);
	box->setDecimals(decimals);
	box->setSingleStep(step);
	box->setKeyboardTracking(false);
	if (!suffix.isEmpty()) box->setSuffix(suffix);
	if (!special.isEmpty()) box->setSpecialValueText(special);
	box->setValue(std::clamp(get(), lo, hi));
	connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, set](double x) {
		if (m_loading) return;
		set(x);
		refreshInspectorValues();   // (the rows that were worked out from the same numbers follow)
	});
	m_refreshers.push_back([box, get, lo, hi]() {
		QSignalBlocker b(box);
		box->setValue(std::clamp(get(), lo, hi));
	});
	f->addRow(label, box);
}

void SceneBuilderWidget::addInt(QFormLayout *f, const QString &label, const std::function<int *()> &ref, int lo, int hi) {
	auto *box = new QSpinBox;
	box->setRange(lo, hi);
	box->setKeyboardTracking(false);
	if (int *v = ref()) box->setValue(*v);
	const QString key = QString("%1:%2:%3").arg(static_cast<int>(m_sel.kind)).arg(m_sel.index).arg(label);
	connect(box, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, ref, key](int x) {
		if (m_loading) return;
		edit(key, [&]() { if (int *v = ref()) *v = x; });
	});
	m_refreshers.push_back([box, ref]() {
		QSignalBlocker b(box);
		if (int *v = ref()) box->setValue(*v);
	});
	f->addRow(label, box);
}

void SceneBuilderWidget::addVec3(QFormLayout *f, const QString &label, const std::function<Float3 *()> &ref, double step) {
	auto *row = new QWidget;
	auto *h = new QHBoxLayout(row);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(4);
	const char *axes[3] = {"X", "Y", "Z"};
	double Float3::*members[3] = {&Float3::x, &Float3::y, &Float3::z};
	for (int a = 0; a < 3; ++a) {
		auto *box = new QDoubleSpinBox;
		box->setRange(-10000, 10000);
		box->setDecimals(3);
		box->setSingleStep(step);
		box->setKeyboardTracking(false);
		box->setPrefix(QString(axes[a]) + " ");
		box->setButtonSymbols(QAbstractSpinBox::NoButtons);  // three boxes in a row have no room for arrows; type, or use the wheel
		box->setMinimumWidth(60);
		auto member = members[a];
		if (Float3 *v = ref()) box->setValue((*v).*member);
		const QString key = QString("%1:%2:%3:%4").arg(static_cast<int>(m_sel.kind)).arg(m_sel.index).arg(label).arg(a);
		connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, ref, member, key](double x) {
			if (m_loading) return;
			edit(key, [&]() { if (Float3 *v = ref()) (*v).*member = x; });
		});
		m_refreshers.push_back([box, ref, member]() {
			QSignalBlocker b(box);
			if (Float3 *v = ref()) box->setValue((*v).*member);
		});
		h->addWidget(box, 1);
	}
	f->addRow(label, row);
}

void SceneBuilderWidget::addColor(QFormLayout *f, const QString &label, const std::function<Rgb *()> &ref) {
	auto *b = new QPushButton;
	b->setAutoDefault(false);
	b->setToolTip(tr("Click to choose a colour. Colours are picked as ordinary (sRGB) colours and stored as linear values for the renderer."));
	auto paint = [b, ref]() {
		if (Rgb *c = ref()) {
			const QColor q = toQColor(*c);
			const QColor fg = q.lightness() > 128 ? Qt::black : Qt::white;
			b->setText(q.name().toUpper());
			b->setStyleSheet(QString("QPushButton { background-color: %1; color: %2; border: 1px solid #666; padding: 4px; }").arg(q.name(), fg.name()));
		}
	};
	paint();
	connect(b, &QPushButton::clicked, this, [this, ref, paint, b]() {
		Rgb *c = ref();
		if (!c) return;
		const QColor picked = QColorDialog::getColor(toQColor(*c), this, tr("Choose a colour"));
		if (!picked.isValid()) return;
		edit(QString(), [&]() { if (Rgb *v = ref()) *v = fromQColor(picked); });
		paint();
	});
	m_refreshers.push_back(paint);
	f->addRow(label, b);
}

void SceneBuilderWidget::addBool(QFormLayout *f, const QString &label, const std::function<bool *()> &ref, bool rebuildAfter) {
	auto *c = new QCheckBox;
	if (bool *v = ref()) c->setChecked(*v);
	connect(c, &QCheckBox::toggled, this, [this, ref, rebuildAfter](bool on) {
		if (m_loading) return;
		edit(QString(), [&]() { if (bool *v = ref()) *v = on; });
		if (rebuildAfter) QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
	});
	f->addRow(label, c);
}

void SceneBuilderWidget::addText(QFormLayout *f, const QString &label, const std::function<std::string *()> &ref) {
	auto *e = new QLineEdit;
	if (std::string *v = ref()) e->setText(QString::fromStdString(*v));
	const QString key = QString("%1:%2:%3").arg(static_cast<int>(m_sel.kind)).arg(m_sel.index).arg(label);
	connect(e, &QLineEdit::textEdited, this, [this, ref, key](const QString &t) {
		if (m_loading) return;
		edit(key, [&]() { if (std::string *v = ref()) *v = t.toStdString(); });
	});
	f->addRow(label, e);
}

// A phone photo is often stored sideways with an EXIF "rotate me" tag. The renderer ignores that tag, so the picture would show sideways (and a quad
// would take the wrong shape). When the tag asks for a rotation, this writes an upright PNG copy under <data>/pictures and returns its path;
// otherwise (no rotation, or a format Qt cannot read, such as .exr/.hdr/.tga) it returns `path` unchanged. `size` gets the upright size when known.
QString SceneBuilderWidget::uprightPictureCopy(const QString &path, QSize *size) {
	QImageReader reader(path);
	reader.setAutoTransform(true);
	const bool rotated = reader.transformation() != QImageIOHandler::TransformationNone;
	const QImage img = reader.read();
	if (img.isNull()) return path;
	if (size) *size = img.size();
	if (!rotated) return path;
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pictures";
	const QFileInfo info(path);
	const QString key = QString::number(qHash(info.absoluteFilePath() + QString::number(info.lastModified().toSecsSinceEpoch())), 16);
	const QString copy = dir + "/" + info.completeBaseName() + "-" + key + ".png";
	if (QFileInfo::exists(copy)) return copy;
	if (!QDir().mkpath(dir) || !img.save(copy)) return path;
	return copy;
}

void SceneBuilderWidget::addFile(QFormLayout *f, const QString &label, const std::function<std::string *()> &ref, const QString &filter, bool clearable,
                                 const std::function<void(const QString &)> &alsoApply) {
	auto *row = new QWidget;
	auto *h = new QHBoxLayout(row);
	h->setContentsMargins(0, 0, 0, 0);
	auto *e = new QLineEdit;
	e->setReadOnly(true);
	e->setMinimumWidth(40);  // the buttons keep their size; the path gives way
	if (std::string *v = ref()) e->setText(QString::fromStdString(*v));
	auto *browse = new QPushButton(tr("Browse..."));
	browse->setStyleSheet("padding: 6px 10px;");  // compact: the row shares a narrow panel with a label and a second button
	browse->setAutoDefault(false);
	connect(browse, &QPushButton::clicked, this, [this, ref, e, filter, alsoApply]() {
		const QString p = QFileDialog::getOpenFileName(this, tr("Choose a file"), e->text(), filter);
		if (p.isEmpty()) return;
		edit(QString(), [&]() {
			if (std::string *v = ref()) *v = p.toStdString();
			if (alsoApply) alsoApply(p);
		});
		e->setText(p);
		if (alsoApply) QTimer::singleShot(0, this, [this]() { rebuildInspector(); });  // what the choice changed is shown in other rows
	});
	h->addWidget(e, 1);
	h->addWidget(browse);
	if (clearable) {
		auto *clear = new QPushButton(tr("Clear"));
		clear->setStyleSheet("padding: 6px 10px;");
		clear->setAutoDefault(false);
		clear->setEnabled(!e->text().isEmpty());
		connect(clear, &QPushButton::clicked, this, [this, ref]() {
			edit(QString(), [&]() { if (std::string *v = ref()) v->clear(); });
			QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
		});
		h->addWidget(clear);
	}
	f->addRow(label, row);
}

void SceneBuilderWidget::inspectCamera(QFormLayout *f) {
	addHeading(f, tr("Scene"));
	addText(f, tr("Title"), [this]() { return &m_doc.title; });
	addHeading(f, tr("Camera"));
	addVec3(f, tr("Position"), [this]() { return &m_doc.camera.position; }, 0.25);
	addVec3(f, tr("Looks at"), [this]() { return &m_doc.camera.target; }, 0.25);
	addNum(f, tr("Field of view"), [this]() { return &m_doc.camera.fov; }, 1, 170, 1, 1, QString::fromUtf8(" \xC2\xB0"));
	// The lens as a photographer says it: a focal length on a sensor of a real size, and an f-number (scene_camera_lens.h). The document keeps the field of view and the lens radius
	// (what the renderer reads); these rows are worked out from them and edit them.
	addDerivedNum(f, tr("Focal length"), [this]() { return scene_doc::focalLengthMm(m_doc.camera.fov, scene_doc::aspectOf(m_doc.render), m_doc.camera.sensorWidthMm); },
	              [this](double mm) { edit(QStringLiteral("camera:focal"), [&]() { m_doc.camera = scene_doc::withFocalLength(m_doc.camera, mm, m_doc.render); }); }, 1, 2000, 1, 1, tr(" mm"));
	auto *lens = new QComboBox;
	lens->addItem(tr("Choose a lens..."), -1.0);
	for (const scene_doc::LensPreset &p : scene_doc::lensPresets()) lens->addItem(lensLabel(p.name), p.focalMm);
	connect(lens, QOverload<int>::of(&QComboBox::activated), this, [this, lens](int idx) {
		const double mm = lens->itemData(idx).toDouble();
		if (m_loading || mm <= 0.0) return;
		edit(QString(), [&]() { m_doc.camera = scene_doc::withFocalLength(m_doc.camera, mm, m_doc.render); });
		lens->setCurrentIndex(0);
		refreshInspectorValues();
	});
	f->addRow(tr("Lens"), lens);
	auto *sensor = new QComboBox;
	for (const scene_doc::SensorPreset &p : scene_doc::sensorPresets()) sensor->addItem(sensorLabel(p.name), p.widthMm);
	sensor->addItem(tr("Other"), -1.0);
	const auto syncSensor = [this, sensor]() {
		QSignalBlocker b(sensor);
		int row = sensor->count() - 1;   // "Other"
		for (int i = 0; i + 1 < sensor->count(); ++i)
			if (std::fabs(sensor->itemData(i).toDouble() - m_doc.camera.sensorWidthMm) < 0.05) row = i;
		sensor->setCurrentIndex(row);
	};
	syncSensor();
	m_refreshers.push_back(syncSensor);
	connect(sensor, QOverload<int>::of(&QComboBox::activated), this, [this, sensor](int idx) {
		const double w = sensor->itemData(idx).toDouble();
		if (m_loading || w <= 0.0) return;   // ("Other" keeps what is there: type a width below)
		edit(QString(), [&]() { m_doc.camera.sensorWidthMm = w; });   // (the field of view stays: what changes is the focal length it means)
		refreshInspectorValues();
	});
	f->addRow(tr("Sensor"), sensor);
	addNum(f, tr("Sensor width"), [this]() { return &m_doc.camera.sensorWidthMm; }, 1, 1000, 0.5, 1, tr(" mm"));
	addDerivedNum(f, tr("Aperture (f-number)"), [this]() { return scene_doc::fNumberFromLensRadius(scene_doc::focalLengthMm(m_doc.camera.fov, scene_doc::aspectOf(m_doc.render), m_doc.camera.sensorWidthMm), m_doc.camera.lensRadius); },
	              [this](double n) {
		              edit(QStringLiteral("camera:aperture"), [&]() {
			              const double mm = scene_doc::focalLengthMm(m_doc.camera.fov, scene_doc::aspectOf(m_doc.render), m_doc.camera.sensorWidthMm);
			              m_doc.camera.lensRadius = scene_doc::lensRadiusFromFNumber(mm, n > 0.0 ? std::max(0.7, n) : 0.0);
		              });
	              },
	              0, 64, 0.2, 1, QString(), tr("Pinhole (everything sharp)"));
	addNum(f, tr("Lens radius"), [this]() { return &m_doc.camera.lensRadius; }, 0, 10, 0.01, 3);
	addNum(f, tr("Focus distance"), [this]() { return &m_doc.camera.focusDistance; }, 0.01, 10000, 0.5, 2);
	auto *pick = new QPushButton(tr("Pick focus in the 3D view"));
	pick->setAutoDefault(false);
	pick->setToolTip(tr("Then click what should be sharp: the focus distance becomes how far that is from the camera along the way it looks"));
	connect(pick, &QPushButton::clicked, this, [this]() { beginPickFocus(); });
	f->addRow(pick);
	auto *hint = new QLabel(tr("The focal length and the sensor tell the field of view the way a photographer's lens does (50 mm on a 36 mm sensor is about 40 degrees across), and "
	                           "the picture's shape crops the sensor, as in a real camera. The aperture sets the lens radius for a scene where one unit is a metre (50 mm at f/1.8 "
	                           "is 0.014): a lens radius above 0 blurs what is not at the focus distance (depth of field), the more the wider the opening and the longer the lens."));
	hint->setWordWrap(true);
	f->addRow(hint);
	addHeading(f, tr("Picture"));
	addInt(f, tr("Width (pixels)"), [this]() { return &m_doc.render.width; }, 1, 16384);
	addInt(f, tr("Height (pixels)"), [this]() { return &m_doc.render.height; }, 1, 16384);
	auto *aspect = new QComboBox;
	aspect->addItem(tr("Set height from width..."), QVariant());
	for (const char *a : {"4:3", "16:9", "3:2", "1:1", "21:9", "9:16"}) aspect->addItem(a, a);
	connect(aspect, QOverload<int>::of(&QComboBox::activated), this, [this, aspect](int i) {
		if (i <= 0) return;
		const QStringList parts = aspect->itemData(i).toString().split(':');
		const double w = parts[0].toDouble(), h = parts[1].toDouble();
		edit(QString(), [&]() { m_doc.render.height = std::max(1, static_cast<int>(std::lround(m_doc.render.width * h / w))); });
		QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
	});
	f->addRow(tr("Aspect ratio"), aspect);
	addInt(f, tr("Samples per pixel"), [this]() { return &m_doc.render.samples; }, 1, 100000);
	addInt(f, tr("Light bounces (max depth)"), [this]() { return &m_doc.render.maxDepth; }, 1, 100);
	inspectCameraViews(f);
}

void SceneBuilderWidget::inspectMaterial(QFormLayout *f, int i) {
	addHeading(f, tr("Material"));
	// A ready-made material (gold, frosted glass, red plastic...) sets the type and its values in one go; they can be tuned afterwards.
	auto *preset = new QComboBox;
	preset->addItem(tr("Choose a ready-made material..."), QString());
	preset->setIconSize(QSize(36, 36));   // each preset shows a small picture of a sphere made of it (qt_gui/material_thumbs, made by scripts/make_material_thumbnails.py)
	for (const scene_doc::MaterialPreset &p : scene_doc::materialPresets())
		preset->addItem(QIcon(QStringLiteral(":/material_thumbs/%1.png").arg(QString::fromLatin1(p.id))), QStringLiteral("%1: %2").arg(presetGroupLabel(p.group), presetLabel(p.name)),
		                QString::fromLatin1(p.id));
	connect(preset, QOverload<int>::of(&QComboBox::activated), this, [this, i, preset](int idx) {
		const QString id = preset->itemData(idx).toString();
		if (m_loading || id.isEmpty() || i >= static_cast<int>(m_doc.objects.size())) return;
		edit(QString(), [&]() { scene_doc::applyMaterialPreset(m_doc.objects[i].material, id.toStdString()); });
		QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
	});
	f->addRow(tr("Preset"), preset);
	auto *kind = new QComboBox;
	for (MaterialKind k : {MaterialKind::Diffuse, MaterialKind::Conductor, MaterialKind::Dielectric, MaterialKind::CoatedDiffuse, MaterialKind::DiffuseTransmission, MaterialKind::Subsurface})
		kind->addItem(materialLabel(k), static_cast<int>(k));
	kind->setCurrentIndex(kind->findData(static_cast<int>(m_doc.objects[i].material.kind)));
	connect(kind, QOverload<int>::of(&QComboBox::activated), this, [this, i, kind](int idx) {
		if (m_loading || i >= static_cast<int>(m_doc.objects.size())) return;
		edit(QString(), [&]() {
			scene_doc::Material &m = m_doc.objects[i].material;
			m.kind = static_cast<MaterialKind>(kind->itemData(idx).toInt());
			// Starting values that look right for the new kind.
			if (m.kind == MaterialKind::Conductor && m.roughness == 0.0) m.roughness = 0.1;
			if (m.kind == MaterialKind::CoatedDiffuse && m.roughness == 0.0) m.roughness = 0.05;
			if (m.kind == MaterialKind::Subsurface && m.ior == 1.5) m.ior = 1.4;   // (skin, milk and wax are nearer water's 1.33 than glass's 1.5)
		});
		QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
	});
	f->addRow(tr("Type"), kind);
	auto mat = [this, i]() -> scene_doc::Material * { return i < static_cast<int>(m_doc.objects.size()) ? &m_doc.objects[i].material : nullptr; };
	const scene_doc::Material &m = m_doc.objects[i].material;
	// A picture can colour a diffuse or glossy-paint surface. Choosing one for a quad also gives the quad the picture's shape.
	const auto addPicture = [&]() {
		addFile(f, tr("Picture"), [mat]() { return mat() ? &mat()->imageFile : nullptr; }, tr("Images (*.png *.jpg *.jpeg *.bmp *.tga *.exr *.hdr)"), true,
		        [this, i](const QString &path) {
			        if (i >= static_cast<int>(m_doc.objects.size())) return;
			        QSize px;
			        scene_doc::Object &o = m_doc.objects[i];
			        o.material.imageFile = uprightPictureCopy(path, &px).toStdString();
			        if (o.shape == ShapeKind::Quad && px.isValid() && px.width() > 0) o.size.z = o.size.x * px.height() / px.width();
		        });
	};
	// What the surface's colour is, when it is not a picture: a plain colour, a checker (matte only), or the renderer's marble or noise (their own colours).
	// Returns true when the colour is a plain one (or a checker's first colour), so the caller shows the colour row.
	const auto addPattern = [&](bool checkerToo) {
		auto *pattern = new QComboBox;
		pattern->addItem(tr("Plain colour"), 0);
		if (checkerToo) pattern->addItem(tr("Checker"), 1);
		pattern->addItem(tr("Marble"), 2);
		pattern->addItem(tr("Noise (soft clouds)"), 3);
		const int current = (checkerToo && m.checker) ? 1 : m.pattern == scene_doc::Pattern::Marble ? 2 : m.pattern == scene_doc::Pattern::Noise ? 3 : 0;
		pattern->setCurrentIndex(pattern->findData(current));
		connect(pattern, QOverload<int>::of(&QComboBox::activated), this, [this, i, pattern](int idx) {
			if (m_loading || i >= static_cast<int>(m_doc.objects.size())) return;
			const int what = pattern->itemData(idx).toInt();
			edit(QString(), [&]() {
				scene_doc::Material &mm = m_doc.objects[i].material;
				mm.checker = what == 1;
				mm.pattern = what == 2 ? scene_doc::Pattern::Marble : what == 3 ? scene_doc::Pattern::Noise : scene_doc::Pattern::None;
			});
			QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
		});
		f->addRow(tr("Pattern"), pattern);
	};
	// A picture that bumps the surface (a grayscale height map) or tilts its normals (an RGB normal map), for matte, glossy paint and metal.
	const auto addBump = [&]() {
		addFile(f, tr("Bump or normal map"), [mat]() { return mat() ? &mat()->bumpFile : nullptr; }, tr("Images (*.png *.jpg *.jpeg *.bmp *.tga *.exr *.hdr)"), true,
		        [](const QString &) {});   // (a choice rebuilds the properties: the kind of picture, its height and the texture repeats now apply)
		if (m.bumpFile.empty()) return;
		auto *bumpKind = new QComboBox;
		bumpKind->addItem(tr("Height map (grayscale: light is high)"), static_cast<int>(scene_doc::BumpKind::Height));
		bumpKind->addItem(tr("Normal map (colour)"), static_cast<int>(scene_doc::BumpKind::Normal));
		bumpKind->setCurrentIndex(bumpKind->findData(static_cast<int>(m.bumpKind)));
		connect(bumpKind, QOverload<int>::of(&QComboBox::activated), this, [this, i, bumpKind](int idx) {
			if (m_loading || i >= static_cast<int>(m_doc.objects.size())) return;
			edit(QString(), [&]() { m_doc.objects[i].material.bumpKind = static_cast<scene_doc::BumpKind>(bumpKind->itemData(idx).toInt()); });
			QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
		});
		f->addRow(tr("Picture is a"), bumpKind);
		auto *kindNote = new QLabel(tr("The renderer tells the two apart by the picture itself: a grayscale picture is a height map and a colour one a normal map, whichever is chosen here."));
		kindNote->setWordWrap(true);
		f->addRow(kindNote);
		if (m.bumpKind == scene_doc::BumpKind::Height)
			addNum(f, tr("Bump height"), [mat]() { return mat() ? &mat()->bumpStrength : nullptr; }, 0, 100, 0.01, 3);
	};
	bool bumpable = false;
	switch (m.kind) {
		case MaterialKind::Diffuse:
			bumpable = true;
			addPicture();
			if (!m.imageFile.empty()) break;  // the picture is the colour
			addPattern(true);
			if (m.pattern == scene_doc::Pattern::Marble) {
				addNum(f, tr("Veins"), [mat]() { return mat() ? &mat()->patternScale : nullptr; }, 0.1, 100, 0.5, 1);
				addNum(f, tr("Wander"), [mat]() { return mat() ? &mat()->patternVariation : nullptr; }, 0, 5, 0.1, 2);
				break;
			}
			if (m.pattern == scene_doc::Pattern::Noise) break;
			addColor(f, m.checker ? tr("Colour A") : tr("Colour"), [mat]() { return mat() ? &mat()->color : nullptr; });
			if (m.checker) {
				addColor(f, tr("Colour B"), [mat]() { return mat() ? &mat()->color2 : nullptr; });
				addNum(f, tr("Checks across"), [mat]() { return mat() ? &mat()->checkerCount : nullptr; }, 1, 1000, 1, 0);
			}
			break;
		case MaterialKind::Conductor:
			bumpable = true;
			addColor(f, tr("Colour"), [mat]() { return mat() ? &mat()->color : nullptr; });
			addNum(f, tr("Roughness"), [mat]() { return mat() ? &mat()->roughness : nullptr; }, 0, 1, 0.02, 2);
			break;
		case MaterialKind::Dielectric:
			addNum(f, tr("Index of refraction"), [mat]() { return mat() ? &mat()->ior : nullptr; }, 1.0, 3.0, 0.05, 2);
			addNum(f, tr("Roughness"), [mat]() { return mat() ? &mat()->roughness : nullptr; }, 0, 1, 0.02, 2);
			break;
		case MaterialKind::CoatedDiffuse:
			bumpable = true;
			addPicture();
			if (m.imageFile.empty()) {
				addPattern(false);
				if (m.pattern == scene_doc::Pattern::Marble) {
					addNum(f, tr("Veins"), [mat]() { return mat() ? &mat()->patternScale : nullptr; }, 0.1, 100, 0.5, 1);
					addNum(f, tr("Wander"), [mat]() { return mat() ? &mat()->patternVariation : nullptr; }, 0, 5, 0.1, 2);
				} else if (m.pattern == scene_doc::Pattern::None) {
					addColor(f, tr("Paint colour"), [mat]() { return mat() ? &mat()->color : nullptr; });
				}
			}
			addNum(f, tr("Coat index of refraction"), [mat]() { return mat() ? &mat()->ior : nullptr; }, 1.0, 3.0, 0.05, 2);
			addNum(f, tr("Coat roughness"), [mat]() { return mat() ? &mat()->roughness : nullptr; }, 0, 1, 0.02, 2);
			break;
		case MaterialKind::DiffuseTransmission:
			addColor(f, tr("Reflects"), [mat]() { return mat() ? &mat()->color : nullptr; });
			addColor(f, tr("Lets through"), [mat]() { return mat() ? &mat()->transmittance : nullptr; });
			break;
		case MaterialKind::Subsurface: {
			addColor(f, tr("Colour"), [mat]() { return mat() ? &mat()->color : nullptr; });
			addNum(f, tr("Travels under the surface: red"), [mat]() { return mat() ? &mat()->scatterDistance.r : nullptr; }, 0.001, 1000, 0.01, 3);
			addNum(f, tr("Travels under the surface: green"), [mat]() { return mat() ? &mat()->scatterDistance.g : nullptr; }, 0.001, 1000, 0.01, 3);
			addNum(f, tr("Travels under the surface: blue"), [mat]() { return mat() ? &mat()->scatterDistance.b : nullptr; }, 0.001, 1000, 0.01, 3);
			addNum(f, tr("Index of refraction"), [mat]() { return mat() ? &mat()->ior : nullptr; }, 1.0, 3.0, 0.05, 2);
			addNum(f, tr("Surface roughness"), [mat]() { return mat() ? &mat()->roughness : nullptr; }, 0, 1, 0.02, 2);
			auto *hint = new QLabel(tr("Light goes into the surface, wanders about and comes out somewhere else, which softens the shading the way skin, wax, milk and marble are soft. The "
			                           "distances are how far each colour travels before it scatters, in the scene's units: a tenth to a half of the object's size looks translucent, "
			                           "much more looks like fog inside glass. A colour that travels further glows further from where the light went in (skin's red does)."));
			hint->setWordWrap(true);
			f->addRow(hint);
			break;
		}
	}
	if (bumpable) addBump();
	// Where the texture sits: how often a picture, a checker or a bump map repeats across and up the surface, and how far it is shifted (scene_model.h Material::uvScaleU...).
	if (scene_doc::hasUvTexture(m)) {
		addNum(f, tr("Texture repeats across"), [mat]() { return mat() ? &mat()->uvScaleU : nullptr; }, -1000, 1000, 0.5, 2);
		addNum(f, tr("Texture repeats up"), [mat]() { return mat() ? &mat()->uvScaleV : nullptr; }, -1000, 1000, 0.5, 2);
		addNum(f, tr("Texture shift across"), [mat]() { return mat() ? &mat()->uvOffsetU : nullptr; }, -1000, 1000, 0.05, 3);
		addNum(f, tr("Texture shift up"), [mat]() { return mat() ? &mat()->uvOffsetV : nullptr; }, -1000, 1000, 0.05, 3);
		const ShapeKind shape = m_doc.objects[i].shape;
		if (shape == ShapeKind::Sphere || shape == ShapeKind::Disk || shape == ShapeKind::Cylinder || shape == ShapeKind::Cone || shape == ShapeKind::Mesh) {
			auto *note = new QLabel(tr("Repeats and shift apply to a box, a quad and the ready-made shapes; this shape keeps the renderer's own texture coordinates, so they have no effect here."));
			note->setWordWrap(true);
			f->addRow(note);
		}
	}
	addMixRows(f, i);
}

// Hidden in the views / Locked, for an object or a light: they live in the builder's own copy of the document (scene_arrange.h) and never reach the .pbrt text.
void SceneBuilderWidget::addVisibilityRows(QFormLayout *f, const std::function<bool *()> &hidden, const std::function<bool *()> &locked) {
	addBool(f, tr("Hidden in the views"), hidden);
	addBool(f, tr("Locked"), locked);
}

// Fog, smoke or a cloud (scene_model.h Volume): where it is and how big, and what its medium does to light. No surface material and no light.
void SceneBuilderWidget::inspectVolume(QFormLayout *f, int i) {
	auto obj = [this, i]() -> Object * { return i < static_cast<int>(m_doc.objects.size()) ? &m_doc.objects[i] : nullptr; };
	using scene_doc::VolumeKind;
	addHeading(f, tr("Volume"));
	addText(f, tr("Name"), [obj]() { return obj() ? &obj()->name : nullptr; });
	addVisibilityRows(f, [obj]() { return obj() ? &obj()->hidden : nullptr; }, [obj]() { return obj() ? &obj()->locked : nullptr; });
	auto *kind = new QComboBox;
	for (VolumeKind k : {VolumeKind::Fog, VolumeKind::Smoke, VolumeKind::Cloud}) kind->addItem(volumeLabel(k), static_cast<int>(k));
	kind->setCurrentIndex(kind->findData(static_cast<int>(m_doc.objects[i].volume.kind)));
	connect(kind, QOverload<int>::of(&QComboBox::activated), this, [this, i, kind](int idx) {
		if (m_loading || i >= static_cast<int>(m_doc.objects.size())) return;
		edit(QString(), [&]() {
			Object &o = m_doc.objects[i];
			o.volume = scene_doc::makeVolume(static_cast<VolumeKind>(kind->itemData(idx).toInt()), o.name).volume;   // the new type's own starting values
			if (o.volume.kind == VolumeKind::Cloud) o.shape = ShapeKind::Sphere;   // a cloud only fills a sphere
		});
		rebuildList();
		QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
	});
	f->addRow(tr("Type"), kind);
	const Object &o = m_doc.objects[i];
	if (o.volume.kind != VolumeKind::Cloud) {   // fog and smoke can also fill a cylinder
		auto *shape = new QComboBox;
		shape->addItem(shapeLabel(ShapeKind::Sphere), static_cast<int>(ShapeKind::Sphere));
		shape->addItem(shapeLabel(ShapeKind::Cylinder), static_cast<int>(ShapeKind::Cylinder));
		shape->setCurrentIndex(shape->findData(static_cast<int>(o.shape)));
		connect(shape, QOverload<int>::of(&QComboBox::activated), this, [this, i, shape](int idx) {
			if (m_loading || i >= static_cast<int>(m_doc.objects.size())) return;
			edit(QString(), [&]() { m_doc.objects[i].shape = static_cast<ShapeKind>(shape->itemData(idx).toInt()); });
			QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
		});
		f->addRow(tr("Shape"), shape);
	}
	addVec3(f, tr("Position"), [obj]() { return obj() ? &obj()->position : nullptr; }, 0.25);
	addVec3(f, tr("Rotation (degrees)"), [obj]() { return obj() ? &obj()->rotation : nullptr; }, 5.0);
	addNum(f, tr("Radius"), [obj]() { return obj() ? &obj()->radius : nullptr; }, 0.001, 10000, 0.1);
	if (o.shape == ShapeKind::Cylinder) addNum(f, tr("Height"), [obj]() { return obj() ? &obj()->height : nullptr; }, 0.001, 10000, 0.1);
	addNum(f, tr("Density"), [obj]() { return obj() ? &obj()->volume.density : nullptr; }, 0.001, 10000, 0.1, 3);
	if (o.volume.kind != VolumeKind::Cloud) addColor(f, tr("Colour"), [obj]() { return obj() ? &obj()->volume.color : nullptr; });
	addNum(f, tr("Forward scattering"), [obj]() { return obj() ? &obj()->volume.anisotropy : nullptr; }, -0.95, 0.95, 0.05, 2);
	if (o.volume.kind == VolumeKind::Cloud) {
		addNum(f, tr("Wispiness"), [obj]() { return obj() ? &obj()->volume.wispiness : nullptr; }, 0, 2, 0.1, 2);
		addNum(f, tr("Detail (lumps across)"), [obj]() { return obj() ? &obj()->volume.frequency : nullptr; }, 0.5, 30, 0.5, 1);
	}
	auto *hint = new QLabel(o.volume.kind == VolumeKind::Cloud
	                            ? tr("A cloud scatters light and never absorbs it. Density is how thick it is: about 6 is a solid white cloud, 1 is a thin haze. Forward scattering above 0 "
	                                 "gives the bright silver edge against the sun.")
	                            : tr("Density is how quickly the light is scattered or absorbed per unit of distance: 0.3 is a light mist in a sphere a few units across, 2 is thick. "
	                                 "The colour is what is scattered: white fog only scatters, so it glows; a dark colour absorbs the rest and looks like smoke."));
	hint->setWordWrap(true);
	f->addRow(hint);
}

void SceneBuilderWidget::inspectObject(QFormLayout *f, int i) {
	if (m_doc.objects[i].volume.kind != scene_doc::VolumeKind::None) {
		inspectVolume(f, i);
		return;
	}
	auto obj = [this, i]() -> Object * { return i < static_cast<int>(m_doc.objects.size()) ? &m_doc.objects[i] : nullptr; };
	addHeading(f, tr("Object"));
	addText(f, tr("Name"), [obj]() { return obj() ? &obj()->name : nullptr; });
	addVisibilityRows(f, [obj]() { return obj() ? &obj()->hidden : nullptr; }, [obj]() { return obj() ? &obj()->locked : nullptr; });
	auto *shape = new QComboBox;
	for (ShapeKind k : scene_doc::allShapeKinds())
		shape->addItem(shapeLabel(k), static_cast<int>(k));
	shape->setCurrentIndex(shape->findData(static_cast<int>(m_doc.objects[i].shape)));
	connect(shape, QOverload<int>::of(&QComboBox::activated), this, [this, i, shape](int idx) {
		if (m_loading || i >= static_cast<int>(m_doc.objects.size())) return;
		edit(QString(), [&]() { m_doc.objects[i].shape = static_cast<ShapeKind>(shape->itemData(idx).toInt()); });
		QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
	});
	f->addRow(tr("Shape"), shape);
	addVec3(f, tr("Position"), [obj]() { return obj() ? &obj()->position : nullptr; }, 0.25);
	addVec3(f, tr("Rotation (degrees)"), [obj]() { return obj() ? &obj()->rotation : nullptr; }, 5.0);

	const Object &o = m_doc.objects[i];
	switch (o.shape) {
		case ShapeKind::Sphere:
		case ShapeKind::Disk:
			addNum(f, tr("Radius"), [obj]() { return obj() ? &obj()->radius : nullptr; }, 0.001, 10000, 0.1);
			break;
		case ShapeKind::Cylinder:
		case ShapeKind::Cone:
			addNum(f, tr("Radius"), [obj]() { return obj() ? &obj()->radius : nullptr; }, 0.001, 10000, 0.1);
			addNum(f, tr("Height"), [obj]() { return obj() ? &obj()->height : nullptr; }, 0.001, 10000, 0.1);
			break;
		case ShapeKind::Dome:
			addNum(f, tr("Radius"), [obj]() { return obj() ? &obj()->radius : nullptr; }, 0.001, 10000, 0.1);
			break;
		case ShapeKind::Capsule:
			addNum(f, tr("Radius"), [obj]() { return obj() ? &obj()->radius : nullptr; }, 0.001, 10000, 0.1);
			addNum(f, tr("Height (with the rounded ends)"), [obj]() { return obj() ? &obj()->height : nullptr; }, 0.001, 10000, 0.1);
			break;
		case ShapeKind::Torus:
			addNum(f, tr("Ring radius"), [obj]() { return obj() ? &obj()->radius : nullptr; }, 0.001, 10000, 0.1);
			addNum(f, tr("Tube radius"), [obj]() { return obj() ? &obj()->radius2 : nullptr; }, 0.001, 10000, 0.05);
			break;
		case ShapeKind::Tube:
			addNum(f, tr("Outer radius"), [obj]() { return obj() ? &obj()->radius : nullptr; }, 0.001, 10000, 0.1);
			addNum(f, tr("Hole radius"), [obj]() { return obj() ? &obj()->radius2 : nullptr; }, 0.001, 10000, 0.1);
			addNum(f, tr("Height"), [obj]() { return obj() ? &obj()->height : nullptr; }, 0.001, 10000, 0.1);
			break;
		case ShapeKind::Pyramid:
			addNum(f, tr("Base width (X)"), [obj]() { return obj() ? &obj()->size.x : nullptr; }, 0.001, 10000, 0.25);
			addNum(f, tr("Base depth (Z)"), [obj]() { return obj() ? &obj()->size.z : nullptr; }, 0.001, 10000, 0.25);
			addNum(f, tr("Height"), [obj]() { return obj() ? &obj()->height : nullptr; }, 0.001, 10000, 0.1);
			break;
		case ShapeKind::Stairs:
			addVec3(f, tr("Size"), [obj]() { return obj() ? &obj()->size : nullptr; }, 0.25);
			addInt(f, tr("Steps"), [obj]() { return obj() ? &obj()->steps : nullptr; }, 1, scene_doc::kMaxStairSteps);
			break;
		case ShapeKind::Wedge:
		case ShapeKind::Box:
			addVec3(f, tr("Size"), [obj]() { return obj() ? &obj()->size : nullptr; }, 0.25);
			break;
		case ShapeKind::RoundedBox:
			addVec3(f, tr("Size"), [obj]() { return obj() ? &obj()->size : nullptr; }, 0.25);
			addNum(f, tr("Roundness of the edges"), [obj]() { return obj() ? &obj()->radius : nullptr; }, 0, 10000, 0.05, 3);
			break;
		case ShapeKind::Cyclorama:
			addVec3(f, tr("Size (width, height, depth)"), [obj]() { return obj() ? &obj()->size : nullptr; }, 0.5);
			addNum(f, tr("Radius of the curve"), [obj]() { return obj() ? &obj()->radius : nullptr; }, 0, 10000, 0.25, 2);
			break;
		case ShapeKind::Terrain:
			addVec3(f, tr("Size (width, hill height, depth)"), [obj]() { return obj() ? &obj()->size : nullptr; }, 0.5);
			addInt(f, tr("Detail (grid cells across)"), [obj]() { return obj() ? &obj()->steps : nullptr; }, 2, scene_doc::kMaxTerrainResolution);
			addNum(f, tr("Hills across"), [obj]() { return obj() ? &obj()->radius2 : nullptr; }, 0.25, 50, 0.5, 2);
			addNum(f, tr("Roughness"), [obj]() { return obj() ? &obj()->radius : nullptr; }, 0, 1, 0.05, 2);
			addInt(f, tr("Seed (which ground)"), [obj]() { return obj() ? &obj()->seed : nullptr; }, 0, 999999);
			break;
		case ShapeKind::Quad:
			addNum(f, tr("Width (X)"), [obj]() { return obj() ? &obj()->size.x : nullptr; }, 0.001, 10000, 0.5);
			addNum(f, tr("Depth (Z)"), [obj]() { return obj() ? &obj()->size.z : nullptr; }, 0.001, 10000, 0.5);
			break;
		case ShapeKind::Mesh:
			addFile(f, tr("Mesh file"), [obj]() { return obj() ? &obj()->meshFile : nullptr; }, tr("Meshes (*.ply *.obj)"));
			addNum(f, tr("Scale"), [obj]() { return obj() ? &obj()->meshScale : nullptr; }, 0.0001, 10000, 0.1, 4);
			break;
	}

	inspectMaterial(f, i);

	addHeading(f, tr("Light"));
	addBool(f, tr("Gives off light"), [obj]() { return obj() ? &obj()->emissive : nullptr; }, true);
	if (o.emissive) {
		addColor(f, tr("Light colour"), [obj]() { return obj() ? &obj()->emission : nullptr; });
		addNum(f, tr("Strength"), [obj]() { return obj() ? &obj()->emissionStrength : nullptr; }, 0, 100000, 1, 2);
		addBool(f, tr("Both sides"), [obj]() { return obj() ? &obj()->twoSided : nullptr; });
		if (o.shape == ShapeKind::Quad || o.shape == ShapeKind::Disk) {
			auto *hint = new QLabel(tr("A quad or disk lights the side that faces up. Rotate it 180 degrees about X to make a ceiling light."));
			hint->setWordWrap(true);
			f->addRow(hint);
		}
	}
}

void SceneBuilderWidget::inspectLight(QFormLayout *f, int i) {
	auto lt = [this, i]() -> Light * { return i < static_cast<int>(m_doc.lights.size()) ? &m_doc.lights[i] : nullptr; };
	addHeading(f, tr("Light"));
	addText(f, tr("Name"), [lt]() { return lt() ? &lt()->name : nullptr; });
	addVisibilityRows(f, [lt]() { return lt() ? &lt()->hidden : nullptr; }, [lt]() { return lt() ? &lt()->locked : nullptr; });
	auto *kind = new QComboBox;
	for (LightKind k : {LightKind::Point, LightKind::Spot, LightKind::Distant, LightKind::Infinite, LightKind::Projector}) kind->addItem(lightLabel(k), static_cast<int>(k));
	kind->setCurrentIndex(kind->findData(static_cast<int>(m_doc.lights[i].kind)));
	connect(kind, QOverload<int>::of(&QComboBox::activated), this, [this, i, kind](int idx) {
		if (m_loading || i >= static_cast<int>(m_doc.lights.size())) return;
		edit(QString(), [&]() { m_doc.lights[i].kind = static_cast<LightKind>(kind->itemData(idx).toInt()); });
		rebuildList();
		QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
	});
	f->addRow(tr("Type"), kind);
	const Light &l = m_doc.lights[i];
	if (l.kind == LightKind::Point || l.kind == LightKind::Spot || l.kind == LightKind::Projector) addVec3(f, tr("Position"), [lt]() { return lt() ? &lt()->position : nullptr; }, 0.25);
	if (l.kind == LightKind::Spot) {
		addVec3(f, tr("Aims at"), [lt]() { return lt() ? &lt()->target : nullptr; }, 0.25);
		addNum(f, tr("Cone angle"), [lt]() { return lt() ? &lt()->coneAngle : nullptr; }, 1, 89, 1, 1, QString::fromUtf8(" \xC2\xB0"));
		addNum(f, tr("Soft edge"), [lt]() { return lt() ? &lt()->coneDelta : nullptr; }, 0, 89, 1, 1, QString::fromUtf8(" \xC2\xB0"));
	}
	if (l.kind == LightKind::Projector) {
		addVec3(f, tr("Aims at"), [lt]() { return lt() ? &lt()->target : nullptr; }, 0.25);
		addNum(f, tr("Field of view"), [lt]() { return lt() ? &lt()->fov : nullptr; }, 1, 170, 1, 1, QString::fromUtf8(" \xC2\xB0"));
		addFile(f, tr("Slide picture"), [lt]() { return lt() ? &lt()->imageFile : nullptr; }, tr("Images (*.png *.jpg *.jpeg *.bmp *.tga *.exr *.hdr)"));
		auto *hint = new QLabel(tr("A slide projector: the picture shines out through a frustum, onto whatever it falls on, with the colours of the picture. The field of view is the angle across "
		                           "the picture's shorter side. Strength is the brightness of the whitest part of the picture; a surface facing the projector from distance d is lit with "
		                           "Strength / d squared. The top of the picture points up as far as the aim allows."));
		hint->setWordWrap(true);
		f->addRow(hint);
	}
	if (l.kind == LightKind::Distant) {
		addVec3(f, tr("Shines from"), [lt]() { return lt() ? &lt()->position : nullptr; }, 0.25);
		addVec3(f, tr("Towards"), [lt]() { return lt() ? &lt()->target : nullptr; }, 0.25);
	}
	if (l.kind == LightKind::Infinite) {
		// Sun & sky: a clear sky for a time of day. Its picture and the scene's Sun are made from these settings (scene_sky.h).
		auto *physical = new QCheckBox(tr("Sun && sky (a clear sky for a time of day)"));
		physical->setChecked(l.physicalSky);
		connect(physical, &QCheckBox::toggled, this, [this, i](bool on) {
			if (m_loading || i >= static_cast<int>(m_doc.lights.size())) return;
			edit(QString(), [&]() {
				Light &sky = m_doc.lights[i];
				sky.physicalSky = on;
				if (on) sky.intensity = 1.0;   // the generated sky is in real proportions: brightness 1 is a daylight scene
				else sky.imageFile.clear();
			});
			rebuildList();
			QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
		});
		f->addRow(physical);
		if (l.physicalSky) {
			auto *time = new QComboBox;
			time->addItem(tr("Time of day..."), -1.0);
			const std::pair<const char *, double> times[] = {{QT_TR_NOOP("Sunrise"), 4.0}, {QT_TR_NOOP("Morning"), 25.0}, {QT_TR_NOOP("Noon"), 70.0},
			                                                  {QT_TR_NOOP("Afternoon"), 40.0}, {QT_TR_NOOP("Sunset"), 3.0}};
			for (const auto &t : times) time->addItem(tr(t.first), t.second);
			connect(time, QOverload<int>::of(&QComboBox::activated), this, [this, i, time](int idx) {
				const double height = time->itemData(idx).toDouble();
				if (m_loading || height < 0.0 || i >= static_cast<int>(m_doc.lights.size())) return;
				edit(QString(), [&]() { m_doc.lights[i].sky.sunElevation = height; });
				QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
			});
			f->addRow(tr("Preset"), time);
			addNum(f, tr("Sun height"), [lt]() { return lt() ? &lt()->sky.sunElevation : nullptr; }, 0.5, 90, 1, 1, QString::fromUtf8(" \xC2\xB0"));
			addNum(f, tr("Sun direction"), [lt]() { return lt() ? &lt()->sky.sunAzimuth : nullptr; }, 0, 360, 5, 0, QString::fromUtf8(" \xC2\xB0"));
			addNum(f, tr("Haze"), [lt]() { return lt() ? &lt()->sky.turbidity : nullptr; }, 1.7, 10, 0.1, 1);
			addNum(f, tr("Ground brightness"), [lt]() { return lt() ? &lt()->sky.groundAlbedo : nullptr; }, 0, 1, 0.05, 2);
			auto *hint = new QLabel(tr("The sky picture and the Sun light are made from these settings; change the sun here rather than moving the Sun light. Direction 0 is "
			                           "towards +X, 90 away from the starting camera, 180 towards -X, 270 behind it. Brightness 1 is a daylight scene."));
			hint->setWordWrap(true);
			f->addRow(hint);
		} else {
			addFile(f, tr("Sky image"), [lt]() { return lt() ? &lt()->imageFile : nullptr; }, tr("Images (*.exr *.hdr *.png *.jpg *.jpeg)"), false,
			        [](const QString &) {});   // (a choice shows the rotation row)
			if (!l.imageFile.empty()) addNum(f, tr("Rotate sky"), [lt]() { return lt() ? &lt()->skySpin : nullptr; }, -360, 360, 5, 0, QString::fromUtf8(" \xC2\xB0"));
			auto *hint = new QLabel(tr("Leave the image empty for a plain colour sky. An image is an equirectangular (lat-long) panorama; Rotate sky turns it about the vertical axis "
			                           "(counter-clockwise seen from above), to put the bright part of the picture where you want the light to come from."));
			hint->setWordWrap(true);
			f->addRow(hint);
		}
	}
	if (l.kind == LightKind::Projector) {}   // (the picture gives the colour)
	else if (l.kind != LightKind::Infinite || (l.imageFile.empty() && !l.physicalSky)) addColor(f, tr("Colour"), [lt]() { return lt() ? &lt()->color : nullptr; });
	addNum(f, l.kind == LightKind::Point || l.kind == LightKind::Spot || l.kind == LightKind::Projector ? tr("Strength") : tr("Brightness"), [lt]() { return lt() ? &lt()->intensity : nullptr; }, 0, 100000, 0.5, 2);
}

