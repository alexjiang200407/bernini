#pragma once

#include <QtNodes/GraphicsView>
#include <QtNodes/internal/GraphicsView.hpp>
#include <qobject.h>
#include <qpoint.h>
#include <qtmetamacros.h>
#include <qwidget.h>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QShowEvent;

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
	/**
	 * Shows the view without re-framing the scene, which is what the base class does here.
	 *
	 * @pre the graph must already have been framed by whoever set the scene -- nothing frames it
	 *      after this.
	 */
	void
	showEvent(QShowEvent* event) override;

	/**
	 * Deletes the selection on Backspace as well as on Delete, which is all the base class binds --
	 * and the key a Mac keyboard labels "delete" is Backspace.
	 */
	void
	keyPressEvent(QKeyEvent* event) override;

	void
	dragEnterEvent(QDragEnterEvent* event) override;
	void
	dragMoveEvent(QDragMoveEvent* event) override;
	void
	dropEvent(QDropEvent* event) override;
};
