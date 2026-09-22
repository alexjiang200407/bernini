#include "Windows/Plugins/PluginsWindow.h"

#include "Plugins/plugin_loader.h"
#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <filesystem>

namespace editor
{
	namespace
	{
		QString
		KindLabel(const plugins::ContributionKind kind)
		{
			switch (kind)
			{
			case plugins::ContributionKind::kAssetKind:
				return "Asset kind";
			case plugins::ContributionKind::kMenu:
				return "Menu";
			case plugins::ContributionKind::kPanel:
				return "Panel";
			case plugins::ContributionKind::kAssetEditor:
				return "Asset editor";
			case plugins::ContributionKind::kAction:
				return "Action";
			case plugins::ContributionKind::kImporter:
				return "Importer";
			case plugins::ContributionKind::kThumbnailProvider:
				return "Thumbnail provider";
			}
			return "Contribution";
		}

		QString
		PathText(const std::filesystem::path& path)
		{
			return QString::fromStdWString(path.wstring());
		}

		QTreeWidgetItem*
		AddRow(
			QTreeWidgetItem& parent,
			const QString&   kind,
			const QString&   id,
			const QString&   detail)
		{
			return new QTreeWidgetItem(&parent, QStringList{ kind, id, detail });
		}
	}

	PluginsWindow::PluginsWindow(
		const plugins::PluginSession& session,
		const plugins::BuildIdentity& build,
		QWidget*                      parent) : QWidget(parent, Qt::Window)
	{
		setObjectName("PluginsWindow");
		setWindowTitle("Plugins");
		resize(900, 520);

		auto* header = new QLabel(
			QString("Engine build %1, %2")
				.arg(QString::fromStdString(build.id), QString::fromStdString(build.configuration)),
			this);
		header->setObjectName("PluginsBuild");

		m_Tree = new QTreeWidget(this);
		m_Tree->setObjectName("PluginsTree");
		m_Tree->setHeaderLabels({ "Kind", "ID", "Detail" });
		m_Tree->setRootIsDecorated(true);
		m_Tree->setSelectionMode(QAbstractItemView::NoSelection);

		for (const plugins::LoadedPlugin& plugin : session.Plugins())
		{
			auto* row = new QTreeWidgetItem(
				m_Tree,
				QStringList{ "Plugin",
			                 QString::fromStdString(plugin.id),
			                 plugin.directory.empty() ? QString("Built into the editor") :
			                                            PathText(plugin.directory) });
			if (!plugin.runtimeModule.empty())
				AddRow(*row, "Runtime module", PathText(plugin.runtimeModule.filename()), {});
			if (!plugin.editorModule.empty())
				AddRow(*row, "Editor module", PathText(plugin.editorModule.filename()), {});
			for (const plugins::LoadedContribution& contribution : plugin.contributions)
				AddRow(
					*row,
					KindLabel(contribution.kind),
					QString::fromStdString(contribution.id),
					QString::fromStdString(contribution.detail));
		}

		QTreeWidgetItem* unused = nullptr;
		for (const plugins::ConfiguredPlugin& configured : session.Configured())
		{
			if (configured.loaded)
				continue;
			if (unused == nullptr)
				unused = new QTreeWidgetItem(
					m_Tree,
					QStringList{ "Configured, not required by this project", {}, {} });
			AddRow(
				*unused,
				"Plugin",
				QString::fromStdString(configured.id),
				PathText(configured.directory));
		}

		m_Tree->expandAll();
		m_Tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

		auto* layout = new QVBoxLayout(this);
		layout->addWidget(header);
		layout->addWidget(m_Tree, 1);
	}
}
