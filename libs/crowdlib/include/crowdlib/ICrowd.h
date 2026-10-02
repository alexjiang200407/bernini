#pragma once
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/QueuePoint.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/GroupReport.h>
#include <crowdlib/ObstacleSegment.h>
#include <crowdlib/RenderTick.h>
#include <crowdlib/api.h>
#include <crowdlib/debug/CrowdReadback.h>
#include <cstdint>
#include <optional>
#include <span>

namespace crowd
{
	/**
	 * A crowd simulated on the GPU, driven one group at a time: its agents are never addressed. See
	 * docs/crowdlib.md.
	 *
	 * Commands take effect at the next Step, which submits one fixed tick and returns at once; its
	 * reports are readable once GetCompletedTick() has reached it. A handle is refused by every call
	 * from the one that releases it, whether or not a tick has applied that yet. Caller misuse
	 * throws std::runtime_error. Single-threaded, like the renderer.
	 *
	 * Releasing the last reference waits for every tick in flight before letting go of the device.
	 */
	class ICrowd : public core::Ref
	{
	public:
		ICrowd(const ICrowd&) noexcept = delete;
		ICrowd(ICrowd&&) noexcept      = delete;

		ICrowd&
		operator=(const ICrowd&) noexcept = delete;

		ICrowd&
		operator=(ICrowd&&) noexcept = delete;

		[[nodiscard]] virtual const CrowdDesc&
		GetDesc() const noexcept = 0;

		/**
		 * @throws std::runtime_error if `desc.agentType` names no type, `desc.agentCount` is zero,
		 *         `desc.orders` is invalid (GroupOrders), or the group would exceed maxAgents or
		 *         maxGroups.
		 */
		virtual GroupHandle
		CreateGroup(const GroupDesc& desc) = 0;

		/** @throws std::runtime_error for a handle this crowd does not hold. */
		virtual void
		DestroyGroup(GroupHandle group) = 0;

		/**
		 * Replaces the group's orders whole.
		 *
		 * @throws std::runtime_error for a handle this crowd does not hold, or invalid orders.
		 */
		virtual void
		SetOrders(GroupHandle group, const GroupOrders& orders) = 0;

		/**
		 * Detaches `agentCount` agents from the rear of the group's formation into a new group of
		 * the same type, under a copy of the group's orders.
		 *
		 * @throws std::runtime_error for a handle this crowd does not hold, an `agentCount` that
		 *         would leave either group empty, or a group past maxGroups.
		 */
		virtual GroupHandle
		SplitGroup(GroupHandle group, uint32_t agentCount) = 0;

		/**
		 * Moves every agent of `from` to the rear of `into`'s formation, under `into`'s orders, and
		 * releases `from`.
		 *
		 * @throws std::runtime_error for a handle this crowd does not hold, `from == into`, or
		 *         groups of different agent types.
		 */
		virtual void
		MergeGroup(GroupHandle from, GroupHandle into) = 0;

		/**
		 * Replaces every static obstacle with `segments`, copied.
		 *
		 * @throws std::runtime_error for more than maxObstacleSegments segments, or one with a
		 *         non-finite end; the obstacles are then left as they were.
		 */
		virtual void
		SetObstacles(std::span<const ObstacleSegment> segments) = 0;

		[[nodiscard]] virtual bool
		HasGroup(GroupHandle group) const noexcept = 0;

		/**
		 * The group's agents as the commands issued so far make them, whether or not a tick has
		 * applied those commands yet.
		 *
		 * @throws std::runtime_error for a handle this crowd does not hold.
		 */
		[[nodiscard]] virtual uint32_t
		GetAgentCount(GroupHandle group) const = 0;

		/**
		 * Applies every command issued since the last Step, submits the next tick and returns
		 * without waiting.
		 *
		 * @return the tick submitted, counted from 1.
		 * @throws std::runtime_error if !CanStep().
		 */
		virtual uint64_t
		Step() = 0;

		/** The last tick submitted; 0 before the first Step. */
		[[nodiscard]] virtual uint64_t
		GetSubmittedTick() const noexcept = 0;

