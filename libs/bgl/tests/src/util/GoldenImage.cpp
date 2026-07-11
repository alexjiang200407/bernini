#include "util/GoldenImage.h"

// STB_IMAGE_STATIC gives this TU its own internal-linkage copy of the decoder, avoiding a
// duplicate-symbol clash with assetlib (which also compiles stb_image, via tinygltf).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace bgl::test
{
	bool
	MatchesGolden(const std::string& expectedPath, const std::string& gotPath, float tolerance)
	{
		namespace fs = std::filesystem;

		// A missing golden is a failure: keep the capture so it can be promoted to
		// the expected reference once it is verified by eye.
		if (!fs::exists(expectedPath))
		{
			logger::warn(
				"Golden image '{}' does not exist; captured output left at '{}'",
				expectedPath,
				gotPath);
			return false;
		}

		int expW = 0, expH = 0, expC = 0;
		int gotW = 0, gotH = 0, gotC = 0;

		unsigned char* expected = stbi_load(expectedPath.c_str(), &expW, &expH, &expC, 4);
		if (expected == nullptr)
		{
			logger::warn("Failed to load golden image '{}'", expectedPath);
			return false;
		}

		unsigned char* got = stbi_load(gotPath.c_str(), &gotW, &gotH, &gotC, 4);
		if (got == nullptr)
		{
			stbi_image_free(expected);
			logger::warn("Failed to load captured image '{}'", gotPath);
			return false;
		}

		bool matches = true;

		if (expW != gotW || expH != gotH)
		{
			logger::warn(
				"Golden image mismatch '{}': dimensions differ; captured output left at '{}'",
				expectedPath,
				gotPath);
			matches = false;
		}

		if (matches)
		{
			// Mean squared error over every RGBA channel, normalized to [0,1] so the tolerance
			// matches the scale DirectX::ComputeMSE used previously.
			const size_t count = static_cast<size_t>(expW) * expH * 4;
			double       sum   = 0.0;
			for (size_t i = 0; i < count; ++i)
			{
				const double d = (static_cast<double>(expected[i]) - got[i]) / 255.0;
				sum += d * d;
			}

			const float mse = static_cast<float>(sum / static_cast<double>(count));
			if (mse > tolerance)
			{
				logger::warn(
					"Golden image mismatch '{}': MSE {} exceeds tolerance {}; captured output left "
					"at '{}'",
					expectedPath,
					mse,
					tolerance,
					gotPath);
				matches = false;
			}
		}

		stbi_image_free(expected);
		stbi_image_free(got);

		if (matches)
		{
			// Match: drop the temporary capture so a passing test leaves nothing behind.
			std::error_code ec;
			fs::remove(gotPath, ec);
		}

		return matches;
	}
}
