// scene_builder_mix.cpp -- the properties of a mixed material (Material::mixWith, scene_model.h): the "Mix with a second material" switch, how much of the second there is, and the
// second material's own type and values. It is the last block of a material's properties (inspectMaterial); the second material is a plain one, with no picture, pattern or bump.
#include "scene_builder_widget.h"

#include "scene_builder_common.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QTimer>

using namespace scene_builder_ui;
using scene_doc::MaterialKind;

namespace {
// What the second material starts as when a material is first mixed: a slightly rough copper, which is plainly something else beside most first materials.
scene_doc::Material defaultSecondMaterial() {
	scene_doc::Material m;
	m.kind = MaterialKind::Conductor;
	m.color = {0.95, 0.64, 0.54};
	m.roughness = 0.3;
	return m;
}
}  // namespace

void SceneBuilderWidget::addMixRows(QFormLayout *f, int i) {
	using scene_doc::Material;
	auto *on = new QCheckBox(tr("Mix with a second material"));
	on->setChecked(i < static_cast<int>(m_doc.objects.size()) && scene_doc::isMix(m_doc.objects[i].material));
	connect(on, &QCheckBox::toggled, this, [this, i](bool mixed) {
		if (m_loading || i >= static_cast<int>(m_doc.objects.size())) return;
		edit(QString(), [&]() {
			Material &m = m_doc.objects[i].material;
			if (mixed) {
				m.mixWith.assign(1, defaultSecondMaterial());
				m.mixAmount = 0.5;
			} else {
				m.mixWith.clear();
			}
		});
		QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
	});
	f->addRow(on);
	if (!on->isChecked()) return;

	auto *heading = new QLabel("<b>" + tr("Second material").toHtmlEscaped() + "</b>");
	heading->setContentsMargins(0, 8, 0, 0);
	auto mat = [this, i]() -> Material * { return i < static_cast<int>(m_doc.objects.size()) && !m_doc.objects[i].material.mixWith.empty() ? &m_doc.objects[i].material : nullptr; };
	auto second = [mat]() -> Material * { Material *m = mat(); return m ? &m->mixWith[0] : nullptr; };
	addNum(f, tr("Amount of the second material"), [mat]() { return mat() ? &mat()->mixAmount : nullptr; }, 0, 1, 0.05, 2);
	f->addRow(heading);
	const auto named = [this](const QString &what) { return tr("Second: %1").arg(what); };

	auto *kind = new QComboBox;
	for (MaterialKind k : {MaterialKind::Diffuse, MaterialKind::Conductor, MaterialKind::Dielectric, MaterialKind::CoatedDiffuse, MaterialKind::DiffuseTransmission, MaterialKind::Subsurface})
		kind->addItem(materialLabel(k), static_cast<int>(k));
	kind->setCurrentIndex(kind->findData(static_cast<int>(second() ? second()->kind : MaterialKind::Conductor)));
	connect(kind, QOverload<int>::of(&QComboBox::activated), this, [this, i, kind](int idx) {
		if (m_loading || i >= static_cast<int>(m_doc.objects.size()) || m_doc.objects[i].material.mixWith.empty()) return;
		edit(QString(), [&]() {
			Material &m = m_doc.objects[i].material.mixWith[0];
			m.kind = static_cast<MaterialKind>(kind->itemData(idx).toInt());
			if (m.kind == MaterialKind::Conductor && m.roughness == 0.0) m.roughness = 0.1;   // (starting values that look right for the new kind, as for the first material)
			if (m.kind == MaterialKind::CoatedDiffuse && m.roughness == 0.0) m.roughness = 0.05;
			if (m.kind == MaterialKind::Subsurface && m.ior == 1.5) m.ior = 1.4;
		});
		QTimer::singleShot(0, this, [this]() { rebuildInspector(); });
	});
	f->addRow(named(tr("Type")), kind);

	const MaterialKind k = second() ? second()->kind : MaterialKind::Conductor;
	switch (k) {
		case MaterialKind::Diffuse:
			addColor(f, named(tr("Colour")), [second]() { return second() ? &second()->color : nullptr; });
			break;
		case MaterialKind::Conductor:
			addColor(f, named(tr("Colour")), [second]() { return second() ? &second()->color : nullptr; });
			addNum(f, named(tr("Roughness")), [second]() { return second() ? &second()->roughness : nullptr; }, 0, 1, 0.02, 2);
			break;
		case MaterialKind::Dielectric:
			addNum(f, named(tr("Index of refraction")), [second]() { return second() ? &second()->ior : nullptr; }, 1.0, 3.0, 0.05, 2);
			addNum(f, named(tr("Roughness")), [second]() { return second() ? &second()->roughness : nullptr; }, 0, 1, 0.02, 2);
			break;
		case MaterialKind::CoatedDiffuse:
			addColor(f, named(tr("Paint colour")), [second]() { return second() ? &second()->color : nullptr; });
			addNum(f, named(tr("Coat index of refraction")), [second]() { return second() ? &second()->ior : nullptr; }, 1.0, 3.0, 0.05, 2);
			addNum(f, named(tr("Coat roughness")), [second]() { return second() ? &second()->roughness : nullptr; }, 0, 1, 0.02, 2);
			break;
		case MaterialKind::DiffuseTransmission:
			addColor(f, named(tr("Reflects")), [second]() { return second() ? &second()->color : nullptr; });
			addColor(f, named(tr("Lets through")), [second]() { return second() ? &second()->transmittance : nullptr; });
			break;
		case MaterialKind::Subsurface:
			addColor(f, named(tr("Colour")), [second]() { return second() ? &second()->color : nullptr; });
			addNum(f, named(tr("Travels under the surface: red")), [second]() { return second() ? &second()->scatterDistance.r : nullptr; }, 0.001, 1000, 0.01, 3);
			addNum(f, named(tr("Travels under the surface: green")), [second]() { return second() ? &second()->scatterDistance.g : nullptr; }, 0.001, 1000, 0.01, 3);
			addNum(f, named(tr("Travels under the surface: blue")), [second]() { return second() ? &second()->scatterDistance.b : nullptr; }, 0.001, 1000, 0.01, 3);
			addNum(f, named(tr("Index of refraction")), [second]() { return second() ? &second()->ior : nullptr; }, 1.0, 3.0, 0.05, 2);
			addNum(f, named(tr("Surface roughness")), [second]() { return second() ? &second()->roughness : nullptr; }, 0, 1, 0.02, 2);
			break;
	}
	addHint(f, tr("The surface is the first material at some points and the second at others, chosen afresh at each point, so the two grade into one another: "
	                           "0 is all the first, 1 all the second, 0.5 half each. Use it for worn paint over metal, rust, dirt, a glaze that has chipped. The second material is "
	                           "a plain one (no picture, pattern or bump); the first keeps all of its own."));
}
