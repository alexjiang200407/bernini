#pragma once

#include <cstddef>
#include <filesystem>

namespace editor
{
	/** Where the project at `projectFile` keeps the surfaces the renderer registers from. */
	[[nodiscard]] std::filesystem::path
	ShadersDirectoryOf(const std::filesystem::path& projectFile);

	/**
	 * Whether a project whose shaders are in `projectShaders` has to be opened in a new editor
	 * process, given this session registered `registeredSurfaces` surfaces from `registeredShaders`.
	 *
	 * Surfaces are registered once, as the renderer is built, so a different directory is only
	 * harmless when neither side could hold one. The new side is judged by the presence of a
	 * `.slang` file rather than by reflection, which needs the Slang session the renderer released.
	 */
	[[nodiscard]] bool
	OpeningNeedsRelaunch(
		const std::filesystem::path& registeredShaders,
		std::size_t                  registeredSurfaces,
		const std::filesystem::path& projectShaders);
}
