#pragma once
#include "types/Format.h"

namespace bgl
{
	/** Maps a bgl Format to its WebGPU texture format. gfatals on an unmapped format. */
	wgpu::TextureFormat
	ToWgpuTextureFormat(Format format) noexcept;
}
