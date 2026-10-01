#pragma once
#include "CrowdPlan.h"
#include "idl/AgentRange.h"
#include "idl/Group.h"
#include <bgpu/GpuContext.h>
#include <bgpu/buffer/UploadBuffer.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/cmd/QueuePoint.h>
#include <bgpu/cmd/TimestampHeap.h>
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/GroupReport.h>
#include <crowdlib/ICrowd.h>
#include <crowdlib/ObstacleSegment.h>
#include <crowdlib/RenderTick.h>
#include <crowdlib/debug/CrowdReadback.h>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace crowd
{
	/**
	 * ICrowd on bgpu's RHI, the same code on every backend: a device, resource manager and compute
	 * queue of its own on the application's context. Each Step uploads what CrowdPlan laid out and
	 * records one tick into the ring slot `tick % (maxTicksInFlight + 1)`, whose readbacks hold
	 * that tick's reports (and agents) until the slot is recorded into again.
	 */
	class Crowd final : public core::RefCounter<ICrowd>
	{
	public:
		/** @throws std::runtime_error for a description a crowd refuses, or a device that fails. */
		Crowd(bgpu::GpuContextRef context, CrowdDesc desc);
		~Crowd() override;

		Crowd(const Crowd&) = delete;
		Crowd(Crowd&&)      = delete;

		Crowd&
		operator=(const Crowd&) = delete;

		Crowd&
		operator=(Crowd&&) = delete;

		[[nodiscard]] const CrowdDesc&
		GetDesc() const noexcept override;

		GroupHandle
		CreateGroup(const GroupDesc& desc) override;

		void
		DestroyGroup(GroupHandle group) override;

		void
		SetOrders(GroupHandle group, const GroupOrders& orders) override;

		GroupHandle
		SplitGroup(GroupHandle group, uint32_t agentCount) override;

		void
		MergeGroup(GroupHandle from, GroupHandle into) override;

		void
		SetObstacles(std::span<const ObstacleSegment> segments) override;

		[[nodiscard]] bool
		HasGroup(GroupHandle group) const noexcept override;

		[[nodiscard]] uint32_t
		GetAgentCount(GroupHandle group) const override;

		uint64_t
		Step() override;

		[[nodiscard]] uint64_t
		GetSubmittedTick() const noexcept override;

		[[nodiscard]] uint64_t
		GetCompletedTick() const noexcept override;

		void
		Wait() noexcept override;

		[[nodiscard]] std::optional<GroupReport>
		GetReport(GroupHandle group) const override;

		[[nodiscard]] std::optional<debug::CrowdReadback>
		ReadDebugAgents() const override;

		[[nodiscard]] bgpu::NativeBufferDesc
		GetRenderRing() const override;

		[[nodiscard]] std::optional<RenderTick>
		GetRenderTick(uint64_t tick) const override;

		void
		ReleaseRenderReads(uint64_t throughTick, const bgpu::QueuePoint& readerDone) override;

		[[nodiscard]] uint64_t
		GetReleasedRenderTick() const noexcept override;

		[[nodiscard]] std::optional<float>
		GetTickGpuMilliseconds(uint64_t tick) const override;

	private:
		/** A reader's ReleaseRenderReads: every tick through `throughTick` is free once `done`. */
		struct RenderRelease
		{
			uint64_t         throughTick = 0;
			bgpu::QueuePoint done;
		};

		/** One tick in the ring: what it recorded into, and which group each readback row is. */
		struct TickSlot
		{
			uint64_t                        tick  = 0;
			uint64_t                        fence = 0;
			bgpu::CommandAllocatorRef       allocator;
			bgpu::CommandListRef            list;
			bgpu::ReadbackBufferHandle      groupSums;
			bgpu::ReadbackBufferHandle      agents;
			uint32_t                        agentCount = 0;
			std::vector<GroupHandle>        groupHandles;
			std::vector<debug::GroupAgents> groups;

			// Mapped on first read, and unmapped before the slot is recorded into again.
			const void* mappedGroupSums = nullptr;
			const void* mappedAgents    = nullptr;

			// Whether the tick recorded a timestamp pair: a tick with no agents dispatches nothing.
			bool timed = false;
		};

		/** Everything made from the device after its queue; on a throw, what was made is released. */
		void
		CreateResources();

		/** Drains the queue and frees what CreateResources made, however far it got. */
		void
		FreeResources() noexcept;

		void
		Record(TickSlot& slot, uint64_t tick, const TickUploads& plan);

		[[nodiscard]] TickSlot&
		SlotOf(uint64_t tick) const noexcept;

		void
		Unmap(TickSlot& slot) const noexcept;

		void
		RequireRenderRing() const;

		// Before the tick's list opens, as a wait must on Metal: waits for the reader to be done
		// with the records the tick overwrites.
		void
		WaitForRenderReader(uint64_t tick);

		// Declared first so it is released last, after every queue on it is drained.
		bgpu::GpuContextRef m_Context;

		CrowdPlan m_Plan;

		// The crowd's own, made on m_Context by bgpu::CreateDevice: the queue, resource manager and
		// kernels below are all built from it, apart from every other owner's (docs/bgpu.md).
		bgpu::DeviceRef          m_Device;
		bgpu::ResourceManagerRef m_ResourceManager;
		bgpu::CommandQueueRef    m_Queue;

		bgpu::ComputeKernel m_Step;
		bgpu::ComputeKernel m_Reduce;
		bgpu::ComputeKernel m_ReadAgents;

		bgpu::UploadBuffer<idl::Group>      m_Groups;
		bgpu::UploadBuffer<idl::AgentRange> m_Ranges;
		// GPU-only: each tick reads the one the tick before it wrote, and writes the other.
		bgpu::BufferHandle m_AgentsPingPong[2];

		// CSReduce's output, one idl::GroupSum per group row, copied into the tick's report readback.
		bgpu::BufferHandle m_GroupSums;
		bgpu::BufferHandle m_AgentReadback;

		mutable std::vector<TickSlot> m_Slots;
		uint64_t                      m_SubmittedTick = 0;
		mutable uint64_t              m_CompletedTick = 0;

		// Two timestamps per tick slot, around the tick's dispatches.
		core::SharedRef<bgpu::ITimestampHeap> m_TickTimer;

		// Null without a render ring. One RenderTick per ring slot, the tick it holds by `tick`.
		bgpu::BufferHandle        m_RenderRing;
		std::vector<RenderTick>   m_RenderTicks;
		std::deque<RenderRelease> m_RenderReleases;
		uint64_t                  m_ReleasedRenderTick = 0;
	};
}
