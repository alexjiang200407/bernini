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
	std::optional<EnvironmentOutput>
	environmentOutputOf(std::string_view outputKey)
	{
		const std::string extension = extensionOf(outputKey);
		if (extension == c_SkyExtension)
			return EnvironmentOutput::kSky;
		if (extension == c_EnvLightingExtension)
			return EnvironmentOutput::kLighting;
		if (outputKey.ends_with(c_SkySourceSuffix))
			return EnvironmentOutput::kSkySource;
		if (outputKey.ends_with(c_PrefilterSourceSuffix))
			return EnvironmentOutput::kPrefilterSource;
		if (outputKey.ends_with(c_IrradianceSourceSuffix))
			return EnvironmentOutput::kIrradianceSource;

		return std::nullopt;
	}

	EnvironmentPart
	partOf(EnvironmentOutput output) noexcept
	{
		switch (output)
		{
		case EnvironmentOutput::kSkySource:
		case EnvironmentOutput::kSky:
			return EnvironmentPart::kSky;
		case EnvironmentOutput::kPrefilterSource:
		case EnvironmentOutput::kIrradianceSource:
		case EnvironmentOutput::kLighting:
			return EnvironmentPart::kLighting;
		}
		return EnvironmentPart::kLighting;
	}

	std::string_view
	outputStemSuffix(EnvironmentOutput output) noexcept
	{
		const auto stemPart = [](std::string_view suffix) {
			return suffix.substr(0, suffix.size() - c_TextureExtension.size());
		};

		switch (output)
		{
		case EnvironmentOutput::kSkySource:
			return stemPart(c_SkySourceSuffix);
		case EnvironmentOutput::kPrefilterSource:
			return stemPart(c_PrefilterSourceSuffix);
		case EnvironmentOutput::kIrradianceSource:
			return stemPart(c_IrradianceSourceSuffix);
		case EnvironmentOutput::kSky:
		case EnvironmentOutput::kLighting:
			return {};
		}
		return {};
	}

	bool
	isPartOutput(std::string_view outputKey, EnvironmentPart part)
	{
		const std::optional<EnvironmentOutput> output = environmentOutputOf(outputKey);
		return output && partOf(*output) == part;
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
