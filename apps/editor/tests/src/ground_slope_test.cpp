#include "Windows/AnimationEditor/ground_slope.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// The Animation panel's ground slope, lifted clear of the window: the scene ground a planted foot
// is solved against, and the floor drawn under the rig, must be the same plane -- a sign or an
// axis wrong between them plants every foot on a slope that leans away from the floor it stands on.

namespace
{
	void
	CheckNear(const glm::vec3& actual, const glm::vec3& expected)
	{
		CHECK(actual.x == Catch::Approx(expected.x).margin(1e-5));
		CHECK(actual.y == Catch::Approx(expected.y).margin(1e-5));
		CHECK(actual.z == Catch::Approx(expected.z).margin(1e-5));
	}
}

TEST_CASE("A slope of zero is the default ground", "[animation][slope]")
{
	const bgl::GroundPlaneDesc ground = editor::GroundForSlope(0.0f);
	CheckNear(ground.point, glm::vec3(0.0f));
	CheckNear(ground.normal, glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST_CASE("A positive slope rises toward +X", "[animation][slope]")
{
	// The normal of a plane rising toward +X leans back toward -X: the slider's tooltip and the
	// floor under the rig both say +X is uphill, so this is the sign that pins the promise.
	const bgl::GroundPlaneDesc ground = editor::GroundForSlope(30.0f);
	CHECK(ground.normal.x < 0.0f);
	CHECK(ground.normal.y == Catch::Approx(std::cos(glm::radians(30.0f))).margin(1e-5));
	CHECK(ground.normal.z == Catch::Approx(0.0f).margin(1e-5));

	// Unit length, which is what bgl's SetGround normalizes anyway -- but a caller reading it
	// back should not have to.
	CHECK(glm::length(ground.normal) == Catch::Approx(1.0f).margin(1e-5));
}

TEST_CASE("The floor stands on the ground it is drawn for", "[animation][slope]")
{
	// AddPlaneGeom's quad has its normal along +Z; once placed, its up must be the ground's
	// normal, at every slope. The two are derived from one rotation, and this is what pins that.
	for (const float degrees : { -30.0f, -7.5f, 0.0f, 12.0f, 30.0f })
	{
		const glm::mat4 floor = editor::FloorTransformForSlope(degrees);
		const glm::vec3 up = glm::normalize(glm::vec3(floor * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));

		CheckNear(up, editor::GroundForSlope(degrees).normal);

		// And it passes through the origin, where the ground does.
		CheckNear(glm::vec3(floor * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)), glm::vec3(0.0f));
	}
}

TEST_CASE("The heading turns uphill about +Y", "[animation][slope]")
{
	// At heading 0 the ground rises toward +X; at 90 it rises toward +Z -- the way the test coyote
	// runs -- and at 180 back toward -X. The slope itself is untouched by the turn.
	const bgl::GroundPlaneDesc east  = editor::GroundForSlope(30.0f, 0.0f);
	const bgl::GroundPlaneDesc south = editor::GroundForSlope(30.0f, 90.0f);
	const bgl::GroundPlaneDesc west  = editor::GroundForSlope(30.0f, 180.0f);

	CHECK(east.normal.x < 0.0f);
	CHECK(east.normal.z == Catch::Approx(0.0f).margin(1e-5));

	CHECK(south.normal.z < 0.0f);
	CHECK(south.normal.x == Catch::Approx(0.0f).margin(1e-5));

	CHECK(west.normal.x > 0.0f);
	CHECK(west.normal.z == Catch::Approx(0.0f).margin(1e-5));

	for (const bgl::GroundPlaneDesc* ground : { &east, &south, &west })
		CHECK(ground->normal.y == Catch::Approx(std::cos(glm::radians(30.0f))).margin(1e-5));

	// And the floor turns with it.
	for (const float heading : { 0.0f, 45.0f, 90.0f, 270.0f })
	{
		const glm::mat4 floor = editor::FloorTransformForSlope(20.0f, heading);
		const glm::vec3 up = glm::normalize(glm::vec3(floor * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
		CheckNear(up, editor::GroundForSlope(20.0f, heading).normal);
	}
}
