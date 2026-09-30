#include "Crowd.h"
#include "CrowdPlan.h"
#include "idl/Agent.h"
#include "idl/Constants.h"
#include "idl/GroupSum.h"
#include <bgpu/GpuContext.h>
#include <bgpu/buffer/UploadBuffer.h>
#include <bgpu/cmd/CommandAllocator.h>  // IWYU pragma: keep
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>  // IWYU pragma: keep
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/QueueType.h>
#include <core/err/util.h>
#include <core/math.h>
#include <core/ref/SharedRef.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/GroupReport.h>
#include <crowdlib/ICrowd.h>
#include <crowdlib/ObstacleSegment.h>
#include <crowdlib/debug/AgentReadback.h>
#include <crowdlib/debug/CrowdReadback.h>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace crowd
{
	namespace
	{
		bgpu::ComputeKernel
		LoadKernel(const bgpu::IDevice& device, const std::string& name)
		{
			auto kernel = device.CreateComputeKernel(
				bgpu::ComputePipelineDesc()
					.SetShader(device.CreateShader(name))
					.SetDebugName(name));
			if (kernel.pipeline == nullptr)
				core::throw_runtime_error("The crowd's kernel {} failed to build", name);
			return kernel;
		}

		template <typename T>
		bgpu::BufferHandle
		CreateComputeBuffer(bgpu::IResourceManager& rm, uint32_t count, const std::string& name)
		{
			const auto handle = rm.CreateComputeBuffer(
				bgpu::ComputeBufferDesc().SetElement<T>().SetInitialCount(count).SetDebugName(
					name));
			if (handle.IsNull())
				core::throw_runtime_error("The crowd could not allocate its {} buffer", name);
			return handle;
		}

		bgpu::ReadbackBufferHandle
		CreateReadback(bgpu::IResourceManager& rm, uint64_t byteSize, std::string name)
		{
			auto desc         = bgpu::ReadbackBufferDesc();
			desc.byteSize     = byteSize;
			desc.debugName    = std::move(name);
			const auto handle = rm.CreateReadbackBuffer(desc);
			if (handle.IsNull())
				core::throw_runtime_error("The crowd could not allocate its readbacks");
			return handle;
		}

		bgpu::BufferBarrierDesc
		Transition(
			bgpu::BarrierSyncFlag   syncBefore,
			bgpu::BarrierAccessFlag accessBefore,
			bgpu::BarrierSyncFlag   syncAfter,
			bgpu::BarrierAccessFlag accessAfter)
		{
			return bgpu::BufferBarrierDesc()
			    .AddSyncBefore(syncBefore)
			    .AddAccessBefore(accessBefore)
			    .AddSyncAfter(syncAfter)
			    .AddAccessAfter(accessAfter);
		}

		const auto c_UploadToRead = Transition(
			bgpu::BarrierSyncFlag::kCopy,
			bgpu::BarrierAccessFlag::kCopyDest,
			bgpu::BarrierSyncFlag::kComputeShader,
			bgpu::BarrierAccessFlag::kShaderResource);
		const auto c_UavToUav = Transition(
			bgpu::BarrierSyncFlag::kComputeShader,
			bgpu::BarrierAccessFlag::kUnorderedAccess,
			bgpu::BarrierSyncFlag::kComputeShader,
			bgpu::BarrierAccessFlag::kUnorderedAccess);
		const auto c_WriteToCopy = Transition(
			bgpu::BarrierSyncFlag::kComputeShader,
			bgpu::BarrierAccessFlag::kUnorderedAccess,
			bgpu::BarrierSyncFlag::kCopy,
			bgpu::BarrierAccessFlag::kCopySource);

		// A tick's first use of each buffer after the last tick's, which may still be running on
		// the queue: nothing else orders one list's work after another's.
		const auto c_ReadToUpload = Transition(
			bgpu::BarrierSyncFlag::kComputeShader,
			bgpu::BarrierAccessFlag::kShaderResource,
			bgpu::BarrierSyncFlag::kCopy,
			bgpu::BarrierAccessFlag::kCopyDest);
		const auto c_CopyToWrite = Transition(
			bgpu::BarrierSyncFlag::kCopy,
			bgpu::BarrierAccessFlag::kCopySource,
			bgpu::BarrierSyncFlag::kComputeShader,
			bgpu::BarrierAccessFlag::kUnorderedAccess);

		void
		SetTickParams(bgpu::ComputeKernel& kernel, const TickUploads& plan)
		{
			auto params               = kernel["gUniforms"]["params"];
			params["tickSeconds"]     = plan.params.tickSeconds;
			params["velocityInertia"] = plan.params.velocityInertia;
			params["agentCount"]      = plan.params.agentCount;
			params["groupCount"]      = plan.params.groupCount;
			params["agentRangeCount"] = plan.params.agentRangeCount;
		}

		void
		Dispatch(bgpu::ICommandList& list, bgpu::ComputeKernel& kernel, uint32_t groups)
		{
			auto state   = bgpu::ComputeState();
			state.kernel = &kernel;
			list.SetComputeState(state);
			list.Dispatch(groups, 1, 1);
		}
	}

	Crowd::Crowd(bgpu::GpuContextRef context, CrowdDesc desc) :
		m_Context(std::move(context)), m_Plan(std::move(desc))
	{
		core::ensure(m_Context != nullptr, "A crowd needs a GPU context");

		m_Device = bgpu::CreateDevice(m_Context);
		if (m_Device == nullptr)
			core::throw_runtime_error("The crowd could not create its device");
		m_ResourceManager = m_Device->CreateResourceManager(bgpu::ResourceManagerDesc());
		m_Queue           = m_Device->CreateCommandQueue(bgpu::QueueType::kCompute);
		if (m_ResourceManager == nullptr || m_Queue == nullptr)
			core::throw_runtime_error("The crowd could not create its compute queue");
		m_ResourceManager->RegisterQueue(m_Queue.Get());

		try
		{
			CreateResources();
		}
		catch (...)
		{
			FreeResources();
			throw;
		}
	}

	void
	Crowd::CreateResources()
	{
		const auto& crowdDesc = m_Plan.GetDesc();

		m_Step       = LoadKernel(*m_Device, "crowd.CSStep");
		m_Reduce     = LoadKernel(*m_Device, "crowd.CSReduce");
		m_ReadAgents = LoadKernel(*m_Device, "crowd.CSReadAgents");

		auto& rm = *m_ResourceManager;
		m_Groups.Init(
			{ .initialCount = crowdDesc.maxGroups, .debugName = "Crowd groups" },
			m_ResourceManager);
		m_Ranges.Init(
			{ .initialCount = crowdDesc.maxGroups, .debugName = "Crowd agent ranges" },
			m_ResourceManager);
		m_AgentsPingPong[0] =
			CreateComputeBuffer<idl::Agent>(rm, crowdDesc.maxAgents, "Crowd agents A");
		m_AgentsPingPong[1] =
			CreateComputeBuffer<idl::Agent>(rm, crowdDesc.maxAgents, "Crowd agents B");
		m_GroupSums =
			CreateComputeBuffer<idl::GroupSum>(rm, crowdDesc.maxGroups, "Crowd group sums");
		if (crowdDesc.debugAgentReadback)
		{
			m_AgentReadback = CreateComputeBuffer<debug::AgentReadback>(
				rm,
				crowdDesc.maxAgents,
				"Crowd debug agents");
		}

		// One more slot than ticks in flight: CanStep lets a Step start while the last completed
		// tick's readbacks are still the ones a read returns, so that slot must not be the next.
		m_Slots.resize(crowdDesc.maxTicksInFlight + 1);
		auto listDesc = bgpu::CommandListDesc();
		listDesc.type = bgpu::QueueType::kCompute;
		for (auto& slot : m_Slots)
		{
			slot.allocator = m_Device->CreateCommandAllocator(bgpu::QueueType::kCompute);
			slot.list = m_Device->CreateCommandList(listDesc, slot.allocator, m_ResourceManager);
			slot.groupSums = CreateReadback(
				rm,
				uint64_t{ crowdDesc.maxGroups } * sizeof(idl::GroupSum),
				"Crowd report readback");
			if (crowdDesc.debugAgentReadback)
			{
				slot.agents = CreateReadback(
					rm,
					uint64_t{ crowdDesc.maxAgents } * sizeof(debug::AgentReadback),
					"Crowd debug agent readback");
			}
		}
	}

	Crowd::~Crowd()
	{
		Wait();
		FreeResources();
	}

	void
	Crowd::FreeResources() noexcept
	{
		// An owner drains the queues it made before it lets go of them (docs/bgpu.md, Teardown).
		m_Queue->Flush();

		auto& rm = *m_ResourceManager;
		for (auto& slot : m_Slots)
		{
			Unmap(slot);
			if (!slot.groupSums.IsNull())
				rm.DestroyReadbackBuffer(slot.groupSums, false);
			if (!slot.agents.IsNull())
				rm.DestroyReadbackBuffer(slot.agents, false);
		}
		for (const auto buffer :
		     { m_AgentsPingPong[0], m_AgentsPingPong[1], m_GroupSums, m_AgentReadback })
		{
			if (!buffer.IsNull())
				rm.DestroyBuffer(buffer, false);
		}
		m_Groups.Release(false);
		m_Ranges.Release(false);
		rm.UnregisterQueue(m_Queue.Get());
	}

	const CrowdDesc&
	Crowd::GetDesc() const noexcept
	{
		return m_Plan.GetDesc();
	}

	GroupHandle
	Crowd::CreateGroup(const GroupDesc& desc)
	{
		return m_Plan.CreateGroup(desc);
	}

	void
	Crowd::DestroyGroup(GroupHandle group)
	{
		m_Plan.DestroyGroup(group);
	}

	void
	Crowd::SetOrders(GroupHandle group, const GroupOrders& orders)
	{
		m_Plan.SetOrders(group, orders);
	}

	GroupHandle
	Crowd::SplitGroup(GroupHandle group, uint32_t agentCount)
	{
		return m_Plan.SplitGroup(group, agentCount);
	}

	void
	Crowd::MergeGroup(GroupHandle from, GroupHandle into)
	{
		m_Plan.MergeGroup(from, into);
	}

	void
	Crowd::SetObstacles(std::span<const ObstacleSegment> segments)
	{
		m_Plan.SetObstacles(segments);
	}

	bool
	Crowd::HasGroup(GroupHandle group) const noexcept
	{
		return m_Plan.HasGroup(group);
	}

	uint32_t
	Crowd::GetAgentCount(GroupHandle group) const
	{
		return m_Plan.GetAgentCount(group);
	}

	uint64_t
	Crowd::Step()
	{
		if (!CanStep())
		{
			core::throw_runtime_error(
				"{} ticks are already in flight",
				m_SubmittedTick - GetCompletedTick());
		}

		const auto plan = m_Plan.PlanTick();
		const auto tick = m_SubmittedTick + 1;
		auto&      slot = SlotOf(tick);
		Unmap(slot);
		Record(slot, tick, plan);

		slot.tick         = tick;
		slot.agentCount   = plan.params.agentCount;
		slot.groupHandles = plan.groupHandles;
		slot.groups.clear();
		for (uint32_t row = 0; row < plan.groupHandles.size(); ++row)
		{
			slot.groups.push_back(
				{ .group = plan.groupHandles[row],
			      .first = plan.groups[row].firstAgent,
			      .count = plan.groups[row].agentCount });
		}

		m_SubmittedTick = tick;
		m_ResourceManager->CleanupExpiredResources();
		return tick;
	}

	uint64_t
	Crowd::GetSubmittedTick() const noexcept
	{
		return m_SubmittedTick;
	}

	uint64_t
	Crowd::GetCompletedTick() const noexcept
	{
		while (m_CompletedTick < m_SubmittedTick &&
		       m_Queue->IsFenceComplete(SlotOf(m_CompletedTick + 1).fence))
			++m_CompletedTick;
		return m_CompletedTick;
	}

	void
	Crowd::Wait() noexcept
	{
		if (m_SubmittedTick == 0)
			return;
		m_Queue->WaitForFenceCPUBlocking(SlotOf(m_SubmittedTick).fence);
		m_CompletedTick = m_SubmittedTick;
	}

	std::optional<GroupReport>
	Crowd::GetReport(GroupHandle group) const
	{
		static_cast<void>(m_Plan.GetAgentCount(group));
		const uint64_t tick = GetCompletedTick();
		if (tick == 0)
			return std::nullopt;

		auto& slot = SlotOf(tick);
		for (uint32_t row = 0; row < slot.groupHandles.size(); ++row)
		{
			if (slot.groupHandles[row] != group)
				continue;
			if (slot.mappedGroupSums == nullptr)
				slot.mappedGroupSums = m_ResourceManager->MapReadback(slot.groupSums);
			const auto& sum = static_cast<const idl::GroupSum*>(slot.mappedGroupSums)[row];
			return GroupReport{ .tick         = tick,
				                .agentCount   = sum.agentCount,
				                .meanPosition = sum.meanPosition,
				                .meanFacing   = sum.meanFacing };
		}
		return std::nullopt;
	}

	std::optional<debug::CrowdReadback>
	Crowd::ReadDebugAgents() const
	{
		if (!GetDesc().debugAgentReadback)
			core::throw_runtime_error("The crowd was created without its debug agent readback");
		const uint64_t tick = GetCompletedTick();
		if (tick == 0)
			return std::nullopt;

		auto& slot = SlotOf(tick);
		if (slot.mappedAgents == nullptr)
			slot.mappedAgents = m_ResourceManager->MapReadback(slot.agents);
		return debug::CrowdReadback{
			.tick   = tick,
			.agents = { static_cast<const debug::AgentReadback*>(slot.mappedAgents),
			            slot.agentCount },
			.groups = slot.groups,
		};
	}

	void
	Crowd::Record(TickSlot& slot, uint64_t tick, const TickUploads& plan)
	{
		// Ping-pong: each tick reads the buffer the tick before it wrote.
		const auto agents   = m_AgentsPingPong[tick % 2];
		const auto previous = m_AgentsPingPong[(tick + 1) % 2];

		slot.allocator->ResetAllocator();
		auto& list = *slot.list;
		list.Open(m_Queue.Get(), slot.allocator.Get());

		m_Groups.Assign(plan.groups);
		m_Ranges.Assign(plan.ranges);
		list.Barrier(m_Groups.GetBufferHandle(), c_ReadToUpload);
		list.Barrier(m_Ranges.GetBufferHandle(), c_ReadToUpload);
		list.Barrier(agents, c_UavToUav);
		list.Barrier(m_GroupSums, c_CopyToWrite);
		if (!m_AgentReadback.IsNull())
			list.Barrier(m_AgentReadback, c_CopyToWrite);
		m_Groups.Update(&list);
		m_Ranges.Update(&list);
		list.Barrier(m_Groups.GetBufferHandle(), c_UploadToRead);
		list.Barrier(m_Ranges.GetBufferHandle(), c_UploadToRead);

		const uint32_t agentCount = plan.params.agentCount;
		const uint32_t groupCount = plan.params.groupCount;
		if (agentCount > 0)
		{
			SetTickParams(m_Step, plan);
			m_Step["gUniforms"]["groups"]   = m_Groups.GetBufferHandle();
			m_Step["gUniforms"]["ranges"]   = m_Ranges.GetBufferHandle();
			m_Step["gUniforms"]["previous"] = previous;
			m_Step["gUniforms"]["agents"]   = agents;
			Dispatch(list, m_Step, core::div_ceil(agentCount, idl::c_ThreadsPerGroup));
			list.Barrier(agents, c_UavToUav);

			m_Reduce["gUniforms"]["groups"]    = m_Groups.GetBufferHandle();
			m_Reduce["gUniforms"]["agents"]    = agents;
			m_Reduce["gUniforms"]["groupSums"] = m_GroupSums;
			Dispatch(list, m_Reduce, groupCount);
			list.Barrier(m_GroupSums, c_WriteToCopy);
			list.CopyBufferToReadback(slot.groupSums, m_GroupSums);

			if (GetDesc().debugAgentReadback)
			{
				SetTickParams(m_ReadAgents, plan);
				m_ReadAgents["gUniforms"]["agents"]   = agents;
				m_ReadAgents["gUniforms"]["readback"] = m_AgentReadback;
				Dispatch(list, m_ReadAgents, core::div_ceil(agentCount, idl::c_ThreadsPerGroup));
				list.Barrier(m_AgentReadback, c_WriteToCopy);
				list.CopyBufferToReadback(slot.agents, m_AgentReadback);
			}
		}

		list.Close();
		slot.fence = m_Queue->ExecuteCommandList(&list);
	}

	Crowd::TickSlot&
	Crowd::SlotOf(uint64_t tick) const noexcept
	{
		return m_Slots[tick % m_Slots.size()];
	}

	void
	Crowd::Unmap(TickSlot& slot) const noexcept
	{
		if (slot.mappedGroupSums != nullptr)
			m_ResourceManager->UnmapReadback(slot.groupSums);
		if (slot.mappedAgents != nullptr)
			m_ResourceManager->UnmapReadback(slot.agents);
		slot.mappedGroupSums = nullptr;
		slot.mappedAgents    = nullptr;
	}

	CrowdRef
	CreateCrowd(bgpu::GpuContextRef context, CrowdDesc desc)
	{
		return core::SharedRef<Crowd>::Make(std::move(context), std::move(desc));
	}
}
