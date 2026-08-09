#include "EditorStyle.h"

namespace
{
	// A dock's title bar sits immediately below the band that resizes it, so a grab that lands low
	// tears the dock out instead. Wider than the platform's for that reason.
	constexpr int c_DockSeparatorExtent = 8;
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
