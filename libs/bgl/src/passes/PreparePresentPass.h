#pragma once
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include <bgpu/types/Barrier.h>
#include <span>
#include <string>
#include <utility>

namespace bgl
{
	/** Every presentable the frame touched back to kPresent: the graph restores no imported state. */
	class PreparePresentPass
	{
	public:
		void
		AttachToFrameGraph(FrameGraph& fg, std::span<const std::string> presentableNames)
		{
			auto desc = PassDesc();
			desc.SetName("PreparePresent").SetSideEffect();

			for (const std::string& name : presentableNames)
			{
				desc.AddTextureArg(
					TextureArg{ name,
				                bgpu::BarrierSyncFlag::kNone,
				                bgpu::BarrierAccessFlag::kNone,
				                bgpu::BarrierLayout::kPresent });
			}

			fg.AddPass(std::move(desc));
		}
	};
}
