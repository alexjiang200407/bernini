// crowd::SlotPosition, the CPU half of the formation layout the crowd's kernels compute: these pin
// the layout itself, not that the kernels agree with it -- the GPU movement cases do that.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <core/glm.h>
#include <crowdlib/GroupOrders.h>
#include <cstdint>

namespace
{
	using Catch::Matchers::WithinAbs;

	constexpr float c_Tolerance = 1e-4f;

	crowd::GroupOrders
	MakeOrders(glm::vec2 goal, glm::vec2 facing, uint32_t frontage, float spacing)
	{
		auto orders      = crowd::GroupOrders();
		orders.goal      = goal;
		orders.facing    = facing;
		orders.formation = { .frontage = frontage, .spacing = spacing };
		return orders;
	}

	glm::vec2
	MeanSlot(const crowd::GroupOrders& orders, uint32_t agentCount)
	{
		auto sum = glm::vec2(0.0f);
		for (uint32_t slot = 0; slot < agentCount; ++slot)
			sum += crowd::SlotPosition(orders, agentCount, slot);
		return sum / static_cast<float>(agentCount);
	}

	void
	CheckNear(glm::vec2 actual, glm::vec2 expected)
	{
		CHECK_THAT(actual.x, WithinAbs(expected.x, c_Tolerance));
		CHECK_THAT(actual.y, WithinAbs(expected.y, c_Tolerance));
	}
}

TEST_CASE("A lone agent stands on the goal", "[crowd][formation]")
{
	const auto orders = MakeOrders(glm::vec2(3.0f, -7.0f), glm::vec2(1.0f, 1.0f), 4, 2.0f);
	CheckNear(crowd::SlotPosition(orders, 1, 0), orders.goal);
}

TEST_CASE("A full block is centred on the goal", "[crowd][formation]")
{
	const auto orders = MakeOrders(glm::vec2(10.0f, 4.0f), glm::vec2(0.0f, 1.0f), 5, 1.5f);
	CheckNear(MeanSlot(orders, 15), orders.goal);
}

TEST_CASE("Ranks fill front to back and files from the left", "[crowd][formation]")
{
	// Facing +z: the front rank has the largest z, and the left, (-facing.y, facing.x), is -x.
	const auto orders = MakeOrders(glm::vec2(0.0f), glm::vec2(0.0f, 1.0f), 3, 2.0f);

	CheckNear(crowd::SlotPosition(orders, 6, 0), glm::vec2(-2.0f, 1.0f));
	CheckNear(crowd::SlotPosition(orders, 6, 1), glm::vec2(0.0f, 1.0f));
	CheckNear(crowd::SlotPosition(orders, 6, 2), glm::vec2(2.0f, 1.0f));
	CheckNear(crowd::SlotPosition(orders, 6, 3), glm::vec2(-2.0f, -1.0f));
	CheckNear(crowd::SlotPosition(orders, 6, 5), glm::vec2(2.0f, -1.0f));
}

TEST_CASE("A short last rank stands centred behind the others", "[crowd][formation]")
{
	const auto orders = MakeOrders(glm::vec2(0.0f), glm::vec2(0.0f, 1.0f), 4, 1.0f);

	// Seven agents: a rank of four, then three, whose middle agent is behind the block's centre.
	const auto middle = crowd::SlotPosition(orders, 7, 5);
	CHECK_THAT(middle.x, WithinAbs(0.0f, c_Tolerance));
	CHECK(middle.y < 0.0f);
}

TEST_CASE("The facing's length does not change the layout", "[crowd][formation]")
{
	const auto unit   = MakeOrders(glm::vec2(1.0f, 2.0f), glm::vec2(0.6f, 0.8f), 3, 1.25f);
	auto       scaled = unit;
	scaled.facing *= 40.0f;

	for (uint32_t slot = 0; slot < 8; ++slot)
		CheckNear(crowd::SlotPosition(scaled, 8, slot), crowd::SlotPosition(unit, 8, slot));
}

TEST_CASE("Neighbours in a rank and across ranks stand spacing apart", "[crowd][formation]")
{
	const auto orders = MakeOrders(glm::vec2(-5.0f, 9.0f), glm::vec2(-1.0f, 2.0f), 6, 0.75f);

	CHECK_THAT(
		glm::distance(crowd::SlotPosition(orders, 18, 7), crowd::SlotPosition(orders, 18, 8)),
		WithinAbs(0.75f, c_Tolerance));
	CHECK_THAT(
		glm::distance(crowd::SlotPosition(orders, 18, 7), crowd::SlotPosition(orders, 18, 13)),
		WithinAbs(0.75f, c_Tolerance));
}
