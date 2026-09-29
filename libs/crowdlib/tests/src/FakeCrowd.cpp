#include "FakeCrowd.h"
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <crowdlib/AgentType.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/GroupReport.h>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace crowd::test
{
	namespace
	{
		bool
		IsPositive(float value) noexcept
		{
			return std::isfinite(value) && value > 0.0f;
		}

		void
		ValidateOrders(const GroupOrders& orders)
		{
			if (!std::isfinite(orders.goal.x) || !std::isfinite(orders.goal.y))
				core::throw_runtime_error("A group's goal must be finite");
			if (!IsPositive(glm::length(orders.facing)))
				core::throw_runtime_error("A group's facing must be finite and not zero");
			if (orders.formation.frontage == 0 || !IsPositive(orders.formation.spacing))
				core::throw_runtime_error("A formation needs a positive frontage and spacing");
		}
	}

	FakeCrowd::FakeCrowd(CrowdDesc desc) : m_Desc(std::move(desc))
	{
		if (m_Desc.agentTypes.empty())
			core::throw_runtime_error("A crowd needs at least one agent type");
		for (const auto& type : m_Desc.agentTypes)
		{
			if (!IsPositive(type.radius) || !IsPositive(type.maxSpeed) ||
			    !IsPositive(type.maxAcceleration) || !IsPositive(type.maxTurnRate) ||
			    !IsPositive(type.mass))
				core::throw_runtime_error("Every field of an agent type must be positive");
		}
		if (m_Desc.maxAgents == 0 || m_Desc.maxGroups == 0 || m_Desc.maxTicksInFlight == 0)
			core::throw_runtime_error("A crowd's capacities must be positive");
		if (!IsPositive(m_Desc.tickSeconds))
			core::throw_runtime_error("A crowd's tick must be positive");

		m_Groups.reset(m_Desc.maxGroups);
	}

	const CrowdDesc&
	FakeCrowd::GetDesc() const noexcept
	{
		return m_Desc;
	}

	GroupHandle
	FakeCrowd::CreateGroup(const GroupDesc& desc)
	{
		if (desc.agentType >= m_Desc.agentTypes.size())
			core::throw_runtime_error("Agent type {} is not one of the crowd's", desc.agentType);
		if (desc.agentCount == 0)
			core::throw_runtime_error("A group needs at least one agent");
		ValidateOrders(desc.orders);
		if (desc.agentCount > m_Desc.maxAgents - m_AgentCount)
		{
			core::throw_runtime_error(
				"{} agents would exceed the crowd's {}",
				m_AgentCount + desc.agentCount,
				m_Desc.maxAgents);
		}

		const auto slot = m_Groups.try_allocate_and_emplace(
			FakeGroup{ .agentType  = desc.agentType,
		               .agentCount = desc.agentCount,
		               .orders     = desc.orders });
		if (slot.is_null())
			core::throw_runtime_error("The crowd already holds its {} groups", m_Desc.maxGroups);

		m_AgentCount += desc.agentCount;
		return GroupHandle{ .handle = slot };
	}

	void
	FakeCrowd::DestroyGroup(GroupHandle group)
	{
		m_AgentCount -= GetGroup(group).agentCount;
		m_Groups.release_slot(group.handle);
	}

	void
	FakeCrowd::SetOrders(GroupHandle group, const GroupOrders& orders)
	{
		ValidateOrders(orders);
		GetGroup(group).orders = orders;
	}

	GroupHandle
	FakeCrowd::SplitGroup(GroupHandle group, uint32_t agentCount)
	{
		auto& source = GetGroup(group);
		if (agentCount == 0 || agentCount >= source.agentCount)
		{
			core::throw_runtime_error(
				"Splitting {} of a group's {} agents would leave a group empty",
				agentCount,
				source.agentCount);
		}

		const auto detached = FakeGroup{ .agentType  = source.agentType,
			                             .agentCount = agentCount,
			                             .orders     = source.orders };
		const auto slot     = m_Groups.try_allocate_and_emplace(detached);
		if (slot.is_null())
			core::throw_runtime_error("The crowd already holds its {} groups", m_Desc.maxGroups);

		GetGroup(group).agentCount -= agentCount;
		return GroupHandle{ .handle = slot };
	}

	void
	FakeCrowd::MergeGroup(GroupHandle from, GroupHandle into)
	{
		if (from == into)
			core::throw_runtime_error("A group cannot be merged into itself");
		const auto& source = GetGroup(from);
		auto&       target = GetGroup(into);
		if (source.agentType != target.agentType)
		{
			core::throw_runtime_error(
				"Agent types {} and {} cannot share a group",
				source.agentType,
				target.agentType);
		}

		target.agentCount += source.agentCount;
		m_Groups.release_slot(from.handle);
	}

	bool
	FakeCrowd::HasGroup(GroupHandle group) const noexcept
	{
		return m_Groups.valid(group.handle);
	}

	uint32_t
	FakeCrowd::GetAgentCount(GroupHandle group) const
	{
		return GetGroup(group).agentCount;
	}

	uint64_t
	FakeCrowd::Step()
	{
		if (!CanStep())
			core::throw_runtime_error("{} ticks are already in flight", m_InFlight.size());

		++m_SubmittedTick;
		auto measurements = std::vector<FakeMeasurement>();
		for (uint32_t index = 0; index < m_Groups.capacity(); ++index)
		{
			if (!m_Groups.allocated(index))
				continue;
			const auto& group   = m_Groups[index];
			auto        report  = GroupReport();
			report.tick         = m_SubmittedTick;
			report.agentCount   = group.agentCount;
			report.meanPosition = group.orders.goal;
			report.meanFacing   = glm::normalize(group.orders.facing);
			measurements.push_back(
				{ .group  = GroupHandle{ .handle = { index, m_Groups.generation(index) } },
			      .report = report });
		}
		m_InFlight.push_back(std::move(measurements));
		return m_SubmittedTick;
	}

	uint64_t
	FakeCrowd::GetSubmittedTick() const noexcept
	{
		return m_SubmittedTick;
	}

	uint64_t
	FakeCrowd::GetCompletedTick() const noexcept
	{
		return m_SubmittedTick - m_InFlight.size();
	}

	void
	FakeCrowd::Wait() noexcept
	{
		while (!m_InFlight.empty()) CompleteTick();
	}

	std::optional<GroupReport>
	FakeCrowd::GetReport(GroupHandle group) const
	{
		static_cast<void>(GetGroup(group));
		for (const auto& measurement : m_Completed)
		{
			if (measurement.group == group)
				return measurement.report;
		}
		return std::nullopt;
	}

	void
	FakeCrowd::CompleteTick()
	{
		if (m_InFlight.empty())
			core::throw_runtime_error("No tick is in flight");
		m_Completed = std::move(m_InFlight.front());
		m_InFlight.pop_front();
	}

	const FakeGroup&
	FakeCrowd::GetGroup(GroupHandle group) const
	{
		if (!m_Groups.valid(group.handle))
			core::throw_runtime_error(
				"The crowd holds no group {}:{}",
				group.handle.index,
				group.handle.generation);
		return m_Groups[group.handle];
	}

	FakeGroup&
	FakeCrowd::GetGroup(GroupHandle group)
	{
		if (!m_Groups.valid(group.handle))
			core::throw_runtime_error(
				"The crowd holds no group {}:{}",
				group.handle.index,
				group.handle.generation);
		return m_Groups[group.handle];
	}
}
