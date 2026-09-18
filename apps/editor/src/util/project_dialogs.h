#pragma once

#include <filesystem>
#include <optional>
#include <string>

class QWidget;

namespace editor
{
	/** A project the user asked to create: where its `.bproj` goes and what it is called. */
	struct NewProjectRequest
	{
		std::filesystem::path projectFile;
		std::string           name;
	};

	/** Asks for a new project's name and location. Empty when the user cancels either question. */
	[[nodiscard]] std::optional<NewProjectRequest>
	AskForNewProject(QWidget* parent);

	/** Asks for an existing `.bproj`. Empty when the user cancels. */
	[[nodiscard]] std::filesystem::path
	AskForProjectToOpen(QWidget* parent);
}
