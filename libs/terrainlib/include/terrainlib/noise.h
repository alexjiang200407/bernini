#pragma once
#include <core/glm.h>
#include <cstdint>

// The noises a terrain is generated from, for anything else that wants a reproducible field over
// the plane. Each is a pure function of its arguments: no table, no state, the same value for the
// same point and seed wherever it is called from.

namespace terrain
{
	/**
	 * Gradient noise over the plane at `p`, in [-1, 1], zero on the integer lattice: the gradient
	 * at each lattice point comes from a hash of its coordinates and `seed`, so the field has no
	 * state and the same seed gives the same field everywhere.
	 */
	[[nodiscard]] float
	GradientNoise(glm::vec2 p, uint32_t seed) noexcept;

	/**
	 * Fractal sum of `octaves` gradient noises, each `lacunarity` times the frequency and `gain`
	 * times the amplitude of the last, normalised to about [-1, 1].
	 */
	[[nodiscard]] float
	Fbm(glm::vec2 p, uint32_t seed, uint32_t octaves, float lacunarity, float gain) noexcept;

	/**
	 * Ridged multifractal (Musgrave) over the same octaves: each octave is one minus the noise's
	 * magnitude, squared, and weighted by the octave before it so detail gathers on the ridges.
	 * In about [0, 1], with the ridges near 1.
	 */
	[[nodiscard]] float
	Ridged(glm::vec2 p, uint32_t seed, uint32_t octaves, float lacunarity, float gain) noexcept;
}
