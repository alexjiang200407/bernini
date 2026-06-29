#include "MainWindow.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
	ui.setupUi(this);

	connect(ui.actionExit, &QAction::triggered, this, &QWidget::close);
}
