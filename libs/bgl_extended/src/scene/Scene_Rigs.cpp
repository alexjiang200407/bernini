#include "scene/GeomRollback.h"
#include "scene/Scene.h"
#include <algorithm>
#include <array>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <bgl/IScene.h>
#include <bgl/PreparedStaticMesh.h>
#include <bgl/RigHandle.h>
#include <bgl/types/BlendSetDesc.h>
#include <bgl/types/FootPlantDesc.h>
#include <bgl_common/idl/BlendNode.h>
#include <bgl_common/idl/BlendNodeKind.h>
#include <bgl_common/idl/BlendSpaceSample.h>
#include <bgl_common/idl/BoneSample.h>
#include <bgl_common/idl/Clip.h>
#include <bgl_common/idl/Constants.h>
#include <bgl_common/idl/SkinnedBone.h>
#include <bgl_common/idl/SkinnedLegChain.h>
#include <cmath>
#include <core/containers/multi_slot_handle.h>
#include <core/containers/slot_handle.h>
#include <core/math.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tracy/Tracy.hpp>

#include <vector>

namespace bgl
{
	void
	Scene::ValidateSkinnedRig(
		const assetlib::Skeleton&     skeleton,
		const assetlib::AnimationSet& animations,
		const FootPlantDesc&          footPlant,
		const BlendSetDesc&           blendSet)
	{
		const size_t boneCount = skeleton.bones.size();
		if (boneCount == 0)
		{
			throw SceneError("skinned geometry: a skeleton with no bones skins nothing");
		}
		for (size_t i = 0; i < boneCount; ++i)
		{
			const uint32_t parent = skeleton.bones[i].parent;
			// A forward-pass walk reads parent[i] before it writes i, so an equal or higher parent
			// would read a transform this frame has not written -- garbage, not a wrong pose.
			if (parent != idl::cInvalidBone && parent >= i)
			{
				throw SceneError(
					"skinned geometry: bones are not topologically sorted; every parent must be a "
					"lower index than its own bone");
			}
		}

		if (animations.boneCount != boneCount)
		{
			throw SceneError(
				"skinned geometry: the clip set was cooked against a rig of a different bone "
				"count");
		}
		if (animations.clips.empty())
		{
			throw SceneError("skinned geometry: the clip table is empty; there is no pose to play");
		}

		for (const assetlib::AnimationClip& clip : animations.clips)
		{
			// The shader clamps to frameCount - 1, and on a uint a zero underflows to four billion
			// frames of out-of-bounds reads.
			if (clip.frameCount == 0)
			{
				throw SceneError("skinned geometry: a clip with no frames has no pose to sample");
			}

			// idl::Clip addresses frames, not samples, so a base that is not a whole number of
			// frames in has no representation -- and would silently truncate to the frame below.
			if (clip.firstSample % boneCount != 0)
			{
				throw SceneError(
					"skinned geometry: a clip's first sample is not on a frame boundary");
			}

			const uint64_t end = static_cast<uint64_t>(clip.firstSample) +
			                     static_cast<uint64_t>(clip.frameCount) * boneCount;
			if (end > animations.samples.size())
			{
				throw SceneError(
					"skinned geometry: a clip's frames run past the end of the sample pool");
			}
		}

		// The pose pass keeps a solve delta per chain bone in groupshared, and that array has a fixed
		// size; past it a rig would overrun it rather than plant anything.
		if (footPlant.legs.size() > idl::cMaxLegsPerRig)
		{
			throw SceneError(
				std::format(
					"skinned geometry: a rig may author at most {} legs, and this one authors {}",
					idl::cMaxLegsPerRig,
					footPlant.legs.size()));
		}

		// A leg is authored by bone name, so a refusal carrying only an index would send whoever
		// wrote the avatar back to the rig to count bones. Falls back to the index for a bone the
		// string pool has no name for.
		const auto nameOf = [&skeleton](uint32_t bone) {
			const std::string_view name = skeleton.stringPool.at(skeleton.bones[bone].nameOffset);
			return name.empty() ? std::format("bone {}", bone) : std::format("'{}'", name);
		};

		for (const FootPlantLegDesc& leg : footPlant.legs)
		{
			const std::array<uint32_t, 4> chain = { { leg.hip, leg.knee, leg.ankle, leg.toe } };
			for (const uint32_t bone : chain)
			{
				if (bone >= boneCount)
				{
					throw SceneError(
						std::format(
							"skinned geometry: a leg names bone {}, which is outside the {}-bone "
							"skeleton it was resolved against",
							bone,
							boneCount));
				}
			}

			for (size_t link = 1; link < chain.size(); ++link)
			{
				// The solve rewrites these four slots and carries their descendants rigidly, so a
				// bone between two links would keep a pose the joints above it no longer agree
				// with.
				if (skeleton.bones[chain[link]].parent != chain[link - 1])
				{
					throw SceneError(
						std::format(
							"skinned geometry: a leg's bones are not a direct chain: {} is not "
							"parented to {}",
							nameOf(chain[link]),
							nameOf(chain[link - 1])));
				}
			}

			// Judged after normalizing, for SetGround's reason: a zero normal divides to NaN and a
			// finite one large enough to overflow the length divides to zero.
			const glm::vec3 normal = leg.soleNormal / glm::length(leg.soleNormal);
			if (!core::is_finite(normal) || glm::length(normal) == 0.0f)
			{
				throw SceneError(
					std::format(
						"skinned geometry: the sole normal on the leg ending at {} must be finite "
						"and "
						"nonzero",
						nameOf(leg.ankle)));
			}
		}

		const size_t frames = animations.samples.size() / boneCount;
		if (footPlant.plantWeights.size() != frames * footPlant.legs.size())
		{
			throw SceneError(
				"skinned geometry: the plant weights are not one byte per leg for every frame in "
				"the sample pool");
		}

		for (size_t s = 0; s < blendSet.spaces.size(); ++s)
		{
			const std::vector<BlendSpaceSampleDesc>& samples = blendSet.spaces[s].samples;

			ValidateBlendSpaceRun(s, samples);

			for (size_t m = 0; m < samples.size(); ++m)
			{
				const BlendSpaceSampleDesc& sample = samples[m];

				if (sample.clipIndex >= animations.clips.size())
				{
					throw SceneError(
						std::format(
							"skinned geometry: sample {} of blend space {} names clip {} of a set "
							"that holds {}",
							m,
							s,
							sample.clipIndex,
							animations.clips.size()));
				}

				// The space wraps its phase and plays each sample at that fraction of the sample's
				// cycle, whatever the clip's own loop flag says. A single frame has no cycle, and the
				// weighted cycle is what the phase advances by.
				if (animations.clips[sample.clipIndex].frameCount < 2)
				{
					throw SceneError(
						std::format(
							"skinned geometry: sample {} of blend space {} names a clip of one "
							"frame, which has no cycle for the space to share",
							m,
							s));
				}
			}
		}
	}

