#pragma once
#include <bgpu/types/Rect.h>
#include <bgpu/types/Viewport.h>
#include <core/containers/static_vector.h>
#include <core/err/util.h>
#include <cstdint>
#include <utility>

namespace bgpu
{
	struct ViewportState
	{
		static constexpr uint32_t                     c_MaxViewports = 16;
		core::static_vector<Viewport, c_MaxViewports> viewports;
		core::static_vector<Rect, c_MaxViewports>     scissorRects;

		template <typename Self>
		Self&&
		AddViewportAndScissorRect(this Self&& self, const Viewport& viewport)
		{
			core::ensure(
				self.viewports.size() < c_MaxViewports,
				"Viewports cannot exceeded {}",
				c_MaxViewports);
			self.viewports.push_back(viewport);
			self.scissorRects.push_back(Rect(viewport));
			return std::forward<Self>(self);
		}
	};
}
