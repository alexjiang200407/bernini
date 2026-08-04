#pragma once

namespace bgl::test
{
	// Compares a freshly captured PNG (`gotPath`) against a golden reference PNG
	// (`expectedPath`) using a mean-squared-error over the decoded RGBA pixels.
	//
	// Returns true iff the images match within `tolerance` (MSE normalized to [0,1]).
	// On any failure the captured image is left on disk at `gotPath` (conventionally
	// "<name>.got.png") for inspection, and false is returned. Failure cases:
	//   - the expected file does not exist,
	//   - the images differ in size,
	//   - the MSE exceeds `tolerance`.
	// On success `gotPath` is deleted, so a passing test leaves no artifact.
	bool
	MatchesGolden(
		const std::string& expectedPath,
		const std::string& gotPath,
		float              tolerance = 1e-4f);

	struct Rgba
	{
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 0.0f;

		// Rough perceptual brightness. A sample that hits the (unlit, skyboxless) background is ~0, so
		// this is what tells a real hit on geometry apart from a test whose sample box missed it and is
		// asserting about nothing.
		[[nodiscard]] float
		Luma() const noexcept
		{
			return 0.2126f * r + 0.7152f * g + 0.0722f * b;
		}
	};

	/**
	 * The mean RGBA of the `w` x `h` box at (`x`, `y`) in the PNG at `path`, each channel in [0,1].
	 *
	 * For a render test that compares two regions of one frame against each other, rather than against
	 * a stored reference -- no golden PNG to regenerate when the scene changes.
	 *
	 * @throws std::runtime_error if the image cannot be read, or the box is not wholly inside it.
	 */
	[[nodiscard]] Rgba
	MeanColor(const std::string& path, int x, int y, int w, int h);

	/**
	 * Mean squared difference between horizontally adjacent pixels in a region -- a measure of how
	 * grainy it is.
	 *
	 * Aliasing shows up as high-frequency energy: a reflection sampled from a mip finer than its
	 * screen footprint speckles neighbouring pixels with unrelated parts of the environment, and this
	 * is what that costs numerically. A smooth gradient scores near zero however bright it is, so the
	 * measure is about detail and not about exposure.
	 */
	[[nodiscard]] float
	AliasEnergy(const std::string& path, int x, int y, int w, int h);

	/**
	 * Mean squared difference between the same region of two frames -- how much a pixel changed
	 * between them.
	 *
	 * The temporal counterpart of AliasEnergy, which measures the same quantity across neighbouring
	 * pixels of one frame. A static camera over a converged accumulation should score near zero;
	 * what it scores instead is flicker, which no single-frame measurement can see.
	 *
	 * @throws std::runtime_error if either image cannot be read, they differ in size, or the box is
	 *         not wholly inside them.
	 */
	[[nodiscard]] float
	FrameDelta(const std::string& pathA, const std::string& pathB, int x, int y, int w, int h);
}