	void
	Scene::ValidateBlendSpaceRun(const size_t space, std::span<const BlendSpaceSampleDesc> samples)
	{
		// One sample is a clip, and every clip is already a node under its own index.
		if (samples.size() < 2)
		{
			throw SceneError(
				std::format(
					"skinned geometry: blend space {} holds {} samples, and a blend space needs at "
					"least two",
					space,
					samples.size()));
		}

		for (size_t m = 0; m < samples.size(); ++m)
		{
			const BlendSpaceSampleDesc& sample = samples[m];

			if (!std::isfinite(sample.parameter))
			{
				throw SceneError(
					std::format(
						"skinned geometry: sample {} of blend space {} has a parameter of {}",
						m,
						space,
						sample.parameter));
			}

			// Strictly increasing, not merely sorted: the span between two samples is what a
			// weight divides by.
			if (m > 0 && !(sample.parameter > samples[m - 1].parameter))
			{
				throw SceneError(
					std::format(
						"skinned geometry: blend space {} has parameter {} at sample {} after {}, "
						"and they must strictly increase",
						space,
						sample.parameter,
						m,
						samples[m - 1].parameter));
			}
		}
	}

	RigMeta*
	Scene::FindRig(RigHandle rig) noexcept
	{
		if (!rig.IsValid() || !m_Rigs.IsValid(rig.handle))
		{
			return nullptr;
		}

		return &m_Rigs.MetaAt(rig.handle.index);
	}

	RigHandle
	Scene::AddRig(
		const assetlib::Skeleton&     skeleton,
		const assetlib::AnimationSet& animations,
		const FootPlantDesc&          footPlant,
		const BlendSetDesc&           blendSet)
	{
		ValidateSkinnedRig(skeleton, animations, footPlant, blendSet);

		const uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());

