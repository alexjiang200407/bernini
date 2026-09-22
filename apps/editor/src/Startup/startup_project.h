#pragma once

#include <QString>

#include "Plugins/plugin_loader.h"
#include <assetlib/Project.h>

#include <filesystem>
#include <optional>

namespace editor
{
	/** The project the editor opens on launch, or why the one it was told to open did not. */
	struct StartupProject
	{
		std::optional<assetlib::Project> project;

		// Empty when the project opened, or when nothing named one.
		QString failure;
	};

	/**
	 * Opens the project `argument` names against `plugins`' kinds, refusing one that lists a plugin
	 * the session did not load. Otherwise it opens the project `argument` names (`--project`), or failing that the config's
	 * `startupProject`. Without one the editor shows the landing page, and no renderer is built.
	 *
	 * @throws std::runtime_error if the config at `configPath` cannot be read.
	 */
	[[nodiscard]] StartupProject
	OpenStartupProject(
		const std::filesystem::path&  argument,
		const std::filesystem::path&  configPath,
		const plugins::PluginSession& plugins);
}
