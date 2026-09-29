// ICrowd's contract, run against every implementation in CrowdFactories: the handle lifetime, the
// capacities, the refusals, and that commands and reports move one fixed tick at a time. Only the
// fake implements it so far, so none of this proves anything about movement, a real queue's timing
// or GPU memory. The cases tagged [fake] need what only the fake can promise: a tick held in flight,
// or a group reported standing at its goal the tick it is ordered there.
#include "FakeCrowd.h"
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <core/ref/SharedRef.h>
#include <crowdlib/AgentType.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/ICrowd.h>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace
{
	struct FakeCrowdFactory
	{
		static crowd::CrowdRef
		Create(crowd::CrowdDesc desc)
		{
			return core::SharedRef<crowd::test::FakeCrowd>::Make(std::move(desc));
		}
	};

	using CrowdFactories = std::tuple<FakeCrowdFactory>;

	constexpr auto c_Infantry = crowd::AgentType{ .radius          = 0.3f,
		                                          .maxSpeed        = 1.5f,
		                                          .maxAcceleration = 3.0f,
		                                          .maxTurnRate     = 3.14f,
		                                          .mass            = 80.0f };
	constexpr auto c_Horse    = crowd::AgentType{ .radius          = 0.8f,
		                                          .maxSpeed        = 8.0f,
		                                          .maxAcceleration = 4.0f,
		                                          .maxTurnRate     = 1.5f,
		                                          .mass            = 500.0f };

	crowd::CrowdDesc
	MakeDesc(uint32_t maxAgents = 1000, uint32_t maxGroups = 8)
	{
		auto desc       = crowd::CrowdDesc();
		desc.agentTypes = { c_Infantry, c_Horse };
		desc.maxAgents  = maxAgents;
		desc.maxGroups  = maxGroups;
		return desc;
	}

	crowd::GroupOrders
	MakeOrders(glm::vec2 goal = glm::vec2(0.0f))
	{
		auto orders      = crowd::GroupOrders();
		orders.goal      = goal;
		orders.facing    = glm::vec2(0.0f, 2.0f);
		orders.formation = { .frontage = 10, .spacing = 1.0f };
		return orders;
	}

	crowd::GroupDesc
	MakeGroup(uint32_t agentCount, uint32_t agentType = 0)
	{
		return { .agentType = agentType, .agentCount = agentCount, .orders = MakeOrders() };
	}

	void
	CompleteOneTick(crowd::ICrowd& crowd)
	{
		crowd.Step();
		crowd.Wait();
	}
}

TEMPLATE_LIST_TEST_CASE(
	"A crowd refuses a description it cannot size or clock",
	"[crowd]",
	CrowdFactories)
{
	auto desc = MakeDesc();
	SECTION("no agent types") { desc.agentTypes.clear(); }
	SECTION("an agent type with a zero field") { desc.agentTypes[1].maxTurnRate = 0.0f; }
	SECTION("an agent type with a non-finite field")
	{
		desc.agentTypes[0].radius = std::numeric_limits<float>::infinity();
	}
	SECTION("no room for an agent") { desc.maxAgents = 0; }
	SECTION("no room for a group") { desc.maxGroups = 0; }
	SECTION("no tick in flight") { desc.maxTicksInFlight = 0; }
	SECTION("a zero tick") { desc.tickSeconds = 0.0f; }
	CHECK_THROWS_AS(TestType::Create(desc), std::runtime_error);
}

TEMPLATE_LIST_TEST_CASE(
	"A group reports nothing until a tick that includes it completes",
	"[crowd]",
	CrowdFactories)
{
	auto crowd = TestType::Create(MakeDesc());
	auto group = crowd->CreateGroup(MakeGroup(120));

	CHECK(crowd->HasGroup(group));
	CHECK(crowd->GetAgentCount(group) == 120);
	CHECK_FALSE(crowd->GetReport(group).has_value());

	const auto tick = crowd->Step();
	crowd->Wait();

	CHECK(crowd->GetCompletedTick() == tick);
	const auto report = crowd->GetReport(group);
	REQUIRE(report.has_value());
	CHECK(report->tick == tick);
	CHECK(report->agentCount == 120);
}

