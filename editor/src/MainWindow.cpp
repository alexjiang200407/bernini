#include "MainWindow.h"

#include <QDockWidget>

#include "Windows/ContentExplorer/ContentExplorerWindow.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
	ui.setupUi(this);

	connect(ui.actionExit, &QAction::triggered, this, &QWidget::close);

	auto* contentExplorerDock = new QDockWidget("Content Explorer", this);
	contentExplorerDock->setObjectName("ContentExplorerDock");
	contentExplorerDock->setWidget(new ContentExplorerWindow(contentExplorerDock));
	addDockWidget(Qt::BottomDockWidgetArea, contentExplorerDock);

	ui.menuWindow->addAction(contentExplorerDock->toggleViewAction());
}
