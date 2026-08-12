#include "Windows/MaterialEditor/MaterialGraphView.h"

#include "util/asset_paths.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QKeyEvent>
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
			if (editor::IsTextureFile(file))
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
	// QGraphicsView's, not QtNodes::GraphicsView's: that one calls centerScene(), which fits the
	// graph to a viewport that is still a few dozen pixels wide when a docked panel is first shown.
	QGraphicsView::showEvent(event);
}

void
MaterialGraphView::keyPressEvent(QKeyEvent* event)
{
	QtNodes::GraphicsView::keyPressEvent(event);

	// Only once nothing else has taken it: whatever is being typed into accepts Backspace, and a
	// node -- or the proxy holding its preview image, which is what a click on a node usually lands
	// on -- does not. Asking who holds focus instead answers "the proxy" for almost every deletion a
	// user makes. The sink survives either key, MaterialGraphModel::deleteNode refusing it.
	if (!event->isAccepted() && event->key() == Qt::Key_Backspace &&
	    event->modifiers() == Qt::NoModifier)
	{
		onDeleteSelectedObjects();
		event->accept();
	}
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
