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

	/** What became of an import, so a multi-file drop knows whether to carry on with the next one. */
	enum class ImportOutcome
	{
		kImported,
		kCancelled,  // by the user, at the overwrite prompt or on the loading screen
		kFailed,     // already reported to the user
	};

	/**
	 * Converts a dropped glTF/glb into the engine .bmesh format written to `targetDir`.
	 *
	 * `textureSubdir` names a folder beneath `AssetImporterDialog::c_TextureRoot` (under the Data
	 * root) to extract the mesh's textures into; empty skips texture extraction. Each import needs
	 * its own folder because the extracted files are named by index, not by source name.
	 *
	 * Runs on a worker thread behind a cancellable loading screen: parsing and, above all,
	 * supercompressing the textures take long enough to freeze the editor. Nothing here touches bgl.
	 *
	 * Asks before overwriting anything, reports a failure to the user, and on either a failure or a
	 * cancel removes the half-written files it had produced -- see RollBack.
	 */
	[[nodiscard]] ImportOutcome
	ImportMesh(const QString& sourceFile, const QString& targetDir, const QString& textureSubdir);

	/**
	 * Deletes what an import got as far as writing, so a cancelled or failed one leaves nothing behind:
	 * a `.bmesh` naming textures that were never extracted, or a half-supercompressed texture folder.
	 *
	 * Only what the import itself created, and never anything that was already there -- a texture folder
	 * that predates the import is left alone, files and all, because the user was asked before it was
	 * written into and its other contents are not ours to delete.
	 */
	static void
	RollBack(
		const std::filesystem::path& bmeshPath,
		bool                         bmeshExisted,
		const std::filesystem::path& textureDir,
		bool                         textureDirExisted);

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
