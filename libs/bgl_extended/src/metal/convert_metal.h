#pragma once
#include "metal_cpp.h"

#include "resource/Sampler.h"
#include "types/BlendState.h"
#include "types/Color.h"
#include "types/DepthStencilState.h"
#include "types/Format.h"
#include "types/RasterState.h"
#include "types/TextureDimension.h"
#include <string>

namespace bgl
{
	// An autoreleased NS::String over `s`, valid until the enclosing pool drains.
	[[nodiscard]] NS::String*
	ConvertString(const std::string& s) noexcept;

	// Maps an engine Format to its Metal pixel format. gfatals on formats with no Metal equivalent:
	// 3-channel RGB32, and BGRA4, whose Metal counterpart orders its components differently.
	[[nodiscard]] MTL::PixelFormat
	ConvertFormat(Format format) noexcept;

	[[nodiscard]] MTL::TextureType
	ConvertTextureType(TextureDimension dimension) noexcept;

	// True for a format carrying a stencil plane, which Metal binds as its own attachment even
	// though one texture holds both.
	/**
	 * The depth increment `RasterState::depthBias` counts in, for `format`.
	 *
	 * D3D12 states the bias in units of the depth format's minimum resolvable difference and scales
	 * it in hardware; Metal's setDepthBias adds its argument to the NDC depth as-is. Multiplying by
	 * this makes the two agree. Zero for a format with no depth, where a bias means nothing.
	 *
	 * Exact for the UNORM formats. D3D12 derives a float format's unit per primitive, from the
	 * exponent of its largest z, so no constant can reproduce it -- this returns the unit at
	 * z near 1, which is where a reversed-Z depth buffer keeps its precision.
	 */
	[[nodiscard]] float
	DepthBiasUnit(Format format) noexcept;

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

	[[nodiscard]] MTL::CompareFunction
	ConvertReduction(SamplerReductionType reduction) noexcept;

	[[nodiscard]] MTL::SamplerAddressMode
	ConvertAddressMode(SamplerAddressMode mode) noexcept;

	[[nodiscard]] MTL::SamplerBorderColor
	ConvertBorderColor(const Color& color) noexcept;
}
