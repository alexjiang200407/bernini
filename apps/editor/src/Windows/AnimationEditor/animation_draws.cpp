#include "animation_draws.h"

#include <QtGlobal>

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
				if (store.DrawsLoose(store.LoadMaterial(relPath)))
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

	std::vector<PreparedAnimationDraw>
	PrepareAnimationDraws(
		const assetlib::AssetStore&                      store,
		std::string_view                                 relPath,
		std::string_view                                 animationsRelPath,
		const AnimationSource                            source,
		const AnimationDrawPlan&                         plan,
		std::span<const std::optional<assetlib::Bounds>> animatedBounds)
	{
		auto out = std::vector<PreparedAnimationDraw>();
		out.reserve(plan.statics.size() + plan.animated.size());

		const auto prepareStatic = [&](const bmesh::InstancePlacement& placement,
		                               std::string                     refusal) {
			auto draw      = PreparedAnimationDraw();
			draw.placement = placement;
			draw.prepared  = game::PrepareMesh(store, relPath, placement.meshIndex);
			draw.refusal   = std::move(refusal);
			out.push_back(std::move(draw));
		};

		for (const bmesh::InstancePlacement& placement : plan.statics) prepareStatic(placement, {});

		for (size_t i = 0; i < plan.animated.size(); ++i)
		{
			const bmesh::InstancePlacement& placement = plan.animated[i];

			// A rig with no clip file anywhere falls back to bind pose as static geometry.
			if (animationsRelPath.empty())
			{
				prepareStatic(placement, {});
				continue;
			}

			const std::optional<assetlib::Bounds> posed =
				i < animatedBounds.size() ? animatedBounds[i] : std::nullopt;

			try
			{
				// The two tiers differ only in which door the prepare takes.
				auto prepared = source == AnimationSource::kSkinned ? game::PrepareSkinnedMesh(
																		  store,
																		  relPath,
																		  animationsRelPath,
																		  placement.meshIndex,
																		  posed) :
				                                                      game::PrepareVatMesh(
																		  store,
																		  relPath,
																		  animationsRelPath,
																		  placement.meshIndex);

				auto draw      = PreparedAnimationDraw();
				draw.placement = placement;
				draw.prepared  = std::move(prepared);
				draw.animated  = true;
				draw.posed     = posed;
				out.push_back(std::move(draw));
			}
			catch (const std::exception& e)
			{
				prepareStatic(placement, e.what());
			}
		}

		return out;
	}
}
