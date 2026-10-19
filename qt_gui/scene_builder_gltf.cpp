// scene_builder_gltf.cpp -- the Scene Builder's glTF import (Add > Mesh..., or Add to the model library's "Choose another file..."): a .glb or .gltf model becomes mesh objects, one for each material it uses,
// grouped so they are picked and moved as one thing.
//
// The reading is src/shared/gltf_import.h (the node hierarchy applied, one part for each material, smooth normals where the file has none). This file fetches what a .gltf names (only files inside the model's
// own folder), writes each part as a PLY and each base-colour picture as a PNG or JPEG in a folder of the user's own data (imported_models/<name>-<id of the file>), and makes the objects: standing on the
// floor at the middle of the view, at the model's real size when it says one that makes sense (metres) and 1.6 units tall otherwise.
#include "scene_builder_widget.h"

#include "app_log.h"
#include "scene_builder_common.h"
#include "../src/shared/gltf_import.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <cmath>

using scene_doc::Object;
using scene_doc::ShapeKind;

namespace {

constexpr qint64 kMaxModelBytes = 600ll * 1000 * 1000;
constexpr qint64 kMaxPartFileBytes = 200ll * 1000 * 1000;
constexpr size_t kMaxLightTriangles = 5000;   // a glowing part with more triangles than this is not made a light

// The Builder's material for a glTF one: glass when it transmits, a metal when it is metallic, a coated surface when it has a clearcoat, else a plain diffuse one; the base-colour picture (a file) goes
// on a diffuse or coated surface. glTF's roughness r gives the microfacet width r * r, which the renderer's roughness (remapped by a square root) says as r to the fourth.
scene_doc::Material materialOf(const gltf_import::Material &m, const QString &picture) {
	scene_doc::Material out;
	const double r4 = m.roughness * m.roughness * m.roughness * m.roughness;
	out.color = {m.baseColor[0], m.baseColor[1], m.baseColor[2]};
	if (m.transmission > 0.5) {
		out.kind = scene_doc::MaterialKind::Dielectric;
		out.ior = m.ior;
		out.roughness = r4;
	} else if (m.metallic > 0.5) {
		out.kind = scene_doc::MaterialKind::Conductor;
		out.roughness = r4;
	} else if (m.clearcoat > 0.3) {
		out.kind = scene_doc::MaterialKind::CoatedDiffuse;
		out.roughness = 0.0;
		out.ior = m.ior;
	}
	if (!picture.isEmpty() && (out.kind == scene_doc::MaterialKind::Diffuse || out.kind == scene_doc::MaterialKind::CoatedDiffuse)) out.imageFile = picture.toStdString();
	return out;
}

QString cleanName(const QString &text, const QString &fallback) {
	QString out = text.simplified();
	out.remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")));
	return out.isEmpty() ? fallback : out.left(60);
}

}  // namespace

