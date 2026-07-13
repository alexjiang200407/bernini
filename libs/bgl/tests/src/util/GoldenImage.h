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
}
