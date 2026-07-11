#pragma once

#include <QtNodes/GraphicsView>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

class MaterialGraphView : public QtNodes::GraphicsView
{
	Q_OBJECT

public:
	explicit MaterialGraphView(QWidget* parent = nullptr);

Q_SIGNALS:
	// A texture file was dropped at `scenePos` in graph coordinates.
	void
	TextureDropped(const QString& path, const QPointF& scenePos);

protected:
	void
	dragEnterEvent(QDragEnterEvent* event) override;
	void
	dragMoveEvent(QDragMoveEvent* event) override;
	void
	dropEvent(QDropEvent* event) override;
};
