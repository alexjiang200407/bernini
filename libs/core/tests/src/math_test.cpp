#include <catch2/catch_test_macros.hpp>
#include <core/math.h>
#include <limits>

TEST_CASE("A vector is finite only when every one of its components is", "[math]")
{
	const float infinity = std::numeric_limits<float>::infinity();
	const float nan      = std::numeric_limits<float>::quiet_NaN();

	CHECK(core::is_finite(glm::vec3(0.0f)));
	CHECK(core::is_finite(glm::vec3(-1e20f, 3.5f, 1e20f)));

	// Every component is asked, not just the first: a normalise that overflows leaves the damage
	// wherever the division happened to land.
	CHECK_FALSE(core::is_finite(glm::vec3(infinity, 0.0f, 0.0f)));
	CHECK_FALSE(core::is_finite(glm::vec3(0.0f, -infinity, 0.0f)));
	CHECK_FALSE(core::is_finite(glm::vec3(0.0f, 0.0f, nan)));

	CHECK(core::is_finite(glm::vec2(1.0f, 2.0f)));
	CHECK_FALSE(core::is_finite(glm::vec4(1.0f, 2.0f, 3.0f, nan)));

	// A zero vector divided by its own length is how the NaN a caller passes on actually arises.
	const glm::vec3 zero = glm::vec3(0.0f);
	CHECK_FALSE(core::is_finite(zero / glm::length(zero)));
}
