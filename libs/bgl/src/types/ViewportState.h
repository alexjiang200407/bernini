#pragma once
#include "types/Rect.h"
#include "types/Viewport.h"
#include <core/containers/static_vector.h>

namespace bgl
{
	struct ViewportState
	{
		static constexpr uint32_t                     c_MaxViewports = 16;
		core::static_vector<Viewport, c_MaxViewports> viewports;
		core::static_vector<Rect, c_MaxViewports>     scissorRects;

		ViewportState&
		AddViewportAndScissorRect(const Viewport& viewport)
		{
			gassert(
				viewports.size() < c_MaxViewports,
				"Viewports cannot exceeded {}",
				c_MaxViewports);
			viewports.push_back(viewport);
			scissorRects.push_back(Rect(viewport));
			return *this;
		}
	};
}
