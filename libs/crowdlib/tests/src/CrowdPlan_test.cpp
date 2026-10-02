// CrowdPlan, the CPU half of the GPU crowd: what each tick uploads for the commands issued before
// it. Every case checks the plan's one invariant -- the ranges lay out every agent exactly once,
// group by group, each group's slots in order -- and then what the commands did to the sources.
#include "CrowdPlan.h"
#include "idl/AgentRange.h"
#include "idl/Constants.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <core/glm.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/ObstacleSegment.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
	crowd::CrowdDesc
	Desc()
	{
		auto desc       = crowd::CrowdDesc();
		desc.agentTypes = {
			{ .radius = 0.3f, .preferredSpeed = 1.2f, .maxSpeed = 1.5f, .mass = 1.0f },
			{ .radius = 0.6f, .preferredSpeed = 2.0f, .maxSpeed = 3.0f, .mass = 4.0f },
		};
		desc.maxAgents           = 100;
		desc.maxGroups           = 4;
		desc.maxObstacleSegments = 2;
		return desc;
	}

	crowd::GroupOrders
	Orders(glm::vec2 goal = glm::vec2(0.0f))
	{
		return { .goal      = goal,
			     .facing    = glm::vec2(0.0f, 2.0f),
			     .formation = { .frontage = 4, .spacing = 1.0f },
			     .pace      = 0.5f };
	}

	crowd::GroupDesc
	Group(uint32_t agentCount, uint32_t agentType = 0)
	{
		return { .agentType = agentType, .agentCount = agentCount, .orders = Orders() };
	}

	/** The plan's invariant: every agent laid out once, group by group, slots in order. */
	void
	CheckLayout(const crowd::TickUploads& plan)
	{
		REQUIRE(plan.groups.size() == plan.groupHandles.size());
		CHECK(plan.params.groupCount == plan.groups.size());
		CHECK(plan.params.agentRangeCount == plan.ranges.size());

		uint32_t next = 0;
		auto     slot = std::vector<uint32_t>(plan.groups.size(), 0);
		for (const auto& range : plan.ranges)
		{
			REQUIRE(range.group < plan.groups.size());
			const auto& group = plan.groups[range.group];
			CHECK(range.firstAgent == next);
			CHECK(range.firstSlot == slot[range.group]);
			CHECK(range.firstAgent == group.firstAgent + range.firstSlot);
			CHECK(range.agentCount > 0);
			slot[range.group] += range.agentCount;
			next += range.agentCount;
		}
		CHECK(next == plan.params.agentCount);
		for (uint32_t row = 0; row < plan.groups.size(); ++row)
			CHECK(slot[row] == plan.groups[row].agentCount);
	}

	struct ModelGroup
	{
		crowd::GroupHandle    handle;
		std::vector<uint32_t> ids;
	};

	uint32_t
	RowOf(const crowd::TickUploads& plan, crowd::GroupHandle group)
	{
		const auto row = std::ranges::find(plan.groupHandles, group);
		REQUIRE(row != plan.groupHandles.end());
		return static_cast<uint32_t>(row - plan.groupHandles.begin());
	}

	std::vector<crowd::idl::AgentRange>
	RangesOf(const crowd::TickUploads& plan, crowd::GroupHandle group)
	{
		const uint32_t row    = RowOf(plan, group);
		auto           ranges = std::vector<crowd::idl::AgentRange>();
		for (const auto& range : plan.ranges)
		{
			if (range.group == row)
				ranges.push_back(range);
		}
		return ranges;
	}
}

