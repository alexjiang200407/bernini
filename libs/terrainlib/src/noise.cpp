#include "noise.h"
#include <array>
#include <cmath>
#include <core/glm.h>
#include <cstdint>

namespace terrain
{
	namespace
	{
		/** A well-mixed word from a lattice point and the seed (lowbias32, Wellons). */
		[[nodiscard]] uint32_t
		HashLattice(const int32_t x, const int32_t z, const uint32_t seed) noexcept
		{
			uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u ^
			             static_cast<uint32_t>(z) * 0xd8163841u ^ seed * 0x9e3779b9u;
			h ^= h >> 16;
			h *= 0x7feb352du;
			h ^= h >> 15;
			h *= 0x846ca68bu;
			h ^= h >> 16;
			return h;
		}

		// Sixteen unit directions, every 22.5 degrees, as constants rather than a cos and a sin per
		// corner per octave: cheaper, and the same bits whichever libm a build links.
		constexpr std::array<std::array<float, 2>, 16> c_Gradients = { {
			{ 1.0f, 0.0f },
			{ 0.92387953f, 0.38268343f },
			{ 0.70710678f, 0.70710678f },
			{ 0.38268343f, 0.92387953f },
			{ 0.0f, 1.0f },
			{ -0.38268343f, 0.92387953f },
			{ -0.70710678f, 0.70710678f },
			{ -0.92387953f, 0.38268343f },
			{ -1.0f, 0.0f },
			{ -0.92387953f, -0.38268343f },
			{ -0.70710678f, -0.70710678f },
			{ -0.38268343f, -0.92387953f },
			{ 0.0f, -1.0f },
			{ 0.38268343f, -0.92387953f },
			{ 0.70710678f, -0.70710678f },
			{ 0.92387953f, -0.38268343f },
		} };

		/** A unit gradient at a lattice point, one of the sixteen. */
		[[nodiscard]] glm::vec2
		Gradient(const int32_t x, const int32_t z, const uint32_t seed) noexcept
		{
			const auto& g = c_Gradients[HashLattice(x, z, seed) & 15u];
			return glm::vec2(g[0], g[1]);
		}

		/** Perlin's quintic fade: zero first and second derivatives at both ends. */
		[[nodiscard]] float
		Fade(const float t) noexcept
		{
			return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
		}
	}

	float
	GradientNoise(const glm::vec2 p, const uint32_t seed) noexcept
	{
		const glm::vec2 cell = glm::floor(p);
		const glm::vec2 f    = p - cell;
		const auto      x0   = static_cast<int32_t>(cell.x);
		const auto      z0   = static_cast<int32_t>(cell.y);

		const float n00 = glm::dot(Gradient(x0, z0, seed), f);
		const float n10 = glm::dot(Gradient(x0 + 1, z0, seed), f - glm::vec2(1.0f, 0.0f));
		const float n01 = glm::dot(Gradient(x0, z0 + 1, seed), f - glm::vec2(0.0f, 1.0f));
		const float n11 = glm::dot(Gradient(x0 + 1, z0 + 1, seed), f - glm::vec2(1.0f, 1.0f));

		const float u = Fade(f.x);
		const float v = Fade(f.y);
		const float n = glm::mix(glm::mix(n00, n10, u), glm::mix(n01, n11, u), v);

		// A gradient dotted with an offset of up to sqrt(2) / 2 reaches about 0.707.
		return glm::clamp(n * 1.4142f, -1.0f, 1.0f);
	}

	float
	Fbm(glm::vec2      p,
	    const uint32_t seed,
	    const uint32_t octaves,
	    const float    lacunarity,
	    const float    gain) noexcept
	{
		float sum       = 0.0f;
		float amplitude = 1.0f;
		float total     = 0.0f;
		for (uint32_t o = 0; o < octaves; ++o)
		{
			sum += amplitude * GradientNoise(p, seed + o * 0x6d2b79f5u);
			total += amplitude;
			amplitude *= gain;
			p *= lacunarity;
		}
		return total > 0.0f ? sum / total : 0.0f;
	}

	float
	Ridged(
		glm::vec2      p,
		const uint32_t seed,
		const uint32_t octaves,
		const float    lacunarity,
		const float    gain) noexcept
	{
		float sum       = 0.0f;
		float amplitude = 1.0f;
		float weight    = 1.0f;
		float total     = 0.0f;
		for (uint32_t o = 0; o < octaves; ++o)
		{
			float ridge = 1.0f - std::abs(GradientNoise(p, seed + o * 0x6d2b79f5u));
			ridge *= ridge;
			ridge *= weight;
			weight = glm::clamp(ridge * 2.0f, 0.0f, 1.0f);

			sum += amplitude * ridge;
			total += amplitude;
			amplitude *= gain;
			p *= lacunarity;
		}
		return total > 0.0f ? sum / total : 0.0f;
	}
}
