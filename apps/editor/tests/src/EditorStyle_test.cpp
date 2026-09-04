#include "EditorStyle.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPalette>
#include <QPixmap>
#include <catch2/catch_test_macros.hpp>
#include <qguiapplication.h>
#include <qrgb.h>
#include <qtypes.h>

// The Content Explorer's back arrow. No platform style here answers SP_ArrowBack, so without
// EditorStyle's own the request falls through to QCommonStyle's bundled bitmap -- a fixed green
// raster that ignores the palette. These pin the properties that bitmap fails.

namespace
{
	constexpr int c_IconDim = 64;

	// Catch2's REQUIRE throws, so the application palette is put back by a destructor rather than
	// at the end of the case.
	struct PaletteGuard
	{
		QPalette previous = QApplication::palette();

		~PaletteGuard() { QApplication::setPalette(previous); }
	};

	void
	SetButtonText(const QColor& colour)
	{
		QPalette palette = QApplication::palette();
		palette.setColor(QPalette::Active, QPalette::ButtonText, colour);
		QApplication::setPalette(palette);
	}

	QImage
	Render(const QIcon& icon)
	{
		return icon.pixmap(QSize(c_IconDim, c_IconDim))
		    .toImage()
		    .convertToFormat(QImage::Format_ARGB32);
	}

	/**
	 * How far the glyph spreads vertically in the first column it paints, scanning from one side.
	 *
	 * A chevron's tip is one stroke and its open end is two, so this separates the two ends without
	 * depending on the rasteriser placing a pixel exactly.
	 */
	int
	FirstPaintedColumnSpread(const QImage& image, bool scanFromLeft)
	{
		for (int step = 0; step < image.width(); ++step)
		{
			const int x = scanFromLeft ? step : image.width() - 1 - step;

			int top    = -1;
			int bottom = -1;
			for (int y = 0; y < image.height(); ++y)
			{
				if (qAlpha(image.pixel(x, y)) == 0)
					continue;

				if (top < 0)
					top = y;
				bottom = y;
			}

			if (top >= 0)
				return bottom - top;
		}

		return -1;
	}
}

TEST_CASE("The navigation arrow is the palette's colour whenever it is painted", "[editorstyle]")
{
	const PaletteGuard guard;
	const EditorStyle  style;

	SetButtonText(QColor(255, 0, 255));
	const QIcon icon = style.standardIcon(QStyle::SP_ArrowBack, nullptr, nullptr);

	auto checkSolidPixelsAre = [](const QImage& image, const QColor& expected) {
		auto solid = 0;
		for (int y = 0; y < image.height(); ++y)
		{
			for (int x = 0; x < image.width(); ++x)
			{
				const QRgb pixel = image.pixel(x, y);
				if (qAlpha(pixel) < 255)
					continue;

				++solid;
				REQUIRE(qRed(pixel) == expected.red());
				REQUIRE(qGreen(pixel) == expected.green());
				REQUIRE(qBlue(pixel) == expected.blue());
			}
		}

		// An icon that painted nothing at all satisfies the loop above.
		REQUIRE(solid > 0);
	};

	checkSolidPixelsAre(Render(icon), QColor(255, 0, 255));

	// The same QIcon, deliberately: an arrow rendered once and kept would still be magenta.
	SetButtonText(QColor(0, 255, 255));
	checkSolidPixelsAre(Render(icon), QColor(0, 255, 255));
}

TEST_CASE("Back points the way the layout reads, and Forward the other", "[editorstyle]")
{
	const EditorStyle style;

	const QImage back    = Render(style.standardIcon(QStyle::SP_ArrowBack, nullptr, nullptr));
	const QImage forward = Render(style.standardIcon(QStyle::SP_ArrowForward, nullptr, nullptr));

	// One stroke at the tip, two at the open end -- so the tip is the narrow column.
	constexpr int c_Tip  = c_IconDim / 4;
	constexpr int c_Open = c_IconDim / 3;

	const bool rightToLeft = QGuiApplication::isRightToLeft();

	CHECK(FirstPaintedColumnSpread(back, !rightToLeft) < c_Tip);
	CHECK(FirstPaintedColumnSpread(back, rightToLeft) > c_Open);

	CHECK(FirstPaintedColumnSpread(forward, rightToLeft) < c_Tip);
	CHECK(FirstPaintedColumnSpread(forward, !rightToLeft) > c_Open);
}

TEST_CASE("The arrow is rasterised at the device pixel ratio asked for", "[editorstyle]")
{
	const EditorStyle style;
	const QIcon       icon = style.standardIcon(QStyle::SP_ArrowBack, nullptr, nullptr);

	// The engine reimplements neither scaledPixmap nor virtual_hook, so this pins that the base's
	// own route to pixmap() carries the ratio through rather than answering a Retina button at 1x.
	for (const qreal ratio : { 1.0, 2.0, 3.0 })
	{
		const QPixmap rendered = icon.pixmap(QSize(32, 32), ratio);

		CHECK(rendered.width() == int(32 * ratio));
		CHECK(rendered.height() == int(32 * ratio));
		CHECK(rendered.devicePixelRatio() == ratio);
	}
}

TEST_CASE("Every other standard pixmap is still the platform's", "[editorstyle]")
{
	const EditorStyle style;

	// The Animation editor's transport bar pulls this one, and is deliberately left on the
	// platform's answer for it.
	const QImage ours = Render(style.standardIcon(QStyle::SP_MediaPlay, nullptr, nullptr));
	const QImage platform =
		Render(QApplication::style()->standardIcon(QStyle::SP_MediaPlay, nullptr, nullptr));

	CHECK(ours == platform);
}