bool SceneBuilderWidget::importGltf(const QString &file, QString *message) {
	const auto fail = [&](const QString &why) {
		logLine(QStringLiteral("builder"), QStringLiteral("import glTF %1: %2").arg(file, why), log_format::Level::Warn);
		if (message) *message = why;
		return false;
	};
	QFile source(file);
	if (!source.open(QIODevice::ReadOnly)) return fail(tr("The file could not be opened."));
	if (source.size() > kMaxModelBytes) return fail(tr("The file is too large to import (over 600 MB)."));
	const QByteArray content = source.readAll();
	source.close();
	const QDir folderOfModel = QFileInfo(file).absoluteDir();
	// A .gltf names its buffers and pictures by relative path: only files inside the model's own folder are read (a file from the internet must not be able to name another place on the disk).
	const gltf_import::FileFetcher fetch = [&](const std::string &uri, std::vector<unsigned char> &bytes) {
		const QString relative = QUrl::fromPercentEncoding(QByteArray::fromStdString(uri));
		if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(QStringLiteral("..")) || relative.contains(QStringLiteral(":"))) return false;
		QFile f(folderOfModel.filePath(relative));
		if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxModelBytes) return false;
		const QByteArray data = f.readAll();
		bytes.assign(reinterpret_cast<const unsigned char *>(data.constData()), reinterpret_cast<const unsigned char *>(data.constData()) + data.size());
		return true;
	};
	const gltf_import::Result parsed = gltf_import::parse(std::string(content.constData(), static_cast<size_t>(content.size())), fetch);
	if (!parsed.ok) return fail(QString::fromStdString(parsed.error));
	const gltf_import::Model &model = parsed.model;

	// where it is put and how big: the base on the floor, the middle at the origin, scaled so that the model is 2 units tall (the convention of the Builder's meshes: a mesh object's Scale is then
	// its half height), and the object's scale is the real size when the model's own is a sensible one (0.05 to 30 metres tall)
	const double extent[3] = {model.hi[0] - model.lo[0], model.hi[1] - model.lo[1], model.hi[2] - model.lo[2]};
	const double widest = std::max({extent[0], extent[1], extent[2]});
	if (!(widest > 1e-9) || !std::isfinite(widest)) return fail(tr("The model has no size."));
	const double norm = std::max(extent[1], 0.01 * widest);   // (a flat model is not stretched out of all proportion)
	const double offset[3] = {0.5 * (model.lo[0] + model.hi[0]), model.lo[1], 0.5 * (model.lo[2] + model.hi[2])};
	const bool realSize = norm >= 0.05 && norm <= 30.0;
	const double meshScale = realSize ? norm / 2.0 : 0.8;

	// the files: a folder named for the model and for the file's own bytes (the same model is written once)
	const QString stem = cleanName(QFileInfo(file).completeBaseName(), tr("Model"));
	const QString id = QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha1).toHex().left(8));
	const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/imported_models/") + stem + QLatin1Char('-') + id;
	QDir().mkpath(folder);
	std::vector<QString> pictureOf(model.images.size());
	std::vector<bool> colourPicture(model.images.size(), false);   // (only the pictures that colour a surface are kept as files)
	for (const gltf_import::Material &m : model.materials)
		if (m.baseColorImage >= 0) colourPicture[static_cast<size_t>(m.baseColorImage)] = true;
	for (size_t i = 0; i < model.images.size(); ++i) {
		if (!colourPicture[i]) continue;
		const gltf_import::Image &image = model.images[i];
		QImage decoded;
		if (!decoded.loadFromData(image.bytes.data(), static_cast<uint>(image.bytes.size()))) continue;   // (a picture format Qt cannot read, such as WebP without its plugin: that surface keeps its plain colour)
		const bool jpeg = image.mime == "image/jpeg";
		const QString path = QStringLiteral("%1/picture_%2.%3").arg(folder).arg(i).arg(jpeg ? QStringLiteral("jpg") : QStringLiteral("png"));
		if (!QFileInfo::exists(path) || QFileInfo(path).size() == 0) {
			if (jpeg) {
				QFile out(path);
				if (!out.open(QIODevice::WriteOnly) || out.write(reinterpret_cast<const char *>(image.bytes.data()), static_cast<qint64>(image.bytes.size())) != static_cast<qint64>(image.bytes.size())) continue;
			} else if (!decoded.save(path, "PNG")) {
				continue;
			}
		}
		pictureOf[i] = QDir::toNativeSeparators(path);
	}
	std::vector<Object> objects;
	bool glowDropped = false;
	for (size_t i = 0; i < model.parts.size(); ++i) {
		const gltf_import::Part &part = model.parts[i];
		const std::string ply = gltf_import::toPly(part, offset, 2.0 / norm);
		if (static_cast<qint64>(ply.size()) > kMaxPartFileBytes) return fail(tr("A part of the model is too large to import."));
		const QString path = QStringLiteral("%1/part_%2.ply").arg(folder).arg(i);
		QFile out(path);
		if (!out.open(QIODevice::WriteOnly) || out.write(ply.data(), static_cast<qint64>(ply.size())) != static_cast<qint64>(ply.size())) return fail(tr("The model could not be written to %1.").arg(path));
		out.close();
		gltf_import::Material material = model.materials[static_cast<size_t>(part.material)];
		if (material.metalRoughImage >= 0) {   // the picture multiplies the factors: its average over the surface does
			const gltf_import::Image &mr = model.images[static_cast<size_t>(material.metalRoughImage)];
			QImage picture;
			if (picture.loadFromData(mr.bytes.data(), static_cast<uint>(mr.bytes.size()))) {
				const QImage small = picture.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
				double green = 0.0, blue = 0.0;
				for (int y = 0; y < small.height(); ++y)
					for (int x = 0; x < small.width(); ++x) { green += qGreen(small.pixel(x, y)); blue += qBlue(small.pixel(x, y)); }
				const double n = 255.0 * small.width() * small.height();
				material.roughness *= green / n;
				material.metallic *= blue / n;
			}
		}
		const QString materialName = cleanName(QString::fromStdString(material.name), QString::number(i + 1));
		Object o = scene_doc::makeObject(ShapeKind::Mesh, (model.parts.size() > 1 ? stem + QLatin1Char(' ') + materialName : stem).toStdString());
		o.meshFile = QDir::toNativeSeparators(path).toStdString();
		o.meshScale = meshScale;
		o.position = {0.0, 0.0, 0.0};
		o.material = materialOf(material, material.baseColorImage >= 0 ? pictureOf[static_cast<size_t>(material.baseColorImage)] : QString());
		const double peak = std::max({material.emissive[0], material.emissive[1], material.emissive[2]});
		if (peak > 0.0 && part.indices.size() / 3 > kMaxLightTriangles) {   // (every triangle of a light is sampled: a big mesh that glows is a slow scene, and rarely meant as a lamp)
			glowDropped = true;
		} else if (peak > 0.0) {
			o.emissive = true;
			o.emission = {material.emissive[0] / peak, material.emissive[1] / peak, material.emissive[2] / peak};
			o.emissionStrength = peak;
		}
		objects.push_back(std::move(o));
	}
	addParts(std::move(objects), stem);
	QString text = tr("Added %1: %2 part(s), %3 triangles, %4 m tall%5.")
	                   .arg(stem, QString::number(model.parts.size()), QString::number(static_cast<qulonglong>(model.triangles)), QString::number(norm, 'f', norm < 10 ? 2 : 1), realSize ? QString() : tr(" in the file, shown 1.6 units tall"));
	if (glowDropped) text += QStringLiteral("\n") + tr("A glowing part of the model has too many triangles to be a light, so it does not glow.");
	for (const std::string &n : model.notes) text += QStringLiteral("\n") + QString::fromStdString(n);
	logLine(QStringLiteral("builder"), QStringLiteral("import glTF %1: %2").arg(file, text.simplified()));
	if (message) *message = text;
	return true;
}
