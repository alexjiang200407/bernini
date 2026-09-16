#pragma once

#include <assetlib/env_import_parameters.h>
#include <cstdint>

namespace assetlib
{
	/**
	 * The cube the lighting convolves: twice the prefilter's face size, so the lighting's pixels
	 * are a function of its own parameters and never of the sky's.
	 */
	[[nodiscard]] uint32_t
	lightingProjectionSize(const EnvironmentImportParameters& parameters) noexcept;
}
