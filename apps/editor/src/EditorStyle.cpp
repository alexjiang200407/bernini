#include "EditorStyle.h"

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
