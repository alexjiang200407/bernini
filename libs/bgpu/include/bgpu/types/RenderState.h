#pragma once
#include <bgpu/types/BlendState.h>
#include <bgpu/types/DepthStencilState.h>
#include <bgpu/types/RasterState.h>

namespace bgl
{
	struct RenderState
	{
		RasterState       rasterState;
		BlendState        blendState;
		DepthStencilState depthStencilState;

		RenderState&
		SetRasterState(const RasterState& state)
		{
			rasterState = state;
			return *this;
		}

		RenderState&
		SetBlendState(const BlendState& state)
		{
			blendState = state;
			return *this;
		}

		RenderState&
		SetDepthStencilState(const DepthStencilState& state)
		{
			depthStencilState = state;
			return *this;
		}
	};
}
