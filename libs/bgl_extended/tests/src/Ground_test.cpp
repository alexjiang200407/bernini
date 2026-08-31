#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// The scene's ground plane: what it defaults to, what SetGround refuses, and that a plane it accepts
// comes back with a unit normal. Whether a pose respects it is SkinnedPose_test's business.

namespace
{
	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = false;
		return opts;
	}

	void
	CheckNear(const glm::vec3& actual, const glm::vec3& expected)
	{
		CHECK(actual.x == Catch::Approx(expected.x).margin(1e-6f));
		CHECK(actual.y == Catch::Approx(expected.y).margin(1e-6f));
		CHECK(actual.z == Catch::Approx(expected.z).margin(1e-6f));
	}
}

TEST_CASE("a scene stands on y = 0 until told otherwise", "[skinned][ground]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(bgl::SceneDesc());

	const bgl::GroundPlaneDesc& ground = scene->GetGround();
	CheckNear(ground.point, glm::vec3(0.0f));
	CheckNear(ground.normal, glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST_CASE("SetGround keeps the point and normalises the normal", "[skinned][ground]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(bgl::SceneDesc());

	// A 3-4-5 triangle, so the unit normal is exact.
	scene->SetGround({ glm::vec3(1.0f, -2.0f, 3.0f), glm::vec3(0.0f, 4.0f, 3.0f) });

	const bgl::GroundPlaneDesc& ground = scene->GetGround();
	CheckNear(ground.point, glm::vec3(1.0f, -2.0f, 3.0f));
	CheckNear(ground.normal, glm::vec3(0.0f, 0.8f, 0.6f));
}

TEST_CASE("SetGround refuses a plane no height can be read under", "[skinned][ground]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(bgl::SceneDesc());
	scene->SetGround({ glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f) });

	const float nan = std::numeric_limits<float>::quiet_NaN();

	SECTION("a zero normal")
	{
		CHECK_THROWS_AS(scene->SetGround({ glm::vec3(0.0f), glm::vec3(0.0f) }), bgl::SceneError);
	}

	SECTION("a normal with no upward component: vertical, and overhanging")
	{
		CHECK_THROWS_AS(
			scene->SetGround({ glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f) }),
			bgl::SceneError);
		CHECK_THROWS_AS(
			scene->SetGround({ glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f) }),
			bgl::SceneError);
	}

	SECTION("a normal whose length overflows, which would normalise to zero")
	{
		CHECK_THROWS_AS(
			scene->SetGround({ glm::vec3(0.0f), glm::vec3(1e20f, 1e20f, 1e20f) }),
			bgl::SceneError);
	}

	SECTION("a point or a normal that is not finite")
	{
		CHECK_THROWS_AS(
			scene->SetGround({ glm::vec3(nan, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f) }),
			bgl::SceneError);
		CHECK_THROWS_AS(
			scene->SetGround({ glm::vec3(0.0f), glm::vec3(0.0f, nan, 0.0f) }),
			bgl::SceneError);
	}

	// A refusal leaves the plane that stood before it.
	const bgl::GroundPlaneDesc& ground = scene->GetGround();
	CheckNear(ground.point, glm::vec3(0.0f, 1.0f, 0.0f));
	CheckNear(ground.normal, glm::vec3(0.0f, 1.0f, 0.0f));
}
