#include "Startup/startup_project.h"

#include <QString>

#include <assetlib/Project.h>
#include <core/platform/util.h>
#include <core/settings/Settings.h>

#include <exception>
#include <filesystem>
#include <qlogging.h>
#include <string>

namespace editor
{
	StartupProject
	OpenStartupProject(
		const std::filesystem::path& argument,
		const std::filesystem::path& configPath)
	{
		std::filesystem::path projectFile = argument;
		if (projectFile.empty())
		{
			const core::Settings settings(configPath);
			projectFile = core::expand_home(settings["startupProject"].GetOrDefault(std::string()));
		}

		auto startup = StartupProject();
		if (projectFile.empty())
			return startup;

		try
		{
			startup.project.emplace(assetlib::Project::Open(projectFile));
		}
		catch (const std::exception& e)
		{
			startup.failure = QString("Could not open %1: %2")
			                      .arg(QString::fromStdWString(projectFile.wstring()), e.what());
			qWarning("Editor: %s", qPrintable(startup.failure));
		}
		return startup;
	}
}
