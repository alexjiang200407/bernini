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
	 * stores, then each of their baked maps.
	 *
	 * The *baked* maps, deliberately -- resolving is a consumer operation, and consumers draw the
	 * shipping RGB9E5, never the float sources. An asset that is referenced but was never baked
	 * therefore throws rather than falling back to its source: the fallback would light the scene
	 * subtly differently from the shipped build.
	 *
	 * @param benvPath The `.benv` file itself.
	 * @param dataRoot What every path stored in the chain is relative to.
	 * @throws std::runtime_error if any referenced file is missing or malformed, or a referenced
	 *         asset has never been baked.
	 */
	[[nodiscard]] ResolvedEnvironment
	resolveEnvironment(
		const std::filesystem::path& benvPath,
		const std::filesystem::path& dataRoot);
}
