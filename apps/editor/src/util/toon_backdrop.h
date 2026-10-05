#pragma once
#include <bgl/types/BackdropGradient.h>
#include <string_view>

namespace editor
{
	/**
	 * `gradient` with every channel clamped to what bgl takes, warning under `who` for each one that
	 * moved. bgl throws on a negative or non-finite colour, which is right for a caller and wrong for
	 * a hand-edited config.json: a typo there should cost a warning, not the editor.
	 */
	[[nodiscard]] bgl::BackdropGradient
	ClampToonBackdrop(bgl::BackdropGradient gradient, std::string_view who);
}
