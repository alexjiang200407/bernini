#include <bgl/MaterialType.h>
#include <bgl/SurfaceType.h>
#include <bgl/types/MaterialHandle.h>
#include <catch2/catch_test_macros.hpp>
#include <editor_sdk/toon_content.h>
#include <span>
#include <vector>

namespace
{
	constexpr auto c_Pbr       = bgl::MaterialType::kGameStart;
	constexpr auto c_Character = static_cast<bgl::MaterialType>(5);
	constexpr auto c_Lit       = static_cast<bgl::MaterialType>(6);

	std::vector<bgl::SurfaceType>
	Surfaces()
	{
		std::vector<bgl::SurfaceType> surfaces(3);
		surfaces[0] = { .surfaceName = "pbr",
			            .kind        = c_Pbr,
			            .shading     = bgl::SurfaceShading::kPbrSurface };
		surfaces[1] = { .surfaceName = "character",
			            .kind        = c_Character,
			            .shading     = bgl::SurfaceShading::kToonCharacter };
		surfaces[2] = { .surfaceName = "lit", .kind = c_Lit, .shading = bgl::SurfaceShading::kLit };
		return surfaces;
	}

	bgl::MaterialHandle
	Material(bgl::MaterialType type)
	{
		return { .materialType = type };
	}
}

// A preview draws the toon backdrop when it shows the toon model's material, and only then.
TEST_CASE("A material is toon when the toon model's surface draws it", "[toon][backdrop]")
{
	const auto surfaces = Surfaces();

	CHECK(editor::IsToonMaterial(surfaces, Material(c_Character)));

	CHECK_FALSE(editor::IsToonMaterial(surfaces, Material(c_Pbr)));
	CHECK_FALSE(editor::IsToonMaterial(surfaces, Material(c_Lit)));
	CHECK_FALSE(editor::IsToonMaterial(surfaces, Material(bgl::MaterialType::kPBR)));
	CHECK_FALSE(editor::IsToonMaterial(surfaces, bgl::MaterialHandle()));
	CHECK_FALSE(editor::IsToonMaterial(std::span<const bgl::SurfaceType>(), Material(c_Character)));
}

TEST_CASE("Any toon material among a mesh's makes it toon", "[toon][backdrop]")
{
	const auto surfaces = Surfaces();

	const std::vector<bgl::MaterialHandle> plain = { Material(c_Pbr),
		                                             bgl::MaterialHandle(),
		                                             Material(c_Lit) };
	CHECK_FALSE(editor::AnyToonMaterial(surfaces, plain));
	CHECK_FALSE(editor::AnyToonMaterial(surfaces, {}));

	std::vector<bgl::MaterialHandle> mixed = plain;
	mixed.push_back(Material(c_Character));
	CHECK(editor::AnyToonMaterial(surfaces, mixed));
}
