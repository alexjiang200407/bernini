#pragma once

#include <bgl/types/BloomSettings.h>
#include <bgl/types/ColorSplitSettings.h>
#include <bgl/types/FilmGrainSettings.h>
#include <bgl/types/PostProcess.h>
#include <optional>

namespace bgl
{
	/** @throws GraphicsError naming the first field outside the range IRenderTarget documents. */
	void
	ValidatePostProcess(const PostProcess& postProcess);

	/** The effects both post-process types have, whichever `postProcess` holds. */
	[[nodiscard]] const std::optional<BloomSettings>&
	BloomOf(const PostProcess& postProcess) noexcept;

	[[nodiscard]] const std::optional<FilmGrainSettings>&
	GrainOf(const PostProcess& postProcess) noexcept;

	[[nodiscard]] const std::optional<ColorSplitSettings>&
	SplitOf(const PostProcess& postProcess) noexcept;
}
