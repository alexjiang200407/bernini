#pragma once
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"

namespace bgl
{
	class PreparePresentPass
	{
	public:
		void
		AttachToFrameGraph(FrameGraph& fg, std::string presentableName)
		{
			fg.AddPass(
				PassDesc{}
					.SetName("PreparePresent")
					.AddTextureArg(
						TextureArg{ std::move(presentableName),
			                        BarrierSyncFlag::kNone,
			                        BarrierAccessFlag::kNone,
			                        BarrierLayout::kPresent })
					.SetSideEffect());
		}
	};
}
