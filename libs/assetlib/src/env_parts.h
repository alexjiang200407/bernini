#pragma once

#include <assetlib/env_import_parameters.h>
#include <cstdint>
#include <string_view>

namespace assetlib
{
	/**
	 * The two halves of an environment import, which are produced, kept and re-cooked apart: a sky
	 * is re-authored in seconds and the lighting convolved beside it takes minutes.
	 */
	enum class EnvironmentPart
	{
		kSky,
		kLighting
	};

	/** The float cubes an import writes under `sourceDir`, one suffix apiece. */
	inline constexpr std::string_view c_SkySourceSuffix        = "_sky.ktx2";
	inline constexpr std::string_view c_PrefilterSourceSuffix  = "_prefilter.ktx2";
	inline constexpr std::string_view c_IrradianceSourceSuffix = "_irradiance.ktx2";

	/**
	 * Which part an import's output belongs to: the `.bsky` and its float cube are the sky, the
	 * `.benvl` and its two are the lighting. Read off the names `ImportEnvironment` gives them,
	 * which is the only place those names are chosen.
	 */
	[[nodiscard]] EnvironmentPart
	environmentPartOf(std::string_view outputKey);

	/**
	 * The hash of the parameters one part's pixels depend on, and only those: the sky's face size
	 * and chain for the sky, the four convolution sizes for the lighting. What an import document
	 * records a part was written with, so an edit to one part's parameters stales that part alone.
	 */
	[[nodiscard]] uint64_t
	partParametersHashOf(const EnvironmentImportParameters& parameters, EnvironmentPart part);

	/**
	 * The cube the lighting convolves: twice the prefilter's face size, so the lighting's pixels
	 * are a function of its own parameters and never of the sky's.
	 */
	[[nodiscard]] uint32_t
	lightingProjectionSize(const EnvironmentImportParameters& parameters) noexcept;
}
