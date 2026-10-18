// scene_builder_occluder.cpp -- "Paint what is in front...": the mask of things in the photo that stand between the camera and the objects put into it (a railing, a branch, a post), so that Render into the
// photo leaves them in front. The mask is painted on the photo with a brush, an eraser and a magic wand (a click selects the joined region of similar colour: a branch is a few similar greens); it is
// kept in the scene (Document::photo.occluder, a PNG as text) and used by the compositor (photo_composite.h). The helpers are src/shared/occluder_mask.h.
#include "scene_builder_widget.h"
#include "app_log.h"
#include "../src/shared/occluder_mask.h"

#include <QBuffer>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QGuiApplication>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <vector>

namespace {

// The photo with the mask over it, and the mouse. Coordinates of the mask are the photo's own (as kept for the camera view: at most 1600 across).
class OccluderCanvas : public QWidget {
public:
	enum class Tool { Paint, Erase, Select, Unselect };

	OccluderCanvas(const QImage &photo, const std::vector<std::uint8_t> &mask, QWidget *parent) : QWidget(parent), m_photo(photo), m_mask(mask) {
		setObjectName("occluderCanvas");
		setMinimumSize(480, 320);
		setMouseTracking(true);
		setCursor(Qt::CrossCursor);
		rebuildOverlay();
	}
	std::vector<std::uint8_t> &mask() { return m_mask; }
	void setTool(Tool t) { m_tool = t; }
	void setBrush(int radius) { m_brush = radius; update(); }
	void setTolerance(int t) { m_tolerance = t; }
	void clearAll() { remember(); std::fill(m_mask.begin(), m_mask.end(), static_cast<std::uint8_t>(0)); rebuildOverlay(); update(); }
	bool undoStroke() {
		if (m_history.empty()) return false;
		m_mask = m_history.back();
		m_history.pop_back();
		rebuildOverlay();
		update();
		return true;
	}

protected:
	QRectF photoRect() const {   // where the photo is drawn: as large as fits, centred
		const double scale = std::min(static_cast<double>(width()) / m_photo.width(), static_cast<double>(height()) / m_photo.height());
		const double w = m_photo.width() * scale, h = m_photo.height() * scale;
		return QRectF((width() - w) / 2.0, (height() - h) / 2.0, w, h);
	}
	QPointF toPhoto(const QPointF &p) const {
		const QRectF r = photoRect();
		return QPointF((p.x() - r.x()) * m_photo.width() / r.width(), (p.y() - r.y()) * m_photo.height() / r.height());
	}
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.fillRect(rect(), palette().color(QPalette::Base));
		const QRectF r = photoRect();
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(r, m_photo);
		p.drawImage(r, m_overlay);
		if (m_hover) {   // the brush, as it will paint
			const double radius = m_brush * r.width() / m_photo.width();
			p.setPen(QPen(Qt::white, 1.5));
			p.setBrush(Qt::NoBrush);
			if (m_tool == Tool::Paint || m_tool == Tool::Erase) p.drawEllipse(*m_hover, radius, radius);
		}
	}
	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() != Qt::LeftButton) return;
		const QPointF at = toPhoto(e->position());
		remember();
		m_down = true;
		m_last = at;
		if (m_tool == Tool::Paint || m_tool == Tool::Erase) {
			occluder_mask::paintDisc(m_mask, m_photo.width(), m_photo.height(), at.x(), at.y(), m_brush, m_tool == Tool::Erase);
		} else {   // the wand: the region of similar colour joined to the pixel
			const QImage rgb = m_photo.convertToFormat(QImage::Format_RGB888);
			std::vector<unsigned char> pixels(static_cast<size_t>(rgb.width()) * rgb.height() * 3);
			for (int y = 0; y < rgb.height(); ++y) std::copy(rgb.constScanLine(y), rgb.constScanLine(y) + rgb.width() * 3, pixels.begin() + static_cast<std::ptrdiff_t>(y) * rgb.width() * 3);
			occluder_mask::floodSelect(pixels.data(), rgb.width(), rgb.height(), static_cast<int>(at.x()), static_cast<int>(at.y()), m_tolerance, m_mask, m_tool == Tool::Unselect, rgb.width() * rgb.height() / 3);
		}
		rebuildOverlay();
		update();
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		m_hover = e->position();
		if (m_down && (m_tool == Tool::Paint || m_tool == Tool::Erase)) {
			const QPointF at = toPhoto(e->position());
			occluder_mask::paintStroke(m_mask, m_photo.width(), m_photo.height(), m_last.x(), m_last.y(), at.x(), at.y(), m_brush, m_tool == Tool::Erase);
			m_last = at;
			rebuildOverlay();
		}
		update();
	}
	void mouseReleaseEvent(QMouseEvent *) override { m_down = false; }
	void leaveEvent(QEvent *) override { m_hover.reset(); update(); }
	void wheelEvent(QWheelEvent *e) override {   // the wheel changes the brush
		m_brush = std::clamp(m_brush + (e->angleDelta().y() > 0 ? 2 : -2), 2, 200);
		update();
	}

