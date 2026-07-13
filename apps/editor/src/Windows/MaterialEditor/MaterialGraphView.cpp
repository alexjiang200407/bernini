#include "Windows/MaterialEditor/MaterialGraphView.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
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
