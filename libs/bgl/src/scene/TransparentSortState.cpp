#include "scene/TransparentSortState.h"
#include "fg/FrameGraph.h"
#include "idl/DispatchArgs.h"

namespace bgl
{
	namespace
	{
		constexpr std::string_view c_SortedName       = "scene.sortedTransparentInstances";
		constexpr std::string_view c_EntriesName      = "scene.transparentSortEntries";
		constexpr std::string_view c_CountName        = "scene.transparentSortCount";
		constexpr std::string_view c_DispatchArgsName = "transparentSort.dispatchArgs";
	}

	void
	TransparentSortState::Init(uint32_t paddedInstances, ResourceManagerRef resourceManager)
	{
		{
			auto desc         = ComputeBufferDesc();
			desc.initialCount = paddedInstances;
			desc.debugName    = "Sorted Transparent Instances";
			desc.SetElement<uint32_t>();

			m_SortedInstances.Init(std::move(desc), resourceManager);
		}

		{
			auto desc         = ComputeBufferDesc();
			desc.initialCount = paddedInstances;
			desc.debugName    = "Transparent Sort Entries";
			desc.SetElement<glm::uvec2>();

			m_Entries.Init(std::move(desc), resourceManager);
		}

		{
			auto desc         = ComputeBufferDesc();
			desc.initialCount = 1;
			desc.debugName    = "Transparent Sort Count";
			desc.SetElement<uint32_t>();

			m_Count.Init(std::move(desc), resourceManager);
		}

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<idl::DispatchArgs>().SetInitialCount(1).SetDebugName(
				"Transparent Dispatch Args");

			m_DispatchArgs.Init(std::move(desc), std::move(resourceManager));
		}
	}

	void
	TransparentSortState::Resize(uint32_t paddedInstances)
	{
		if (paddedInstances <= m_SortedInstances.GetDesc().initialCount)
		{
			return;
		}

		m_SortedInstances.Resize(paddedInstances);
		m_Entries.Resize(paddedInstances);
	}

	void
	TransparentSortState::Release(bool deferred) noexcept
	{
		m_SortedInstances.Release(deferred);
		m_Entries.Release(deferred);
		m_Count.Release(deferred);
		m_DispatchArgs.Release(deferred);
	}

	void
	TransparentSortState::Update(ICommandList* cmdList)
	{
		m_SortedInstances.Update(cmdList);
		m_Entries.Update(cmdList);
	}

	void
	TransparentSortState::ImportResources(FrameGraph& fg, std::vector<std::string>& updateArgs)
		const
	{
		const auto importUpdated = [&](std::string_view name, const ComputeBuffer& buffer) {
			fg.ImportBuffer(name, buffer.GetBufferHandle());
			updateArgs.emplace_back(name);
		};

		importUpdated(c_SortedName, m_SortedInstances);
		importUpdated(c_EntriesName, m_Entries);
		importUpdated(c_CountName, m_Count);

		fg.ImportBuffer(c_DispatchArgsName, m_DispatchArgs.GetBufferHandle());
	}
}
