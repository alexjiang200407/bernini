#pragma once

namespace bgl
{
	// Samples in the sequence before it repeats. Eight rather than sixteen because the history blend
	// is what fills in the rest of the distribution, and a longer sequence lengthens the ghosting
	// tail for no visible gain at the blend weights TAA uses.
	constexpr uint32_t c_JitterSequenceLength = 8;

	/**
	 * The `index`th term of the radical-inverse (van der Corput) sequence in `base`, in [0, 1).
	 *
	 * @pre `base` is at least 2.
	 */
	[[nodiscard]] constexpr float
	RadicalInverse(uint32_t index, uint32_t base) noexcept
	{
		float result   = 0.0f;
		float fraction = 1.0f / static_cast<float>(base);

		while (index > 0)
		{
			result += static_cast<float>(index % base) * fraction;
			index /= base;
			fraction /= static_cast<float>(base);
		}

		return result;
	}

	/**
	 * The sub-pixel offset frame `frameCounter` renders with, in NDC, from the Halton(2, 3) sequence.
	 *
	 * The offset spans one pixel about the pixel centre, so the sequence walks the sample position
	 * across the footprint the frame would otherwise point-sample. Returned in NDC rather than pixels
	 * because that is where it is applied -- a clip-space translation ahead of the projection -- and
	 * where the motion-vector shader subtracts it back out.
	 *
	 * @pre both viewport dimensions are non-zero.
	 */
	[[nodiscard]] inline glm::vec2
	HaltonJitter(uint64_t frameCounter, float width, float height) noexcept
	{
		gassert(width > 0.0f && height > 0.0f, "Jitter needs a non-degenerate viewport");

		// The sequence is 1-based: term 0 of every radical inverse is 0, which would spend one frame
		// in eight on no jitter at all.
		const auto index = static_cast<uint32_t>(frameCounter % c_JitterSequenceLength) + 1;

		const float offsetX = RadicalInverse(index, 2) - 0.5f;
		const float offsetY = RadicalInverse(index, 3) - 0.5f;

		// NDC spans 2 across the viewport, so one pixel is 2/extent.
		return glm::vec2(offsetX * 2.0f / width, offsetY * 2.0f / height);
	}
}
