#pragma once

#include <QProxyStyle>

/**
 * The platform's own style, with the two rules that make a tabbed dock area look like a document
 * picker overridden. macOS centres a tab strip and rules a base line across the full width of it;
 * the editor's dock tabs name panes, so they read as a toolbar and are left-aligned and bare on
 * every platform.
 */
class EditorStyle final : public QProxyStyle
{
	Q_OBJECT

public:
	int
	styleHint(
		StyleHint           hint,
		const QStyleOption* option,
		const QWidget*      widget,
		QStyleHintReturn*   returnData) const override;

	void
	drawPrimitive(
		PrimitiveElement    element,
		const QStyleOption* option,
		QPainter*           painter,
		const QWidget*      widget) const override;
};
