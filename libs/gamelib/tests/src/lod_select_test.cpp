#include <bgl/LodLevel.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/Viewport.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/math.h>
#include <gamelib/lod_select.h>
#include <limits>
#include <optional>
#include <vector>

// The cull's size test as a tool reads it. That the GPU chooses the same level is
// bgl_extended_tests' LodSelect_test; this pins the arithmetic with no device.

namespace
{
	// Levels 0, 1 and 2 from 30, 10 and 3 pixels; nothing below 3.
	const std::vector<float> c_Thresholds = { 30.0f, 10.0f, 3.0f };
}

TEST_CASE(
	"a world unit spans the pixels the projection gives it at a distance of one",
	"[lod][culling]")
{
	// A 90-degree vertical field of view puts a unit at distance one across half the height.
	const glm::mat4 viewProj =
		bgl::Camera()
			.LookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(90.0f), 16.0f / 9.0f, 0.1f, 100.0f)
			.GetViewProjection();

	CHECK(game::PixelsPerUnit(bgl::Viewport(1920.0f, 1080.0f), viewProj) == Catch::Approx(540.0f));

	SECTION("turning the camera does not change it")
	{
		const glm::mat4 turned = bgl::Camera()
		                             .LookAt(
										 glm::vec3(4.0f, 1.0f, 2.0f),
										 glm::vec3(-3.0f, 0.5f, 7.0f),
										 glm::vec3(0.0f, 1.0f, 0.0f))
		                             .Perspective(glm::radians(90.0f), 16.0f / 9.0f, 0.1f, 100.0f)
		                             .GetViewProjection();
		CHECK(
			game::PixelsPerUnit(bgl::Viewport(1920.0f, 1080.0f), turned) == Catch::Approx(540.0f));
	}
}

TEST_CASE("a placed sphere grows by its placement's largest scale", "[lod][culling]")
{
	const glm::vec4 sphere =
		core::bounding_sphere_of(glm::vec3(-1.0f), glm::vec3(1.0f, 3.0f, 1.0f));
	CHECK(glm::vec3(sphere) == glm::vec3(0.0f, 1.0f, 0.0f));
	CHECK(sphere.w == Catch::Approx(glm::sqrt(6.0f)));

	const glm::mat4 world = glm::scale(
		glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f)),
		glm::vec3(1.0f, 4.0f, 2.0f));
	const glm::vec4 placed = game::TransformSphere(world, sphere);
	CHECK(placed.x == Catch::Approx(5.0f));
	CHECK(placed.y == Catch::Approx(4.0f));
	CHECK(placed.w == Catch::Approx(4.0f * glm::sqrt(6.0f)));
}

TEST_CASE("a sphere's size on screen falls with its distance, not its direction", "[lod][culling]")
{
	const auto sphere = glm::vec4(0.0f, 0.0f, -10.0f, 1.0f);
	CHECK(game::ProjectedDiameter(sphere, glm::vec3(0.0f), 32.0f) == Catch::Approx(6.4f));

	const auto aside = glm::vec4(10.0f, 0.0f, 0.0f, 1.0f);
	CHECK(game::ProjectedDiameter(aside, glm::vec3(0.0f), 32.0f) == Catch::Approx(6.4f));

	SECTION("a camera inside the sphere sees it larger than any threshold")
	{
		CHECK(
			game::ProjectedDiameter(sphere, glm::vec3(0.0f, 0.0f, -10.5f), 32.0f) ==
			std::numeric_limits<float>::max());
	}
}

TEST_CASE("a size earns the finest level whose floor it meets", "[lod][culling]")
{
	CHECK(game::LevelBySize(c_Thresholds, 55.0f, 1.0f) == 0u);
	CHECK(game::LevelBySize(c_Thresholds, 30.0f, 1.0f) == 0u);
	CHECK(game::LevelBySize(c_Thresholds, 29.9f, 1.0f) == 1u);
	CHECK(game::LevelBySize(c_Thresholds, 5.5f, 1.0f) == 2u);

	SECTION("below the last level's floor is the draw-nothing tier")
	{
		CHECK(game::LevelBySize(c_Thresholds, 2.2f, 1.0f) == 3u);
	}

	SECTION("the view's scale moves every floor")
	{
		CHECK(game::LevelBySize(c_Thresholds, 55.0f, 2.0f) == 1u);
	}
}

TEST_CASE("a level resting just past a threshold holds against a finer one", "[lod][culling]")
{
	CHECK(game::ChooseLevel(c_Thresholds, 32.0f, 1.0f, std::nullopt) == 0u);
	CHECK(game::ChooseLevel(c_Thresholds, 32.0f, 1.0f, 1u) == 1u);

	const float clear = 30.0f * (1.0f + bgl::cLodHysteresis);
	CHECK(game::ChooseLevel(c_Thresholds, clear, 1.0f, 1u) == 0u);

	SECTION("while going coarser happens at the threshold itself")
	{
		CHECK(game::ChooseLevel(c_Thresholds, 29.9f, 1.0f, 0u) == 1u);
	}
}
