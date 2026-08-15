#pragma once

#include <QWidget>

/**
 * The transport's timeline: a click-anywhere-to-seek scrubber painted by hand. Everything is
 * derived from the widget's current width inside paintEvent, so there is no native style and no
 * cached groove to go stale when a tabified dock is revealed at a size it was not laid out at --
 * the artifact QSlider showed here.
 *
 * Values are integer ticks on [0, tickCount], the contract the panel's TimelineTicks /
 * TimelineSeconds mapping already speaks.
 */
class TimelineScrubber : public QWidget
{
	Q_OBJECT

public:
	explicit TimelineScrubber(QWidget* parent = nullptr);

	void
	SetTickCount(int tickCount);

	/** Clamps into [0, tickCount]. Emits ValueChanged only when the value actually moves. */
	void
	SetValue(int ticks);

	[[nodiscard]] int
	GetValue() const noexcept
	{
		return m_Value;
	}

	/** True while the user holds the handle; the panel then leaves the widget alone. */
	[[nodiscard]] bool
	IsScrubbing() const noexcept
	{
		return m_Scrubbing;
	}

	/** The tick a press at `x` seeks to, in a widget `width` wide: pure, for the tests. */
	[[nodiscard]] static int
	ValueForX(int x, int width, int tickCount) noexcept;

	[[nodiscard]] QSize
	sizeHint() const override;

Q_SIGNALS:
	/** The value moved -- by a seek or by SetValue. `ticks` is on [0, tickCount]. */
	void
	ValueChanged(int ticks);

protected:
	void
	paintEvent(QPaintEvent* event) override;

	void
	mousePressEvent(QMouseEvent* event) override;
	void
	mouseMoveEvent(QMouseEvent* event) override;
	void
	mouseReleaseEvent(QMouseEvent* event) override;

private:
	void
	SeekTo(int x);

	int  m_TickCount = 1000;
	int  m_Value     = 0;
	bool m_Scrubbing = false;
};
