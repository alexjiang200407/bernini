#include "TimelineScrubber.h"

#include <QMouseEvent>
#include <QPainter>

namespace
{
	constexpr int c_GrooveHeight   = 4;
	constexpr int c_HandleRadius   = 7;
	constexpr int c_PreferredWidth = 200;
}

TimelineScrubber::TimelineScrubber(QWidget* parent) : QWidget(parent)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setCursor(Qt::PointingHandCursor);
}

void
TimelineScrubber::SetTickCount(const int tickCount)
{
	m_TickCount = std::max(1, tickCount);
	SetValue(m_Value);
	update();
}

void
TimelineScrubber::SetValue(const int ticks)
{
	const int clamped = std::clamp(ticks, 0, m_TickCount);
	if (clamped == m_Value)
		return;

	m_Value = clamped;
	update();
	Q_EMIT ValueChanged(m_Value);
}

int
TimelineScrubber::ValueForX(const int x, const int width, const int tickCount) noexcept
{
	// The handle's center travels [radius, width - radius], so the full range stays reachable
	// with the handle drawn wholly inside the widget.
	const int span = width - 2 * c_HandleRadius;
	if (span <= 0 || tickCount <= 0)
		return 0;

	const float normalized =
		static_cast<float>(std::clamp(x - c_HandleRadius, 0, span)) / static_cast<float>(span);
	return static_cast<int>(std::lround(normalized * static_cast<float>(tickCount)));
}

QSize
TimelineScrubber::sizeHint() const
{
	return QSize(c_PreferredWidth, 2 * c_HandleRadius + 6);
}

void
TimelineScrubber::paintEvent(QPaintEvent*)
{
	auto painter = QPainter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);

	const int   centerY = height() / 2;
	const int   span    = std::max(1, width() - 2 * c_HandleRadius);
	const float t       = static_cast<float>(m_Value) / static_cast<float>(m_TickCount);
	const int   handleX =
		c_HandleRadius + static_cast<int>(std::lround(t * static_cast<float>(span)));

	const auto groove = QRect(
		c_HandleRadius,
		centerY - c_GrooveHeight / 2,
		width() - 2 * c_HandleRadius,
		c_GrooveHeight);

	painter.setBrush(palette().color(QPalette::Mid));
	painter.drawRoundedRect(groove, 2, 2);

	painter.setBrush(palette().color(isEnabled() ? QPalette::Highlight : QPalette::Mid));
	painter.drawRoundedRect(
		QRect(groove.x(), groove.y(), handleX - groove.x(), groove.height()),
		2,
		2);

	painter.setBrush(palette().color(isEnabled() ? QPalette::BrightText : QPalette::Midlight));
	painter.drawEllipse(QPoint(handleX, centerY), c_HandleRadius, c_HandleRadius);
}

void
TimelineScrubber::SeekTo(const int x)
{
	SetValue(ValueForX(x, width(), m_TickCount));
}

void
TimelineScrubber::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
		return;

	m_Scrubbing = true;
	SeekTo(event->position().toPoint().x());
}

void
TimelineScrubber::mouseMoveEvent(QMouseEvent* event)
{
	if (m_Scrubbing)
		SeekTo(event->position().toPoint().x());
}

void
TimelineScrubber::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
		m_Scrubbing = false;
}
