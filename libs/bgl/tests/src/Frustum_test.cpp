#include "culling/Frustum.h"
#include <bgl/Camera.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>

// CPU-only: the plane math must hold before any GPU cull kernel can trust it. Points are checked
// as zero-radius spheres so the tests exercise the same predicate the cull will.

namespace
{
	bool
	PointInFrustum(const bgl::FrustumPlanes& frustum, const glm::vec3& p)
	{
		return bgl::SphereIntersectsFrustum(frustum, p, 0.0f);
	}
}

TEST_CASE("Frustum planes classify points around a perspective camera", "[culling]")
{
	// Eye at origin looking down -Z, 90-degree vertical fov, square aspect: at depth d the visible
	// half-extent is exactly d.
	auto camera =
		bgl::Camera()
			.LookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);

	const auto planes = bgl::ExtractFrustumPlanes(camera.GetViewProjection());

	SECTION("inside")
	{
		CHECK(PointInFrustum(planes, glm::vec3(0.0f, 0.0f, -50.0f)));
		CHECK(PointInFrustum(planes, glm::vec3(49.0f, 0.0f, -50.0f)));
		CHECK(PointInFrustum(planes, glm::vec3(-49.0f, 0.0f, -50.0f)));
		CHECK(PointInFrustum(planes, glm::vec3(0.0f, 49.0f, -50.0f)));
		CHECK(PointInFrustum(planes, glm::vec3(0.0f, -49.0f, -50.0f)));
		CHECK(PointInFrustum(planes, glm::vec3(0.0f, 0.0f, -1.5f)));
		CHECK(PointInFrustum(planes, glm::vec3(0.0f, 0.0f, -99.0f)));
	}

	SECTION("outside each plane")
	{
		CHECK_FALSE(PointInFrustum(planes, glm::vec3(-60.0f, 0.0f, -50.0f)));  // left
		CHECK_FALSE(PointInFrustum(planes, glm::vec3(60.0f, 0.0f, -50.0f)));   // right
		CHECK_FALSE(PointInFrustum(planes, glm::vec3(0.0f, -60.0f, -50.0f)));  // bottom
		CHECK_FALSE(PointInFrustum(planes, glm::vec3(0.0f, 60.0f, -50.0f)));   // top
		CHECK_FALSE(PointInFrustum(planes, glm::vec3(0.0f, 0.0f, -0.5f)));     // near
		CHECK_FALSE(PointInFrustum(planes, glm::vec3(0.0f, 0.0f, 5.0f)));      // behind the eye
		CHECK_FALSE(PointInFrustum(planes, glm::vec3(0.0f, 0.0f, -150.0f)));   // far
	}
}

TEST_CASE("Orthographic planes come out normalized with exact distances", "[culling]")
{
	// Identity view: the frustum is the axis-aligned box x,y in [-1,1], z in [-10,-0.1].
	const glm::mat4 viewProj =
		bgl::Camera().Orthographic(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f).GetViewProjection();

	const auto planes = bgl::ExtractFrustumPlanes(viewProj);

	const glm::vec4 expected[6] = {
		{ 1.0f, 0.0f, 0.0f, 1.0f },    // left
		{ -1.0f, 0.0f, 0.0f, 1.0f },   // right
		{ 0.0f, 1.0f, 0.0f, 1.0f },    // bottom
		{ 0.0f, -1.0f, 0.0f, 1.0f },   // top
		{ 0.0f, 0.0f, -1.0f, -0.1f },  // near
		{ 0.0f, 0.0f, 1.0f, 10.0f },   // far
	};

	for (size_t i = 0; i < 6; ++i)
	{
		CAPTURE(i);
		CHECK(planes[i].x == Catch::Approx(expected[i].x).margin(1e-5));
		CHECK(planes[i].y == Catch::Approx(expected[i].y).margin(1e-5));
		CHECK(planes[i].z == Catch::Approx(expected[i].z).margin(1e-5));
		CHECK(planes[i].w == Catch::Approx(expected[i].w).margin(1e-5));
	}
}

TEST_CASE(
	"Spheres straddling or tangent to a plane survive; fully outside ones do not",
	"[culling]")
{
	const glm::mat4 viewProj =
		bgl::Camera().Orthographic(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f).GetViewProjection();

	const auto planes = bgl::ExtractFrustumPlanes(viewProj);

	SECTION("straddling each plane")
	{
		CHECK(bgl::SphereIntersectsFrustum(planes, glm::vec3(-1.2f, 0.0f, -5.0f), 0.5f));
		CHECK(bgl::SphereIntersectsFrustum(planes, glm::vec3(1.2f, 0.0f, -5.0f), 0.5f));
		CHECK(bgl::SphereIntersectsFrustum(planes, glm::vec3(0.0f, -1.2f, -5.0f), 0.5f));
		CHECK(bgl::SphereIntersectsFrustum(planes, glm::vec3(0.0f, 1.2f, -5.0f), 0.5f));
		CHECK(bgl::SphereIntersectsFrustum(planes, glm::vec3(0.0f, 0.0f, 0.15f), 0.5f));
		CHECK(bgl::SphereIntersectsFrustum(planes, glm::vec3(0.0f, 0.0f, -10.3f), 0.5f));
	}

	// Not exact tangency: plane normalization is inexact (1/9.9), so a distance of exactly -radius
	// can land on either side of the compare. The property worth pinning is that any real overlap
	// survives, however small.
	SECTION("a sphere overlapping by a hair survives")
	{
		CHECK(bgl::SphereIntersectsFrustum(planes, glm::vec3(1.499f, 0.0f, -5.0f), 0.5f));
		CHECK(bgl::SphereIntersectsFrustum(planes, glm::vec3(0.0f, 0.0f, -10.499f), 0.5f));
	}

	SECTION("fully outside each plane")
	{
		CHECK_FALSE(bgl::SphereIntersectsFrustum(planes, glm::vec3(-1.6f, 0.0f, -5.0f), 0.5f));
		CHECK_FALSE(bgl::SphereIntersectsFrustum(planes, glm::vec3(1.6f, 0.0f, -5.0f), 0.5f));
		CHECK_FALSE(bgl::SphereIntersectsFrustum(planes, glm::vec3(0.0f, -1.6f, -5.0f), 0.5f));
		CHECK_FALSE(bgl::SphereIntersectsFrustum(planes, glm::vec3(0.0f, 1.6f, -5.0f), 0.5f));
		CHECK_FALSE(bgl::SphereIntersectsFrustum(planes, glm::vec3(0.0f, 0.0f, 1.0f), 0.5f));
		CHECK_FALSE(bgl::SphereIntersectsFrustum(planes, glm::vec3(0.0f, 0.0f, -11.0f), 0.5f));
	}
}

TEST_CASE("BuildCullView mirrors the matrix and its extracted planes", "[culling]")
{
	const glm::mat4 viewProj =
		bgl::Camera()
			.LookAt(glm::vec3(3.0f, 4.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f)
			.GetViewProjection();

	const bgl::idl::CullView view   = bgl::BuildCullView(viewProj);
	const auto               planes = bgl::ExtractFrustumPlanes(viewProj);

	CHECK(view.viewProj == viewProj);

	for (size_t i = 0; i < 6; ++i)
	{
		CAPTURE(i);
		CHECK(view.frustumPlanes[i] == planes[i]);
		CHECK(glm::length(glm::vec3(view.frustumPlanes[i])) == Catch::Approx(1.0f).margin(1e-5));
	}
}
