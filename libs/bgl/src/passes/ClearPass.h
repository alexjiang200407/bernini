#pragma once
#include "cmd/CommandList.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "resource/ResourceManager.h"

namespace bgl
{
	// Clears a set of render targets and depth targets. Each target is declared to the graph by name
	// so the graph derives its transition to render-target / depth-write; the pass only records the
	// clears, never barriers.
	class ClearPass
	{
	public:
		struct ColorTarget
		{
			std::string          name;  // graph resource name of the target
			RtvHandle            rtv;
			std::array<float, 4> clearColor;
		};

		struct DepthTarget
		{
			std::string name;
			DsvHandle   dsv;
		};

		void
		AttachToFrameGraph(
			FrameGraph&                  fg,
			IResourceManager*            resourceManager,
			std::span<const ColorTarget> colors,
			std::span<const DepthTarget> depths)
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

			for (const DepthTarget& depth : depths)
			{
				if (depth.dsv.IsNull())
				{
					continue;
				}

				desc.AddTextureArg(
					TextureArg{ depth.name,
				                BarrierSyncFlag::kDepthStencil,
				                BarrierAccessFlag::kDepthWrite,
				                BarrierLayout::kDepthWrite });
			}

			std::vector<ColorTarget> colorTargets(colors.begin(), colors.end());
			std::vector<DepthTarget> depthTargets(depths.begin(), depths.end());

			desc.SetExec(
				[resourceManager, colorTargets, depthTargets](const PassContext& resources) {
					ICommandList* cmd = resources.GetCommandList();
					for (const DepthTarget& depth : depthTargets)
					{
						if (!depth.dsv.IsNull())
						{
							resourceManager->ClearDsv(cmd, depth.dsv, 1.0f, 0);
						}
					}
					for (const ColorTarget& color : colorTargets)
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
