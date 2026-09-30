// The GPU crowd's movement, read through the debug readback a tick at a time: each agent heads for
// its formation slot at its group's speed, blended with its last velocity and capped at its type's
// maximum, faces where it walks and its orders' front once it stands. What the contract suite cannot
// see, since the fake teleports every group to its goal.
#include "formation.h"
#include <algorithm>
#include <bgpu/GpuContext.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
#include <core/glm.h>
#include <crowdlib/AgentType.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/GroupReport.h>
#include <crowdlib/ICrowd.h>
#include <crowdlib/debug/AgentReadback.h>
#include <crowdlib/debug/CrowdReadback.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace
{
	constexpr float c_Tick = 1.0f / 30.0f;

	// Room for a float's rounding over the ticks a case runs; far below anything a bug would move.
	constexpr float c_Epsilon = 1e-3f;

	constexpr auto c_Infantry =
		crowd::AgentType{ .radius = 0.3f, .preferredSpeed = 1.2f, .maxSpeed = 1.5f, .mass = 80.0f };

	crowd::CrowdRef
	MakeCrowd(float velocityInertia = 0.01f, uint32_t maxAgents = 1000, uint32_t maxGroups = 8)
	{
		auto contextDesc             = bgpu::GpuContextDesc();
		contextDesc.enableDebugLayer = true;

		auto desc                   = crowd::CrowdDesc();
		desc.agentTypes             = { c_Infantry };
		desc.maxAgents              = maxAgents;
		desc.maxGroups              = maxGroups;
		desc.tickSeconds            = c_Tick;
		desc.solver.velocityInertia = velocityInertia;
		desc.debugAgentReadback     = true;
		return crowd::CreateCrowd(bgpu::CreateGpuContext(contextDesc), desc);
	}

	crowd::GroupOrders
	Orders(glm::vec2 goal, glm::vec2 facing = glm::vec2(0.0f, 1.0f), float pace = 1.0f)
	{
		return { .goal      = goal,
			     .facing    = facing,
			     .formation = { .frontage = 5, .spacing = 1.0f },
			     .pace      = pace };
	}

	/** Each agent's readback in slot order: a copy, so it outlives the next tick. */
	std::vector<crowd::debug::AgentReadback>
	AgentsOf(const crowd::ICrowd& crowd, crowd::GroupHandle group)
	{
		const auto readback = crowd.ReadDebugAgents();
		REQUIRE(readback.has_value());
		const auto range =
			std::ranges::find(readback->groups, group, &crowd::debug::GroupAgents::group);
		REQUIRE(range != readback->groups.end());
		const auto agents = readback->agents.subspan(range->first, range->count);
		return { agents.begin(), agents.end() };
	}

	void
	Tick(crowd::ICrowd& crowd)
	{
		crowd.Step();
		crowd.Wait();
	}
}

TEST_CASE("A group walks to its new goal and stands in its slots there", "[crowd][movement]")
{
	auto       crowd = MakeCrowd();
	const auto goal  = glm::vec2(6.0f, -4.0f);
	const auto group =
		crowd->CreateGroup({ .agentType = 0, .agentCount = 12, .orders = Orders(glm::vec2(0.0f)) });
	Tick(*crowd);

	const auto facing = glm::vec2(1.0f, 0.0f);
	crowd->SetOrders(group, Orders(goal, facing));
	// The farthest slot is under 10 world units away, at 1.2 per second: give it 10 seconds.
	for (int i = 0; i < static_cast<int>(10.0f / c_Tick); ++i) Tick(*crowd);

	const auto agents = AgentsOf(*crowd, group);
	REQUIRE(agents.size() == 12);
	// Ranks of 5, 5 and 2: the block is centred on the goal, but the short rear rank moves the mean
	// of its agents forward of it.
	auto mean = glm::vec2(0.0f);
	for (uint32_t slot = 0; slot < agents.size(); ++slot)
	{
		const auto expected = crowd::SlotPosition(Orders(goal, facing), 12, slot);
		mean += expected / 12.0f;
		CHECK_THAT(agents[slot].position.x, Catch::Matchers::WithinAbs(expected.x, c_Epsilon));
		CHECK_THAT(agents[slot].position.y, Catch::Matchers::WithinAbs(expected.y, c_Epsilon));
		CHECK_THAT(agents[slot].facing.x, Catch::Matchers::WithinAbs(1.0f, c_Epsilon));
	}

	const auto report = crowd->GetReport(group);
	REQUIRE(report.has_value());
	CHECK(report->agentCount == 12);
	CHECK_THAT(report->meanPosition.x, Catch::Matchers::WithinAbs(mean.x, c_Epsilon));
	CHECK_THAT(report->meanPosition.y, Catch::Matchers::WithinAbs(mean.y, c_Epsilon));
	CHECK_THAT(report->meanFacing.x, Catch::Matchers::WithinAbs(1.0f, c_Epsilon));
}