		// Depth is derived rather than read: no container carries it, and one forward pass is
		// enough because a parent always precedes its child.
		auto     bones    = std::vector<idl::SkinnedBone>();
		uint32_t maxDepth = 0;
		bones.reserve(boneCount);
		for (const assetlib::Bone& bone : skeleton.bones)
		{
			const uint32_t depth =
				bone.parent == idl::cInvalidBone ? 0 : bones[bone.parent].depth + 1;
			maxDepth = std::max(maxDepth, depth);
			bones.push_back({ bone.inverseBind, bone.parent, depth });
		}

		auto samples = std::vector<idl::BoneSample>();
		samples.reserve(animations.samples.size());
		for (const assetlib::Transform& sample : animations.samples)
		{
			samples.push_back(
				{ glm::vec4(sample.translation, 0.0f),
			      glm::vec4(
					  sample.rotation.x,
					  sample.rotation.y,
					  sample.rotation.z,
					  sample.rotation.w),
			      glm::vec4(sample.scale, 0.0f) });
		}

		auto clips = std::vector<idl::Clip>();
		clips.reserve(animations.clips.size());
		for (const assetlib::AnimationClip& clip : animations.clips)
		{
			clips.push_back(
				{ clip.firstSample / boneCount,
			      clip.frameCount,
			      clip.sampleRate,
			      clip.loop ? 1u : 0u });
		}

		auto legs = std::vector<idl::SkinnedLegChain>();
		legs.reserve(footPlant.legs.size());
		for (const FootPlantLegDesc& leg : footPlant.legs)
		{
			legs.push_back(
				{ glm::vec4(leg.solePoint, 0.0f),
			      glm::vec4(glm::normalize(leg.soleNormal), 0.0f),
			      leg.hip,
			      leg.knee,
			      leg.ankle,
			      leg.toe });
		}

		// Four bytes to a uint, least-significant first. The tail of the last word is zero, which is
		// a leg that is never planted -- the same thing an absent weight would mean.
		auto weights = std::vector<uint32_t>((footPlant.plantWeights.size() + 3) / 4, 0u);
		for (size_t i = 0; i < footPlant.plantWeights.size(); ++i)
		{
			weights[i / 4] |= uint32_t(footPlant.plantWeights[i]) << (8 * (i % 4));
		}

		// The node table: one clip node per clip in clip order, then the authored spaces. Clips
		// first is what lets a slot naming node `n` below the clip count play clip `n`, so a
		// one-clip spawn means what it did before blend spaces existed.
		auto nodes = std::vector<idl::BlendNode>();

		// Not `samples`: the bone samples above already own that name here, and the rig holds both.
		auto blendSamples = std::vector<idl::BlendSpaceSample>();
		nodes.reserve(clips.size() + blendSet.spaces.size());
		for (uint32_t clip = 0; clip < clips.size(); ++clip)
		{
			auto node = idl::BlendNode();
			node.kind = idl::BlendNodeKind::kClip;
			node.clip = clip;
			nodes.emplace_back(node);
		}
		for (const BlendSpaceDesc& space : blendSet.spaces)
		{
			auto node        = idl::BlendNode();
			node.kind        = idl::BlendNodeKind::kSpace;
			node.firstSample = static_cast<uint32_t>(blendSamples.size());
			node.sampleCount = static_cast<uint32_t>(space.samples.size());
			nodes.emplace_back(node);

			for (const BlendSpaceSampleDesc& sample : space.samples)
			{
				auto entry      = idl::BlendSpaceSample();
				entry.clip      = sample.clipIndex;
				entry.parameter = sample.parameter;
				blendSamples.emplace_back(entry);
			}
		}

		auto rollback = GeomRollback();

