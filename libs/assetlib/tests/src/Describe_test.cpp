#include <assetlib/asset_describe.h>

#include <assetlib/bmaterial_io.h>

using namespace assetlib;

namespace
{
	BMaterial
	routedMaterial()
	{
		BMaterial material;
		material.name                 = "skin";
		material.mode                 = MaterialMode::kLoose;
		material.pbr.metallicFactor   = 0.0f;
		material.pbr.roughnessFactor  = 0.75f;
		material.pbr.baseColorTexture = "Textures/basecolor_dead.ktx2";

		material.pbr.routes[0] = { "textures_src/skin.ktx2", 0 };
		material.pbr.routes[1] = { "textures_src/skin.ktx2", 1 };
		material.pbr.routes[5] = { "textures_src/mask.ktx2", 3 };  // roughness <- mask.a
		return material;
	}
}

// The point of describe() is to answer "what is in this file" without hand-decoding it, so the
// properties a reader actually goes looking for have to survive into the text: the routing table's
// channel selectors, the unrouted channels, and the mode.
TEST_CASE("describe(BMaterial) reports the routing table", "[describe]")
{
	const std::string text = describe(routedMaterial());

	CHECK(text.find("skin") != std::string::npos);
	CHECK(text.find("loose") != std::string::npos);

	// A route names its source and the channel it draws from -- routes[5] is roughness <- mask.a, and
	// a swizzle that silently printed the wrong letter would make the dump worse than useless.
	CHECK(text.find("baseColor.r") != std::string::npos);
	CHECK(text.find("textures_src/skin.ktx2 [r]") != std::string::npos);
	CHECK(text.find("textures_src/skin.ktx2 [g]") != std::string::npos);
	CHECK(text.find("textures_src/mask.ktx2 [a]") != std::string::npos);

	// The channels left unrouted are exactly the ones that fall back to a default texture at render
	// time, which is the single most common cause of a material looking wrong. They must be visible.
	CHECK(text.find("metallic        (unrouted)") != std::string::npos);
	CHECK(text.find("normal.x        (unrouted)") != std::string::npos);

	CHECK(text.find("Textures/basecolor_dead.ktx2") != std::string::npos);
	CHECK(text.find("orm             (none)") != std::string::npos);
}

// With a data root, each routed source is stat'd and compared against the stamp taken at bake time.
TEST_CASE("describe(BMaterial) reports bake staleness against the data root", "[describe]")
{
	const auto root = std::filesystem::temp_directory_path() / "bernini_describe";
	std::filesystem::create_directories(root / "textures_src");

	const auto source = root / "textures_src" / "skin.ktx2";
	{
		std::ofstream out(source, std::ios::binary);
		out << "some source bytes";
	}

	BMaterial material;
	material.pbr.routes[0] = { "textures_src/skin.ktx2", 0 };

	SECTION("a source that has drifted from its stamp is STALE")
	{
		// The stamp is left zeroed: this route was never baked, so it cannot match the live source.
		const std::string text = describe(material, root);
		CHECK(text.find("STALE") != std::string::npos);
	}

	SECTION("a source matching its stamp is up to date")
	{
		material.pbr.routeStamps[0] = stampOf(source);
		material.pbr.baseColorTexture =
			"Textures/baked.ktx2";  // a bake that actually produced a map

		const std::string text = describe(material, root);
		CHECK(text.find("up to date") != std::string::npos);
		CHECK(text.find("STALE") == std::string::npos);
	}

	SECTION("a missing source is called out rather than reported as a mismatch")
	{
		material.pbr.routes[0] = { "textures_src/gone.ktx2", 0 };

		const std::string text = describe(material, root);
		CHECK(text.find("source is missing") != std::string::npos);
	}

	std::filesystem::remove_all(root);
}

// A submesh whose material index is out of range draws with the renderer's default material. That is
// invisible in the raw bytes and easy to misread as "material 0", so the dump has to name it.
TEST_CASE("describe(BMesh) resolves each submesh's material path", "[describe]")
{
	BMesh mesh;
	mesh.stringPool = { '\0', 'h', 'e', 'a', 'd', '\0' };
	mesh.materials  = { "Materials/head.bmaterial" };
	mesh.meshes.push_back(Mesh{ .firstSubmesh = 0, .submeshCount = 2, .nameOffset = 0 });

	Submesh named{};
	named.nameOffset  = 1;  // "head"
	named.material    = 0;
	named.vertexCount = 12;
	named.indexCount  = 36;
	named.indexType   = IndexType::kUint16;
	mesh.submeshes.push_back(named);

	Submesh dangling{};
	dangling.material = 7;  // no such entry in `materials`
	mesh.submeshes.push_back(dangling);

	const std::string text = describe(mesh);

	CHECK(text.find("'head'") != std::string::npos);
	CHECK(text.find("[0] Materials/head.bmaterial") != std::string::npos);
	CHECK(text.find("[7] (out of range -- no material)") != std::string::npos);

	// Brief mode keeps the material table but drops the per-submesh listing.
	const std::string brief = describe(mesh, /*verbose*/ false);
	CHECK(brief.find("Materials/head.bmaterial") != std::string::npos);
	CHECK(brief.find("'head'") == std::string::npos);
}
