#pragma once
#include "cmd/CommandList.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "resource/ResourceManager.h"

namespace bgl
{
	// Clears a set of render targets (and an optional depth target). Each color
	// target is declared to the graph by name so the graph derives its transition
	// to render-target; the pass only records the clears, never barriers.
	class ClearPass
	{
	public:
		struct ColorTarget
		{
			std::string          name;  // graph resource name of the target
			RtvHandle            rtv;
			std::array<float, 4> clearColor;
		};

		void
		AttachToFrameGraph(
			FrameGraph&                  fg,
			IResourceManager*            resourceManager,
			std::span<const ColorTarget> colors,
			DsvHandle                    depth)
		{
			PassDesc desc;
			desc.SetName("Clear");

			for (const ColorTarget& color : colors)
			{
				desc.AddTextureArg(
					TextureArg{ color.name,
				                BarrierSyncFlag::kRenderTarget,
				                BarrierAccessFlag::kRenderTarget,
				                BarrierLayout::kRenderTarget });
			}

			std::vector<ColorTarget> targets(colors.begin(), colors.end());
			desc.SetExec([resourceManager, targets, depth](const PassContext& resources) {
				ICommandList* cmd = resources.GetCommandList();
				if (!depth.IsNull())
				{
					resourceManager->ClearDsv(cmd, depth, 1.0f, 0);
				}
				for (const ColorTarget& color : targets)
				{
					float clear[4] = { color.clearColor[0],
						               color.clearColor[1],
						               color.clearColor[2],
						               color.clearColor[3] };
					resourceManager->ClearRtv(cmd, color.rtv, clear);
				}
			});

			fg.AddPass(std::move(desc));
		}
	};
}
