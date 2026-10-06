#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <core/noise.h>

// The noises' promises: zero on the lattice, bounded between, and a different field per seed.

TEST_CASE("gradient noise is zero on the lattice and bounded between", "[noise]")
{
	for (int x = -3; x <= 3; ++x)
	{
		for (int z = -3; z <= 3; ++z)
		{
			CHECK(core::gradient_noise(glm::vec2(x, z), 3) == 0.0f);
		}
	}

	float lowest  = 0.0f;
	float highest = 0.0f;
	for (int i = 0; i < 4000; ++i)
	{
		const glm::vec2 p(static_cast<float>(i) * 0.137f, static_cast<float>(i) * 0.071f);
		const float     n = core::gradient_noise(p, 3);
		lowest            = std::min(lowest, n);
		highest           = std::max(highest, n);
	}
	CHECK(lowest >= -1.0f);
	CHECK(highest <= 1.0f);
	CHECK(lowest < -0.3f);
	CHECK(highest > 0.3f);

	// Another seed is another field.
	CHECK(
		core::gradient_noise(glm::vec2(0.37f, 0.61f), 3) !=
		core::gradient_noise(glm::vec2(0.37f, 0.61f), 4));
}

TEST_CASE("the ridged noise keeps to [0, 1] and the fractal to [-1, 1]", "[noise]")
{
	for (int i = 0; i < 2000; ++i)
	{
		const glm::vec2 p(static_cast<float>(i) * 0.113f, static_cast<float>(i) * 0.059f);
		const float     ridged = core::ridged_noise(p, 11, 5, 2.0f, 0.5f);
		const float     fbm    = core::fbm(p, 11, 5, 2.0f, 0.5f);
		CHECK(ridged >= 0.0f);
		CHECK(ridged <= 1.0f);
		CHECK(fbm >= -1.0f);
		CHECK(fbm <= 1.0f);
	}
}
