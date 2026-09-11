#include <assetlib/AssetStore.h>
#include <assetlib/benv.h>
#include <assetlib/codecs.h>
#include <assetlib/envmap.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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
