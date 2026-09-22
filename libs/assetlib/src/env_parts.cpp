#include "env_parts.h"

#include <array>
#include <assetlib/codecs.h>
#include <assetlib/env_import_parameters.h>
#include <core/hash.h>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "ref_paths.h"

namespace assetlib
{
	std::optional<EnvironmentPart>
	environmentPartOf(std::string_view outputKey)
	{
		const std::string extension = extensionOf(outputKey);
		if (extension == c_SkyExtension)
			return EnvironmentPart::kSky;
		if (extension == c_EnvLightingExtension)
			return EnvironmentPart::kLighting;

		return std::nullopt;
	}

	bool
	isRetiredEnvironmentOutput(std::string_view outputKey) noexcept
	{
		return outputKey.ends_with("_sky.ktx2") || outputKey.ends_with("_prefilter.ktx2") ||
		       outputKey.ends_with("_irradiance.ktx2");
	}

	bool
	isPartOutput(std::string_view outputKey, EnvironmentPart part)
	{
		return environmentPartOf(outputKey) == part;
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
