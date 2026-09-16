#include "util/SyntheticCube.h"
#include <algorithm>
#include <assetlib/envmap.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <core/containers/fixed_buffer.h>
#include <cstddef>
#include <cstdint>

namespace bgl::test
{
	assetlib::ImageData
	MakeBlackFloatCube(uint32_t faceSize)
	{
		auto out      = assetlib::ImageData();
		out.width     = faceSize;
		out.height    = faceSize;
		out.mipLevels = 1;
		out.arraySize = 6;
		out.isCubemap = true;
		out.vkFormat  = assetlib::VkFormat::R32G32B32A32_SFLOAT;
		out.pixels    = core::fixed_buffer<std::byte>(
			static_cast<size_t>(faceSize) * faceSize * 6 * sizeof(float) * 4);

		std::fill(out.pixels.begin(), out.pixels.end(), std::byte{ 0 });

		const auto pitch = static_cast<uint64_t>(faceSize) * sizeof(float) * 4;
		for (uint32_t face = 0; face < 6; ++face)
		{
			out.subresources.push_back(
				{ static_cast<size_t>(pitch) * faceSize * face, pitch, pitch * faceSize });
		}

		return out;
	}

	void
	ApplyBlackEnvironment(bgl::IScene* scene, bgl::ISceneView* view)
	{
		// EnvOrientation_test's cube shape: a 7-mip prefilter chain is MAX_REFLECTION_LOD + 1.
		constexpr uint32_t c_SourceFace     = 64;
		constexpr uint32_t c_IrradianceFace = 32;
		constexpr uint32_t c_PrefilterMips  = 7;

		auto desc      = assetlib::PrefilterDesc();
		desc.faceSize  = c_SourceFace;
		desc.mipLevels = c_PrefilterMips;
		desc.samples   = 32;

		const auto radiance = MakeBlackFloatCube(c_SourceFace);

		view->SetEnvironmentMap(
			{ scene->AddTextureAsset(
				  assetlib::irradianceSh(radiance, c_IrradianceFace),
				  "black_irradiance"),
		      scene->AddTextureAsset(
				  assetlib::prefilterRadiance(radiance, desc, nullptr),
				  "black_prefilter") });

		view->SetExposure(1.0f);
	}
}
