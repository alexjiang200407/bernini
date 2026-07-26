#pragma once
#include "types/BlendState.h"
#include "types/DepthStencilState.h"
#include "types/Format.h"
#include "types/RasterState.h"

namespace bgl
{
	/** Maps a bgl Format to its WebGPU texture format. gfatals on an unmapped format. */
	wgpu::TextureFormat
	ToWgpuTextureFormat(Format format) noexcept;

	/** Maps a bgl cull mode to its WebGPU equivalent. */
	wgpu::CullMode
	ToWgpuCullMode(RasterCullMode mode) noexcept;

	/** WebGPU names the front face by winding; bgl carries the same choice as a flag. */
	wgpu::FrontFace
	ToWgpuFrontFace(bool frontCounterClockwise) noexcept;

	/** Maps a bgl comparison function to its WebGPU equivalent. */
	wgpu::CompareFunction
	ToWgpuCompareFunction(ComparisonFunc func) noexcept;

	/** Maps a bgl stencil op to its WebGPU equivalent. */
	wgpu::StencilOperation
	ToWgpuStencilOperation(StencilOp op) noexcept;

	/** Maps a bgl blend factor to its WebGPU equivalent. */
	wgpu::BlendFactor
	ToWgpuBlendFactor(BlendFactor factor) noexcept;

	/** Maps a bgl blend op to its WebGPU equivalent. */
	wgpu::BlendOperation
	ToWgpuBlendOperation(BlendOp op) noexcept;

	/** Maps a bgl colour-write mask (a bitfield) to the WebGPU flags. */
	wgpu::ColorWriteMask
	ToWgpuColorWriteMask(ColorMask mask) noexcept;
}
