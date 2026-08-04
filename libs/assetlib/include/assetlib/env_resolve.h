#pragma once
#include <assetlib/benv_io.h>

namespace assetlib
{
	/**
	 * A `.benv` followed to its pixels: the decoded maps of the sky and lighting it references,
	 * plus the sky's presentation. What a renderer consumes, where BEnv is what a project stores.
	 */
	struct ResolvedEnvironment
	{
		// Pieces the .benv does not reference stay empty; loading half an environment is the
		// caller's decision to make, not an error.
		EnvironmentMaps maps;

		uint32_t skyMipLevel  = 0;
		float    skyRotationY = 0.0f;

		// Move-only, following EnvironmentMaps: the maps are megabytes each.
		ResolvedEnvironment()                               = default;
		ResolvedEnvironment(ResolvedEnvironment&&) noexcept = default;
		ResolvedEnvironment(const ResolvedEnvironment&)     = delete;
		ResolvedEnvironment&
		operator=(ResolvedEnvironment&&) noexcept = default;
		ResolvedEnvironment&
		operator=(const ResolvedEnvironment&) = delete;
	};

	/**
	 * Loads a v2 `.benv` and every asset it references: the `.bsky` and `.benvl` by the paths it
	 * stores, then the map each of their routes draws.
	 *
	 * Which map that is, is `envMapToDraw`: the baked RGB9E5 while it is there and current, the
	 * float source it was compiled from while it is not. A project keeps its baked maps out of
	 * source control -- they are regenerated per platform -- so a fresh checkout has the sources and
	 * not the bakes, and would otherwise resolve nothing at all.
	 *
	 * @param benvPath The `.benv` file itself.
	 * @param dataRoot What every path stored in the chain is relative to.
	 * @throws std::runtime_error if any referenced file is missing or malformed, or a referenced
	 *         route has neither a baked map nor a source on disk.
	 */
	[[nodiscard]] ResolvedEnvironment
	resolveEnvironment(
		const std::filesystem::path& benvPath,
		const std::filesystem::path& dataRoot);
}
