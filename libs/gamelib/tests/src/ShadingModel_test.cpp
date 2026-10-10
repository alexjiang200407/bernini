#include <assetlib/bmaterial.h>
#include <assetlib_structs/BMaterial.h>
#include <bgl/SurfaceType.h>
#include <gamelib/shading_model.h>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

using assetlib::ShadingModel;
using bgl::SurfaceShading;

// Every contract maps to its own document model and back. A model that fell to the default would
// have its materials checked against the PBR surface contract, and refused by name at load.
TEST_CASE("Each surface contract round-trips through its document model", "[surface][toon][water]")
{
	for (const SurfaceShading shading : { SurfaceShading::kPbrSurface,
	                                      SurfaceShading::kLit,
	                                      SurfaceShading::kToonCharacter,
	                                      SurfaceShading::kWater })
	{
		const ShadingModel model = game::ToShadingModel(shading);
		CAPTURE(static_cast<int>(shading));
		CHECK(assetlib::isSurfaceModel(model));
		CHECK(game::ToSurfaceShading(model) == shading);
	}

	CHECK(
		game::ToShadingModel(SurfaceShading::kToonCharacter) ==
		ShadingModel::kToonCharacterSurface);
	CHECK(game::ToShadingModel(SurfaceShading::kWater) == ShadingModel::kWaterSurface);
}