TEST_CASE("A created group is spawned whole, and read back from its own range after", "[crowdplan]")
{
	auto       plan  = crowd::CrowdPlan(Desc());
	const auto group = plan.CreateGroup(Group(10));

	const auto first = plan.PlanTick();
	CheckLayout(first);
	REQUIRE(first.ranges.size() == 1);
	CHECK(first.ranges[0].sourceFirstAgent == crowd::idl::c_SpawnSource);
	CHECK(first.ranges[0].agentCount == 10);
	CHECK(first.groupHandles[0] == group);

	const auto record = first.groups[0];
	CHECK(record.front == glm::vec2(0.0f, 1.0f));
	CHECK(record.speed == 1.2f * 0.5f);
	CHECK(record.maxSpeed == 1.5f);
	CHECK(record.frontage == 4);
	CHECK(first.params.tickSeconds == Desc().tickSeconds);
	CHECK(first.params.velocityInertia == Desc().solver.velocityInertia);

	const auto second = plan.PlanTick();
	CheckLayout(second);
	REQUIRE(second.ranges.size() == 1);
	CHECK(second.ranges[0].sourceFirstAgent == 0);
	CHECK(second.ranges[0].agentCount == 10);
}

TEST_CASE("A split takes the rear of the formation, and its slots start again", "[crowdplan]")
{
	auto       plan  = crowd::CrowdPlan(Desc());
	const auto front = plan.CreateGroup(Group(10));
	static_cast<void>(plan.PlanTick());

	const auto rear = plan.SplitGroup(front, 3);
	CHECK(plan.GetAgentCount(front) == 7);
	CHECK(plan.GetAgentCount(rear) == 3);

	const auto tick = plan.PlanTick();
	CheckLayout(tick);
	const auto kept = RangesOf(tick, front);
	REQUIRE(kept.size() == 1);
	CHECK(kept[0].sourceFirstAgent == 0);
	CHECK(kept[0].agentCount == 7);
	const auto detached = RangesOf(tick, rear);
	REQUIRE(detached.size() == 1);
	CHECK(detached[0].sourceFirstAgent == 7);
	CHECK(detached[0].agentCount == 3);
	CHECK(detached[0].firstSlot == 0);
	CHECK(tick.groups[RowOf(tick, rear)].speed == tick.groups[RowOf(tick, front)].speed);
}

TEST_CASE("A split before any tick spawns both halves", "[crowdplan]")
{
	auto       plan  = crowd::CrowdPlan(Desc());
	const auto front = plan.CreateGroup(Group(10));
	const auto rear  = plan.SplitGroup(front, 4);

	const auto tick = plan.PlanTick();
	CheckLayout(tick);
	for (const auto& range : tick.ranges)
		CHECK(range.sourceFirstAgent == crowd::idl::c_SpawnSource);
	CHECK(tick.groups[RowOf(tick, front)].agentCount == 6);
	CHECK(tick.groups[RowOf(tick, rear)].agentCount == 4);
}

TEST_CASE("A merge appends every agent of one group to the rear of the other", "[crowdplan]")
{
	auto       plan      = crowd::CrowdPlan(Desc());
	const auto into      = plan.CreateGroup(Group(5));
	const auto from      = plan.CreateGroup(Group(3));
	const auto before    = plan.PlanTick();
	const auto fromFirst = before.groups[RowOf(before, from)].firstAgent;

	plan.MergeGroup(from, into);
	CHECK_FALSE(plan.HasGroup(from));
	CHECK(plan.GetAgentCount(into) == 8);

	const auto tick = plan.PlanTick();
	CheckLayout(tick);
	REQUIRE(tick.groupHandles.size() == 1);
	const auto ranges = RangesOf(tick, into);
	REQUIRE_FALSE(ranges.empty());
	CHECK(ranges.back().firstSlot + ranges.back().agentCount == 8);
	// The merged agents were laid out right after `into`'s, so the two runs read as one.
	REQUIRE(ranges.size() == 1);
	CHECK(fromFirst == 5);
}

TEST_CASE("A split merged straight back reads as it was", "[crowdplan]")
{
	auto       plan  = crowd::CrowdPlan(Desc());
	const auto group = plan.CreateGroup(Group(12));
	static_cast<void>(plan.PlanTick());

	plan.MergeGroup(plan.SplitGroup(group, 5), group);
	const auto tick = plan.PlanTick();
	CheckLayout(tick);
	REQUIRE(tick.ranges.size() == 1);
	CHECK(tick.ranges[0].sourceFirstAgent == 0);
	CHECK(tick.ranges[0].agentCount == 12);
}

