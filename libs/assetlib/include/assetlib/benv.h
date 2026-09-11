#pragma once
#include <assetlib_structs/BEnv.h>

namespace assetlib
{
	/**
	 * What was authored on the environment, or the bake's derivation until something is.
	 *
	 * Here and not beside `BEnv`: `assetlib_structs` is data, so a question about a container is
	 * answered by the library that holds the answers.
	 */
	[[nodiscard]] inline float
	effectiveExposure(const BEnv& env, const BEnvLighting& lighting) noexcept
	{
		return env.exposureOverride.value_or(lighting.exposure);
	}
}
