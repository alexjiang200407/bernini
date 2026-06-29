#pragma once

#include <QWidget>

#include "ui_ContentExplorerWindow.h"

class QStandardItemModel;

class ContentExplorerWindow : public QWidget
{
	Q_OBJECT

public:
	explicit ContentExplorerWindow(QWidget* parent = nullptr);

private:
	void
	PopulateMockData() noexcept;

	Ui::ContentExplorerWindow ui;
	QStandardItemModel*       m_directoryModel;
	QStandardItemModel*       m_fileModel;
};
