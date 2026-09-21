#pragma once

#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/internal/DataFlowGraphicsScene.hpp>
#include <qmenu.h>
#include <qpoint.h>
#include <qtmetamacros.h>

class MaterialGraphScene : public QtNodes::DataFlowGraphicsScene
{
	Q_OBJECT

public:
	using QtNodes::DataFlowGraphicsScene::DataFlowGraphicsScene;

	/** Null: this graph has no nodes you add from a menu. See the .cpp. */
	QMenu*
	createSceneMenu(QPointF scenePos) override;
};
