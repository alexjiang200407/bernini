#pragma once

#include "Windows/AnimationEditor/transition_spans.h"

#include <QWidget>
#include <qsize.h>
#include <qstring.h>
#include <qtmetamacros.h>

class QMouseEvent;
class QPaintEvent;

/**
 * Two clip bars on one timeline, the second offset from the first, with the overlap between them
 * drawn as the fade. One control answers which two clips, how long, and show me: the overlap *is*
 * the duration, and the playhead across it is the scrub.
 *
 * Hand-painted for the reason `Scrubber` is -- everything derives from the current width inside
 * paintEvent, so there is no cached geometry to go stale when a tabified dock is revealed at a size
 * it was not laid out at.
 *
 * It holds no clock and no record. The panel stamps the crossfade and drives `SetTime`; this draws
 * what that produced and reports where the playhead was dragged to.
 */
class TransitionStrip : public QWidget
{
	Q_OBJECT

public:
	explicit TransitionStrip(QWidget* parent = nullptr);

	/** The window, the fade inside it and where the clock sits. Repaints. */
	void
	SetLayout(const editor::TransitionLayout& layout);

	[[nodiscard]] const editor::TransitionLayout&
	GetLayout() const noexcept
	{
		return m_Layout;
	}

	/** What the two bars are labelled -- a clip or a blend space. Empty for an end not yet chosen. */
	void
	SetEndNames(const QString& from, const QString& to);

	/** True while the playhead is held, so a driver leaves the widget alone. */
	[[nodiscard]] bool
	IsScrubbing() const noexcept
	{
		return m_Scrubbing;
	}

	/** The clock a press at `x` seeks to, in a strip `width` wide: pure, for the tests. */
	[[nodiscard]] static float
	TimeForX(const editor::TransitionLayout& layout, int x, int width) noexcept;

	[[nodiscard]] QSize
	sizeHint() const override;

Q_SIGNALS:
	/** The playhead moved -- every tick of a drag included. The clock, in the window's domain. */
	void
	TimeScrubbed(float seconds);

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

	editor::TransitionLayout m_Layout;
	QString                  m_FromName;
	QString                  m_ToName;
	bool                     m_Scrubbing = false;
};