		/** The last tick whose reports are readable, read without blocking. */
		[[nodiscard]] virtual uint64_t
		GetCompletedTick() const noexcept = 0;

		/**
		 * Whether a Step may run now: fewer than maxTicksInFlight ticks are in flight and, with a
		 * render ring, its reader has released the tick the next one would overwrite.
		 */
		[[nodiscard]] bool
		CanStep() const noexcept
		{
			const CrowdDesc& desc = GetDesc();
			const uint64_t   next = GetSubmittedTick() + 1;
			return GetSubmittedTick() - GetCompletedTick() < desc.maxTicksInFlight &&
			       (desc.renderRingTicks == 0 ||
			        next <= GetReleasedRenderTick() + desc.renderRingTicks);
		}

		/** Blocks until every tick submitted has completed. */
		virtual void
		Wait() noexcept = 0;

		/**
		 * The group as GetCompletedTick() measured it: empty until a completed tick has included
		 * the group.
		 *
		 * @throws std::runtime_error for a handle this crowd does not hold.
		 */
		[[nodiscard]] virtual std::optional<GroupReport>
		GetReport(GroupHandle group) const = 0;

		/**
		 * Every agent as GetCompletedTick() left them: empty until a tick has completed. The one
		 * per-agent read a crowd has, for seeing it and never for driving a game.
		 *
		 * @throws std::runtime_error unless the crowd was created with
		 *         CrowdDesc::debugAgentReadback.
		 */
		[[nodiscard]] virtual std::optional<debug::CrowdReadback>
		ReadDebugAgents() const = 0;

		/**
		 * The render ring, for another owner to import once (bgpu::IResourceManager::
		 * ImportNativeBuffer): renderRingTicks * maxAgents RenderAgent records in one buffer, fixed
		 * for the crowd's life, tick t's from record (t % renderRingTicks) * maxAgents on.
		 *
		 * @throws std::runtime_error unless the crowd was created with CrowdDesc::renderRingTicks.
		 */
		[[nodiscard]] virtual bgpu::NativeBufferDesc
		GetRenderRing() const = 0;

		/**
		 * Where `tick`'s records are, from its Step until the ring is stepped past it
		 * renderRingTicks Steps later; empty outside that, and for tick 0.
		 *
		 * @throws std::runtime_error unless the crowd was created with CrowdDesc::renderRingTicks.
		 */
		[[nodiscard]] virtual std::optional<RenderTick>
		GetRenderTick(uint64_t tick) const = 0;

		/**
		 * The reader is done with every tick through `throughTick` once `readerDone` passes on its
		 * own queue. A Step that overwrites a released tick's records first waits for that point on
		 * the GPU; one that would overwrite a tick not yet released cannot run (CanStep). The crowd
		 * never waits for a reader on the CPU.
		 *
		 * @throws std::runtime_error without a render ring, for a null `readerDone`, or a
		 *         `throughTick` past GetSubmittedTick() or before an earlier release's.
		 */
		virtual void
		ReleaseRenderReads(uint64_t throughTick, const bgpu::QueuePoint& readerDone) = 0;

		/** The last tick released by ReleaseRenderReads; 0 before the first. */
		[[nodiscard]] virtual uint64_t
		GetReleasedRenderTick() const noexcept = 0;

		/**
		 * The GPU time `tick` took on the crowd's queue, once it has completed and while it is one of
		 * the last maxTicksInFlight + 1 ticks; empty otherwise.
		 */
		[[nodiscard]] virtual std::optional<float>
		GetTickGpuMilliseconds(uint64_t tick) const = 0;

	protected:
		ICrowd() noexcept = default;
	};

	using CrowdRef = core::SharedRef<ICrowd>;

	/**
	 * A crowd on `context`'s device, with a device, resource manager and compute queue of its own
	 * beside every other owner's. The application keeps the context alive until the crowd is gone.
	 *
	 * @throws std::runtime_error for a description a crowd refuses, or a device that cannot make the
	 *         crowd's kernels or buffers.
	 */
	[[nodiscard]] CROWD_API CrowdRef
	CreateCrowd(bgpu::GpuContextRef context, CrowdDesc desc);
}
