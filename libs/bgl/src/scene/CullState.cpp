#include "scene/CullState.h"
#include "fg/FrameGraph.h"
#include "scene/scene_buffer_names.h"
#include <algorithm>
#include <bgl/idl/CullView.h>
#include <bgl/idl/DispatchArgs.h>
#include <bgl/idl/DrawBucket.h>
#include <bgl/idl/InstanceLod.h>
#include <bgl/idl/InstanceVisibility.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/resource/ResourceManager.h>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		bgpu::ComputeBuffer
		CreateLodWords(
			uint32_t                        placements,
			const bgpu::ResourceManagerRef& resourceManager,
			std::string                     debugName)
		{
			return bgpu::ComputeBuffer(
				resourceManager,
				bgpu::ComputeBufferDesc()
					.SetElement<idl::InstanceLod>()
					.SetInitialCount(std::max(placements, 1u))
					.SetDebugName(std::move(debugName)));
		}
	}

	CullState::CullState(
		const bgpu::ResourceManagerRef& resourceManager,
		uint32_t                        paddedInstances,
		uint32_t                        placements) :
		// Twice the slots: a placement fading between two levels draws both, one entry each.
		m_CompactedInstances(
			resourceManager,
			bgpu::ComputeBufferDesc()
				.SetElement<uint32_t>()
				.SetInitialCount(paddedInstances * 2)
				.SetDebugName("Compacted Instances")),
		m_InstanceVisibility(
			resourceManager,
			bgpu::ComputeBufferDesc()
				.SetElement<idl::InstanceVisibility>()
				.SetInitialCount(paddedInstances)
				.SetDebugName("Instance Visibility")),
		m_DrawBucketPrefixSum(
			resourceManager,
			bgpu::ComputeBufferDesc()
				.SetElement<uint32_t>()
				.SetInitialCount(idl::cMaxDrawLanes)
				.SetDebugName("Draw Bucket Prefix Sum")),
		m_CompactedDispatchArgs(
			resourceManager,
			bgpu::ComputeBufferDesc()
				.SetElement<idl::DispatchArgs>()
				.SetInitialCount(idl::cMaxDrawLanes)
				.SetDebugName("Compacted Dispatch Args")),
		m_CullView(
			resourceManager,
			bgpu::UploadBufferDesc().SetInitialCount(1).SetDebugName("Cull View")),
		m_InstanceLod{ CreateLodWords(placements, resourceManager, "Instance LOD A"),
		               CreateLodWords(placements, resourceManager, "Instance LOD B") }
	{}

	void
	CullState::Resize(uint32_t paddedInstances, uint32_t placements)
	{
		if (paddedInstances > m_InstanceVisibility.GetDesc().initialCount)
		{
			m_CompactedInstances.Resize(paddedInstances * 2);
			m_InstanceVisibility.Resize(paddedInstances);
		}

		if (placements > m_InstanceLod[0].GetDesc().initialCount)
		{
			for (bgpu::ComputeBuffer& words : m_InstanceLod) words.Resize(placements);
			m_LodNeedsClear = true;
		}
	}

	void
	CullState::ClearFresh(bgpu::ICommandList* cmdList)
	{
		const auto zero = idl::InstanceLod();
		for (const uint32_t placement : m_FreshPlacements)
		{
			for (bgpu::ComputeBuffer& words : m_InstanceLod)
			{
				if (placement < words.GetDesc().initialCount)
				{
					cmdList->WriteBuffer(
						words.GetBufferHandle(),
						&zero,
						size_t(placement) * sizeof(zero),
						sizeof(zero));
				}
			}
		}
		m_FreshPlacements.clear();
	}

	void
	CullState::AdvanceLodHistory() noexcept
	{
		m_LodCurrent ^= 1u;
	}

	void
	CullState::Update(bgpu::ICommandList* cmdList)
	{
		m_CompactedInstances.Update(cmdList);
		m_InstanceVisibility.Update(cmdList);
		for (bgpu::ComputeBuffer& words : m_InstanceLod) words.Update(cmdList);
	}

	void
	CullState::ImportResources(
		FrameGraph&               fg,
		std::string_view          scope,
		std::vector<std::string>& updateArgs) const
	{
		fg.SetResourceNamespace(std::string(scope));

		const auto importUpdated = [&](std::string_view name, const bgpu::ComputeBuffer& buffer) {
			fg.ImportBuffer(name, buffer.GetBufferHandle());
			updateArgs.push_back(std::format("{}{}", scope, name));
		};

		importUpdated(c_CompactedInstancesName, m_CompactedInstances);
		importUpdated(c_InstanceVisibilityName, m_InstanceVisibility);

		fg.ImportBuffer(c_DrawBucketPrefixSumName, m_DrawBucketPrefixSum.GetBufferHandle());
		fg.ImportBuffer(c_CompactDispatchArgsName, m_CompactedDispatchArgs.GetBufferHandle());
		fg.ImportBuffer(c_CullViewName, m_CullView.GetBufferHandle());
		fg.ImportBuffer(c_InstanceLodName, m_InstanceLod[m_LodCurrent].GetBufferHandle());
		fg.ImportBuffer(
			c_InstanceLodPreviousName,
			m_InstanceLod[m_LodCurrent ^ 1u].GetBufferHandle());
	}
}
