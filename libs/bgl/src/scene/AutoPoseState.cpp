#include "scene/AutoPoseState.h"
#include "fg/FrameGraph.h"
#include "scene/scene_buffer_names.h"
#include <algorithm>
#include <bgl/idl/AutoPosedInstance.h>
#include <bgl/idl/DominantFrames.h>
#include <bgl/idl/InstancePose.h>
#include <bgl/idl/PosePool.h>
#include <bgpu/cmd/CommandList.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace bgl
{
	AutoPoseState::AutoPoseState(
		const bgpu::ResourceManagerRef& resourceManager,
		uint32_t                        placements) :
		m_Placements(
			resourceManager,
			bgpu::UploadBufferDesc().SetInitialCount(1).SetDebugName("Automatic Placements")),
		m_InstancePose(
			resourceManager,
			bgpu::ComputeBufferDesc()
				.SetElement<idl::InstancePose>()
				.SetInitialCount(std::max(placements, 1u))
				.SetDebugName("Instance Pose")),
		m_DominantFrames(
			resourceManager,
			bgpu::ComputeBufferDesc()
				.SetElement<idl::DominantFrames>()
				.SetInitialCount(std::max(placements, 1u))
				.SetDebugName("Dominant Frames")),
		m_Pool(
			resourceManager,
			bgpu::ComputeBufferDesc().SetElement<idl::PosePool>().SetInitialCount(1).SetDebugName(
				"Pose Pool")),
		m_Posed(
			resourceManager,
			bgpu::ComputeBufferDesc()
				.SetElement<idl::AutoPosedInstance>()
				.SetInitialCount(1)
				.SetDebugName("Automatic Pose List")),
		m_Requests(
			resourceManager,
			bgpu::ComputeBufferDesc().SetElement<uint32_t>().SetInitialCount(1).SetDebugName(
				"Pose Requests"))
	{}

	void
	AutoPoseState::Resize(uint32_t placements)
	{
		if (placements > m_InstancePose.GetDesc().initialCount)
		{
			m_InstancePose.Resize(placements);
			m_DominantFrames.Resize(placements);
		}
	}

	void
	AutoPoseState::Assign(
		std::span<const uint32_t> placements,
		uint32_t                  maxPosed,
		uint32_t                  poolStart,
		uint32_t                  capacity)
	{
		m_Placements.Assign(placements);
		m_PlacementCount = static_cast<uint32_t>(placements.size());
		m_MaxPosed       = maxPosed;
		m_PoolStart      = poolStart;
		m_Capacity       = capacity;

		if (maxPosed > m_Posed.GetDesc().initialCount)
		{
			m_Posed.Resize(maxPosed);
		}
		if (m_PlacementCount > m_Requests.GetDesc().initialCount)
		{
			m_Requests.Resize(m_PlacementCount);
		}
	}

	void
	AutoPoseState::Update(bgpu::ICommandList* cmdList)
	{
		m_Placements.Update(cmdList);
		m_InstancePose.Update(cmdList);
		m_DominantFrames.Update(cmdList);
		m_Posed.Update(cmdList);
		m_Requests.Update(cmdList);
		m_Pool.Update(cmdList);

		// The counters start every frame at zero; the capacity is the one field the CPU knows.
		auto pool     = idl::PosePool();
		pool.capacity = m_Capacity;
		cmdList->WriteBuffer(m_Pool.GetBufferHandle(), &pool, sizeof(pool));
	}

	void
	AutoPoseState::ImportResources(FrameGraph& fg, std::vector<std::string>& updateArgs) const
	{
		const auto import = [&](std::string_view name, bgpu::BufferHandle handle) {
			fg.ImportBuffer(name, handle);
			updateArgs.emplace_back(name);
		};

		import(c_AutoPlacementsName, m_Placements.GetBufferHandle());
		import(c_InstancePoseName, m_InstancePose.GetBufferHandle());
		import(c_DominantFramesName, m_DominantFrames.GetBufferHandle());
		import(c_PosePoolName, m_Pool.GetBufferHandle());
		import(c_AutoPosedName, m_Posed.GetBufferHandle());
		import(c_PoseRequestsName, m_Requests.GetBufferHandle());
	}
}
