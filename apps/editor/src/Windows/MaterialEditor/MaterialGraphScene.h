#pragma once

#include <QtNodes/DataFlowGraphicsScene>

class MaterialGraphScene : public QtNodes::DataFlowGraphicsScene
{
	Q_OBJECT

public:
	using QtNodes::DataFlowGraphicsScene::DataFlowGraphicsScene;

	QMenu*
	createSceneMenu(QPointF scenePos) override;
};