TEMPLATE_LIST_TEST_CASE(
	"A group created after a Step is not in that tick's reports",
	"[crowd]",
	CrowdFactories)
{
	auto crowd = TestType::Create(MakeDesc());
	auto early = crowd->CreateGroup(MakeGroup(10));
	crowd->Step();
	auto late = crowd->CreateGroup(MakeGroup(10));
	crowd->Wait();

	CHECK(crowd->GetReport(early).has_value());
	CHECK_FALSE(crowd->GetReport(late).has_value());

	CompleteOneTick(*crowd);
	CHECK(crowd->GetReport(late).has_value());
}

TEMPLATE_LIST_TEST_CASE("Steps submit consecutive ticks from 1", "[crowd]", CrowdFactories)
{
	auto crowd = TestType::Create(MakeDesc());
	CHECK(crowd->GetSubmittedTick() == 0);
	CHECK(crowd->GetCompletedTick() == 0);

	CHECK(crowd->Step() == 1);
	crowd->Wait();
	CHECK(crowd->Step() == 2);
	CHECK(crowd->GetSubmittedTick() == 2);
	crowd->Wait();
	CHECK(crowd->GetCompletedTick() == 2);
}

TEMPLATE_LIST_TEST_CASE(
	"A destroyed group's handle is refused by every call",
	"[crowd]",
	CrowdFactories)
{
	auto crowd = TestType::Create(MakeDesc());
	auto group = crowd->CreateGroup(MakeGroup(10));
	auto live  = crowd->CreateGroup(MakeGroup(10));
	CompleteOneTick(*crowd);
	crowd->DestroyGroup(group);

	CHECK_FALSE(crowd->HasGroup(group));
	CHECK_THROWS_AS(crowd->GetAgentCount(group), std::runtime_error);
	CHECK_THROWS_AS(crowd->GetReport(group), std::runtime_error);
	CHECK_THROWS_AS(crowd->SetOrders(group, MakeOrders()), std::runtime_error);
	CHECK_THROWS_AS(crowd->SplitGroup(group, 1), std::runtime_error);
	CHECK_THROWS_AS(crowd->MergeGroup(group, live), std::runtime_error);
	CHECK_THROWS_AS(crowd->MergeGroup(live, group), std::runtime_error);
	CHECK_THROWS_AS(crowd->DestroyGroup(group), std::runtime_error);
	CHECK(crowd->GetAgentCount(live) == 10);
}

TEMPLATE_LIST_TEST_CASE(
	"A handle stays refused after its slot is reused",
	"[crowd]",
	CrowdFactories)
{
	auto crowd = TestType::Create(MakeDesc(1000, 1));
	auto first = crowd->CreateGroup(MakeGroup(10));
	crowd->DestroyGroup(first);
	auto second = crowd->CreateGroup(MakeGroup(20));

	CHECK(second != first);
	CHECK_FALSE(crowd->HasGroup(first));
	CHECK_THROWS_AS(crowd->GetAgentCount(first), std::runtime_error);
	CHECK(crowd->GetAgentCount(second) == 20);
}

TEMPLATE_LIST_TEST_CASE(
	"A group in a reused slot never reports a tick measured before it existed",
	"[crowd]",
	CrowdFactories)
{
	auto crowd = TestType::Create(MakeDesc(1000, 1));
	auto first = crowd->CreateGroup(MakeGroup(10));
	crowd->Step();
	crowd->DestroyGroup(first);
	auto second = crowd->CreateGroup(MakeGroup(20));
	crowd->Wait();

	CHECK_FALSE(crowd->GetReport(second).has_value());
}

TEMPLATE_LIST_TEST_CASE(
	"A group past a capacity is refused, and a destroyed one frees its room",
	"[crowd]",
	CrowdFactories)
{
	auto crowd = TestType::Create(MakeDesc(100, 2));
	auto a     = crowd->CreateGroup(MakeGroup(60));

	CHECK_THROWS_AS(crowd->CreateGroup(MakeGroup(41)), std::runtime_error);
	auto b = crowd->CreateGroup(MakeGroup(40));
	CHECK_THROWS_AS(crowd->CreateGroup(MakeGroup(1)), std::runtime_error);

	crowd->DestroyGroup(a);
	CHECK(crowd->HasGroup(crowd->CreateGroup(MakeGroup(60))));
	CHECK(crowd->HasGroup(b));
}

