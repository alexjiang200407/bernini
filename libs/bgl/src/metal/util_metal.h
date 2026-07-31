#pragma once
#include "metal_cpp.h"

#include "types/BlendState.h"
#include "types/DepthStencilState.h"
#include "types/Format.h"
#include "types/RasterState.h"
#include "types/TextureDimension.h"

namespace bgl
{
	// Maps an engine Format to its Metal pixel format. gfatals on formats with no Metal equivalent:
	// 3-channel RGB32, and BGRA4, whose Metal counterpart orders its components differently.
	[[nodiscard]] MTL::PixelFormat
	ConvertFormat(Format format) noexcept;

	[[nodiscard]] MTL::TextureType
	ConvertTextureType(TextureDimension dimension) noexcept;

	// True for a format carrying a stencil plane, which Metal binds as its own attachment even
	// though one texture holds both.
	[[nodiscard]] bool
	FormatHasStencil(Format format) noexcept;

	[[nodiscard]] MTL::BlendFactor
	ConvertBlendFactor(BlendFactor factor) noexcept;

	[[nodiscard]] MTL::BlendOperation
	ConvertBlendOp(BlendOp op) noexcept;

	[[nodiscard]] MTL::ColorWriteMask
	ConvertColorWriteMask(ColorMask mask) noexcept;

	[[nodiscard]] MTL::CompareFunction
	ConvertComparisonFunc(ComparisonFunc func) noexcept;

	[[nodiscard]] MTL::StencilOperation
	ConvertStencilOp(StencilOp op) noexcept;

	[[nodiscard]] MTL::CullMode
	ConvertCullMode(RasterCullMode mode) noexcept;

	[[nodiscard]] MTL::TriangleFillMode
	ConvertFillMode(RasterFillMode mode) noexcept;
}