TEST_CASE("Agents walk at their group's speed, facing where they walk", "[crowd][movement]")
{
	const float pace  = GENERATE(0.25f, 1.0f);
	auto        crowd = MakeCrowd(0.0f);
	const auto  group =
		crowd->CreateGroup({ .agentType = 0, .agentCount = 5, .orders = Orders(glm::vec2(0.0f)) });
	Tick(*crowd);
	crowd->SetOrders(group, Orders(glm::vec2(0.0f, 50.0f), glm::vec2(0.0f, 1.0f), pace));

	auto before = AgentsOf(*crowd, group);
	for (int i = 0; i < 10; ++i)
	{
		Tick(*crowd);
		const auto after = AgentsOf(*crowd, group);
		for (uint32_t slot = 0; slot < after.size(); ++slot)
		{
			const auto step = after[slot].position - before[slot].position;
			CHECK_THAT(
				glm::length(step),
				Catch::Matchers::WithinAbs(c_Infantry.preferredSpeed * pace * c_Tick, 1e-5));
			CHECK_THAT(after[slot].facing.y, Catch::Matchers::WithinAbs(1.0f, 1e-5));
		}
		before = after;
	}
}

TEST_CASE("No agent outruns its type's maximum speed, whatever its pace", "[crowd][movement]")
{
	auto       crowd = MakeCrowd(0.0f);
	const auto group =
		crowd->CreateGroup({ .agentType = 0, .agentCount = 10, .orders = Orders(glm::vec2(0.0f)) });
	Tick(*crowd);
	crowd->SetOrders(group, Orders(glm::vec2(40.0f, 0.0f), glm::vec2(1.0f, 0.0f), 4.0f));

	auto before = AgentsOf(*crowd, group);
	for (int i = 0; i < 20; ++i)
	{
		Tick(*crowd);
		const auto after = AgentsOf(*crowd, group);
		for (uint32_t slot = 0; slot < after.size(); ++slot)
		{
			const float speed = glm::length(after[slot].position - before[slot].position) / c_Tick;
			CHECK(speed <= c_Infantry.maxSpeed + 1e-4f);
			CHECK(speed >= c_Infantry.maxSpeed - 1e-3f);
		}
		before = after;
	}
}

TEST_CASE("Inertia keeps a share of the last velocity when the orders turn", "[crowd][movement]")
{
	constexpr float c_Inertia = 0.5f;
	auto            crowd     = MakeCrowd(c_Inertia);
	const auto      group =
		crowd->CreateGroup({ .agentType = 0, .agentCount = 1, .orders = Orders(glm::vec2(0.0f)) });
	Tick(*crowd);

	// Standing: the first tick toward a far goal keeps half of a zero velocity.
	crowd->SetOrders(group, Orders(glm::vec2(100.0f, 0.0f)));
	const auto start = AgentsOf(*crowd, group)[0].position;
	Tick(*crowd);
	const auto first = AgentsOf(*crowd, group)[0].position;
	CHECK_THAT(
		(first - start).x / c_Tick,
		Catch::Matchers::WithinAbs((1.0f - c_Inertia) * c_Infantry.preferredSpeed, 1e-4));

	// Reversed: half of the old velocity cancels half of the new.
	for (int i = 0; i < 30; ++i) Tick(*crowd);
	crowd->SetOrders(group, Orders(glm::vec2(-100.0f, 0.0f)));
	const auto walking = AgentsOf(*crowd, group)[0].position;
	Tick(*crowd);
	const auto turned = AgentsOf(*crowd, group)[0].position;
	CHECK_THAT((turned - walking).x / c_Tick, Catch::Matchers::WithinAbs(0.0f, 1e-3));
}

