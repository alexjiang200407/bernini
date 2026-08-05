#include "scene/ViewCullState.h"
#include "fg/FrameGraph.h"
#include "idl/InstanceVisibility.h"

namespace bgl
{
	namespace
	{
		constexpr std::string_view c_CompactedInstancesName = "scene.compactedInstances";
		constexpr std::string_view c_InstanceVisibilityName = "scene.instanceVisibility";
		constexpr std::string_view c_SortedTransparentName  = "scene.sortedTransparentInstances";
		constexpr std::string_view c_TransparentKeysName    = "scene.transparentSortEntries";
		constexpr std::string_view c_TransparentCountName   = "scene.transparentSortCount";
	}

	void
	ViewCullState::Init(uint32_t paddedInstances, ResourceManagerRef resourceManager)
	{
		{
			auto desc         = ComputeBufferDesc();
			desc.initialCount = paddedInstances;
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
			auto desc         = ComputeBufferDesc();
			desc.initialCount = paddedInstances;
			desc.debugName    = "Sorted Transparent Instances";
			desc.SetElement<uint32_t>();

			m_SortedTransparentInstances.Init(std::move(desc), resourceManager);
		}

		{
			auto desc         = ComputeBufferDesc();
			desc.initialCount = paddedInstances;
			desc.debugName    = "Transparent Sort Entries";
			desc.SetElement<glm::uvec2>();

			m_TransparentSortEntries.Init(std::move(desc), resourceManager);
		}

		{
			auto desc         = ComputeBufferDesc();
			desc.initialCount = 1;
			desc.debugName    = "Transparent Sort Count";
			desc.SetElement<uint32_t>();

			m_TransparentSortCount.Init(std::move(desc), std::move(resourceManager));
		}
	}

	void
	ViewCullState::Resize(uint32_t paddedInstances)
	{
		if (paddedInstances <= m_CompactedInstances.GetDesc().initialCount)
		{
			return;
		}

		m_CompactedInstances.Resize(paddedInstances);
		m_InstanceVisibility.Resize(paddedInstances);
		m_SortedTransparentInstances.Resize(paddedInstances);
		m_TransparentSortEntries.Resize(paddedInstances);
	}

	void
	ViewCullState::Release(bool deferred) noexcept
	{
		m_CompactedInstances.Release(deferred);
		m_InstanceVisibility.Release(deferred);
		m_SortedTransparentInstances.Release(deferred);
		m_TransparentSortEntries.Release(deferred);
		m_TransparentSortCount.Release(deferred);
	}

	void
	ViewCullState::Update(ICommandList* cmdList)
	{
		m_CompactedInstances.Update(cmdList);
		m_InstanceVisibility.Update(cmdList);
		m_SortedTransparentInstances.Update(cmdList);
		m_TransparentSortEntries.Update(cmdList);
	}

	void
	ViewCullState::ImportResources(FrameGraph& fg, std::vector<std::string>& resourceNames) const
	{
		const auto importBuffer = [&](std::string_view name, const ComputeBuffer& buffer) {
			std::string key(name);
			fg.ImportBuffer(key, buffer.GetBufferHandle());
			resourceNames.push_back(std::move(key));
		};

		importBuffer(c_CompactedInstancesName, m_CompactedInstances);
		importBuffer(c_InstanceVisibilityName, m_InstanceVisibility);
		importBuffer(c_SortedTransparentName, m_SortedTransparentInstances);
		importBuffer(c_TransparentKeysName, m_TransparentSortEntries);
		importBuffer(c_TransparentCountName, m_TransparentSortCount);
	}
}
