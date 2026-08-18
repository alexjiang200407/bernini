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

		const glm::vec3 headPos = glm::vec3(pose[*head][3]);

		// Where the head looks, which is toward its own children: a face is built out of them --
		// nose, jaw, eyes, cheeks -- and they outnumber and outweigh the ears that point elsewhere.
		//
		// *Not* the direction from the root to the head. That is the obvious reading and it is wrong
		// on any rig whose clip stands it up: the test coyote rears, putting its head 45 units above
		// its pelvis and 4 to the side, so the horizontal part of root-to-head is noise -- and acting
		// on it put the camera behind the animal.
		auto sum      = glm::vec3(0.0f);
		auto children = 0;
		for (uint32_t i = 0; i < skeleton.bones.size(); ++i)
		{
			if (skeleton.bones[i].parent != *head)
				continue;

			sum += glm::vec3(pose[i][3]);
			++children;
		}

		if (children == 0)
			return std::nullopt;

		const glm::vec3 look   = sum / static_cast<float>(children) - headPos;
		const auto      facing = glm::vec2(look.x, look.z);

		// A head looking straight up or down says nothing about which way the rig faces.
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
