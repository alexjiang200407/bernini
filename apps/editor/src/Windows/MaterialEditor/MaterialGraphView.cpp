#include "Windows/MaterialEditor/MaterialGraphView.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QShowEvent>
#include <QUrl>

namespace
{
	QString
	FirstTextureUrl(const QMimeData* mime)
	{
		if (mime == nullptr || !mime->hasUrls())
			return {};

		for (const QUrl& url : mime->urls())
		{
			if (!url.isLocalFile())
				continue;
			const QString file = url.toLocalFile();
			if (file.endsWith(".ktx2", Qt::CaseInsensitive))
				return file;
		}
		return {};
	}
}

MaterialGraphView::MaterialGraphView(QWidget* parent) : QtNodes::GraphicsView(parent)
{
	setAcceptDrops(true);

	// Hold the centre through a resize, so a graph centred while the panel was still being laid out
	// stays centred once it has its real size.
	setResizeAnchor(QGraphicsView::AnchorViewCenter);
}

void
MaterialGraphView::showEvent(QShowEvent* event)
{
	// QGraphicsView's, not QtNodes::GraphicsView's: that one calls centerScene(), which fitInViews
	// the graph against the viewport it has at this moment -- and a docked panel is still a few
	// dozen pixels wide when it is first shown. A 275x201 output node measured against a 75x229
	// viewport leaves the view at scale 0.22, and nothing puts it back: the resize anchor holds the
	// centre through the panel's growth to full size, not the zoom.
	QGraphicsView::showEvent(event);
}

void
MaterialGraphView::dragEnterEvent(QDragEnterEvent* event)
{
	if (!FirstTextureUrl(event->mimeData()).isEmpty())
	{
		event->acceptProposedAction();
		return;
	}
	QtNodes::GraphicsView::dragEnterEvent(event);
}

void
MaterialGraphView::dragMoveEvent(QDragMoveEvent* event)
{
	if (!FirstTextureUrl(event->mimeData()).isEmpty())
	{
		event->acceptProposedAction();
		return;
	}
	QtNodes::GraphicsView::dragMoveEvent(event);
}

void
MaterialGraphView::dropEvent(QDropEvent* event)
{
	const QString file = FirstTextureUrl(event->mimeData());
	if (file.isEmpty())
	{
		QtNodes::GraphicsView::dropEvent(event);
		return;
	}

	Q_EMIT TextureDropped(file, mapToScene(event->position().toPoint()));
	event->acceptProposedAction();
}
