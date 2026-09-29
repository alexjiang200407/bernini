#pragma once
#include <core/containers/slot_vector.h>
#include <core/ref/RefCounter.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/GroupReport.h>
#include <crowdlib/ICrowd.h>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace crowd::test
{
	struct FakeGroup
	{
		uint32_t    agentType  = 0;
		uint32_t    agentCount = 0;
		GroupOrders orders;
	};

	struct FakeMeasurement
	{
		GroupHandle group;
		GroupReport report;
	};

	/**
	 * ICrowd on the CPU, with no simulation: a tick measures every group standing at its orders'
	 * goal, as if it had arrived at once. A tick completes only when the test completes it
	 * (CompleteTick) or waits, which is what lets a case hold one in flight.
	 */
	class FakeCrowd final : public core::RefCounter<ICrowd>
	{
	public:
		/** @throws std::runtime_error for a description a crowd refuses. */
		explicit FakeCrowd(CrowdDesc desc);

		FakeCrowd(const FakeCrowd&) = delete;
		FakeCrowd(FakeCrowd&&)      = delete;

		FakeCrowd&
		operator=(const FakeCrowd&) = delete;

		FakeCrowd&
		operator=(FakeCrowd&&) = delete;

		[[nodiscard]] const CrowdDesc&
		GetDesc() const noexcept override;

		GroupHandle
		CreateGroup(const GroupDesc& desc) override;

		void
		DestroyGroup(GroupHandle group) override;

		void
		SetOrders(GroupHandle group, const GroupOrders& orders) override;

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

		/** @throws std::runtime_error when no tick is in flight. */
		void
		CompleteTick();

	private:
		[[nodiscard]] const FakeGroup&
		GetGroup(GroupHandle group) const;

		[[nodiscard]] FakeGroup&
		GetGroup(GroupHandle group);

		CrowdDesc                                m_Desc;
		core::slot_vector<FakeGroup>             m_Groups;
		uint32_t                                 m_AgentCount    = 0;
		uint64_t                                 m_SubmittedTick = 0;
		std::deque<std::vector<FakeMeasurement>> m_InFlight;
		std::vector<FakeMeasurement>             m_Completed;
	};
}
