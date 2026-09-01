#pragma once

#include "resource/Sampler.h"
#include "resource/Srv.h"
#include "resource/Texture.h"
#include "types/Barrier.h"
#include "types/BlendState.h"
#include "types/ClearValue.h"
#include "types/DepthStencilState.h"
#include "types/Format.h"
#include "types/FormatInfo.h"
#include "types/QueueType.h"
#include "types/RasterState.h"
#include "types/TextureDimension.h"

namespace bgl
{
	Format
	ConvertFormat(DXGI_FORMAT dxgiFormat);

	DXGI_FORMAT
	ConvertFormat(Format bglFormat);

	/**
	 * The format to create a texture resource with. Depth formats that will also be sampled must be
	 * created typeless -- D3D12 forbids an SRV over a resource created with a D* format -- and the
	 * DSV/SRV formats then re-type the views. Everything else is ConvertFormat.
	 */
	DXGI_FORMAT
	ConvertResourceFormat(Format bglFormat, TextureUsage usage);

	/** The format an SRV reads a depth texture's depth aspect through; ConvertFormat otherwise. */
	DXGI_FORMAT
	ConvertSrvFormat(Format bglFormat);

	D3D12_RESOURCE_DIMENSION
	ConvertResourceDimension(TextureDimension dimension);

	D3D12_RTV_DIMENSION
	ConvertRTVDimension(TextureDimension dimension);

	D3D12_DSV_DIMENSION
	ConvertDSVDimension(TextureDimension dimension);

	D3D12_SRV_DIMENSION
	ConvertSRVDimension(TextureDimension dimension);

	D3D12_SHADER_RESOURCE_VIEW_DESC
	ConvertSrvDesc(const SrvDesc& desc);

	D3D12_BARRIER_SYNC
	ConvertBarrierSync(BarrierSync sync);

	D3D12_BARRIER_ACCESS
	ConvertBarrierAccess(BarrierAccess access);

	D3D12_BARRIER_LAYOUT
	ConvertBarrierLayout(BarrierLayout layout);

	D3D12_COMMAND_LIST_TYPE
	ConvertQueueType(QueueType queueType);

	D3D12_BLEND
	ConvertBlendValue(BlendFactor value);

	D3D12_BLEND_OP
	ConvertBlendOp(BlendOp value);

	D3D12_BLEND_DESC
	ConvertBlendState(BlendState state);

	D3D12_DEPTH_STENCIL_DESC
	ConvertDepthStencilState(DepthStencilState state);

	D3D12_RASTERIZER_DESC
	ConvertRasterState(RasterState state);

	D3D12_STENCIL_OP
	ConvertStencilOp(StencilOp value);

	D3D12_COMPARISON_FUNC
	ConvertComparisonFunc(ComparisonFunc value);

	D3D12_CLEAR_VALUE
	ConvertClearValue(Format format, ClearValue clearValue);

	D3D12_TEXTURE_ADDRESS_MODE
	ConvertSamplerAddressMode(SamplerAddressMode mode);

	D3D12_SAMPLER_DESC
	ConvertSamplerDesc(const SamplerDesc& desc);
}
