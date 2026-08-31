#pragma once

#include <QIcon>
#include <QProxyStyle>

/**
 * The platform's own style, with the rules that make a docked layout awkward overridden.
 *
 * macOS centres a tab strip and rules a base line across the full width of it; the editor's dock
 * tabs name panes, so they read as a toolbar and are left-aligned and bare on every platform. And
 * the band between two docks is widened, because the platforms make it too thin to aim at.
 *
 * It also paints the navigation arrows itself, because no platform style here answers them and the
 * fallback is a fixed green bitmap that ignores the palette.
 */
class EditorStyle final : public QProxyStyle
{
	Q_OBJECT

public:
	int
	pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget)
		const override;

	int
	styleHint(
		StyleHint           hint,
		const QStyleOption* option,
		const QWidget*      widget,
		QStyleHintReturn*   returnData) const override;

	/**
	 * The navigation arrows, drawn from the palette; every other pixmap is the platform's.
	 *
	 * `SP_ArrowBack` and `SP_ArrowForward` follow the application's layout direction, as
	 * `QCommonStyle` does.
	 */
	QIcon
	standardIcon(StandardPixmap standardPixmap, const QStyleOption* option, const QWidget* widget)
		const override;

	void
	drawPrimitive(
		PrimitiveElement    element,
		const QStyleOption* option,
		QPainter*           painter,
		const QWidget*      widget) const override;
};