		try
		{
			auto record    = idl::Rig();
			record.bones   = rollback.Track(m_SkinnedBones, m_SkinnedBones.Add(std::span(bones)));
			record.samples = rollback.Track(m_BoneSamples, m_BoneSamples.Add(std::span(samples)));
			record.clips   = rollback.Track(m_Clips, m_Clips.Add(std::span(clips)));
			if (!legs.empty())
			{
				record.legs = rollback.Track(m_SkinnedLegs, m_SkinnedLegs.Add(std::span(legs)));
				record.plantWeights =
					rollback.Track(m_PlantWeights, m_PlantWeights.Add(std::span(weights)));
			}
			record.nodes = rollback.Track(m_BlendNodes, m_BlendNodes.Add(std::span(nodes)));
			if (!blendSamples.empty())
			{
				record.blendSamples =
					rollback.Track(m_BlendSamples, m_BlendSamples.Add(std::span(blendSamples)));
			}

			record.boneCount = boneCount;
			record.maxDepth  = maxDepth;

			const core::slot_handle entry = m_Rigs.Add(record);

			RigMeta& meta   = m_Rigs.MetaAt(entry.index);
			meta.boneCount  = boneCount;
			meta.clipCount  = static_cast<uint32_t>(clips.size());
			meta.legCount   = static_cast<uint32_t>(legs.size());
			meta.nodeCount  = static_cast<uint32_t>(nodes.size());
			meta.frameCount = static_cast<uint32_t>(animations.samples.size() / boneCount);
			meta.useCount   = 0;

			rollback.Commit();
			return RigHandle{ entry };
		}
		catch (const std::runtime_error& e)
		{
			throw SceneError(e.what());
		}
	}

	void
	Scene::SetRigBlendParameters(RigHandle rig, const BlendSetDesc& blendSet)
	{
		const RigMeta* meta = FindRig(rig);
		if (meta == nullptr)
		{
			throw SceneError(
				"RigHandle passed to SetRigBlendParameters is null, or already deleted");
		}

		const idl::Rig& record = m_Rigs.AtIndex(rig.handle.index);

		// Clips come first in the node table, so the spaces are whatever is left over.
		const size_t spaceCount = meta->nodeCount - meta->clipCount;
		if (blendSet.spaces.size() != spaceCount)
		{
			throw SceneError(
				std::format(
					"skinned geometry: the rig carries {} blend spaces and the set names {}; only "
					"the parameters may move, and a rig whose spaces change shape is re-uploaded",
					spaceCount,
					blendSet.spaces.size()));
		}

		// A rig with no spaces has a null sample range, so there is no handle to take: an empty set
		// against one is the write that was asked for, and it is no bytes.
		if (spaceCount == 0)
		{
			return;
		}

		const uint32_t firstSpaceNode = record.nodes.range.offsetStart + meta->clipCount;
		const uint32_t sampleBase     = record.blendSamples.offsetStart;

		// Checked in full before a byte is written: a half-applied set is a rig posing from a run
		// nobody authored, and there is no rollback for a write straight into the mirror.
		for (size_t s = 0; s < spaceCount; ++s)
		{
			const std::vector<BlendSpaceSampleDesc>& samples = blendSet.spaces[s].samples;
			ValidateBlendSpaceRun(s, samples);

			const idl::BlendNode& node =
				m_BlendNodes.AtIndex(firstSpaceNode + static_cast<uint32_t>(s));
			if (samples.size() != node.sampleCount)
			{
				throw SceneError(
					std::format(
						"skinned geometry: blend space {} holds {} samples and the set names {}; "
						"adding or removing one moves the sample table",
						s,
						node.sampleCount,
						samples.size()));
			}

			for (uint32_t m = 0; m < node.sampleCount; ++m)
			{
				const uint32_t clip =
					m_BlendSamples.AtIndex(sampleBase + node.firstSample + m).clip;
				if (samples[m].clipIndex != clip)
				{
					throw SceneError(
						std::format(
							"skinned geometry: sample {} of blend space {} plays clip {} and the "
							"set names clip {}; only the parameters may move",
							m,
							s,
							clip,
							samples[m].clipIndex));
				}
			}
		}

		const core::multi_slot_handle handle = m_BlendSamples.HandleAt(sampleBase);

		for (size_t s = 0; s < spaceCount; ++s)
		{
			const idl::BlendNode& node =
				m_BlendNodes.AtIndex(firstSpaceNode + static_cast<uint32_t>(s));

			for (uint32_t m = 0; m < node.sampleCount; ++m)
			{
				auto entry      = idl::BlendSpaceSample();
				entry.clip      = blendSet.spaces[s].samples[m].clipIndex;
				entry.parameter = blendSet.spaces[s].samples[m].parameter;
				m_BlendSamples.Set(handle, node.firstSample + m, entry);
			}
		}
	}

	void
	Scene::RequestBoneAnimTable(RigHandle rig)
	{
		RigMeta* meta = FindRig(rig);
		if (meta == nullptr)
		{
			throw SceneError(
				"RigHandle passed to RequestBoneAnimTable is null, or already deleted");
		}

		if (meta->boneAnimTable)
		{
			// Allocated already. Either it holds a pose, or it is waiting for one -- and the sweep in
			// PendingRigFills is what finds the second case, so there is nothing to record here.
			return;
		}

		const uint32_t float4s = meta->frameCount * meta->boneCount * idl::cFloat4sPerBone;

		// Reserving it is what can cost -- a growth reallocates the whole arena on the device. The
		// posing itself is the GPU's, and no timestamp query exists to measure it from here.
		ZoneScopedN("bgl reserve bone anim table");
		ZoneTextF(
			"%u bones x %u frames, %llu KiB",
			meta->boneCount,
			meta->frameCount,
			(unsigned long long)((uint64_t(float4s) * sizeof(glm::vec4)) / 1024));

		// Read before the allocation, because a growth is what discards every other rig's table and
		// the capacity is the only thing that reports one.
		const uint32_t capacityBefore = m_BoneAnimTables.Capacity();

		// The arena discards on growth, and a table is written once rather than every frame, so every
		// rig holding one has to be posed again. The offsets survive; the contents do not. Run on the
		// throwing path too: a growth that then fails to hand out the slice has already discarded.
		const auto requeueIfGrown = [&] {
			if (m_BoneAnimTables.Capacity() == capacityBefore)
			{
				return;
			}

			for (uint32_t i = 0; i < m_Rigs.Capacity(); ++i)
			{
				if (m_Rigs.IsIndexValid(i) && m_Rigs.MetaAt(i).boneAnimTable)
				{
					m_Rigs.MetaAt(i).tableFilled = false;
				}
			}
		};

		try
		{
			meta->boneAnimTable = m_BoneAnimTables.Allocate(float4s);
		}
		catch (const std::runtime_error& e)
		{
			requeueIfGrown();
			throw SceneError(e.what());
		}

		m_Rigs.MetaAt(rig.handle.index).tableFilled = false;

		auto record          = m_Rigs[rig.handle];
		record.boneAnimTable = m_Rigs.MetaAt(rig.handle.index).boneAnimTable;
		m_Rigs.Set(rig.handle, record);

		requeueIfGrown();
	}

	std::span<const Scene::RigFill>
	Scene::PendingRigFills()
	{
		m_PendingRigFills.clear();

		for (uint32_t i = 0; i < m_Rigs.Capacity(); ++i)
		{
			if (!m_Rigs.IsIndexValid(i))
			{
				continue;
			}

			const RigMeta& meta = m_Rigs.MetaAt(i);
			if (meta.boneAnimTable && !meta.tableFilled)
			{
				m_PendingRigFills.push_back(RigFill{ i, meta.frameCount });
			}
		}

		return m_PendingRigFills;
	}

	void
	Scene::MarkRigFillsRecorded() noexcept
	{
		for (const RigFill& fill : m_PendingRigFills)
		{
			if (m_Rigs.IsIndexValid(fill.rigIndex))
			{
				m_Rigs.MetaAt(fill.rigIndex).tableFilled = true;
			}
		}

		m_PendingRigFills.clear();
	}

	void
	Scene::DeleteRig(RigHandle rig)
	{
		const RigMeta* meta = FindRig(rig);
		if (meta == nullptr)
		{
			throw SceneError("RigHandle passed to DeleteRig refers to a deleted or unknown rig");
		}

		// Refused rather than permitted, unlike a texture asset: a geom left naming freed bone and
		// sample ranges poses from whatever lands in them next.
		if (meta->useCount > 0)
		{
			throw SceneError(
				"RigHandle passed to DeleteRig still has geoms skinned to it; delete them first");
		}

		if (meta->boneAnimTable)
		{
			m_BoneAnimTables.Free(meta->boneAnimTable);
		}

		const idl::Rig record = m_Rigs[rig.handle];
		m_SkinnedBones.EraseByIndex(record.bones.offsetStart);
		m_BoneSamples.EraseByIndex(record.samples.offsetStart);
		m_Clips.EraseByIndex(record.clips.range.offsetStart);
		if (!record.legs.Null())
		{
			m_SkinnedLegs.EraseByIndex(record.legs.range.offsetStart);
			m_PlantWeights.EraseByIndex(record.plantWeights.offsetStart);
		}
		m_BlendNodes.EraseByIndex(record.nodes.range.offsetStart);
		if (!record.blendSamples.Null())
		{
			m_BlendSamples.EraseByIndex(record.blendSamples.offsetStart);
		}
		m_Rigs.Erase(rig.handle);
	}
}
