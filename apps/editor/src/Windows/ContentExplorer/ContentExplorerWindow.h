#pragma once

#include <QWidget>

#include "ui_ContentExplorerWindow.h"

class QFileSystemModel;
class QLabel;
class QModelIndex;
class QPoint;

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

protected:
	// Keeps the empty-directory placeholder sized to the file table's viewport.
	bool
	eventFilter(QObject* watched, QEvent* event) override;

	void
	dragEnterEvent(QDragEnterEvent* event) override;
	void
	dragMoveEvent(QDragMoveEvent* event) override;
	void
	dropEvent(QDropEvent* event) override;

private:
	void
	AttachModels();

	QString
	ResolveDropDirectory(const QPoint& windowPos) const;

	/**
	 * Converts a dropped glTF/glb into the engine .bmesh format written to `targetDir`.
	 * the mesh's textures are written to the project's single `textures_src` folder
	 * (under the Data root).
	 */
	void
	ImportMesh(const QString& sourceFile, const QString& targetDir, bool importTextures);

	/** Detaches the models and disables the explorer, leaving both views empty. */
	void
	Clear();

	void
	ShowHierarchyMenu(const QPoint& pos);

	void
	ShowFileMenu(const QPoint& pos);

	void
	AddDirectory(QFileSystemModel* model, const QModelIndex& parent);

	void
	UpdateEmptyPlaceholder();

	Ui::ContentExplorerWindow m_Ui;
	QFileSystemModel*         m_HierarchyModel;
	QFileSystemModel*         m_FileModel;
	QLabel*                   m_EmptyPlaceholder = nullptr;
	QString                   m_RootPath;
};
