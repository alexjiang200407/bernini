#include "Startup/ProjectLauncher.h"

#include "Plugins/plugin_loader.h"
#include "util/editor_language.h"

#include "util/project_dialogs.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

#include <assetlib/Project.h>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
#include <qfont.h>
#include <qnamespace.h>
#include <utility>
#include <vector>

namespace editor
{
	ProjectLauncher::ProjectLauncher(
		std::vector<std::filesystem::path> recent,
		const plugins::PluginSession&      plugins,
		const QString&                     notice,
		QWidget* parent) : QDialog(parent), m_Recent(std::move(recent)), m_Plugins(&plugins)
	{
		setWindowTitle(editor::Localize("editor.main.title", "Bernini Editor"));
		setMinimumSize(560, 420);

		auto* layout = new QVBoxLayout(this);
		layout->setContentsMargins(24, 20, 24, 20);
		layout->setSpacing(12);

		auto* title   = new QLabel(editor::Localize("editor.main.title", "Bernini Editor"), this);
		QFont heading = title->font();
		heading.setPointSize(heading.pointSize() + 6);
		heading.setBold(true);
		title->setFont(heading);
		layout->addWidget(title);

		if (!notice.isEmpty())
		{
			auto* warning = new QLabel(notice, this);
			warning->setObjectName("LauncherNotice");
			warning->setTextFormat(Qt::PlainText);
			warning->setWordWrap(true);
			layout->addWidget(warning);
		}

		layout->addWidget(new QLabel(
			editor::Localize("editor.startup.recent_projects", "Recent Projects"),
			this));

		m_RecentList = new QListWidget(this);
		m_RecentList->setObjectName("RecentProjects");
		for (const std::filesystem::path& project : m_Recent)
		{
			const QString path = QString::fromStdWString(project.wstring());
			auto*         item = new QListWidgetItem(
				editor::Localize(
					"editor.startup.recent_project_item",
					{ QString::fromStdWString(project.stem().wstring()), path },
					"{0}\n{1}"),
				m_RecentList);
			item->setToolTip(path);
		}
		layout->addWidget(m_RecentList, 1);

		if (m_Recent.empty())
		{
			m_RecentList->hide();
			auto* none = new QLabel(
				editor::Localize("editor.startup.no_recent_projects", "No recent projects."),
				this);
			none->setAlignment(Qt::AlignCenter);
			none->setEnabled(false);
			layout->addWidget(none, 1);
		}

		auto* buttons = new QHBoxLayout();
		auto* create  = new QPushButton(
			editor::Localize("editor.startup.new_project_button", "New Project..."),
			this);
		auto* browse = new QPushButton(
			editor::Localize("editor.startup.open_project_button", "Open Project..."),
			this);
		auto* open = new QPushButton(editor::Localize("editor.startup.open_button", "Open"), this);
		open->setEnabled(false);
		open->setDefault(true);
		buttons->addWidget(create);
		buttons->addWidget(browse);
		buttons->addStretch(1);
		buttons->addWidget(open);
		layout->addLayout(buttons);

		connect(create, &QPushButton::clicked, this, &ProjectLauncher::NewProject);
		connect(browse, &QPushButton::clicked, this, &ProjectLauncher::OpenProject);
		connect(m_RecentList, &QListWidget::currentRowChanged, open, [open](int row) {
			open->setEnabled(row >= 0);
		});
		connect(open, &QPushButton::clicked, this, [this] {
			if (const int row = m_RecentList->currentRow(); row >= 0)
				OpenAt(m_Recent[static_cast<std::size_t>(row)]);
		});
		connect(m_RecentList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
			OpenAt(m_Recent[static_cast<std::size_t>(m_RecentList->row(item))]);
		});

		if (!m_Recent.empty())
			m_RecentList->setCurrentRow(0);
	}

	assetlib::Project
	ProjectLauncher::TakeProject()
	{
		assetlib::Project project = std::move(m_Project.value());
		m_Project.reset();
		return project;
	}

	void
	ProjectLauncher::NewProject()
	{
		const std::optional<NewProjectRequest> request = AskForNewProject(this);
		if (!request)
			return;

		try
		{
			m_Project.emplace(assetlib::Project::Create(request->projectFile, request->name));
			accept();
		}
		catch (const std::exception& e)
		{
			QMessageBox::warning(
				this,
				editor::Localize("editor.startup.new_project_title", "New Project"),
				e.what());
		}
	}

	void
	ProjectLauncher::OpenProject()
	{
		const std::filesystem::path projectFile = AskForProjectToOpen(this);
		if (!projectFile.empty())
			OpenAt(projectFile);
	}

	void
	ProjectLauncher::OpenAt(const std::filesystem::path& projectFile)
	{
		try
		{
			m_Project.emplace(plugins::OpenProjectWithPlugins(projectFile, *m_Plugins));
			accept();
		}
		catch (const std::exception& e)
		{
			QMessageBox::warning(
				this,
				editor::Localize("editor.startup.open_project_title", "Open Project"),
				e.what());
		}
	}
}
