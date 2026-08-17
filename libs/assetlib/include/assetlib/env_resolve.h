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

}
