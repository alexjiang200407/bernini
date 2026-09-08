#pragma once

#include <QWidget>
#include <cstddef>
#include <optional>
#include <qtmetamacros.h>

class QMouseEvent;
class QPaintEvent;
class QEvent;

namespace editor
{
	class PassHistory;

	/**
	 * The chart itself: it draws a history it does not own and reports what the pointer is over.
	 *
	 * The history belongs to the window, because the window is what the frames arrive at and what
	 * pauses; this widget holds a pointer to it and repaints when told.
	 */
	class PassGraphView : public QWidget
	{
		Q_OBJECT

	public:
		explicit PassGraphView(const PassHistory& history, QWidget* parent = nullptr);

		/** The sample under the pointer, or nullopt when the pointer is elsewhere. */
		[[nodiscard]] std::optional<std::size_t>
		Marked() const noexcept
		{
			return m_Marked;
		}

	protected:
		void
		paintEvent(QPaintEvent* event) override;

		void
		mouseMoveEvent(QMouseEvent* event) override;

		void
		leaveEvent(QEvent* event) override;

	private:
		const PassHistory&         m_History;
		std::optional<std::size_t> m_Marked;
	};
}
