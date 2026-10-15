// scene_builder_text.cpp -- the outlines of 3D text: Qt turns a font and a string into the letters' outlines, which src/shared/scene_text.h triangulates and extrudes.
// The Scene Builder sets this as the text provider once (installTextOutlineProvider); the unit tests use a fake one.
#include "scene_builder_common.h"
#include "app_log.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainterPath>
#include <QPolygonF>
#include <QStringList>
#include <QTransform>

#include <algorithm>
#include <mutex>

namespace {

// Outlines of `p.text` in one block, lines under each other. A pixel size this big makes the curves flatten into many short segments, so a big letter is not faceted.
bool qtTextOutline(const scene_doc::TextParams &p, scene_doc::TextOutline &out) {
	if (!QGuiApplication::instance()) return false;
	QFont font = p.font.empty() ? QGuiApplication::font() : QFont(QString::fromStdString(p.font));
	font.setBold(p.bold);
	font.setItalic(p.italic);
	font.setPixelSize(240);
	font.setHintingPreference(QFont::PreferNoHinting);
	const QFontMetricsF metrics(font);
	QString text = QString::fromStdString(p.text);
	text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
	const QStringList lines = text.split(QLatin1Char('\n'));
	const qreal advance = metrics.height() * 1.05;   // from one baseline to the next
	qreal widest = 0;
	for (const QString &line : lines) widest = std::max(widest, metrics.horizontalAdvance(line));
	QPainterPath path;
	for (int i = 0; i < lines.size(); ++i) {
		if (lines[i].trimmed().isEmpty()) continue;
		const qreal width = metrics.horizontalAdvance(lines[i]);
		const qreal x = p.align == 0 ? 0.0 : p.align == 2 ? widest - width : (widest - width) / 2;
		path.addText(QPointF(x, i * advance), font, lines[i]);
	}
	if (path.isEmpty()) return false;
	// A font fills its outlines with the non-zero winding rule, and many modern fonts build a letter from overlapping contours (a bold k, an accented letter, italic glyphs that
	// touch): read by parity (as scene_text.h's triangulator does) the overlap would be a hole. Simplifying the path under the winding rule merges what overlaps into outlines
	// that do not cross, which parity reads right.
	path.setFillRule(Qt::WindingFill);
	path = path.simplified();
	if (path.isEmpty()) return false;
	// (Qt's y runs down: the letters are turned over to stand up.)
	for (const QPolygonF &polygon : path.toSubpathPolygons(QTransform())) {
		scene_doc::TextContour contour;
		contour.reserve(static_cast<std::size_t>(polygon.size()));
		for (const QPointF &pt : polygon) contour.push_back({pt.x(), -pt.y()});
		if (contour.size() >= 3) out.contours.push_back(std::move(contour));
	}
	out.capHeight = metrics.capHeight() > 0 ? metrics.capHeight() : metrics.ascent() * 0.7;
	return !out.contours.empty();
}

}  // namespace

namespace scene_builder_ui {
void installTextOutlineProvider() {
	static std::once_flag once;
	std::call_once(once, []() {
		scene_doc::setTextOutlineProvider(&qtTextOutline);
		scene_doc::setTextLog([](const std::string &message) { AppLog::warn(QStringLiteral("builder"), QString::fromStdString(message)); });
	});
}
}  // namespace scene_builder_ui
