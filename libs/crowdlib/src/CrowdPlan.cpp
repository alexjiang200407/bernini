#include "CrowdPlan.h"
#include "idl/AgentRange.h"
#include "idl/Constants.h"
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <crowdlib/AgentType.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/ObstacleSegment.h>
#include <crowdlib/SolverDesc.h>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace crowd
{
	namespace
	{
		bool
		IsPositive(float value) noexcept
		{
			return std::isfinite(value) && value > 0.0f;
		}

		bool
		IsFinite(glm::vec2 value) noexcept
		{
			return std::isfinite(value.x) && std::isfinite(value.y);
		}

		bool
		IsUnitInterval(float value) noexcept
		{
			return value >= 0.0f && value <= 1.0f;
		}

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
			if (!IsUnitInterval(solver.avoidanceStiffness) ||
			    !IsUnitInterval(solver.cohesionStiffness))
				core::throw_runtime_error("A solver's stiffnesses must be in [0, 1]");
		}

		void
		ValidateOrders(const GroupOrders& orders)
		{
			if (!IsFinite(orders.goal))
				core::throw_runtime_error("A group's goal must be finite");
			if (!IsPositive(glm::length(orders.facing)))
				core::throw_runtime_error("A group's facing must be finite and not zero");
			if (orders.formation.frontage == 0 || !IsPositive(orders.formation.spacing))
				core::throw_runtime_error("A formation needs a positive frontage and spacing");
			if (!IsPositive(orders.pace))
				core::throw_runtime_error("A group's pace must be positive");
		}

		/** Takes the last `agentCount` agents off `pieces`, splitting the piece the cut falls in. */
		std::vector<PlannedPiece>
		TakeRear(std::vector<PlannedPiece>& pieces, uint32_t agentCount)
		{
			auto rear = std::vector<PlannedPiece>();
			while (agentCount > 0)
			{
				auto& last = pieces.back();
				if (last.agentCount <= agentCount)
				{
					agentCount -= last.agentCount;
					rear.push_back(last);
					pieces.pop_back();
					continue;
				}

				const uint32_t kept = last.agentCount - agentCount;
				rear.push_back(
					{ .sourceFirstAgent = last.sourceFirstAgent == idl::c_SpawnSource ?
				                              idl::c_SpawnSource :
				                              last.sourceFirstAgent + kept,
				      .agentCount       = agentCount });
				last.agentCount = kept;
				agentCount      = 0;
			}
			return { rear.rbegin(), rear.rend() };
		}

		/** Whether `piece` can extend `range`: both spawned, or read from adjacent runs. */
		bool
		Continues(const idl::AgentRange& range, const PlannedPiece& piece) noexcept
		{
			if (range.sourceFirstAgent == idl::c_SpawnSource ||
			    piece.sourceFirstAgent == idl::c_SpawnSource)
				return range.sourceFirstAgent == piece.sourceFirstAgent;
			return range.sourceFirstAgent + range.agentCount == piece.sourceFirstAgent;
		}
	}

	CrowdPlan::CrowdPlan(CrowdDesc desc) : m_Desc(std::move(desc))
	{
		if (m_Desc.agentTypes.empty())
			core::throw_runtime_error("A crowd needs at least one agent type");
		for (const auto& type : m_Desc.agentTypes)
		{
			if (!IsPositive(type.radius) || !IsPositive(type.preferredSpeed) ||
			    !IsPositive(type.maxSpeed) || !IsPositive(type.mass))
				core::throw_runtime_error("Every field of an agent type must be positive");
			if (type.preferredSpeed > type.maxSpeed)
				core::throw_runtime_error("An agent type's preferred speed exceeds its maximum");
		}
		if (m_Desc.maxAgents == 0 || m_Desc.maxGroups == 0 || m_Desc.maxTicksInFlight == 0)
			core::throw_runtime_error("A crowd's capacities must be positive");
		if (!IsPositive(m_Desc.tickSeconds))
			core::throw_runtime_error("A crowd's tick must be positive");
		ValidateSolver(m_Desc.solver);

		m_Groups.reset(m_Desc.maxGroups);
	}

	const CrowdDesc&
	CrowdPlan::GetDesc() const noexcept
	{
		return m_Desc;
	}

	GroupHandle
	CrowdPlan::CreateGroup(const GroupDesc& desc)
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

		const auto handle = AddGroup(
			PlannedGroup{ .agentType  = desc.agentType,
		                  .agentCount = desc.agentCount,
		                  .orders     = desc.orders,
		                  .pieces     = { { .sourceFirstAgent = idl::c_SpawnSource,
		                                    .agentCount       = desc.agentCount } } });
		m_AgentCount += desc.agentCount;
		return handle;
	}

	void
	CrowdPlan::DestroyGroup(GroupHandle group)
	{
		m_AgentCount -= GetGroup(group).agentCount;
		m_Groups.release_slot(group.handle);
	}

	void
	CrowdPlan::SetOrders(GroupHandle group, const GroupOrders& orders)
	{
		ValidateOrders(orders);
		GetGroup(group).orders = orders;
	}

	GroupHandle
	CrowdPlan::SplitGroup(GroupHandle group, uint32_t agentCount)
	{
		const auto& source = GetGroup(group);
		if (agentCount == 0 || agentCount >= source.agentCount)
		{
			core::throw_runtime_error(
				"Splitting {} of a group's {} agents would leave a group empty",
				agentCount,
				source.agentCount);
		}

		// Added before anything is taken, so a crowd with no group left to spare is unchanged.
		const auto detached = AddGroup(
			PlannedGroup{ .agentType  = source.agentType,
		                  .agentCount = agentCount,
		                  .orders     = source.orders });
		auto& remaining = GetGroup(group);
		remaining.agentCount -= agentCount;
		GetGroup(detached).pieces = TakeRear(remaining.pieces, agentCount);
		return detached;
	}

	void
	CrowdPlan::MergeGroup(GroupHandle from, GroupHandle into)
	{
		if (from == into)
			core::throw_runtime_error("A group cannot be merged into itself");
		auto& source = GetGroup(from);
		auto& target = GetGroup(into);
		if (source.agentType != target.agentType)
		{
			core::throw_runtime_error(
				"Agent types {} and {} cannot share a group",
				source.agentType,
				target.agentType);
		}

		target.agentCount += source.agentCount;
		target.pieces.insert(target.pieces.end(), source.pieces.begin(), source.pieces.end());
		m_Groups.release_slot(from.handle);
	}

	void
	CrowdPlan::SetObstacles(std::span<const ObstacleSegment> segments)
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
			if (!IsFinite(segment.from) || !IsFinite(segment.to))
				core::throw_runtime_error("An obstacle segment's ends must be finite");
		}
		m_Obstacles.assign(segments.begin(), segments.end());
	}

	bool
	CrowdPlan::HasGroup(GroupHandle group) const noexcept
	{
		return m_Groups.valid(group.handle);
	}

	uint32_t
	CrowdPlan::GetAgentCount(GroupHandle group) const
	{
		return GetGroup(group).agentCount;
	}

	uint32_t
	CrowdPlan::GetTotalAgentCount() const noexcept
	{
		return m_AgentCount;
	}

	std::span<const ObstacleSegment>
	CrowdPlan::GetObstacles() const noexcept
	{
		return m_Obstacles;
	}

	TickPlan
	CrowdPlan::PlanTick()
	{
		auto     plan       = TickPlan();
		uint32_t firstAgent = 0;
		for (uint32_t index = 0; index < m_Groups.capacity(); ++index)
		{
			if (!m_Groups.allocated(index))
				continue;
			auto&          group = m_Groups[index];
			const auto&    type  = m_Desc.agentTypes[group.agentType];
			const uint32_t row   = static_cast<uint32_t>(plan.groups.size());

			uint32_t firstSlot = 0;
			for (const auto& piece : group.pieces)
			{
				if (firstSlot > 0 && Continues(plan.ranges.back(), piece))
				{
					plan.ranges.back().agentCount += piece.agentCount;
					firstSlot += piece.agentCount;
					continue;
				}
				plan.ranges.push_back(
					{ .firstAgent       = firstAgent + firstSlot,
				      .sourceFirstAgent = piece.sourceFirstAgent,
				      .agentCount       = piece.agentCount,
				      .group            = row,
				      .firstSlot        = firstSlot });
				firstSlot += piece.agentCount;
			}

			plan.groups.push_back(
				{ .goal       = group.orders.goal,
			      .front      = glm::normalize(group.orders.facing),
			      .firstAgent = firstAgent,
			      .agentCount = group.agentCount,
			      .frontage   = group.orders.formation.frontage,
			      .spacing    = group.orders.formation.spacing,
			      .speed      = type.preferredSpeed * group.orders.pace,
			      .maxSpeed   = type.maxSpeed });
			plan.rows.push_back({ .handle = { index, m_Groups.generation(index) } });

			group.pieces = { { .sourceFirstAgent = firstAgent, .agentCount = group.agentCount } };
			firstAgent += group.agentCount;
		}

		plan.params = { .tickSeconds     = m_Desc.tickSeconds,
			            .velocityInertia = m_Desc.solver.velocityInertia,
			            .agentCount      = firstAgent,
			            .groupCount      = static_cast<uint32_t>(plan.groups.size()),
			            .agentRangeCount = static_cast<uint32_t>(plan.ranges.size()) };
		return plan;
	}

	const PlannedGroup&
	CrowdPlan::GetGroup(GroupHandle group) const
	{
		if (!m_Groups.valid(group.handle))
			core::throw_runtime_error(
				"The crowd holds no group {}:{}",
				group.handle.index,
				group.handle.generation);
		return m_Groups[group.handle];
	}

	PlannedGroup&
	CrowdPlan::GetGroup(GroupHandle group)
	{
		if (!m_Groups.valid(group.handle))
			core::throw_runtime_error(
				"The crowd holds no group {}:{}",
				group.handle.index,
				group.handle.generation);
		return m_Groups[group.handle];
	}

	GroupHandle
	CrowdPlan::AddGroup(PlannedGroup group)
	{
		const auto slot = m_Groups.try_allocate_and_emplace(std::move(group));
		if (slot.is_null())
			core::throw_runtime_error("The crowd already holds its {} groups", m_Desc.maxGroups);
		return GroupHandle{ .handle = slot };
	}
}
