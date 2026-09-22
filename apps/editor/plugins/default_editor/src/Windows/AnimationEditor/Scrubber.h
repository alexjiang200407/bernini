#pragma once

#include <QWidget>
#include <qsize.h>
#include <qtmetamacros.h>

/**
 * A click-anywhere value bar, painted by hand. Everything is derived from the widget's current
 * width inside paintEvent, so there is no native style and no cached groove to go stale when a
 * tabified dock is revealed at a size it was not laid out at -- the artifact QSlider showed here,
 * and the reason this exists at all.
 *
 * Integer values on a closed range. The transport drives it over [0, ticks]; the ground controls
 * over their own, which is why the range has a floor rather than being a count.
 */
class Scrubber : public QWidget
{
	Q_OBJECT

public:
	explicit Scrubber(QWidget* parent = nullptr);

	/** The closed range the value lives on. A `max` below `min` is taken as `min`. */
	void
	SetRange(int min, int max);

	/** Clamps into the range. Emits ValueChanged only when the value actually moves. */
	void
	SetValue(int value);

	[[nodiscard]] int
	GetValue() const noexcept
	{
		return m_Value;
	}

	/** True while the user holds the handle; a driver then leaves the widget alone. */
	[[nodiscard]] bool
	IsScrubbing() const noexcept
	{
		return m_Scrubbing;
	}

	/** The value a press at `x` seeks to, in a widget `width` wide: pure, for the tests. */
	[[nodiscard]] static int
	ValueForX(int x, int width, int min, int max) noexcept;

	[[nodiscard]] QSize
	sizeHint() const override;

Q_SIGNALS:
	/** The value moved -- by a seek, a key or SetValue. Every tick of a drag included. */
	void
	ValueChanged(int value);

	/**
	 * The value settled: a drag ended, a click landed, or a key moved it. What a listener commits
	 * on when committing is expensive -- the ground plane moves the scene's temporal epoch, so
	 * following every tick of a drag would hold the preview unaccumulated for the whole gesture.
	 */
	void
	Committed(int value);

protected:
	void
	paintEvent(QPaintEvent* event) override;

	void
	mousePressEvent(QMouseEvent* event) override;
	void
	mouseMoveEvent(QMouseEvent* event) override;
	void
	mouseReleaseEvent(QMouseEvent* event) override;
	void
	keyPressEvent(QKeyEvent* event) override;

private:
	void
	SeekTo(int x);

	int  m_Min       = 0;
	int  m_Max       = 1000;
	int  m_Value     = 0;
	bool m_Scrubbing = false;
};
