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
}
