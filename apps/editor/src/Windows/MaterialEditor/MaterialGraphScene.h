#pragma once

#include <QtNodes/DataFlowGraphicsScene>

class MaterialGraphScene : public QtNodes::DataFlowGraphicsScene
{
	Q_OBJECT

public:
	using QtNodes::DataFlowGraphicsScene::DataFlowGraphicsScene;

	/** Null: this graph has no nodes you add from a menu. See the .cpp. */
	QMenu*
	createSceneMenu(QPointF scenePos) override;
};
