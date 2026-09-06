#include "animation_draws.h"
#include "Mesh/BMeshUtil.h"
#include "Windows/AnimationEditor/PlaybackTransport.h"
#include <assetlib/AssetStore.h>
#include <assetlib/bmesh.h>
#include <assetlib_structs/BMaterial.h>
#include <gamelib/ClipInfo.h>

#include <QtGlobal>
#include <exception>
#include <qlogging.h>
#include <span>
#include <string>
#include <vector>

namespace editor
{
	AnimationDrawPlan
	PlanAnimationDraws(const assetlib::BMesh& mesh)
	{
		auto plan = AnimationDrawPlan();

		for (const bmesh::InstancePlacement& placement : bmesh::PlanInstances(mesh))
		{
			if (assetlib::isSkinned(mesh, placement.meshIndex))
				plan.animated.push_back(placement);
			else
				plan.statics.push_back(placement);
		}

		return plan;
	}

	std::vector<std::string>
	BakeableMaterials(const assetlib::AssetStore& store, std::span<const std::string> materials)
	{
		auto loose = std::vector<std::string>();
		for (const std::string& relPath : materials)
		{
			if (relPath.empty())
				continue;

			try
			{
				if (store.DrawsLoose(store.Load<assetlib::BMaterial>(relPath)))
					loose.push_back(relPath);
			}
			catch (const std::exception& e)
			{
				qWarning("AnimationPreview: could not read '%s': %s", relPath.c_str(), e.what());
			}
		}
		return loose;
	}

	std::vector<ClipInfo>
	ToClipInfos(std::span<const game::ClipInfo> clips)
	{
		auto infos = std::vector<ClipInfo>();
		infos.reserve(clips.size());
		for (const game::ClipInfo& clip : clips)
			infos.push_back(
				{ clip.name, clip.frameCount, clip.sampleRate, clip.duration, clip.loop });
		return infos;
	}
}
