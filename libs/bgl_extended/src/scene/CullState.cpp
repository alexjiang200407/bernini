#include "scene/CullState.h"
#include "fg/FrameGraph.h"
#include "resource/ResourceManager.h"
#include "scene/scene_buffer_names.h"
#include <algorithm>
#include <bgl_common/idl/CullView.h>
#include <bgl_common/idl/DispatchArgs.h>
#include <bgl_common/idl/DrawBucket.h>
#include <bgl_common/idl/InstanceLod.h>
#include <bgl_common/idl/InstanceVisibility.h>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

namespace bgl
{
	void
	CullState::Init(
		uint32_t           paddedInstances,
		uint32_t           placements,
		ResourceManagerRef resourceManager)
	{
		{
			// Twice the slots: a placement fading between two levels draws both, one entry each.
			auto desc         = ComputeBufferDesc();
			desc.initialCount = paddedInstances * 2;
			desc.debugName    = "Compacted Instances";
			desc.SetElement<uint32_t>();

			m_CompactedInstances.Init(std::move(desc), resourceManager);
		}

		{
			auto desc         = ComputeBufferDesc();
			desc.initialCount = paddedInstances;
			desc.debugName    = "Instance Visibility";
			desc.SetElement<idl::InstanceVisibility>();

			m_InstanceVisibility.Init(std::move(desc), resourceManager);
		}

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<uint32_t>()
				.SetInitialCount(idl::cMaxDrawLanes)
				.SetDebugName("Draw Bucket Prefix Sum");

			m_DrawBucketPrefixSum.Init(std::move(desc), resourceManager);
		}

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<idl::DispatchArgs>()
				.SetInitialCount(idl::cMaxDrawLanes)
				.SetDebugName("Compacted Dispatch Args");

			m_CompactedDispatchArgs.Init(std::move(desc), resourceManager);
		}

		for (uint32_t i = 0; i < m_InstanceLod.size(); ++i)
		{
			auto desc         = ComputeBufferDesc();
			desc.initialCount = std::max(placements, 1u);
			desc.debugName    = i == 0 ? "Instance LOD A" : "Instance LOD B";
			desc.SetElement<idl::InstanceLod>();

			m_InstanceLod[i].Init(std::move(desc), resourceManager);
		}
		m_LodNeedsClear = true;

		{
			auto desc         = UploadBufferDesc();
			desc.initialCount = 1;
			desc.debugName    = "Cull View";

			m_CullView.Init(std::move(desc), std::move(resourceManager));
		}
	}

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
			for (ComputeBuffer& words : m_InstanceLod) words.Resize(placements);
			m_LodNeedsClear = true;
		}
	}

	void
	CullState::AdvanceLodHistory() noexcept
	{
		m_LodCurrent ^= 1u;
	}

	void
	CullState::Release(bool deferred) noexcept
	{
		m_CompactedInstances.Release(deferred);
		m_InstanceVisibility.Release(deferred);
		m_DrawBucketPrefixSum.Release(deferred);
		m_CompactedDispatchArgs.Release(deferred);
		m_CullView.Release(deferred);
		for (ComputeBuffer& words : m_InstanceLod) words.Release(deferred);
	}

	void
	CullState::Update(ICommandList* cmdList)
	{
		m_CompactedInstances.Update(cmdList);
		m_InstanceVisibility.Update(cmdList);
		for (ComputeBuffer& words : m_InstanceLod) words.Update(cmdList);
	}

	void
	CullState::ImportResources(
		FrameGraph&               fg,
		std::string_view          scope,
		std::vector<std::string>& updateArgs) const
	{
		fg.SetResourceNamespace(std::string(scope));

		const auto importUpdated = [&](std::string_view name, const ComputeBuffer& buffer) {
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
