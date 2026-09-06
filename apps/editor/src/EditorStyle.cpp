#include "EditorStyle.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QtCore>
#include <algorithm>
#include <qnamespace.h>
#include <qobject.h>
#include <qpoint.h>
#include <qpolygon.h>
#include <qproxystyle.h>
#include <qsize.h>
#include <qstyle.h>
#include <qtypes.h>

namespace
{
	// A dock's title bar sits immediately below the band that resizes it, so a grab that lands low
	// tears the dock out instead. Wider than the platform's for that reason.
	constexpr int c_DockSeparatorExtent = 8;

	// Chevron geometry, as a fraction of the icon box: how far each arm reaches across and down
	// from the tip, and how thick the stroke is.
	constexpr qreal c_ChevronReach = 0.15;
	constexpr qreal c_ChevronArm   = 0.22;
	constexpr qreal c_ChevronWidth = 0.12;

	/**
	 * A chevron drawn on demand in the palette's button-text colour.
	 *
	 * Painting rather than storing a pixmap is what keeps the colour current: the palette is read
	 * at paint time, so an appearance switch retints the glyph instead of leaving the colour it
	 * was built with.
	 */
	class ChevronIconEngine final : public QIconEngine
	{
	public:
		explicit ChevronIconEngine(bool pointsLeft) noexcept : m_PointsLeft(pointsLeft) {}

		void
		paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override;

		QPixmap
		pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override;

		QIconEngine*
		clone() const override
		{
			return new ChevronIconEngine(m_PointsLeft);
		}

	private:
		bool m_PointsLeft;
	};

	void
	ChevronIconEngine::paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State)
	{
		const auto side = qreal(std::min(rect.width(), rect.height()));
		if (side <= 0.0)
			return;

		const auto& palette = QApplication::palette();
		const auto  group   = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Normal;

		const auto centre = QPointF(rect.center()) + QPointF(0.5, 0.5);
		const auto reach  = side * c_ChevronReach;
		const auto arm    = side * c_ChevronArm;
		const auto tipX   = m_PointsLeft ? centre.x() - reach : centre.x() + reach;
		const auto backX  = m_PointsLeft ? centre.x() + reach : centre.x() - reach;

		auto pen =
			QPen(palette.brush(group, QPalette::ButtonText), std::max(1.0, side * c_ChevronWidth));
		pen.setCapStyle(Qt::RoundCap);
		pen.setJoinStyle(Qt::RoundJoin);

		painter->save();
		painter->setRenderHint(QPainter::Antialiasing, true);
		painter->setPen(pen);
		painter->drawPolyline(QPolygonF(
			{
				QPointF(backX, centre.y() - arm),
				QPointF(tipX, centre.y()),
				QPointF(backX, centre.y() + arm),
			}));
		painter->restore();
	}

	QPixmap
	ChevronIconEngine::pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state)
	{
		// The base's pixmap() does not clear what it allocates, so the chevron would land on
		// undefined contents -- opaque black, as it happens.
		auto canvas = QPixmap(size);
		canvas.fill(Qt::transparent);

		auto painter = QPainter(&canvas);
		paint(&painter, QRect(QPoint(0, 0), size), mode, state);

		return canvas;
	}
}

int
EditorStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget)
	const
{
	if (metric == PM_DockWidgetSeparatorExtent)
		return c_DockSeparatorExtent;

	return QProxyStyle::pixelMetric(metric, option, widget);
}

int
EditorStyle::styleHint(
	StyleHint           hint,
	const QStyleOption* option,
	const QWidget*      widget,
	QStyleHintReturn*   returnData) const
{
	if (hint == SH_TabBar_Alignment)
		return Qt::AlignLeft;

	return QProxyStyle::styleHint(hint, option, widget, returnData);
}

QIcon
EditorStyle::standardIcon(
	StandardPixmap      standardPixmap,
	const QStyleOption* option,
	const QWidget*      widget) const
{
	const auto rightToLeft = QGuiApplication::isRightToLeft();

	switch (standardPixmap)
	{
	case SP_ArrowBack:
		return QIcon(new ChevronIconEngine(!rightToLeft));
	case SP_ArrowForward:
		return QIcon(new ChevronIconEngine(rightToLeft));
	default:
		return QProxyStyle::standardIcon(standardPixmap, option, widget);
	}
}

void
EditorStyle::drawPrimitive(
	PrimitiveElement    element,
	const QStyleOption* option,
	QPainter*           painter,
	const QWidget*      widget) const
{
	// The base spans the whole strip, so with left-aligned tabs it reads as a rule hanging off the
	// last one rather than as the edge of anything.
	if (element == PE_FrameTabBarBase)
		return;

	QProxyStyle::drawPrimitive(element, option, painter, widget);
}
