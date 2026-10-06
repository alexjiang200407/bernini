#include "FakeCrowd.h"
#include "formation.h"
#include <bgpu/cmd/QueuePoint.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <core/math.h>
#include <crowdlib/AgentType.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/GroupReport.h>
#include <crowdlib/ObstacleSegment.h>
#include <crowdlib/RenderAgent.h>  // IWYU pragma: keep
#include <crowdlib/RenderTick.h>
#include <crowdlib/SolverDesc.h>
#include <crowdlib/debug/CrowdReadback.h>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace crowd::test
{
	namespace
	{
		void
		ValidateSolver(const SolverDesc& solver)
		{
			if (solver.iterations == 0)
				core::throw_runtime_error("A solver needs at least one iteration");
			if (!(solver.velocityInertia >= 0.0f && solver.velocityInertia < 1.0f))
				core::throw_runtime_error("A solver's velocity inertia must be in [0, 1)");
			if (!std::isfinite(solver.avoidanceHorizon) || solver.avoidanceHorizon < 0.0f)
				core::throw_runtime_error(
					"A solver's avoidance horizon must be finite and not negative");
			if (!core::is_unit_interval(solver.avoidanceStiffness) ||
			    !core::is_unit_interval(solver.cohesionStiffness))
				core::throw_runtime_error("A solver's stiffnesses must be in [0, 1]");
		}

		void
		ValidateOrders(const GroupOrders& orders)
		{
			if (!core::is_finite(orders.goal))
				core::throw_runtime_error("A group's goal must be finite");
			if (!core::is_finite_positive(glm::length(orders.facing)))
				core::throw_runtime_error("A group's facing must be finite and not zero");
			if (orders.formation.frontage == 0 ||
			    !core::is_finite_positive(orders.formation.spacing))
				core::throw_runtime_error("A formation needs a positive frontage and spacing");
			if (!core::is_finite_positive(orders.pace))
				core::throw_runtime_error("A group's pace must be positive");
		}
	}

	FakeCrowd::FakeCrowd(CrowdDesc desc) : m_Desc(std::move(desc))
	{
		if (m_Desc.agentTypes.empty())
			core::throw_runtime_error("A crowd needs at least one agent type");
		for (const auto& type : m_Desc.agentTypes)
		{
			if (!core::is_finite_positive(type.radius) ||
			    !core::is_finite_positive(type.preferredSpeed) ||
			    !core::is_finite_positive(type.maxSpeed) || !core::is_finite_positive(type.mass))
				core::throw_runtime_error("Every field of an agent type must be positive");
			if (type.preferredSpeed > type.maxSpeed)
				core::throw_runtime_error("An agent type's preferred speed exceeds its maximum");
		}
		if (m_Desc.maxAgents == 0 || m_Desc.maxGroups == 0 || m_Desc.maxTicksInFlight == 0)
			core::throw_runtime_error("A crowd's capacities must be positive");
		if (!core::is_finite_positive(m_Desc.tickSeconds))
			core::throw_runtime_error("A crowd's tick must be positive");
		if (m_Desc.renderRingTicks != 0 && m_Desc.renderRingTicks < m_Desc.maxTicksInFlight + 3)
			core::throw_runtime_error("A render ring holds at least maxTicksInFlight + 3 ticks");
		if (uint64_t{ m_Desc.renderRingTicks } * m_Desc.maxAgents >
		    std::numeric_limits<uint32_t>::max())
			core::throw_runtime_error("A render ring's records must be indexable by a uint");
		ValidateSolver(m_Desc.solver);

		m_Groups.reset(m_Desc.maxGroups);
		m_RenderTicks.resize(m_Desc.renderRingTicks);
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

	void
	FakeCrowd::SetObstacles(std::span<const ObstacleSegment> segments)
	{
		if (segments.size() > m_Desc.maxObstacleSegments)
		{
			core::throw_runtime_error(
				"{} obstacle segments exceed the crowd's {}",
				segments.size(),
				m_Desc.maxObstacleSegments);
		}
		for (const auto& segment : segments)
		{
			if (!core::is_finite(segment.from) || !core::is_finite(segment.to))
				core::throw_runtime_error("An obstacle segment's ends must be finite");
		}
		m_Obstacles.assign(segments.begin(), segments.end());
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
		{
			if (m_InFlight.size() >= m_Desc.maxTicksInFlight)
				core::throw_runtime_error("{} ticks are already in flight", m_InFlight.size());
			core::throw_runtime_error(
				"The render ring's reader has not released tick {}",
				m_SubmittedTick + 1 - m_Desc.renderRingTicks);
		}

		++m_SubmittedTick;
		auto tick = FakeTick();
		for (uint32_t index = 0; index < m_Groups.capacity(); ++index)
		{
			if (!m_Groups.allocated(index))
				continue;
			const auto& group   = m_Groups[index];
			const auto  handle  = GroupHandle{ .handle = { index, m_Groups.generation(index) } };
			const auto  front   = glm::normalize(group.orders.facing);
			auto        report  = GroupReport();
			report.tick         = m_SubmittedTick;
			report.agentCount   = group.agentCount;
			report.meanPosition = group.orders.goal;
			report.meanFacing   = front;
			tick.measurements.push_back({ .group = handle, .report = report });

			if (!m_Desc.debugAgentReadback)
				continue;
			tick.groups.push_back(
				{ .group = handle,
			      .first = static_cast<uint32_t>(tick.agents.size()),
			      .count = group.agentCount });
			for (uint32_t slot = 0; slot < group.agentCount; ++slot)
			{
				tick.agents.push_back(
					{ .position = SlotPosition(group.orders, group.agentCount, slot),
				      .facing   = front });
			}
		}
		m_InFlight.push_back(std::move(tick));

		if (!m_RenderTicks.empty())
		{
			const auto ring    = static_cast<uint32_t>(m_RenderTicks.size());
			auto       written = RenderTick{
				.tick = m_SubmittedTick,
				.firstRecordIndex =
					static_cast<uint32_t>(m_SubmittedTick % ring) * m_Desc.maxAgents,
				.agentCount = m_AgentCount,
			};
			written.types.resize(m_Desc.agentTypes.size());
			for (uint32_t index = 0; index < m_Groups.capacity(); ++index)
			{
				if (m_Groups.allocated(index))
					written.types[m_Groups[index].agentType].count += m_Groups[index].agentCount;
			}
			uint32_t first = written.firstRecordIndex;
			for (auto& type : written.types)
			{
				type.firstRecordIndex = first;
				first += type.count;
			}
			m_RenderTicks[m_SubmittedTick % ring] = std::move(written);
		}
		return m_SubmittedTick;
	}

	bgpu::NativeBufferDesc
	FakeCrowd::GetRenderRing() const
	{
		if (m_RenderTicks.empty())
			core::throw_runtime_error("The crowd was created without a render ring");
		return bgpu::NativeBufferDesc().SetBuffer(
			bgpu::StructBufferDesc().SetElement<RenderAgent>().SetElementCount(
				m_Desc.renderRingTicks * m_Desc.maxAgents));
	}

	std::optional<RenderTick>
	FakeCrowd::GetRenderTick(uint64_t tick) const
	{
		if (m_RenderTicks.empty())
			core::throw_runtime_error("The crowd was created without a render ring");
		const uint64_t ring = m_RenderTicks.size();
		if (tick == 0 || tick > m_SubmittedTick || tick + ring <= m_SubmittedTick)
			return std::nullopt;
		return m_RenderTicks[tick % ring];
	}

	void
	FakeCrowd::ReleaseRenderReads(uint64_t throughTick, const bgpu::QueuePoint& readerDone)
	{
		if (m_RenderTicks.empty())
			core::throw_runtime_error("The crowd was created without a render ring");
		if (readerDone.IsNull())
			core::throw_runtime_error("ReleaseRenderReads needs the reader's queue point");
		if (throughTick > m_SubmittedTick || throughTick < m_ReleasedRenderTick)
			core::throw_runtime_error(
				"ReleaseRenderReads through tick {} is out of order",
				throughTick);
		m_ReleasedRenderTick = throughTick;
	}

	uint64_t
	FakeCrowd::GetReleasedRenderTick() const noexcept
	{
		return m_ReleasedRenderTick;
	}

	std::optional<float>
	FakeCrowd::GetTickGpuMilliseconds(uint64_t) const
	{
		return std::nullopt;
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
		for (const auto& measurement : m_Completed.measurements)
		{
			if (measurement.group == group)
				return measurement.report;
		}
		return std::nullopt;
	}

	std::optional<debug::CrowdReadback>
	FakeCrowd::ReadDebugAgents() const
	{
		if (!m_Desc.debugAgentReadback)
			core::throw_runtime_error("The crowd was created without its debug agent readback");
		if (GetCompletedTick() == 0)
			return std::nullopt;
		return debug::CrowdReadback{ .tick   = GetCompletedTick(),
			                         .agents = m_Completed.agents,
			                         .groups = m_Completed.groups };
	}

	void
	FakeCrowd::CompleteTick()
	{
		if (m_InFlight.empty())
			core::throw_runtime_error("No tick is in flight");
		m_Completed = std::move(m_InFlight.front());
		m_InFlight.pop_front();
	}

	std::span<const ObstacleSegment>
	FakeCrowd::GetObstacles() const noexcept
	{
		return m_Obstacles;
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
