#include "Windows/MaterialEditor/nodes/MaterialSinkNode.h"

#include <QApplication>
#include <QEvent>
#include <QWidget>
#include <QtNodes/internal/NodeDelegateModel.hpp>
#include <qtmetamacros.h>

void
MaterialSinkNode::WatchEmbeddedWidget(QWidget* widget)
{
	m_WatchedWidget = widget;
	widget->installEventFilter(this);
}

QWidget*
MaterialSinkNode::DialogOwnerFor(QWidget* embedded)
{
	QWidget* owner = embedded != nullptr ? embedded->window() : nullptr;
	if (owner == nullptr || owner->graphicsProxyWidget() != nullptr)
		owner = QApplication::activeWindow();

	return owner;
}

bool
MaterialSinkNode::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_WatchedWidget)
	{
		// A row shown or hidden invalidates the layout without resizing the widget -- nothing
		// lays the proxied widget out from outside, so a shrunk size hint would otherwise leave
		// a gap. Adopting the hint is what turns it into the resize below.
		if (event->type() == QEvent::LayoutRequest)
			m_WatchedWidget->adjustSize();

		// No recursion: the re-measure repositions the proxy and repaints, and never resizes the
		// widget back.
		if (event->type() == QEvent::Resize)
			Q_EMIT requestNodeUpdate();
	}

	return QtNodes::NodeDelegateModel::eventFilter(watched, event);
}
