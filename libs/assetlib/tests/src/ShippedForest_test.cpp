#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/codecs.h>
#include <assetlib/envmap.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>

using namespace assetlib;

// The `forest` environment under assets/ is the one every preview and every render test lights
// through, and it exists to be compared against Blender's Material Preview, which draws the same
// forest.exr at strength 1.0 with no normalization. These pin the two authored facts that keep that
// comparison honest; a re-import that forgot either would pass every golden after a re-baseline.

TEST_CASE("The shipped forest renders at Blender's exposure", "[envmap][forest]")
{
	const auto store = AssetStore("assets/Data");
	const auto env   = store.Load<BEnv>("Authored/Environments/forest.benv");
	const auto light = store.Load<BEnvLighting>(env.lighting);

	// The bake still proposes its middle-grey normalization; the document overrules it.
	CHECK(light.exposure > 1.2f);
	REQUIRE(env.exposureOverride.has_value());
	CHECK(effectiveExposure(env, light) == Catch::Approx(1.0f));
}

TEST_CASE("The shipped forest sky is a defocus chain, presented sharp", "[envmap][forest]")
{
	const auto store = AssetStore("assets/Data");
	const auto env   = store.Load<BEnv>("Authored/Environments/forest.benv");

	// The material preview asks for mip 3 of this chain; a single-mip sky makes that a no-op.
	const ResolvedEnvironment resolved = store.ResolveEnvironment(
		std::filesystem::path("assets/Data/Authored/Environments/forest.benv"));
	CHECK(resolved.maps.skybox.isCubemap);
	CHECK(resolved.maps.skybox.mipLevels == 6);
	CHECK(resolved.maps.skybox.width == 512);
	CHECK(env.skyMipLevel == 0);
}

TEST_CASE("The shipped forest lights the way Eevee's preview does", "[envmap][forest]")
{
	const auto                store    = AssetStore("assets/Data");
	const ResolvedEnvironment resolved = store.ResolveEnvironment(
		std::filesystem::path("assets/Data/Authored/Environments/forest.benv"));
	const ImageData& map = resolved.maps.irradiance;
	REQUIRE(map.isCubemap);
	REQUIRE(map.vkFormat == VkFormat::E5B9G9R9_UFLOAT_PACK32);

	// The one fact no container records: this map is the Eevee model, a deringed first-order
	// harmonic, and it is flatter than the integral. The exact bake of the same source spans a
	// 24:1 range of luma over the cube; the model's spans 9:1. A re-import that took the default
	// would put the integral back and pass every golden after a re-baseline.
	float lo = 1e9f;
	float hi = 0.0f;
	for (const auto& sub : map.subresources)
	{
		const auto*  px = reinterpret_cast<const uint32_t*>(map.pixels.data() + sub.offset);
		const size_t n  = static_cast<size_t>(map.width) * map.height;
		for (size_t t = 0; t < n; ++t)
		{
			const uint32_t v     = px[t];
			const float    scale = std::ldexp(1.0f, static_cast<int>(v >> 27) - 15 - 9);
			const float    r     = static_cast<float>(v & 0x1ff) * scale;
			const float    g     = static_cast<float>((v >> 9) & 0x1ff) * scale;
			const float    b     = static_cast<float>((v >> 18) & 0x1ff) * scale;
			const float    luma  = 0.2126f * r + 0.7152f * g + 0.0722f * b;
			lo                   = std::min(lo, luma);
			hi                   = std::max(hi, luma);
		}
	}
	INFO("irradiance luma " << lo << " .. " << hi);
	CHECK(hi / lo < 15.0f);
}