TEST_CASE("A standing group faces its orders' front, not its last step", "[crowd][movement]")
{
	auto       crowd = MakeCrowd();
	const auto group =
		crowd->CreateGroup({ .agentType = 0, .agentCount = 3, .orders = Orders(glm::vec2(0.0f)) });
	Tick(*crowd);

	// Walk east, then stand facing north.
	crowd->SetOrders(group, Orders(glm::vec2(3.0f, 0.0f), glm::vec2(0.0f, 1.0f)));
	Tick(*crowd);
	for (const auto& agent : AgentsOf(*crowd, group))
		CHECK_THAT(agent.facing.x, Catch::Matchers::WithinAbs(1.0f, 1e-4));

	for (int i = 0; i < 90; ++i) Tick(*crowd);
	for (const auto& agent : AgentsOf(*crowd, group))
		CHECK_THAT(agent.facing.y, Catch::Matchers::WithinAbs(1.0f, 1e-6));
}

TEST_CASE("Split, merge and destroy move no agent further than a tick's walk", "[crowd][movement]")
{
	auto       crowd = MakeCrowd();
	const auto front =
		crowd->CreateGroup({ .agentType = 0, .agentCount = 20, .orders = Orders(glm::vec2(0.0f)) });
	const auto other = crowd->CreateGroup(
		{ .agentType = 0, .agentCount = 7, .orders = Orders(glm::vec2(10.0f, 0.0f)) });
	Tick(*crowd);

	const auto maxStep = c_Infantry.maxSpeed * c_Tick + 1e-5f;
	const auto before  = AgentsOf(*crowd, front);

	// The rear five, slots 15..19, become slots 0..4 of a group of their own.
	const auto rear = crowd->SplitGroup(front, 5);
	crowd->SetOrders(rear, Orders(glm::vec2(-10.0f, 0.0f)));
	Tick(*crowd);
	const auto kept     = AgentsOf(*crowd, front);
	const auto detached = AgentsOf(*crowd, rear);
	REQUIRE(kept.size() == 15);
	REQUIRE(detached.size() == 5);
	for (uint32_t slot = 0; slot < 15; ++slot)
		CHECK(glm::length(kept[slot].position - before[slot].position) <= maxStep);
	for (uint32_t slot = 0; slot < 5; ++slot)
		CHECK(glm::length(detached[slot].position - before[15 + slot].position) <= maxStep);

	// `other`'s seven follow `front`'s fifteen, in their slot order.
	const auto merging = AgentsOf(*crowd, other);
	crowd->MergeGroup(other, front);
	crowd->DestroyGroup(rear);
	Tick(*crowd);
	const auto merged = AgentsOf(*crowd, front);
	REQUIRE(merged.size() == 22);
	for (uint32_t slot = 0; slot < 15; ++slot)
		CHECK(glm::length(merged[slot].position - kept[slot].position) <= maxStep);
	for (uint32_t slot = 0; slot < 7; ++slot)
		CHECK(glm::length(merged[15 + slot].position - merging[slot].position) <= maxStep);

	const auto readback = crowd->ReadDebugAgents();
	REQUIRE(readback.has_value());
	CHECK(readback->agents.size() == 22);
	CHECK(readback->groups.size() == 1);
}

