#include "Scrubber.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace
{
	constexpr int c_GrooveHeight   = 4;
	constexpr int c_HandleRadius   = 7;
	constexpr int c_PreferredWidth = 200;
}

Scrubber::Scrubber(QWidget* parent) : QWidget(parent)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::StrongFocus);
}

void
Scrubber::SetRange(const int min, const int max)
{
	m_Min = min;
	m_Max = std::max(min, max);
	SetValue(m_Value);
	update();
}

void
Scrubber::SetValue(const int value)
{
	const int clamped = std::clamp(value, m_Min, m_Max);
	if (clamped == m_Value)
		return;

	m_Value = clamped;
	update();
	Q_EMIT ValueChanged(m_Value);
}

int
Scrubber::ValueForX(const int x, const int width, const int min, const int max) noexcept
{
	// The handle's center travels [radius, width - radius], so the full range stays reachable
	// with the handle drawn wholly inside the widget.
	const int span = width - 2 * c_HandleRadius;
	if (span <= 0 || max <= min)
		return min;

	const float normalized =
		static_cast<float>(std::clamp(x - c_HandleRadius, 0, span)) / static_cast<float>(span);
	return min + static_cast<int>(std::lround(normalized * static_cast<float>(max - min)));
}

QSize
Scrubber::sizeHint() const
{
	return QSize(c_PreferredWidth, 2 * c_HandleRadius + 6);
}

void
Scrubber::paintEvent(QPaintEvent*)
{
	auto painter = QPainter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);

	const int   centerY = height() / 2;
	const int   span    = std::max(1, width() - 2 * c_HandleRadius);
	const float t = m_Max > m_Min ?
	                    static_cast<float>(m_Value - m_Min) / static_cast<float>(m_Max - m_Min) :
	                    0.0f;
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
Scrubber::SeekTo(const int x)
{
	SetValue(ValueForX(x, width(), m_Min, m_Max));
}

void
Scrubber::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
		return;

	m_Scrubbing = true;
	SeekTo(event->position().toPoint().x());
}

void
Scrubber::mouseMoveEvent(QMouseEvent* event)
{
	if (m_Scrubbing)
		SeekTo(event->position().toPoint().x());
}

void
Scrubber::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
		return;

	m_Scrubbing = false;

	// A click that never moved settles here too: the press seeked, and this is the release that
	// says so.
	Q_EMIT Committed(m_Value);
}

void
Scrubber::keyPressEvent(QKeyEvent* event)
{
	const int step = event->key() == Qt::Key_Left || event->key() == Qt::Key_Down ? -1 :
	                 event->key() == Qt::Key_Right || event->key() == Qt::Key_Up  ? 1 :
	                                                                                0;
	if (step == 0)
	{
		QWidget::keyPressEvent(event);
		return;
	}

	SetValue(m_Value + step);
	Q_EMIT Committed(m_Value);
	event->accept();
}
