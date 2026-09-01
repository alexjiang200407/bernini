#pragma once
#include "types/BlendState.h"
#include "types/DepthStencilState.h"
#include "types/RasterState.h"

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
