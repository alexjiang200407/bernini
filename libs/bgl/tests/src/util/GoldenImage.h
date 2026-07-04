#pragma once

namespace bgl::test
{
	// Compares a freshly captured DDS (`gotPath`) against a golden reference DDS
	// (`expectedPath`) using DirectXTex's mean-squared-error.
	//
	// Returns true iff the images match within `tolerance` (per-pixel MSE). On any
	// failure the captured image is left on disk at `gotPath` (conventionally
	// "<name>.got.dds") for inspection, and false is returned. Failure cases:
	//   - the expected file does not exist,
	//   - the images differ in size/format,
	//   - the MSE exceeds `tolerance`.
	// On success `gotPath` is deleted, so a passing test leaves no artifact.
	bool
	MatchesGoldenDDS(
		const std::string& expectedPath,
		const std::string& gotPath,
		float              tolerance = 1e-4f);
}
