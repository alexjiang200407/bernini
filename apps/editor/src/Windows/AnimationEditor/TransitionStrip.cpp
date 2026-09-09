#include "TransitionStrip.h"

#include "Windows/AnimationEditor/transition_spans.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <algorithm>
#include <qbrush.h>
#include <qcolor.h>
#include <qnamespace.h>
#include <qpalette.h>
#include <qpen.h>
#include <qpoint.h>
#include <qrect.h>
#include <qsize.h>
#include <qsizepolicy.h>
#include <qstring.h>
#include <qtmetamacros.h>

namespace
{
	constexpr int c_BarHeight       = 18;
	constexpr int c_BarGap          = 4;
	constexpr int c_PreferredWidth  = 220;
	constexpr int c_LabelMargin     = 4;
	constexpr int c_PlayheadWidth   = 2;
	constexpr int c_OverlapMinWidth = 1;
}

TransitionStrip::TransitionStrip(QWidget* parent) : QWidget(parent)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::StrongFocus);
}

void
TransitionStrip::SetLayout(const editor::TransitionLayout& layout)
{
	m_Layout = layout;
	update();
}

void
TransitionStrip::SetClipNames(const QString& from, const QString& to)
{
	m_FromName = from;
	m_ToName   = to;
	update();
}

float
TransitionStrip::TimeForX(
	const editor::TransitionLayout& layout,
	const int                       x,
	const int                       width) noexcept
{
	if (width <= 0 || !(layout.windowEnd > layout.windowStart))
		return layout.windowStart;

	const float at = std::clamp(static_cast<float>(x) / static_cast<float>(width), 0.0f, 1.0f);
	return layout.windowStart + at * (layout.windowEnd - layout.windowStart);
}

QSize
TransitionStrip::sizeHint() const
{
	return { c_PreferredWidth, 2 * c_BarHeight + c_BarGap };
}

void
TransitionStrip::paintEvent(QPaintEvent*)
{
	auto painter = QPainter(this);
	painter.setPen(Qt::NoPen);

	const editor::TransitionSpans spans = editor::SpansForTransition(m_Layout, width());

	const int fromY = 0;
	const int toY   = c_BarHeight + c_BarGap;

	const QColor bar = palette().color(isEnabled() ? QPalette::Highlight : QPalette::Mid);

	// The two clips, on their own rows so the offset between them is the picture. The incoming one
	// is drawn lighter, so which is arriving reads without a legend.
	painter.setBrush(bar);
	painter.drawRoundedRect(QRect(spans.from.x, fromY, spans.from.width, c_BarHeight), 2, 2);

	painter.setBrush(bar.lighter(130));
	painter.drawRoundedRect(QRect(spans.to.x, toY, spans.to.width, c_BarHeight), 2, 2);

	// The fade, spanning both rows: it is the one thing being authored, and drawing it across the
	// gap is what makes it read as the two clips overlapping rather than a third bar.
	if (spans.overlap.width >= c_OverlapMinWidth)
	{
		painter.setBrush(palette().color(QPalette::AlternateBase));
		painter.setOpacity(0.55);
		painter.drawRect(
			QRect(spans.overlap.x, fromY, spans.overlap.width, 2 * c_BarHeight + c_BarGap));
		painter.setOpacity(1.0);
	}

	painter.setPen(palette().color(QPalette::BrightText));
	if (!m_FromName.isEmpty())
	{
		painter.drawText(
			QRect(spans.from.x + c_LabelMargin, fromY, spans.from.width, c_BarHeight),
			Qt::AlignVCenter | Qt::AlignLeft,
			m_FromName);
	}
	if (!m_ToName.isEmpty())
	{
		painter.drawText(
			QRect(spans.to.x + c_LabelMargin, toY, spans.to.width, c_BarHeight),
			Qt::AlignVCenter | Qt::AlignLeft,
			m_ToName);
	}

	painter.setPen(Qt::NoPen);
	painter.setBrush(palette().color(isEnabled() ? QPalette::BrightText : QPalette::Midlight));
	painter.drawRect(QRect(spans.playheadX, fromY, c_PlayheadWidth, 2 * c_BarHeight + c_BarGap));
}

void
TransitionStrip::SeekTo(const int x)
{
	const float seconds = TimeForX(m_Layout, x, width());
	m_Layout.time       = seconds;
	update();
	Q_EMIT TimeScrubbed(seconds);
}

void
TransitionStrip::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
		return;

	m_Scrubbing = true;
	SeekTo(static_cast<int>(event->position().x()));
}

void
TransitionStrip::mouseMoveEvent(QMouseEvent* event)
{
	if (m_Scrubbing)
		SeekTo(static_cast<int>(event->position().x()));
}

void
TransitionStrip::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
		m_Scrubbing = false;
}
