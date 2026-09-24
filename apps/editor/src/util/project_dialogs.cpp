#include "util/project_dialogs.h"

#include "util/editor_language.h"
#include <QFileDialog>
#include <QInputDialog>
#include <QString>

#include <assetlib/Project.h>

#include <filesystem>
#include <optional>

namespace editor
{
	std::optional<NewProjectRequest>
	AskForNewProject(QWidget* parent)
	{
		const QString name = QInputDialog::getText(
								 parent,
								 Localize("editor.util.new_project_title", "New Project"),
								 Localize("editor.util.project_name_label", "Project name:"))
		                         .trimmed();
		if (name.isEmpty())
			return std::nullopt;

		const QString location = QFileDialog::getExistingDirectory(
			parent,
			Localize("editor.util.select_project_location", "Select Project Location"));
		if (location.isEmpty())
			return std::nullopt;

		const auto root = std::filesystem::path(location.toStdWString()) / name.toStdWString();
		return NewProjectRequest{
			.projectFile =
				root / (name.toStdWString() +
			            QString::fromUtf8(assetlib::Project::c_FileExtension).toStdWString()),
			.name = name.toStdString(),
		};
	}

	std::filesystem::path
	AskForProjectToOpen(QWidget* parent)
	{
		const QString filter = Localize(
			"editor.util.project_file_filter",
			{ QString::fromUtf8(assetlib::Project::c_FileExtension) },
			"Bernini Project (*{0})");
		const QString file = QFileDialog::getOpenFileName(
			parent,
			Localize("editor.util.open_project_title", "Open Project"),
			QString(),
			filter);
		if (file.isEmpty())
			return {};

		return std::filesystem::path(file.toStdWString());
	}
}
