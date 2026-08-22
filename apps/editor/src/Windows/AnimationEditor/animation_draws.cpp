#include "animation_draws.h"
#include <assetlib/bmesh.h>

#include <QtGlobal>

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

	AnimationLoadSteps
	PlanAnimationLoad(const AnimationSource source, const bool hasAnimations)
	{
		// With no clip file there is nothing to play on either tier: the mesh stands in its bind
		// pose as static geometry, and neither a bake nor a bake offer means anything.
		if (!hasAnimations)
			return {};

		const bool vat = source == AnimationSource::kVat;
		return { .needsFreshBake = vat, .framedByBake = vat };
	}

	BoneOverlayOffer
	OfferBoneOverlay(const AnimationSource source, const bool hasAnimations)
	{
		if (!hasAnimations)
		{
			return { .allowed = false,
				     .refusal =
				         "This mesh has no clips, so it is drawn as static geometry and has "
				         "no rig posed on the GPU to show." };
		}

		if (source == AnimationSource::kVat)
		{
			return { .allowed = false,
				     .refusal =
				         "The VAT tier draws baked vertices, not a rig. Switch Preview As to "
				         "Skinned to see the bones." };
		}

		return { .allowed = true };
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
