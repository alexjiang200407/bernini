#pragma once

#include <QMainWindow>

#include "ui_AssetImportWindow.h"

class AssetImportWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit AssetImportWindow(QWidget* parent = nullptr);

private:
	Ui::AssetImportWindow ui;
};
