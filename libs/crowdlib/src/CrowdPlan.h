#pragma once
#include "idl/AgentRange.h"
#include "idl/Group.h"
#include "idl/TickParams.h"
#include <core/containers/slot_vector.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/ObstacleSegment.h>
#include <cstdint>
#include <span>
#include <vector>

namespace crowd
{
	/**
	 * What one tick uploads: a Group record per row, and the ranges that lay its agents out from the
	 * previous tick's buffer.
	 */
	struct TickUploads
	{
		idl::TickParams              params{};
		std::vector<idl::Group>      groups;
		std::vector<idl::AgentRange> ranges;

		// Not uploaded: groupHandles[row] is the group behind groups[row], which a report row and a
		// debug readback's range are matched to.
		std::vector<GroupHandle> groupHandles;

		// Not uploaded: the tick's agents of each type, in CrowdDesc::agentTypes order -- the
		// lengths of the runs its render records are grouped into.
		std::vector<uint32_t> typeCounts;
	};

	/** A run of a group's agents in slot order: spawned, or read from the previous tick's buffer. */
	struct PlannedPiece
	{
		uint32_t sourceFirstAgent = 0;
		uint32_t agentCount       = 0;
	};

	/** Where one group's agents and render records were in the last plan. */
	struct LaidOutGroup
	{
		uint32_t firstAgent  = 0;
		uint32_t agentCount  = 0;
		uint32_t firstRecord = 0;
	};

	struct PlannedGroup
	{
		uint32_t                  agentType  = 0;
		uint32_t                  agentCount = 0;
		GroupOrders               orders;
		std::vector<PlannedPiece> pieces;
	};

	/**
	 * The CPU half of a crowd: its groups, the commands issued since the last tick, and the layout
	 * each tick's agents take. Every refusal ICrowd documents is made here, as the command is issued.
	 *
	 * A group's agents are a list of pieces in slot order, each spawned or read from a run of the
	 * previous tick's buffer; the commands move pieces between groups, and PlanTick lays the groups
	 * out contiguously, one AgentRange per piece.
	 */
	class CrowdPlan
	{
	public:
		/** @throws std::runtime_error for a description a crowd refuses. */
		explicit CrowdPlan(CrowdDesc desc);

		[[nodiscard]] const CrowdDesc&
		GetDesc() const noexcept;

		/** @throws std::runtime_error as ICrowd::CreateGroup. */
		GroupHandle
		CreateGroup(const GroupDesc& desc);

		/** @throws std::runtime_error as ICrowd::DestroyGroup. */
		void
		DestroyGroup(GroupHandle group);

		/** @throws std::runtime_error as ICrowd::SetOrders. */
		void
		SetOrders(GroupHandle group, const GroupOrders& orders);

		/** @throws std::runtime_error as ICrowd::SplitGroup. */
		GroupHandle
		SplitGroup(GroupHandle group, uint32_t agentCount);

		/** @throws std::runtime_error as ICrowd::MergeGroup. */
		void
		MergeGroup(GroupHandle from, GroupHandle into);

		/** @throws std::runtime_error as ICrowd::SetObstacles. */
		void
		SetObstacles(std::span<const ObstacleSegment> segments);

		[[nodiscard]] bool
		HasGroup(GroupHandle group) const noexcept;

		/** @throws std::runtime_error as ICrowd::GetAgentCount. */
		[[nodiscard]] uint32_t
		GetAgentCount(GroupHandle group) const;

		[[nodiscard]] uint32_t
		GetTotalAgentCount() const noexcept;

		[[nodiscard]] std::span<const ObstacleSegment>
		GetObstacles() const noexcept;

		/**
		 * Applies every command issued since the last call: the returned ranges read the buffer the
		 * last plan laid out, and the next plan reads the one this lays out.
		 */
		[[nodiscard]] TickUploads
		PlanTick();

	private:
		[[nodiscard]] const PlannedGroup&
		GetGroup(GroupHandle group) const;

		[[nodiscard]] PlannedGroup&
		GetGroup(GroupHandle group);

		GroupHandle
		AddGroup(PlannedGroup group);

		/** The last plan's render record of its agent `agent`. */
		[[nodiscard]] uint32_t
		LastRecordOf(uint32_t agent) const;

		CrowdDesc                       m_Desc;
		core::slot_vector<PlannedGroup> m_Groups;
		uint32_t                        m_AgentCount = 0;
		std::vector<ObstacleSegment>    m_Obstacles;

		// The last plan's groups in agent order, which this plan's sources are found in.
		std::vector<LaidOutGroup> m_LastLayout;
	};
}
