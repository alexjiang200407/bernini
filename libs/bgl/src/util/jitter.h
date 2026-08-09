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
	 * The sub-pixel offset frame `index` of the sequence renders with, in pixels, about the centre.
	 *
	 * The sequence is 1-based: term 0 of every radical inverse is 0, which would spend one frame in
	 * eight on no jitter at all.
	 */
	[[nodiscard]] inline glm::vec2
	JitterPixels(uint32_t index) noexcept
	{
		const auto term = index % c_JitterSequenceLength + 1;
		return glm::vec2(RadicalInverse(term, 2) - 0.5f, RadicalInverse(term, 3) - 0.5f);
	}

	/**
	 * How much of frame `frameCounter`'s sample belongs in the accumulation, from where its jitter
	 * landed inside the pixel. Scales the resolve's blend weight, and averages to 1 over the
	 * sequence.
	 *
	 * Blending every frame equally converges to the mean of the samples across the footprint --
	 * a box one pixel wide, which is the softest reconstruction the sequence can produce and is
	 * what a still TAA image is blurry against. Weighting by a kernel narrower than the footprint
	 * converges to that kernel instead: the sample nearest the pixel's own centre is the one that
	 * describes it best, and says so. The mean is normalised out because the weight also sets the
	 * accumulation's time constant, which is measured against flicker and must not move with it.
	 */
	[[nodiscard]] inline float
	JitterFilterWeight(uint64_t frameCounter) noexcept
	{
		// A Gaussian of 0.4 of a pixel: narrow enough to be worth the sharpening, wide enough that
		// the far corners of the footprint still carry a fifth of the weight rather than being
		// dropped, which would cost the antialiasing the offsets are there for.
		constexpr float c_FilterFalloff = 3.0f;

		const auto kernel = [](uint32_t index) {
			const auto offset = JitterPixels(index);
			return std::exp(-c_FilterFalloff * glm::dot(offset, offset));
		};

		float total = 0.0f;
		for (uint32_t i = 0; i < c_JitterSequenceLength; ++i)
		{
			total += kernel(i);
		}

		return kernel(static_cast<uint32_t>(frameCounter)) * c_JitterSequenceLength / total;
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

		// NDC spans 2 across the viewport, so one pixel is 2/extent.
		return JitterPixels(static_cast<uint32_t>(frameCounter)) * 2.0f / glm::vec2(width, height);
	}
}
