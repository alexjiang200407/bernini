#include "Windows/MaterialEditor/MaterialGraphScene.h"
#include <qmenu.h>
#include <qpoint.h>

QMenu*
MaterialGraphScene::createSceneMenu(QPointF)
{
	// Nothing in this graph is added from a menu. A sink is switched rather than added, and a texture
	// arrives by being dragged in from the Content Explorer -- which is what carries the file it
	// samples, so a texture node picked off a menu would have nothing to point at. GraphicsView
	// treats a null menu as "no menu".
	return nullptr;
}
