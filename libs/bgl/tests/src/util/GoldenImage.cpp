#include "util/GoldenImage.h"

#include <DirectXTex.h>

namespace bgl::test
{
	namespace
	{
		std::wstring
		Widen(const std::string& path)
		{
			return std::wstring(path.begin(), path.end());
		}
	}

	bool
	MatchesGoldenDDS(const std::string& expectedPath, const std::string& gotPath, float tolerance)
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

		DirectX::ScratchImage expectedImage;
		DirectX::ScratchImage gotImage;

		if (FAILED(
				DirectX::LoadFromDDSFile(
					Widen(expectedPath).c_str(),
					DirectX::DDS_FLAGS_NONE,
					nullptr,
					expectedImage)))
		{
			logger::warn("Failed to load golden image '{}'", expectedPath);
			return false;
		}

		if (FAILED(
				DirectX::LoadFromDDSFile(
					Widen(gotPath).c_str(),
					DirectX::DDS_FLAGS_NONE,
					nullptr,
					gotImage)))
		{
			logger::warn("Failed to load captured image '{}'", gotPath);
			return false;
		}

		const auto& expectedMeta = expectedImage.GetMetadata();
		const auto& gotMeta      = gotImage.GetMetadata();

		if (expectedMeta.width != gotMeta.width || expectedMeta.height != gotMeta.height ||
		    expectedMeta.format != gotMeta.format)
		{
			logger::warn(
				"Golden image mismatch '{}': dimensions/format differ; captured output left at "
				"'{}'",
				expectedPath,
				gotPath);
			return false;
		}

		const DirectX::Image* expected = expectedImage.GetImage(0, 0, 0);
		const DirectX::Image* got      = gotImage.GetImage(0, 0, 0);

		if (expected == nullptr || got == nullptr)
		{
			return false;
		}

		float mse     = 0.0f;
		float mseV[4] = {};
		if (FAILED(DirectX::ComputeMSE(*expected, *got, mse, mseV)))
		{
			logger::warn("Failed to compute MSE for golden image '{}'", expectedPath);
			return false;
		}

		if (mse > tolerance)
		{
			logger::warn(
				"Golden image mismatch '{}': MSE {} exceeds tolerance {}; captured output left at "
				"'{}'",
				expectedPath,
				mse,
				tolerance,
				gotPath);
			return false;
		}

		// Match: drop the temporary capture so a passing test leaves nothing behind.
		std::error_code ec;
		fs::remove(gotPath, ec);
		return true;
	}
}