TEMPLATE_LIST_TEST_CASE("A group the crowd cannot place is refused", "[crowd]", CrowdFactories)
{
	auto crowd = TestType::Create(MakeDesc());
	auto desc  = MakeGroup(10);
	SECTION("an agent type the crowd lacks") { desc.agentType = 2; }
	SECTION("no agents") { desc.agentCount = 0; }
	SECTION("a zero facing") { desc.orders.facing = glm::vec2(0.0f); }
	SECTION("a non-finite goal") { desc.orders.goal.x = std::numeric_limits<float>::quiet_NaN(); }
	SECTION("a formation with no frontage") { desc.orders.formation.frontage = 0; }
	SECTION("a formation with no spacing") { desc.orders.formation.spacing = 0.0f; }
	CHECK_THROWS_AS(crowd->CreateGroup(desc), std::runtime_error);
}

TEST_CASE("Invalid orders leave a group's orders as they were", "[crowd][fake]")
{
	auto crowd = core::SharedRef<crowd::test::FakeCrowd>::Make(MakeDesc());
	auto group = crowd->CreateGroup(MakeGroup(10));
	crowd->SetOrders(group, MakeOrders(glm::vec2(5.0f, 0.0f)));

	auto orders              = MakeOrders(glm::vec2(-30.0f, 7.0f));
	orders.formation.spacing = -1.0f;
	CHECK_THROWS_AS(crowd->SetOrders(group, orders), std::runtime_error);
	orders.formation.spacing = 1.0f;
	orders.facing            = glm::vec2(1e30f, 0.0f);
	CHECK_THROWS_AS(crowd->SetOrders(group, orders), std::runtime_error);

	CompleteOneTick(*crowd);
	CHECK(crowd->GetReport(group)->meanPosition == glm::vec2(5.0f, 0.0f));
}

TEST_CASE("A tick in flight hides its reports until it completes", "[crowd][fake]")
{
	auto crowd = core::SharedRef<crowd::test::FakeCrowd>::Make(MakeDesc());
	auto group = crowd->CreateGroup(MakeGroup(10));
	CompleteOneTick(*crowd);

	crowd->SetOrders(group, MakeOrders(glm::vec2(40.0f, -8.0f)));
	const auto tick = crowd->Step();
	CHECK(crowd->GetCompletedTick() == tick - 1);
	CHECK(crowd->GetReport(group)->meanPosition == glm::vec2(0.0f));

	crowd->CompleteTick();
	CHECK(crowd->GetReport(group)->tick == tick);
	CHECK(crowd->GetReport(group)->meanPosition == glm::vec2(40.0f, -8.0f));
}

TEST_CASE("Step is refused while maxTicksInFlight ticks are in flight", "[crowd][fake]")
{
	auto desc             = MakeDesc();
	desc.maxTicksInFlight = 2;
	auto crowd            = core::SharedRef<crowd::test::FakeCrowd>::Make(std::move(desc));

	crowd->Step();
	crowd->Step();
	CHECK_FALSE(crowd->CanStep());
	CHECK_THROWS_AS(crowd->Step(), std::runtime_error);
	CHECK(crowd->GetSubmittedTick() == 2);

	crowd->CompleteTick();
	CHECK(crowd->CanStep());
	CHECK(crowd->Step() == 3);
}

TEMPLATE_LIST_TEST_CASE(
	"A split divides a group's agents between it and a new group",
	"[crowd]",
	CrowdFactories)
{
	auto crowd    = TestType::Create(MakeDesc());
	auto group    = crowd->CreateGroup(MakeGroup(100));
	auto detached = crowd->SplitGroup(group, 30);

	CHECK(detached != group);
	CHECK(crowd->GetAgentCount(group) == 70);
	CHECK(crowd->GetAgentCount(detached) == 30);
	CHECK_FALSE(crowd->GetReport(detached).has_value());

	CompleteOneTick(*crowd);
	CHECK(crowd->GetReport(group)->agentCount == 70);
	CHECK(crowd->GetReport(detached)->agentCount == 30);
}