TEST_CASE("Reports arrive by polling, with ticks in flight and no wait", "[crowd][movement]")
{
	auto       crowd = MakeCrowd();
	const auto group =
		crowd->CreateGroup({ .agentType = 0, .agentCount = 50, .orders = Orders(glm::vec2(0.0f)) });

	while (crowd->CanStep()) crowd->Step();
	CHECK(crowd->GetSubmittedTick() == crowd->GetDesc().maxTicksInFlight);

	// Reads made while ticks are in flight answer at once, from whatever has completed.
	const auto early = crowd->GetReport(group);
	if (early.has_value())
		CHECK(early->tick <= crowd->GetCompletedTick());

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (crowd->GetCompletedTick() < crowd->GetSubmittedTick())
		REQUIRE(std::chrono::steady_clock::now() < deadline);
	const auto report = crowd->GetReport(group);
	REQUIRE(report.has_value());
	CHECK(report->tick == crowd->GetSubmittedTick());
	CHECK(report->agentCount == 50);
}

TEST_CASE("A read made while newer ticks run holds the tick it names", "[crowd][movement]")
{
	// A group joins before every tick, so tick t holds t + 1 groups: a read that returned a newer
	// tick's rows under an older tick's number would miscount. The large group keeps each tick on
	// the GPU long enough for Steps to land while one is in flight.
	constexpr uint32_t c_Large = 150000;
	auto               crowd   = MakeCrowd(0.01f, c_Large + 100, 100);
	const auto         large   = crowd->CreateGroup(
		{ .agentType = 0, .agentCount = c_Large, .orders = Orders(glm::vec2(0.0f)) });

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (crowd->GetSubmittedTick() < 60 || crowd->GetCompletedTick() < 60)
	{
		REQUIRE(std::chrono::steady_clock::now() < deadline);
		if (crowd->GetSubmittedTick() < 60 && crowd->CanStep())
		{
			static_cast<void>(crowd->CreateGroup(
				{ .agentType = 0, .agentCount = 1, .orders = Orders(glm::vec2(20.0f)) }));
			crowd->Step();
		}

		const auto readback = crowd->ReadDebugAgents();
		if (!readback.has_value())
			continue;
		CHECK(readback->groups.size() == readback->tick + 1);
		CHECK(readback->agents.size() == c_Large + readback->tick);
		const auto report = crowd->GetReport(large);
		REQUIRE(report.has_value());
		CHECK(report->tick >= readback->tick);
		CHECK(report->agentCount == c_Large);
	}
}

TEST_CASE("The same commands give the same bits on one machine", "[crowd][movement]")
{
	const auto run = [] {
		auto       crowd = MakeCrowd(0.2f);
		const auto a     = crowd->CreateGroup(
			{ .agentType = 0, .agentCount = 37, .orders = Orders(glm::vec2(0.0f)) });
		const auto b = crowd->CreateGroup(
			{ .agentType = 0, .agentCount = 11, .orders = Orders(glm::vec2(5.0f, 5.0f)) });
		auto frames = std::vector<std::vector<crowd::debug::AgentReadback>>();
		auto sums   = std::vector<crowd::GroupReport>();
		for (int i = 0; i < 60; ++i)
		{
			if (i == 5)
				crowd->SetOrders(a, Orders(glm::vec2(-8.0f, 3.0f), glm::vec2(1.0f, 1.0f), 0.7f));
			if (i == 20)
				crowd->MergeGroup(crowd->SplitGroup(a, 9), b);
			Tick(*crowd);
			const auto readback = crowd->ReadDebugAgents();
			frames.emplace_back(readback->agents.begin(), readback->agents.end());
			sums.push_back(*crowd->GetReport(b));
		}
		return std::pair(frames, sums);
	};

	const auto [firstFrames, firstSums]   = run();
	const auto [secondFrames, secondSums] = run();
	REQUIRE(firstFrames.size() == secondFrames.size());
	for (size_t i = 0; i < firstFrames.size(); ++i)
	{
		REQUIRE(firstFrames[i].size() == secondFrames[i].size());
		CHECK(
			std::memcmp(
				firstFrames[i].data(),
				secondFrames[i].data(),
				firstFrames[i].size() * sizeof(crowd::debug::AgentReadback)) == 0);
		CHECK(
			std::memcmp(
				&firstSums[i].meanPosition,
				&secondSums[i].meanPosition,
				sizeof(glm::vec2)) == 0);
		CHECK(
			std::memcmp(&firstSums[i].meanFacing, &secondSums[i].meanFacing, sizeof(glm::vec2)) ==
			0);
	}
}
