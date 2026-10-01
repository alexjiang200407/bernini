#pragma once
#include <bgpu/types/BlendState.h>
#include <bgpu/types/DepthStencilState.h>
#include <bgpu/types/RasterState.h>
#include <utility>

namespace bgpu
{
	struct RenderState
	{
		RasterState       rasterState;
		BlendState        blendState;
		DepthStencilState depthStencilState;

		template <typename Self>
		Self&&
		SetRasterState(this Self&& self, const RasterState& state)
		{
			self.rasterState = state;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetBlendState(this Self&& self, const BlendState& state)
		{
			self.blendState = state;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDepthStencilState(this Self&& self, const DepthStencilState& state)
		{
			self.depthStencilState = state;
			return std::forward<Self>(self);
		}
	};
}
