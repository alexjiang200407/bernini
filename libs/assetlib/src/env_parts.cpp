#include "env_parts.h"

#include <array>
#include <assetlib/codecs.h>
#include <assetlib/env_import_parameters.h>
#include <core/hash.h>
#include <cstdint>
#include <string>
#include <string_view>

#include "ref_paths.h"

namespace assetlib
{
	EnvironmentPart
	environmentPartOf(std::string_view outputKey)
	{
		const std::string extension = extensionOf(outputKey);
		if (extension == c_SkyExtension || outputKey.ends_with(c_SkySourceSuffix))
			return EnvironmentPart::kSky;

		return EnvironmentPart::kLighting;
	}

	uint64_t
	partParametersHashOf(const EnvironmentImportParameters& parameters, EnvironmentPart part)
	{
		// The part is hashed with its fields, so the two can never collide on equal numbers.
		const std::array<uint32_t, 5> fields =
			part == EnvironmentPart::kSky ?
				std::array<uint32_t, 5>{ { 0, parameters.skyFaceSize, parameters.skyMips, 0, 0 } } :
				std::array<uint32_t, 5>{ { 1,
			                               parameters.prefilterFaceSize,
			                               parameters.prefilterMips,
			                               parameters.prefilterSamples,
			                               parameters.irradianceFaceSize } };
		return core::hash_bytes(fields.data(), sizeof(fields), core::hash_seed());
	}

	uint32_t
	lightingProjectionSize(const EnvironmentImportParameters& parameters) noexcept
	{
		return parameters.prefilterFaceSize * 2;
	}
}
