#pragma once
#include "types/Rect.h"
#include "types/Viewport.h"
#include <core/containers/static_vector.h>

namespace bgl
{
	struct ViewportState
	{
		static constexpr uint32_t                   MaxViewports = 16;
		core::static_vector<Viewport, MaxViewports> viewports;
		core::static_vector<Rect, MaxViewports>     scissorRects;

		ViewportState&
		AddViewportAndScissorRect(const Viewport& viewport)
		{
			gassert(viewports.size() < MaxViewports, "Viewports cannot exceeded {}", MaxViewports);
			viewports.push_back(viewport);
			scissorRects.push_back(Rect(viewport));
			return *this;
		}
	};
}
