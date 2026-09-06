#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <gamelib/Ray.h>

namespace
{
	constexpr float c_Tolerance = 1e-4f;

	bool
	Near(float value, float expected)
	{
		return std::abs(value - expected) < c_Tolerance;
	}

	bool
	Near(const glm::vec3& value, const glm::vec3& expected)
	{
		return glm::length(value - expected) < c_Tolerance;
	}
}

TEST_CASE("A triangle is hit from either side", "[gamelib][ray]")
{
	const glm::vec3 a(-1.0f, -1.0f, 0.0f);
	const glm::vec3 b(1.0f, -1.0f, 0.0f);
	const glm::vec3 c(0.0f, 1.0f, 0.0f);

	const auto front = game::IntersectTriangle(
		{ glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(0.0f, 0.0f, 1.0f) },
		a,
		b,
		c);
	REQUIRE(front.has_value());
	CHECK(Near(*front, 2.0f));

	const auto back = game::IntersectTriangle(
		{ glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, -1.0f) },
		a,
		b,
		c);
	REQUIRE(back.has_value());
	CHECK(Near(*back, 3.0f));
}

TEST_CASE("A triangle beside or behind the ray is not hit", "[gamelib][ray]")
{
	const glm::vec3 a(-1.0f, -1.0f, 0.0f);
	const glm::vec3 b(1.0f, -1.0f, 0.0f);
	const glm::vec3 c(0.0f, 1.0f, 0.0f);

	// Aimed past the c corner: inside the AABB of the triangle, outside its edges.
	CHECK_FALSE(
		game::IntersectTriangle(
			{ glm::vec3(0.9f, 0.9f, -2.0f), glm::vec3(0.0f, 0.0f, 1.0f) },
			a,
			b,
			c)
			.has_value());

	// The triangle sits behind the origin along the ray.
	CHECK_FALSE(
		game::IntersectTriangle(
			{ glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(0.0f, 0.0f, -1.0f) },
			a,
			b,
			c)
			.has_value());

	// Parallel to the triangle's plane.
	CHECK_FALSE(
		game::IntersectTriangle(
			{ glm::vec3(0.0f, -2.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f) },
			a,
			b,
			c)
			.has_value());
}

TEST_CASE("An AABB reports its entry distance, and zero from inside", "[gamelib][ray]")
{
	const glm::vec3 boxMin(-1.0f);
	const glm::vec3 boxMax(1.0f);

	const auto entry = game::IntersectAabb(
		{ glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(0.0f, 0.0f, 1.0f) },
		boxMin,
		boxMax);
	REQUIRE(entry.has_value());
	CHECK(Near(*entry, 4.0f));

	const auto inside =
		game::IntersectAabb({ glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f) }, boxMin, boxMax);
	REQUIRE(inside.has_value());
	CHECK(*inside == 0.0f);

	CHECK_FALSE(
		game::IntersectAabb(
			{ glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 1.0f) },
			boxMin,
			boxMax)
			.has_value());
}

TEST_CASE("An axis-parallel ray beside an AABB misses it", "[gamelib][ray]")
{
	// The degenerate axes carry no distance information; only the origin can rule the box out.
	CHECK_FALSE(
		game::IntersectAabb(
			{ glm::vec3(2.0f, 0.0f, -5.0f), glm::vec3(0.0f, 0.0f, 1.0f) },
			glm::vec3(-1.0f),
			glm::vec3(1.0f))
			.has_value());
}

TEST_CASE("A sphere is hit at the near surface, or the far one from inside", "[gamelib][ray]")
{
	const glm::vec3 center(0.0f);

	const auto outside = game::IntersectSphere(
		{ glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(0.0f, 0.0f, 1.0f) },
		center,
		1.0f);
	REQUIRE(outside.has_value());
	CHECK(Near(*outside, 4.0f));

	const auto inside =
		game::IntersectSphere({ glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f) }, center, 1.0f);
	REQUIRE(inside.has_value());
	CHECK(Near(*inside, 1.0f));

	CHECK_FALSE(
		game::IntersectSphere(
			{ glm::vec3(0.0f, 2.0f, -5.0f), glm::vec3(0.0f, 0.0f, 1.0f) },
			center,
			1.0f)
			.has_value());

	CHECK_FALSE(
		game::IntersectSphere(
			{ glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 1.0f) },
			center,
			1.0f)
			.has_value());
}

TEST_CASE("A Transformed ray keeps its parameterization", "[gamelib][ray]")
{
	// A hit found in an instance's local space must measure the same t as one found in world
	// space, or hits across instances would not compare.
	const auto world = game::Ray{ glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(0.0f, 0.0f, 2.0f) };

	const glm::mat4 toLocal =
		glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f)));
	const game::Ray local = game::Transformed(world, toLocal);

	// The same triangle, expressed in both spaces, one unit in front of the ray's turned origin.
	const auto worldT = game::IntersectTriangle(
		world,
		glm::vec3(-1.0f, -1.0f, 0.0f),
		glm::vec3(1.0f, -1.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f));
	const auto localT = game::IntersectTriangle(
		local,
		glm::vec3(-4.0f, -1.0f, 0.0f),
		glm::vec3(-2.0f, -1.0f, 0.0f),
		glm::vec3(-3.0f, 1.0f, 0.0f));

	REQUIRE(worldT.has_value());
	REQUIRE(localT.has_value());
	CHECK(Near(*worldT, *localT));
	CHECK(Near(*worldT, 2.5f));  // 5 units of distance in steps of length 2
}

TEST_CASE("A pixel ray leaves the near plane toward what that pixel sees", "[gamelib][ray]")
{
	const glm::vec3 eye(0.0f, 0.0f, -10.0f);
	const glm::vec3 target(0.0f);
	const float     nearZ = 0.1f;
	const float     farZ  = 100.0f;

	const glm::mat4 view       = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 projection = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, nearZ, farZ);
	const glm::mat4 viewProjection = projection * view;

	const glm::vec2 viewport(1600.0f, 900.0f);

	// The center pixel looks straight down the view axis.
	const game::Ray center = game::RayThroughPixel(viewProjection, viewport * 0.5f, viewport);
	CHECK(Near(center.origin, eye + glm::vec3(0.0f, 0.0f, nearZ)));
	CHECK(Near(glm::normalize(center.direction), glm::vec3(0.0f, 0.0f, 1.0f)));

	// t = 1 lands on the far plane. Unprojecting the far end divides by a w near zero, so the
	// tolerance is relative to the distance covered.
	CHECK(std::abs(center.origin.z + center.direction.z - (eye.z + farZ)) < farZ * 1e-3f);

	// A pixel in the upper-left quadrant looks up and to the camera's left.
	const game::Ray corner =
		game::RayThroughPixel(viewProjection, glm::vec2(400.0f, 225.0f), viewport);
	CHECK(corner.direction.y > 0.0f);

	// The camera looks down +Z, so its left is world +X.
	CHECK(corner.direction.x > 0.0f);
}
