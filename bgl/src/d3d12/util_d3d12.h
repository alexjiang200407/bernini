#pragma once
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

	D3D12_RESOURCE_DIMENSION
	ConvertResourceDimension(TextureDimension dimension);

	D3D12_RTV_DIMENSION
	ConvertRTVDimension(TextureDimension dimension);

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
	ConvertClearValue(ClearValue clearValue);
}