private:
	void remember() {
		m_history.push_back(m_mask);
		if (m_history.size() > 20) m_history.pop_front();
	}
	void rebuildOverlay() {   // red where something is in front, as strongly as the mask says
		QImage overlay(m_photo.size(), QImage::Format_ARGB32_Premultiplied);
		for (int y = 0; y < overlay.height(); ++y) {
			QRgb *row = reinterpret_cast<QRgb *>(overlay.scanLine(y));
			for (int x = 0; x < overlay.width(); ++x) {
				const int a = m_mask[static_cast<size_t>(y) * overlay.width() + x] * 110 / 255;   // (up to 43% red)
				row[x] = qRgba(a, 0, 0, a);
			}
		}
		m_overlay = overlay;
	}

	QImage m_photo, m_overlay;
	std::vector<std::uint8_t> m_mask;
	std::deque<std::vector<std::uint8_t>> m_history;
	Tool m_tool = Tool::Paint;
	int m_brush = 16, m_tolerance = 24;
	bool m_down = false;
	QPointF m_last;
	std::optional<QPointF> m_hover;
};

// A grayscale picture as text (PNG, base64): how the scene keeps it.
QString maskToText(const std::vector<std::uint8_t> &mask, int w, int h) {
	if (!occluder_mask::any(mask)) return QString();
	QImage image(w, h, QImage::Format_Grayscale8);
	for (int y = 0; y < h; ++y) std::copy(mask.begin() + static_cast<std::ptrdiff_t>(y) * w, mask.begin() + static_cast<std::ptrdiff_t>(y + 1) * w, image.scanLine(y));
	QByteArray png;
	QBuffer buffer(&png);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return QString::fromLatin1(png.toBase64());
}

}  // namespace

QImage SceneBuilderWidget::occluderFromText(const QString &text) {
	if (text.isEmpty()) return QImage();
	QImage image;
	if (!image.loadFromData(QByteArray::fromBase64(text.toLatin1()), "PNG")) return QImage();
	return image.convertToFormat(QImage::Format_Grayscale8);
}

// The mask for a picture of this size (what the compositor puts the photo back with): scaled to it, one byte a pixel; empty when there is no mask.
std::vector<unsigned char> SceneBuilderWidget::occluderBytes(int w, int h) const {
	if (m_occluder.isNull() || w < 1 || h < 1) return {};
	const QImage scaled = m_occluder.size() == QSize(w, h) ? m_occluder : m_occluder.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	std::vector<unsigned char> bytes(static_cast<size_t>(w) * h);
	for (int y = 0; y < h; ++y) std::copy(scaled.constScanLine(y), scaled.constScanLine(y) + w, bytes.begin() + static_cast<std::ptrdiff_t>(y) * w);
	return bytes;
}

