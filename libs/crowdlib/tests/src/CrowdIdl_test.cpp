// The crowd's IDL as the CPU sees it. idlgen pins each generated struct to the layout its module
// reflects; this checks that a record written at its stride into a buffer and read back is the same
// record, as a kernel's upload and a readback both rely on.
#include "idl/Agent.h"
#include "idl/AgentRange.h"
#include "idl/Constants.h"
#include "idl/Group.h"
#include "idl/GroupSum.h"
#include "idl/TickParams.h"
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
	template <typename T>
	std::vector<T>
	RoundTrip(const std::vector<T>& records)
	{
		auto bytes = std::vector<std::byte>(records.size() * sizeof(T));
		std::memcpy(bytes.data(), records.data(), bytes.size());

		auto back = std::vector<T>(records.size());
		std::memcpy(back.data(), bytes.data(), bytes.size());
		return back;
	}
}

TEST_CASE("A spawned range's source is no agent index", "[crowd][idl]")
{
	CHECK(crowd::idl::c_SpawnSource == std::numeric_limits<uint32_t>::max());
	CHECK(crowd::idl::c_WalkingSpeedShare > 0.0f);
	CHECK(crowd::idl::c_WalkingSpeedShare < 1.0f);
}

TEST_CASE("The crowd's GPU records survive a buffer at their stride", "[crowd][idl]")
{
	const auto agents = std::vector<crowd::idl::Agent>{
		{ .position = glm::vec2(1.0f, 2.0f),
		  .velocity = glm::vec2(-0.5f, 0.25f),
		  .facing   = glm::vec2(0.0f, 1.0f),
		  .group    = 3,
		  .slot     = 17 },
		{ .position = glm::vec2(-4.0f, 8.0f),
		  .velocity = glm::vec2(0.0f),
		  .facing   = glm::vec2(1.0f, 0.0f),
		  .group    = 0,
		  .slot     = 0 },
	};
	const auto back = RoundTrip(agents);
	CHECK(back[0].position == agents[0].position);
	CHECK(back[0].velocity == agents[0].velocity);
	CHECK(back[0].slot == 17);
	CHECK(back[1].facing == agents[1].facing);
	CHECK(back[1].group == 0);

	const auto groups = RoundTrip(
		std::vector<crowd::idl::Group>{ { .goal       = glm::vec2(5.0f),
	                                      .front      = glm::vec2(0.0f, 1.0f),
	                                      .firstAgent = 40,
	                                      .agentCount = 12,
	                                      .frontage   = 4,
	                                      .spacing    = 1.5f,
	                                      .speed      = 1.2f,
	                                      .maxSpeed   = 1.5f } });
	CHECK(groups[0].firstAgent == 40);
	CHECK(groups[0].maxSpeed == 1.5f);

	const auto ranges = RoundTrip(
		std::vector<crowd::idl::AgentRange>{ { .firstAgent       = 0,
	                                           .sourceFirstAgent = crowd::idl::c_SpawnSource,
	                                           .agentCount       = 9,
	                                           .group            = 2,
	                                           .firstSlot        = 3 } });
	CHECK(ranges[0].sourceFirstAgent == crowd::idl::c_SpawnSource);
	CHECK(ranges[0].firstSlot == 3);

	const auto sums = RoundTrip(
		std::vector<crowd::idl::GroupSum>{ { .meanPosition = glm::vec2(-1.0f, 1.0f),
	                                         .meanFacing   = glm::vec2(1.0f, 0.0f),
	                                         .agentCount   = 7,
	                                         .reserved     = 0 } });
	CHECK(sums[0].meanPosition == glm::vec2(-1.0f, 1.0f));
	CHECK(sums[0].agentCount == 7);

	const auto params = RoundTrip(
		std::vector<crowd::idl::TickParams>{ { .tickSeconds     = 1.0f / 30.0f,
	                                           .velocityInertia = 0.01f,
	                                           .agentCount      = 100,
	                                           .groupCount      = 3,
	                                           .agentRangeCount = 4 } });
	CHECK(params[0].agentRangeCount == 4);
}
