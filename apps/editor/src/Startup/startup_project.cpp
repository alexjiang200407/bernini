#include "Startup/startup_project.h"

#include "Plugins/plugin_loader.h"
#include "util/editor_language.h"

#include <QString>

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
		const std::filesystem::path&  argument,
		const std::filesystem::path&  configPath,
		const plugins::PluginSession& plugins)
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
			startup.project.emplace(plugins::OpenProjectWithPlugins(projectFile, plugins));
		}
		catch (const std::exception& e)
		{
			startup.failure = editor::Localize(
				"editor.startup.could_not_open_project",
				{ QString::fromStdWString(projectFile.wstring()), e.what() },
				"Could not open {0}: {1}");
			qWarning("Editor: %s", qPrintable(startup.failure));
		}
		return startup;
	}
}