void SceneBuilderWidget::paintOccluderDialog() {
	if (m_cameraPhoto.isNull()) {
		QMessageBox::information(this, tr("Paint what is in front"), tr("Match the camera to a photo first (Camera and image: Match to a photo...): the mask is painted on that photo."));
		return;
	}
	const int w = m_cameraPhoto.width(), h = m_cameraPhoto.height();
	std::vector<std::uint8_t> mask(static_cast<size_t>(w) * h, 0);
	if (!m_occluder.isNull()) {   // the mask there is already, at the photo's size
		const QImage same = m_occluder.size() == QSize(w, h) ? m_occluder : m_occluder.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		for (int y = 0; y < h; ++y) std::copy(same.constScanLine(y), same.constScanLine(y) + w, mask.begin() + static_cast<std::ptrdiff_t>(y) * w);
	}
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Paint what is in front"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(tr("Paint over what is in front of the objects in the photo (a railing, a branch, a post): where it is painted red, the photo stays in front of the objects. Paint and Erase use the brush (the "
	                            "wheel changes its size); Select region takes the joined region of similar colour under a click, and Unselect region takes one out; the tolerance says how similar."),
	                         &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *canvas = new OccluderCanvas(m_cameraPhoto, mask, &dialog);
	auto *bar = new QHBoxLayout();
	auto *tool = new QComboBox(&dialog);
	tool->addItem(tr("Paint"), static_cast<int>(OccluderCanvas::Tool::Paint));
	tool->addItem(tr("Erase"), static_cast<int>(OccluderCanvas::Tool::Erase));
	tool->addItem(tr("Select region"), static_cast<int>(OccluderCanvas::Tool::Select));
	tool->addItem(tr("Unselect region"), static_cast<int>(OccluderCanvas::Tool::Unselect));
	connect(tool, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [canvas, tool](int) { canvas->setTool(static_cast<OccluderCanvas::Tool>(tool->currentData().toInt())); });
	bar->addWidget(tool);
	bar->addWidget(new QLabel(tr("Brush"), &dialog));
	auto *brush = new QSlider(Qt::Horizontal, &dialog);
	brush->setRange(2, 120);
	brush->setValue(std::max(4, w / 100));
	brush->setFixedWidth(160);
	canvas->setBrush(brush->value());
	connect(brush, &QSlider::valueChanged, &dialog, [canvas](int v) { canvas->setBrush(v); });
	bar->addWidget(brush);
	bar->addWidget(new QLabel(tr("Tolerance"), &dialog));
	auto *tolerance = new QSlider(Qt::Horizontal, &dialog);
	tolerance->setRange(2, 90);
	tolerance->setValue(24);
	tolerance->setFixedWidth(160);
	connect(tolerance, &QSlider::valueChanged, &dialog, [canvas](int v) { canvas->setTolerance(v); });
	bar->addWidget(tolerance);
	bar->addStretch(1);
	auto *undo = new QPushButton(tr("Undo stroke"), &dialog);
	undo->setAutoDefault(false);
	connect(undo, &QPushButton::clicked, &dialog, [canvas]() { canvas->undoStroke(); });
	bar->addWidget(undo);
	auto *clear = new QPushButton(tr("Clear all"), &dialog);
	clear->setAutoDefault(false);
	connect(clear, &QPushButton::clicked, &dialog, [canvas]() { canvas->clearAll(); });
	bar->addWidget(clear);
	layout->addLayout(bar);
	layout->addWidget(canvas, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	QSize want(std::max(900, w + 60), std::max(640, h + 190));   // (large, so the photo is big enough to paint on: as much as the screen allows)
	if (const QScreen *screen = dialog.screen() ? dialog.screen() : QGuiApplication::primaryScreen()) want = want.boundedTo(QSize(screen->availableGeometry().width() * 92 / 100, screen->availableGeometry().height() * 92 / 100));
	dialog.resize(want);
	if (dialog.exec() != QDialog::Accepted) return;

	std::vector<std::uint8_t> final = canvas->mask();
	occluder_mask::feather(final, w, h, std::max(1, w / 700));   // (the edge a little soft: a pasted object meets the thing in front of it without a staircase)
	const QString text = maskToText(final, w, h);
	if (text.toStdString() == m_doc.photo.occluder) return;   // (nothing changed)
	edit(QString(), [&]() { m_doc.photo.occluder = text.toStdString(); });   // (one undo step; syncCameraPhoto brings the picture of it in)
	logLine(QStringLiteral("photo"), text.isEmpty() ? QStringLiteral("what is in front of the objects: cleared") : QStringLiteral("what is in front of the objects: painted (%1 of %2 pixels)").arg(std::count_if(final.begin(), final.end(), [](std::uint8_t v) { return v > 127; })).arg(final.size()));
	emit statusMessage(text.isEmpty() ? tr("Nothing is in front of the objects any more.") : tr("What is in front of the objects is saved with the scene; Render into the photo leaves it in front."));
}

void SceneBuilderWidget::setOccluderForTest(const QImage &mask) {
	QByteArray png;
	QBuffer buffer(&png);
	buffer.open(QIODevice::WriteOnly);
	mask.convertToFormat(QImage::Format_Grayscale8).save(&buffer, "PNG");
	edit(QString(), [&]() { m_doc.photo.occluder = QString::fromLatin1(png.toBase64()).toStdString(); });
}
