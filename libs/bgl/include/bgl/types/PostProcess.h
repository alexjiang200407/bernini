#pragma once
#include <bgl/types/FilmicPostProcess.h>
#include <bgl/types/ToonPostProcess.h>
#include <variant>

namespace bgl
{
	/**
	 * Everything between a target's resolved scene and its display, as one value: which curve, and
	 * the effects that curve has. Set whole with IRenderTarget::SetPostProcess.
	 */
	using PostProcess = std::variant<FilmicPostProcess, ToonPostProcess>;
}