TEMPLATE_LIST_TEST_CASE(
	"A split that would leave a group empty is refused",
	"[crowd]",
	CrowdFactories)
{
	auto crowd = TestType::Create(MakeDesc());
	auto group = crowd->CreateGroup(MakeGroup(100));

	CHECK_THROWS_AS(crowd->SplitGroup(group, 0), std::runtime_error);
	CHECK_THROWS_AS(crowd->SplitGroup(group, 100), std::runtime_error);
	CHECK_THROWS_AS(crowd->SplitGroup(group, 101), std::runtime_error);
	CHECK(crowd->GetAgentCount(group) == 100);
}

TEMPLATE_LIST_TEST_CASE(
	"A split past maxGroups is refused and leaves the group whole",
	"[crowd]",
	CrowdFactories)
{
	auto crowd = TestType::Create(MakeDesc(1000, 1));
	auto group = crowd->CreateGroup(MakeGroup(100));

	CHECK_THROWS_AS(crowd->SplitGroup(group, 50), std::runtime_error);
	CHECK(crowd->GetAgentCount(group) == 100);
}

TEMPLATE_LIST_TEST_CASE(
	"A merge moves every agent into the target and releases the source",
	"[crowd]",
	CrowdFactories)
{
	auto crowd = TestType::Create(MakeDesc(1000, 2));
	auto from  = crowd->CreateGroup(MakeGroup(40));
	auto into  = crowd->CreateGroup(MakeGroup(60));
	CompleteOneTick(*crowd);

	crowd->MergeGroup(from, into);
	CHECK(crowd->GetAgentCount(into) == 100);
	CHECK_FALSE(crowd->HasGroup(from));
	CHECK_THROWS_AS(crowd->GetReport(from), std::runtime_error);

	// The released slot is room for another group.
	CHECK(crowd->HasGroup(crowd->CreateGroup(MakeGroup(10))));

	CompleteOneTick(*crowd);
	CHECK(crowd->GetReport(into)->agentCount == 100);
}

TEMPLATE_LIST_TEST_CASE(
	"A merge into itself or across agent types is refused",
	"[crowd]",
	CrowdFactories)
{
	auto crowd    = TestType::Create(MakeDesc());
	auto infantry = crowd->CreateGroup(MakeGroup(40, 0));
	auto horses   = crowd->CreateGroup(MakeGroup(10, 1));

	CHECK_THROWS_AS(crowd->MergeGroup(infantry, infantry), std::runtime_error);
	CHECK_THROWS_AS(crowd->MergeGroup(horses, infantry), std::runtime_error);
	CHECK(crowd->HasGroup(horses));
	CHECK(crowd->GetAgentCount(infantry) == 40);
}

TEMPLATE_LIST_TEST_CASE("A split merged back restores the group", "[crowd]", CrowdFactories)
{
	auto crowd    = TestType::Create(MakeDesc());
	auto group    = crowd->CreateGroup(MakeGroup(100));
	auto detached = crowd->SplitGroup(group, 25);
	crowd->MergeGroup(detached, group);

	CHECK(crowd->GetAgentCount(group) == 100);
	CHECK_FALSE(crowd->HasGroup(detached));
}

TEST_CASE("A split group carries the source's orders", "[crowd][fake]")
{
	auto crowd = core::SharedRef<crowd::test::FakeCrowd>::Make(MakeDesc());
	auto group = crowd->CreateGroup(MakeGroup(100));
	crowd->SetOrders(group, MakeOrders(glm::vec2(12.0f, 3.0f)));
	auto detached = crowd->SplitGroup(group, 10);
	CompleteOneTick(*crowd);

	CHECK(crowd->GetReport(detached)->meanPosition == glm::vec2(12.0f, 3.0f));
}
