#pragma once

#include <assetlib/env_import_parameters.h>
#include <cstdint>
#include <optional>
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

	/**
	 * Which part `outputKey` is, read off the names `ImportEnvironment` gives its files -- the only
	 * place those names are chosen: a `.bsky` is the sky, a `.benvl` the lighting. Nullopt for a key
	 * no environment import writes.
	 */
	[[nodiscard]] std::optional<EnvironmentPart>
	environmentPartOf(std::string_view outputKey);

	/**
	 * Whether `outputKey` names one of the float cubes an environment import wrote under
	 * `Derived/SourceTextures/` before the bake read its source directly. A document still claiming
	 * one is read as if it did not (ImportDocument's codec drops them), so nothing re-produces it.
	 */
	[[nodiscard]] bool
	isRetiredEnvironmentOutput(std::string_view outputKey) noexcept;

	/** Whether `outputKey` is a file `part` writes. */
	[[nodiscard]] bool
	isPartOutput(std::string_view outputKey, EnvironmentPart part);

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