TEST_CASE("Moves between groups keep every agent's source", "[crowdplan]")
{
	auto       plan = crowd::CrowdPlan(Desc());
	const auto a    = plan.CreateGroup(Group(6));
	const auto b    = plan.CreateGroup(Group(4));
	static_cast<void>(plan.PlanTick());

	// a: agents 0..5, b: 6..9. Split two off a's rear (4, 5), merge them into b, then b into a.
	plan.MergeGroup(plan.SplitGroup(a, 2), b);
	plan.MergeGroup(b, a);
	const auto c = plan.CreateGroup(Group(1));

	const auto tick = plan.PlanTick();
	CheckLayout(tick);
	auto sources = std::vector<uint32_t>();
	for (const auto& range : RangesOf(tick, a))
	{
		for (uint32_t i = 0; i < range.agentCount; ++i)
			sources.push_back(range.sourceFirstAgent + i);
	}
	CHECK(sources == std::vector<uint32_t>{ 0, 1, 2, 3, 6, 7, 8, 9, 4, 5 });
	REQUIRE(RangesOf(tick, c).size() == 1);
	CHECK(RangesOf(tick, c)[0].sourceFirstAgent == crowd::idl::c_SpawnSource);
}

TEST_CASE("A destroyed group's agents are dropped and the rest close up", "[crowdplan]")
{
	auto       plan       = crowd::CrowdPlan(Desc());
	const auto gone       = plan.CreateGroup(Group(5));
	const auto stays      = plan.CreateGroup(Group(3));
	const auto before     = plan.PlanTick();
	const auto staysFirst = before.groups[RowOf(before, stays)].firstAgent;

	plan.DestroyGroup(gone);
	CHECK_FALSE(plan.HasGroup(gone));
	CHECK(plan.GetTotalAgentCount() == 3);
	CHECK_THROWS_AS(plan.GetAgentCount(gone), std::runtime_error);

	const auto tick = plan.PlanTick();
	CheckLayout(tick);
	REQUIRE(tick.ranges.size() == 1);
	CHECK(tick.ranges[0].firstAgent == 0);
	CHECK(tick.ranges[0].sourceFirstAgent == staysFirst);
}

TEST_CASE("New orders reach the next plan's group record", "[crowdplan]")
{
	auto       plan  = crowd::CrowdPlan(Desc());
	const auto group = plan.CreateGroup(Group(4, 1));
	auto       moved = Orders(glm::vec2(10.0f, -3.0f));
	moved.pace       = 1.5f;
	moved.facing     = glm::vec2(-3.0f, 0.0f);
	plan.SetOrders(group, moved);

	const auto tick = plan.PlanTick();
	CHECK(tick.groups[0].goal == glm::vec2(10.0f, -3.0f));
	CHECK(tick.groups[0].front == glm::vec2(-1.0f, 0.0f));
	CHECK(tick.groups[0].speed == 2.0f * 1.5f);
	CHECK(tick.groups[0].maxSpeed == 3.0f);
}

