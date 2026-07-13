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

	Rgba
	MeanColor(const std::string& path, int x, int y, int w, int h)
	{
		int width = 0, height = 0, channels = 0;

		unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
		if (pixels == nullptr)
			throw std::runtime_error("MeanColor: cannot read '" + path + "'");

		if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > width || y + h > height)
		{
			stbi_image_free(pixels);
			throw std::runtime_error(
				"MeanColor: the box falls outside '" + path + "' (" + std::to_string(width) + "x" +
				std::to_string(height) + ")");
		}

		double sum[4] = { 0.0, 0.0, 0.0, 0.0 };

		for (int row = y; row < y + h; ++row)
		{
			for (int col = x; col < x + w; ++col)
			{
				const size_t texel = (static_cast<size_t>(row) * width + col) * 4;
				for (int c = 0; c < 4; ++c) sum[c] += pixels[texel + c];
			}
		}

		stbi_image_free(pixels);

		const auto texels = static_cast<double>(w) * h * 255.0;

		return Rgba{ static_cast<float>(sum[0] / texels),
			         static_cast<float>(sum[1] / texels),
			         static_cast<float>(sum[2] / texels),
			         static_cast<float>(sum[3] / texels) };
	}
}
