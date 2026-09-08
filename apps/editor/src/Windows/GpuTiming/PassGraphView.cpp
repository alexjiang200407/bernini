#include "Windows/GpuTiming/PassGraphView.h"

#include "Windows/GpuTiming/pass_graph_paint.h"
#include <QEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QWidget>
#include <Qt>
#include <cstddef>
#include <optional>

namespace editor
{
	PassGraphView::PassGraphView(const PassHistory& history, QWidget* parent) :
		QWidget(parent), m_History(history)
	{
		setObjectName("PassGraphView");
		setMouseTracking(true);
		setMinimumSize(480, 220);
		setAutoFillBackground(true);
	}

	void
	PassGraphView::paintEvent(QPaintEvent* event)
	{
		QWidget::paintEvent(event);

		QPainter painter(this);
		PaintPassGraph(painter, rect(), m_History, m_Marked, palette());
	}

	void
	PassGraphView::mouseMoveEvent(QMouseEvent* event)
	{
		const std::optional<std::size_t> marked =
			PassGraphSampleAt(rect(), m_History, event->position().toPoint().x());

		if (marked != m_Marked)
		{
			m_Marked = marked;
			update();
		}
	}

	void
	PassGraphView::leaveEvent(QEvent* event)
	{
		QWidget::leaveEvent(event);

		if (m_Marked.has_value())
		{
			m_Marked.reset();
			update();
		}
	}
}
