#include "Windows/Plugins/PluginsWindow.h"

#include "Plugins/plugin_loader.h"
#include "util/editor_language.h"
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QPalette>
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
				lines << Localize(
					"editor.plugins_window.runtime_module_line",
					{ PathText(plugin.runtimeModule.filename()) },
					"Runtime: {0}");
			if (!plugin.editorModule.empty())
				lines << Localize(
					"editor.plugins_window.editor_module_line",
					{ PathText(plugin.editorModule.filename()) },
					"Editor: {0}");
			return lines.join('\n');
		}
	}

	PluginsWindow::PluginsWindow(const plugins::PluginSession& session, QWidget* parent) :
		QWidget(parent, Qt::Window)
	{
		setObjectName("PluginsWindow");
		setWindowTitle(Localize("editor.plugins_window.window_title", "Plugins"));
		resize(480, 360);

		auto* list = new QWidget(this);
		auto* rows = new QVBoxLayout(list);
		rows->setContentsMargins(12, 12, 12, 12);
		rows->setSpacing(8);
		for (const plugins::LoadedPlugin& plugin : session.Plugins())
		{
			// One box per plugin, on the base colour so it stands off the window like a list entry.
			auto* row = new QFrame(list);
			row->setObjectName("PluginBox");
			row->setFrameShape(QFrame::StyledPanel);
			row->setFrameShadow(QFrame::Raised);
			row->setAutoFillBackground(true);
			row->setBackgroundRole(QPalette::Base);
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
			text->setContentsMargins(10, 8, 10, 8);
			text->setSpacing(2);
			text->addWidget(name);
			text->addWidget(description);
			rows->addWidget(row);
		}
		rows->addStretch(1);

		// Scrolls once the boxes outgrow the window; the stretch keeps a short list at the top.
		auto* scroll = new QScrollArea(this);
		scroll->setObjectName("PluginsScroll");
		scroll->setFrameShape(QFrame::NoFrame);
		scroll->setWidgetResizable(true);
		scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		scroll->setWidget(list);

		auto* layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(scroll);
	}
}