TEST_CASE("The plan refuses what a crowd refuses, and is unchanged by it", "[crowdplan]")
{
	SECTION("A description with no type, no capacity or a bad solver")
	{
		auto noTypes       = Desc();
		noTypes.agentTypes = {};
		CHECK_THROWS_AS(crowd::CrowdPlan(noTypes), std::runtime_error);

		auto noAgents      = Desc();
		noAgents.maxAgents = 0;
		CHECK_THROWS_AS(crowd::CrowdPlan(noAgents), std::runtime_error);

		auto inertia                   = Desc();
		inertia.solver.velocityInertia = 1.0f;
		CHECK_THROWS_AS(crowd::CrowdPlan(inertia), std::runtime_error);
	}

	auto plan = crowd::CrowdPlan(Desc());

	SECTION("Groups and agents past the capacities")
	{
		CHECK_THROWS_AS(plan.CreateGroup(Group(101)), std::runtime_error);
		for (int i = 0; i < 4; ++i) static_cast<void>(plan.CreateGroup(Group(2)));
		CHECK_THROWS_AS(plan.CreateGroup(Group(1)), std::runtime_error);

		const auto full = plan.PlanTick();
		CHECK_THROWS_AS(plan.SplitGroup(full.groupHandles[0], 1), std::runtime_error);
		CHECK(plan.GetAgentCount(full.groupHandles[0]) == 2);
		CHECK(plan.GetTotalAgentCount() == 8);
	}

	SECTION("Bad orders, splits and merges")
	{
		const auto a   = plan.CreateGroup(Group(3));
		const auto b   = plan.CreateGroup(Group(3, 1));
		auto       bad = Orders();
		bad.facing     = glm::vec2(0.0f);
		CHECK_THROWS_AS(plan.SetOrders(a, bad), std::runtime_error);
		CHECK_THROWS_AS(plan.SplitGroup(a, 0), std::runtime_error);
		CHECK_THROWS_AS(plan.SplitGroup(a, 3), std::runtime_error);
		CHECK_THROWS_AS(plan.MergeGroup(a, a), std::runtime_error);
		CHECK_THROWS_AS(plan.MergeGroup(a, b), std::runtime_error);
		CHECK(plan.GetAgentCount(a) == 3);
		CHECK(plan.GetAgentCount(b) == 3);
	}

	SECTION("A released handle, before any tick has applied the release")
	{
		const auto a = plan.CreateGroup(Group(3));
		plan.DestroyGroup(a);
		CHECK_THROWS_AS(plan.SetOrders(a, Orders()), std::runtime_error);
		CHECK_THROWS_AS(plan.DestroyGroup(a), std::runtime_error);
		CheckLayout(plan.PlanTick());
	}

	SECTION("Obstacles past the capacity or not finite")
	{
		const auto three = std::vector<crowd::ObstacleSegment>(3);
		CHECK_THROWS_AS(plan.SetObstacles(three), std::runtime_error);
		const auto infinite = std::vector<crowd::ObstacleSegment>{
			{ .from = glm::vec2(0.0f),
			  .to   = glm::vec2(std::numeric_limits<float>::infinity(), 0.0f) }
		};
		CHECK_THROWS_AS(plan.SetObstacles(infinite), std::runtime_error);
		CHECK(plan.GetObstacles().empty());
	}
}

