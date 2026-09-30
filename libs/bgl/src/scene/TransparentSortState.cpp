#include "scene/TransparentSortState.h"
#include "fg/FrameGraph.h"
#include "scene/scene_buffer_names.h"
#include <bgl/idl/DispatchArgs.h>
#include <bgpu/resource/ResourceManager.h>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace bgl
{
	TransparentSortState::TransparentSortState(
		const bgpu::ResourceManagerRef& resourceManager,
		uint32_t                        paddedInstances) :
		m_SortedInstances(
			resourceManager,
			bgpu::ComputeBufferDesc()
				.SetElement<uint32_t>()
				.SetInitialCount(paddedInstances)
				.SetDebugName("Sorted Transparent Instances")),
		m_Entries(
			resourceManager,
			bgpu::ComputeBufferDesc()
				.SetElement<glm::uvec2>()
				.SetInitialCount(paddedInstances)
				.SetDebugName("Transparent Sort Entries")),
		m_Count(
			resourceManager,
			bgpu::ComputeBufferDesc().SetElement<uint32_t>().SetInitialCount(1).SetDebugName(
				"Transparent Sort Count")),
		m_DispatchArgs(
			resourceManager,
			bgpu::ComputeBufferDesc()
				.SetElement<idl::DispatchArgs>()
				.SetInitialCount(1)
				.SetDebugName("Transparent Dispatch Args"))
	{}

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
	TransparentSortState::Update(bgpu::ICommandList* cmdList)
	{
		m_SortedInstances.Update(cmdList);
		m_Entries.Update(cmdList);
	}

	void
	TransparentSortState::ImportResources(FrameGraph& fg, std::vector<std::string>& updateArgs)
		const
	{
		const auto importUpdated = [&](std::string_view name, const bgpu::ComputeBuffer& buffer) {
			fg.ImportBuffer(name, buffer.GetBufferHandle());
			updateArgs.emplace_back(name);
		};

		importUpdated(c_SortedTransparentInstancesName, m_SortedInstances);
		importUpdated(c_TransparentSortEntriesName, m_Entries);
		importUpdated(c_TransparentSortCountName, m_Count);

		fg.ImportBuffer(c_TransparentDispatchArgsName, m_DispatchArgs.GetBufferHandle());
	}
}
