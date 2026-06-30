#pragma once

#include <QWidget>

#include "ui_ContentExplorerWindow.h"

class QFileSystemModel;

class ContentExplorerWindow : public QWidget
{
	Q_OBJECT

public:
	explicit ContentExplorerWindow(QWidget* parent = nullptr);

	/**
	 * Points both views at the given directory and enables the explorer: the tree
	 * shows its sub-folders and the table shows the contents of the selected folder.
	 *
	 * @param path Absolute path to the directory the explorer should be rooted at.
	 */
	void
	SetRootPath(const QString& path);

private:
	void
	AttachModels();

	/** Detaches the models and disables the explorer, leaving both views empty. */
	void
	Clear();

	Ui::ContentExplorerWindow m_Ui;
	QFileSystemModel*         m_DirectoryModel;
	QFileSystemModel*         m_FileModel;
};
