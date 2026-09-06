#include "main_window_ui.h"

#include <QAction>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <qstringliteral.h>

namespace editor
{
	MainWindowWidgets
	BuildMainWindowUi(QMainWindow* parent)
	{
		auto widgets = MainWindowWidgets();

		parent->resize(1280, 720);

		QMenu* file = parent->menuBar()->addMenu(QStringLiteral("File"));

		widgets.newProject = file->addAction(QStringLiteral("New Project..."));
		widgets.newProject->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));

		widgets.openProject = file->addAction(QStringLiteral("Open Project..."));
		widgets.openProject->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));

		file->addSeparator();
		widgets.save = file->addAction(QStringLiteral("Save"));

		file->addSeparator();
		widgets.cleanUnusedTextures = file->addAction(QStringLiteral("Clean Unused Textures..."));
		widgets.cleanUnusedTextures->setToolTip(QStringLiteral(
			"Delete the baked textures that no material in this project references any more"));

		file->addSeparator();
		widgets.exit = file->addAction(QStringLiteral("Exit"));

		widgets.editMenu   = parent->menuBar()->addMenu(QStringLiteral("Edit"));
		widgets.windowMenu = parent->menuBar()->addMenu(QStringLiteral("Window"));

		return widgets;
	}
}
