#pragma once
#include "metal_cpp.h"

#include "types/Format.h"

namespace bgl
{
	// Maps an engine Format to its Metal pixel format. gfatals on formats with no Metal equivalent
	// (3-channel RGB32; BC block formats, which Apple-silicon GPUs do not support).
	[[nodiscard]] MTL::PixelFormat
	ConvertFormat(Format format) noexcept;
}
