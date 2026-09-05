#include "Render/OrbitCamera.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

// The orbit math shared by the preview viewports, pinned without a window: FocusOn's framing,
// the pitch clamp, the geometric dolly and its bounds, and the view-plane pan.

namespace
{
	using Catch::Approx;
	using editor::OrbitCamera;
}

TEST_CASE("A fresh orbit looks down -Z from three units out", "[animation]")
{
	const OrbitCamera orbit;

	const glm::vec3 eye = orbit.GetEyePosition();
	CHECK(eye.x == Approx(0.0f).margin(1e-6f));
	CHECK(eye.y == Approx(0.0f).margin(1e-6f));
	CHECK(eye.z == Approx(3.0f));
}

TEST_CASE("FocusOn frames the sphere: view reset, pulled back to three radii", "[animation]")
{
	auto orbit = OrbitCamera();
	orbit.Orbit(50.0f, 50.0f);
	orbit.FocusOn(glm::vec3(1.0f, 2.0f, 3.0f), 2.0f);

	CHECK(orbit.GetDistance() == Approx(6.0f));
	CHECK(orbit.GetPitch() == 0.0f);

	const glm::vec3 eye = orbit.GetEyePosition();
	CHECK(eye.x == Approx(1.0f));
	CHECK(eye.y == Approx(2.0f));
	CHECK(eye.z == Approx(9.0f));
}

TEST_CASE("Pitch clamps just short of the poles", "[animation]")
{
	auto orbit = OrbitCamera();
	orbit.Orbit(0.0f, 100000.0f);
	CHECK(orbit.GetPitch() == Approx(1.55f));

	orbit.Orbit(0.0f, -200000.0f);
	CHECK(orbit.GetPitch() == Approx(-1.55f));
}

TEST_CASE("Dolly is geometric per notch and clamped to the focus sphere", "[animation]")
{
	auto orbit = OrbitCamera();

	orbit.Dolly(1.0f);
	CHECK(orbit.GetDistance() == Approx(2.7f));

	orbit.Dolly(10000.0f);
	CHECK(orbit.GetDistance() == Approx(0.1f));  // 0.1 x radius 1

	orbit.Dolly(-10000.0f);
	CHECK(orbit.GetDistance() == Approx(50.0f));  // 50 x radius 1
}

TEST_CASE("Pan slides the focus across the view plane", "[animation]")
{
	auto orbit = OrbitCamera();  // eye on +Z looking back: right is +X, up is +Y

	orbit.Pan(10.0f, 0.0f);
	CHECK(orbit.GetFocusCenter().x == Approx(-0.06f));

	orbit.Pan(0.0f, 10.0f);
	CHECK(orbit.GetFocusCenter().y == Approx(0.06f));
}

TEST_CASE("FocusOn takes an opening view; pitch is clamped like Orbit's", "[animation]")
{
	auto orbit = OrbitCamera();
	orbit.FocusOn(glm::vec3(0.0f), 2.0f, glm::radians(45.0f), glm::radians(15.0f));

	CHECK(orbit.GetPitch() == Approx(glm::radians(15.0f)));

	// yaw 45, pitch 15 at distance 6: the eye sits on the diagonal, above the center.
	const glm::vec3 eye = orbit.GetEyePosition();
	CHECK(eye.x == Approx(6.0f * std::cos(glm::radians(15.0f)) * std::sin(glm::radians(45.0f))));
	CHECK(eye.y == Approx(6.0f * std::sin(glm::radians(15.0f))));
	CHECK(eye.z == Approx(6.0f * std::cos(glm::radians(15.0f)) * std::cos(glm::radians(45.0f))));

	orbit.FocusOn(glm::vec3(0.0f), 1.0f, 0.0f, 10.0f);
	CHECK(orbit.GetPitch() == Approx(1.55f));  // clamped short of the pole
}
