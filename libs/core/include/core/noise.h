#pragma once
#include <core/glm.h>
#include <cstdint>

// Reproducible noise over the plane, for anything that wants a field that looks random and is the
// same wherever it is asked: a terrain's heights, where to scatter a prop, the drift of a cloud.
// Each is a pure function of its arguments, keyed on hash_mix32 (core/hash.h): no table, no state.

namespace core
{
	/**
	 * Gradient noise at `p`, in [-1, 1] and zero on the integer lattice: the gradient at each
	 * lattice point comes from a hash of its coordinates and `seed`, so the same seed gives the
	 * same field everywhere.
	 */
	[[nodiscard]] float
	gradient_noise(glm::vec2 p, uint32_t seed) noexcept;

	/**
	 * Fractal sum of `octaves` gradient noises, each `lacunarity` times the frequency and `gain`
	 * times the amplitude of the last, normalised to about [-1, 1].
	 */
	[[nodiscard]] float
	fbm(glm::vec2 p, uint32_t seed, uint32_t octaves, float lacunarity, float gain) noexcept;

	/**
	 * Ridged multifractal (Musgrave) over the same octaves: each octave is one minus the noise's
	 * magnitude, squared, and weighted by the octave before it so detail gathers on the ridges.
	 * In about [0, 1], with the ridges near 1.
	 */
	[[nodiscard]] float
	ridged_noise(
		glm::vec2 p,
		uint32_t  seed,
		uint32_t  octaves,
		float     lacunarity,
		float     gain) noexcept;
}
