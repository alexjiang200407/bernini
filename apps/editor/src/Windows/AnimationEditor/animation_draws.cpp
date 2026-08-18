#include "animation_draws.h"

#include <assetlib/skeleton.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Skeleton.h>

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
		return { .bakeVat = vat, .offerBakeOnRefusal = vat };
	}

	std::optional<float>
	RestFacingYaw(const assetlib::Skeleton& skeleton, const assetlib::AnimationSet& animations)
	{
		if (skeleton.bones.size() < 2 || animations.clips.empty())
			return std::nullopt;

		// The shortest name containing "head": a rig with both 'Head' and 'L Forehead' means the
		// former, and every rig that names its head at all names it more tersely than its parts.
		auto head     = std::optional<uint32_t>();
		auto headName = size_t(0);

		for (uint32_t i = 0; i < skeleton.bones.size(); ++i)
		{
			const std::string_view name  = skeleton.stringPool.at(skeleton.bones[i].nameOffset);
			auto                   lower = std::string(name);
			std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});

			if (lower.find("head") == std::string::npos)
				continue;

			if (!head.has_value() || name.size() < headName)
			{
				head     = i;
				headName = name.size();
			}
		}

		if (!head.has_value())
			return std::nullopt;

		// Frame 0 of the first clip: a rest-ish pose, and the one the panel opens on anyway.
		const std::vector<glm::mat4> pose =
			assetlib::poseModelTransforms(skeleton, animations, 0, 0);
		if (pose.size() <= *head)
			return std::nullopt;

		const glm::vec3 root   = glm::vec3(pose[0][3]);
		const glm::vec3 toHead = glm::vec3(pose[*head][3]) - root;
		const auto      facing = glm::vec2(toHead.x, toHead.z);

		// A head directly above its root -- an upright biped seen from the top down -- says nothing
		// about which way the rig looks.
		if (glm::length(facing) < 1e-4f)
			return std::nullopt;

		// The orbit camera puts its eye at (sin(yaw), _, cos(yaw)); to look *at* the face, the eye
		// goes where the face points.
		return std::atan2(facing.x, facing.y);
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
