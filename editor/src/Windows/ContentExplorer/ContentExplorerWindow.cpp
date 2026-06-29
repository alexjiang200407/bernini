#include "ContentExplorerWindow.h"

#include <QHeaderView>
#include <QStandardItemModel>

namespace
{
	QStandardItem*
	MakeFolder(const QString& name)
	{
		auto* item = new QStandardItem(name);
		item->setEditable(false);
		return item;
	}

	QList<QStandardItem*>
	MakeFileRow(const QString& name, const QString& type, const QString& size)
	{
		QList<QStandardItem*> row = { new QStandardItem(name),
			                          new QStandardItem(type),
			                          new QStandardItem(size) };
		for (auto* cell : row) cell->setEditable(false);
		return row;
	}
}

ContentExplorerWindow::ContentExplorerWindow(QWidget* parent) : QWidget(parent)
{
	ui.setupUi(this);

	PopulateMockData();
}

void
ContentExplorerWindow::PopulateMockData() noexcept
{
	m_directoryModel = new QStandardItemModel(this);

	auto* content = MakeFolder("Content");

	auto* meshes = MakeFolder("Meshes");
	meshes->appendRow(MakeFolder("Characters"));
	meshes->appendRow(MakeFolder("Environment"));
	content->appendRow(meshes);

	auto* textures = MakeFolder("Textures");
	textures->appendRow(MakeFolder("Albedo"));
	textures->appendRow(MakeFolder("Normal"));
	content->appendRow(textures);

	content->appendRow(MakeFolder("Materials"));
	content->appendRow(MakeFolder("Scenes"));
	content->appendRow(MakeFolder("Audio"));

	m_directoryModel->appendRow(content);

	ui.FileExplorer->setModel(m_directoryModel);
	ui.FileExplorer->setHeaderHidden(true);
	ui.FileExplorer->expandAll();

	m_fileModel = new QStandardItemModel(this);
	m_fileModel->setHorizontalHeaderLabels({ "Name", "Type", "Size" });
	m_fileModel->appendRow(MakeFileRow("hero.mesh", "Mesh", "1.2 MB"));
	m_fileModel->appendRow(MakeFileRow("crate.mesh", "Mesh", "256 KB"));
	m_fileModel->appendRow(MakeFileRow("brick_albedo.png", "Texture", "2.1 MB"));
	m_fileModel->appendRow(MakeFileRow("brick_normal.png", "Texture", "2.0 MB"));
	m_fileModel->appendRow(MakeFileRow("stone.mat", "Material", "4 KB"));
	m_fileModel->appendRow(MakeFileRow("level_01.scene", "Scene", "88 KB"));
	m_fileModel->appendRow(MakeFileRow("footstep.wav", "Audio", "512 KB"));

	ui.CurrentDirectoryExplorer->setModel(m_fileModel);
	ui.CurrentDirectoryExplorer->resizeColumnsToContents();
	ui.CurrentDirectoryExplorer->horizontalHeader()->setStretchLastSection(true);
}