TEST_CASE("Any sequence of commands reads every agent from where it last was", "[crowdplan]")
{
	// A model that follows each agent by identity: a group is its agents' ids in slot order, and
	// each tick's buffer is the ids in the order the plan laid them out.
	auto random     = std::mt19937(GENERATE(1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u));
	auto desc       = Desc();
	desc.maxGroups  = 8;
	auto     plan   = crowd::CrowdPlan(desc);
	auto     model  = std::map<uint32_t, ModelGroup>();  // by slot index
	auto     buffer = std::vector<uint32_t>();
	auto     laid   = std::set<uint32_t>();
	uint32_t nextId = 0;

	const auto live = [&] {
		auto handles = std::vector<crowd::GroupHandle>();
		for (const auto& [index, group] : model) handles.push_back(group.handle);
		return handles;
	};
	const auto pick = [&](const std::vector<crowd::GroupHandle>& handles) {
		return handles[std::uniform_int_distribution<size_t>(0, handles.size() - 1)(random)];
	};

	for (int tick = 0; tick < 40; ++tick)
	{
		for (int command = 0; command < 3; ++command)
		{
			const auto handles = live();
			const int  kind    = std::uniform_int_distribution<int>(0, 3)(random);
			if (kind == 0 || handles.empty())
			{
				const uint32_t count = std::uniform_int_distribution<uint32_t>(1, 6)(random);
				if (handles.size() == desc.maxGroups || plan.GetTotalAgentCount() + count > 100)
					continue;
				const auto group = plan.CreateGroup(Group(count));
				auto&      entry = model[group.handle.index];
				entry            = { .handle = group, .ids = {} };
				for (uint32_t i = 0; i < count; ++i) entry.ids.push_back(nextId++);
			}
			else if (kind == 1)
			{
				const auto  group = pick(handles);
				const auto& ids   = model.at(group.handle.index).ids;
				if (ids.size() < 2 || handles.size() == desc.maxGroups)
					continue;
				const auto count = std::uniform_int_distribution<uint32_t>(
					1,
					static_cast<uint32_t>(ids.size()) - 1)(random);
				const auto rear          = plan.SplitGroup(group, count);
				auto&      from          = model.at(group.handle.index).ids;
				model[rear.handle.index] = { .handle = rear,
					                         .ids    = { from.end() - count, from.end() } };
				from.resize(from.size() - count);
			}
			else if (kind == 2 && handles.size() >= 2)
			{
				const auto from = pick(handles);
				const auto into = pick(handles);
				if (from == into)
					continue;
				plan.MergeGroup(from, into);
				auto&       target = model.at(into.handle.index).ids;
				const auto& source = model.at(from.handle.index).ids;
				target.insert(target.end(), source.begin(), source.end());
				model.erase(from.handle.index);
			}
			else if (kind == 3)
			{
				const auto group = pick(handles);
				plan.DestroyGroup(group);
				model.erase(group.handle.index);
			}
		}

		const auto step = plan.PlanTick();
		CheckLayout(step);
		auto next = std::vector<uint32_t>(step.params.agentCount);
		for (const auto& range : step.ranges)
		{
			const auto& entry = model.at(step.groupHandles[range.group].handle.index);
			REQUIRE(entry.handle == step.groupHandles[range.group]);
			const auto& ids = entry.ids;
			for (uint32_t i = 0; i < range.agentCount; ++i)
			{
				const uint32_t id = ids[range.firstSlot + i];
				if (range.sourceFirstAgent == crowd::idl::c_SpawnSource)
					CHECK_FALSE(laid.contains(id));
				else
					CHECK(buffer.at(range.sourceFirstAgent + i) == id);
				next[range.firstAgent + i] = id;
				laid.insert(id);
			}
		}
		buffer = std::move(next);
	}
}

TEST_CASE("A plan's render records run by type, and its sources are records", "[crowd][plan]")
{
	auto       plan = crowd::CrowdPlan(Desc());
	const auto foot = plan.CreateGroup(Group(3, 0));
	plan.CreateGroup(Group(2, 1));
	plan.CreateGroup(Group(4, 0));

	// Agents: foot 0-2, horse 3-4, rear 5-8. Records: foot 0-2, rear 3-6, horse 7-8.
	const auto first = plan.PlanTick();
	CheckLayout(first);
	CHECK(first.typeCounts == std::vector<uint32_t>{ 7, 2 });
	CHECK(first.groups[0].firstRecord == 0);
	CHECK(first.groups[1].firstRecord == 7);
	CHECK(first.groups[2].firstRecord == 3);
	for (const auto& range : first.ranges)
		CHECK(range.sourceFirstRecord == crowd::idl::c_SpawnSource);

	// Without foot: agents horse 0-1, rear 2-5; records rear 0-3, horse 4-5. Each range reads the
	// record its first agent had, not that agent's index.
	plan.DestroyGroup(foot);
	const auto second = plan.PlanTick();
	CheckLayout(second);
	CHECK(second.typeCounts == std::vector<uint32_t>{ 4, 2 });
	REQUIRE(second.ranges.size() == 2);
	CHECK(second.ranges[0].sourceFirstAgent == 3);
	CHECK(second.ranges[0].sourceFirstRecord == 7);
	CHECK(second.groups[0].firstRecord == 4);
	CHECK(second.ranges[1].sourceFirstAgent == 5);
	CHECK(second.ranges[1].sourceFirstRecord == 3);
	CHECK(second.groups[1].firstRecord == 0);
}
