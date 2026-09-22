#include "util/project_dialogs.h"

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
		const QString name =
			QInputDialog::getText(parent, "New Project", "Project name:").trimmed();
		if (name.isEmpty())
			return std::nullopt;

		const QString location =
			QFileDialog::getExistingDirectory(parent, "Select Project Location");
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
		const QString filter = QString("Bernini Project (*%1)")
		                           .arg(QString::fromUtf8(assetlib::Project::c_FileExtension));
		const QString file =
			QFileDialog::getOpenFileName(parent, "Open Project", QString(), filter);
		if (file.isEmpty())
			return {};

		return std::filesystem::path(file.toStdWString());
	}
}
