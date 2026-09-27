#pragma once

#include <cstdint>
#include <filesystem>

namespace assetlib
{
	struct BEnvLighting;
	struct BSky;
	struct ImageData;
	struct SourceStamp;

	/**
	 * Encodes `chain` into `sky`'s content-addressed baked map and records the route's `stamp`. The
	 * name covers the route's source, `stamp`, `parametersHash`, the encoding and
	 * `c_EnvSourceBakeToken`, so a map already on disk under it is this bake's and is not rewritten.
	 *
	 * @pre `sky.sky.source` is set: it is part of the name.
	 * @throws std::runtime_error if the map cannot be written; `sky` is untouched then.
	 */
	void
	bakeSkyFrom(
		BSky&                        sky,
		const ImageData&             chain,
		const SourceStamp&           stamp,
		uint64_t                     parametersHash,
		const std::filesystem::path& dataRoot);

	/**
	 * bakeSkyFrom for the lighting's two maps, and the exposure derived from `irradiance`.
	 *
	 * @pre Both routes' sources are set.
	 */
	void
	bakeEnvLightingFrom(
		BEnvLighting&                lighting,
		const ImageData&             prefilter,
		const ImageData&             irradiance,
		const SourceStamp&           stamp,
		uint64_t                     parametersHash,
		const std::filesystem::path& dataRoot);
}
