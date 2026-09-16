#include "env_parts.h"

#include <assetlib/env_import_parameters.h>
#include <cstdint>

namespace assetlib
{
	uint32_t
	lightingProjectionSize(const EnvironmentImportParameters& parameters) noexcept
	{
		return parameters.prefilterFaceSize * 2;
	}
}
