#include "Windows/Plugins/PluginsWindow.h"

#include "Plugins/plugin_loader.h"
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <filesystem>
#include <qcontainerfwd.h>

namespace editor
{
	namespace
	{
		QString
		PathText(const std::filesystem::path& path)
		{
			return QString::fromStdWString(path.wstring());
		}

		QString
		Tooltip(const plugins::LoadedPlugin& plugin)
		{
			QStringList lines{ QString::fromStdString(plugin.id) };
			if (!plugin.directory.empty())
				lines << PathText(plugin.directory);
			if (!plugin.runtimeModule.empty())
				lines << "Runtime: " + PathText(plugin.runtimeModule.filename());
			if (!plugin.editorModule.empty())
				lines << "Editor: " + PathText(plugin.editorModule.filename());
			return lines.join('\n');
		}
	}

	PluginsWindow::PluginsWindow(const plugins::PluginSession& session, QWidget* parent) :
		QWidget(parent, Qt::Window)
	{
		setObjectName("PluginsWindow");
		setWindowTitle("Plugins");
		resize(480, 360);

		auto* list = new QWidget(this);
		auto* rows = new QVBoxLayout(list);
		rows->setSpacing(12);
		for (const plugins::LoadedPlugin& plugin : session.Plugins())
		{
			auto* row = new QWidget(list);
			row->setToolTip(Tooltip(plugin));

			auto* name = new QLabel(QString::fromStdString(plugin.name), row);
			name->setObjectName("PluginName");
			QFont bold = name->font();
			bold.setBold(true);
			name->setFont(bold);

			auto* description = new QLabel(QString::fromStdString(plugin.description), row);
			description->setObjectName("PluginDescription");
			description->setWordWrap(true);
			description->setVisible(!plugin.description.empty());

			auto* text = new QVBoxLayout(row);
			text->setContentsMargins(0, 0, 0, 0);
			text->setSpacing(2);
			text->addWidget(name);
			text->addWidget(description);
			rows->addWidget(row);
		}
		rows->addStretch(1);

		auto* scroll = new QScrollArea(this);
		scroll->setFrameShape(QFrame::NoFrame);
		scroll->setWidgetResizable(true);
		scroll->setWidget(list);

		auto* layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(scroll);
	}
}
