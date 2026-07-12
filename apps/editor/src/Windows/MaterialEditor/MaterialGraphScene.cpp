#include "Windows/MaterialEditor/MaterialGraphScene.h"

#include <QMenu>
#include <QTreeWidget>

#include "Windows/MaterialEditor/MaterialGraphModel.h"

QMenu*
MaterialGraphScene::createSceneMenu(QPointF scenePos)
{
	QMenu* menu = DataFlowGraphicsScene::createSceneMenu(scenePos);

	// QtNodes builds the menu as a tree of registry categories. Drop the sinks' category whole: they
	// are still registered (the graph has to be able to create one by name, and to load one from a
	// saved graph) but they are not nodes you add.
	if (auto* tree = menu->findChild<QTreeWidget*>())
	{
		const QList<QTreeWidgetItem*> sinks =
			tree->findItems(QLatin1String(c_OutputCategory), Qt::MatchExactly);

		for (QTreeWidgetItem* item : sinks)
			delete tree->takeTopLevelItem(tree->indexOfTopLevelItem(item));
	}

	return menu;
}
