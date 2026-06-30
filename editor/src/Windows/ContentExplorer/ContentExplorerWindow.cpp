#include "ContentExplorerWindow.h"

#include <QDir>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QItemSelectionModel>

ContentExplorerWindow::ContentExplorerWindow(QWidget* parent) : QWidget(parent)
{
	m_Ui.setupUi(this);

	m_DirectoryModel = new QFileSystemModel(this);
	m_DirectoryModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

	m_FileModel = new QFileSystemModel(this);
	m_FileModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);

	// No project is open yet: the explorer stays disabled and empty until rooted.
	Clear();
}

void
ContentExplorerWindow::SetRootPath(const QString& path)
{
	AttachModels();
	setEnabled(true);

	m_Ui.FileExplorer->setRootIndex(m_DirectoryModel->setRootPath(path));
	m_Ui.CurrentDirectoryExplorer->setRootIndex(m_FileModel->setRootPath(path));
}

void
ContentExplorerWindow::AttachModels()
{
	if (m_Ui.FileExplorer->model() == m_DirectoryModel)
		return;

	m_Ui.FileExplorer->setModel(m_DirectoryModel);
	m_Ui.FileExplorer->setHeaderHidden(true);
	for (auto column = 1; column < m_DirectoryModel->columnCount(); ++column)
		m_Ui.FileExplorer->hideColumn(column);

	m_Ui.CurrentDirectoryExplorer->setModel(m_FileModel);
	m_Ui.CurrentDirectoryExplorer->verticalHeader()->setVisible(false);
	m_Ui.CurrentDirectoryExplorer->horizontalHeader()->setStretchLastSection(true);

	// Selecting a folder on the left shows that folder's contents on the right.
	connect(
		m_Ui.FileExplorer->selectionModel(),
		&QItemSelectionModel::currentChanged,
		this,
		[this](const QModelIndex& current, const QModelIndex&) {
			const auto path = m_DirectoryModel->filePath(current);
			m_Ui.CurrentDirectoryExplorer->setRootIndex(m_FileModel->setRootPath(path));
		});
}

void
ContentExplorerWindow::Clear()
{
	m_Ui.FileExplorer->setModel(nullptr);
	m_Ui.CurrentDirectoryExplorer->setModel(nullptr);
	setEnabled(false);
}
