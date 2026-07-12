#pragma once

#include <QtNodes/DataFlowGraphicsScene>

/**
 * The material graph's scene. Identical to QtNodes' own, except that the context menu does not offer
 * the sink nodes: a graph has exactly one, it is created with the graph, and it is switched from the
 * toolbar. Letting the menu add a second would give the graph two sinks and compile the material from
 * whichever one happened to be found first.
 */
class MaterialGraphScene : public QtNodes::DataFlowGraphicsScene
{
	Q_OBJECT

public:
	using QtNodes::DataFlowGraphicsScene::DataFlowGraphicsScene;

	QMenu*
	createSceneMenu(QPointF scenePos) override;
};
