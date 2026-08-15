#include "animation_draws.h"

#include <assetlib/bmesh_io.h>

namespace editor
{
	AnimationDrawPlan
	PlanAnimationDraws(const assetlib::BMesh& mesh)
	{
		auto plan = AnimationDrawPlan();

		for (const bmesh::InstancePlacement& placement : bmesh::PlanInstances(mesh))
		{
			if (assetlib::isSkinned(mesh, placement.meshIndex))
				plan.vat.push_back(placement);
			else
				plan.statics.push_back(placement);
		}

		return plan;
	}

	std::vector<ClipInfo>
	ToClipInfos(std::span<const game::AssetManager::VatClipInfo> clips)
	{
		auto infos = std::vector<ClipInfo>();
		infos.reserve(clips.size());
		for (const game::AssetManager::VatClipInfo& clip : clips)
			infos.push_back(
				{ clip.name, clip.frameCount, clip.sampleRate, clip.duration, clip.loop });
		return infos;
	}
}
