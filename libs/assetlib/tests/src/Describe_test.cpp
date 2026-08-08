#include <assetlib/asset_describe.h>

#include <assetlib/bmaterial_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>

using namespace assetlib;

namespace
{
	BMaterial
	RoutedMaterial()
	{
		BMaterial material;
		material.name                 = "skin";
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
// channel selectors and the unrouted channels.
TEST_CASE("describe(BMaterial) reports the routing table", "[describe]")
{
	const std::string text = describe(RoutedMaterial());

	CHECK(text.find("skin") != std::string::npos);

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
		material.pbr.routeStamps[0]   = stampOf(source);
		material.pbr.baseColorTexture = "Textures/baked.ktx2";

		// The map has to be there as well as named: a triplet entry pointing at nothing is stale.
		std::filesystem::create_directories(root / "Textures");
		{
			std::ofstream out(root / "Textures" / "baked.ktx2", std::ios::binary);
			out << "baked bytes";
		}

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
	REQUIRE(mesh.stringPool.add("head") == 1);
	mesh.materials = { "Materials/head.bmaterial" };
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

// A .benv holds no pixels at all, so the only thing worth reading out of it is what it names -- and
// whether those names resolve. A dangling reference renders as an environment that silently loses its
// sky or its lighting, which is exactly the case a dump has to make visible.
TEST_CASE("describe(BEnv) reports whether the files it names are there", "[describe]")
{
	const auto root = std::filesystem::temp_directory_path() / "bernini_describe_benv";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "Sky");

	{
		std::ofstream out(root / "Sky" / "forest.bsky", std::ios::binary);
		out << "x";
	}

	BEnv env;
	env.name     = "forest";
	env.sky      = "Sky/forest.bsky";
	env.lighting = "EnvLighting/forest.benvl";  // never written

	// Without a root there is nothing to resolve against, so neither is judged.
	const std::string bare = describe(env);
	CHECK(bare.find("forest") != std::string::npos);
	CHECK(bare.find("Sky/forest.bsky") != std::string::npos);
	CHECK(bare.find("(missing)") == std::string::npos);

	const std::string text = describe(env, root);
	CHECK(text.find("Sky/forest.bsky\n") != std::string::npos);  // present: unannotated
	CHECK(text.find("EnvLighting/forest.benvl (missing)") != std::string::npos);

	// An unset half is not the same as a missing one, and must not read as a broken reference.
	BEnv skyless;
	skyless.lighting            = "EnvLighting/forest.benvl";
	const std::string unsetText = describe(skyless, root);
	CHECK(unsetText.find("sky               (unset)") != std::string::npos);

	std::filesystem::remove_all(root);
}

// The sky and the lighting carry EnvMapRoutes, which stale exactly the way a material's channel
// routes do -- so the same question ("is what I am about to render still what was baked?") has to be
// answerable here too.
TEST_CASE("describe(BSky) and describe(BEnvLighting) report bake staleness", "[describe]")
{
	const auto root = std::filesystem::temp_directory_path() / "bernini_describe_env";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "textures_src");

	std::filesystem::create_directories(root / "Textures");

	const auto source = root / "textures_src" / "forest.ktx2";
	const auto baked  = root / "Textures" / "forest_sky_ab12.ktx2";
	const auto write  = [](const std::filesystem::path& path, std::string_view bytes) {
		std::ofstream out(path, std::ios::binary);
		out << bytes;
	};
	write(source, "aaaa");

	// The map has to be on disk, not merely named: a route claiming one that is gone is stale, so a
	// fixture that never wrote it would be describing a stale bake while calling itself current.
	write(baked, "bbbb");

	BSky sky;
	sky.name       = "forest";
	sky.mipLevel   = 2;
	sky.rotationY  = 1.5f;
	sky.sky.source = "textures_src/forest.ktx2";
	sky.sky.baked  = "Textures/forest_sky_ab12.ktx2";
	sky.sky.stamp  = stampOf(source);

	SECTION("a sky reports its presentation and a current bake")
	{
		const std::string text = describe(sky, root);

		CHECK(text.find("bsky 'forest'") != std::string::npos);
		CHECK(text.find("mipLevel          2") != std::string::npos);
		CHECK(text.find("rotationY         1.5 rad") != std::string::npos);
		CHECK(text.find("textures_src/forest.ktx2") != std::string::npos);
		CHECK(text.find("Textures/forest_sky_ab12.ktx2") != std::string::npos);
		CHECK(text.find("source up to date") != std::string::npos);
		CHECK(text.find("STALE") == std::string::npos);
	}

	// The case the stricter rule exists for: nothing about the source moved, but what was baked from
	// it is gone, so the route names a file there is nothing to sample.
	SECTION("a sky whose baked map is gone says so and reads as stale")
	{
		std::filesystem::remove(baked);

		const std::string text = describe(sky, root);
		CHECK(text.find("baked map is missing") != std::string::npos);
		CHECK(text.find("bake              STALE") != std::string::npos);
	}

	SECTION("a sky whose source moved on reads as stale")
	{
		write(source, "aaaaaaaa");  // different size

		const std::string text = describe(sky, root);
		CHECK(text.find("STALE") != std::string::npos);
	}

	SECTION("a sky whose source is gone says so rather than comparing stamps")
	{
		std::filesystem::remove(source);

		const std::string text = describe(sky, root);
		CHECK(text.find("source is missing") != std::string::npos);
	}

	BEnvLighting lighting;
	lighting.name              = "forest";
	lighting.exposure          = 1.25f;
	lighting.prefilter.source  = "textures_src/forest.ktx2";
	lighting.prefilter.baked   = "Textures/forest_prefilter.ktx2";
	lighting.prefilter.stamp   = stampOf(source);
	lighting.irradiance.source = "textures_src/forest.ktx2";
	lighting.irradiance.baked  = "Textures/forest_irradiance.ktx2";
	lighting.irradiance.stamp  = stampOf(source);

	SECTION("a lighting names both halves and its exposure")
	{
		const std::string text = describe(lighting, root);

		CHECK(text.find("benvl 'forest'") != std::string::npos);
		CHECK(text.find("exposure          1.25") != std::string::npos);
		CHECK(text.find("prefilter") != std::string::npos);
		CHECK(text.find("irradiance") != std::string::npos);
		CHECK(text.find("Textures/forest_prefilter.ktx2") != std::string::npos);
		CHECK(text.find("Textures/forest_irradiance.ktx2") != std::string::npos);
	}

	// The pair is one verdict: they convolve the same radiance, so one drifting makes both suspect.
	SECTION("one half drifting makes the whole lighting stale")
	{
		lighting.irradiance.stamp = SourceStamp{ 1, 1 };

		const std::string text = describe(lighting, root);
		CHECK(text.find("bake              STALE") != std::string::npos);
	}

	// Without a root nothing is stat'd, so the recorded stamp is all that can be reported -- and no
	// verdict may be printed, because none was measured.
	SECTION("no data root reports the recorded stamp and no verdict")
	{
		const std::string text = describe(sky);

		CHECK(text.find("baked from") != std::string::npos);
		CHECK(text.find("up to date") == std::string::npos);
		CHECK(text.find("STALE") == std::string::npos);
	}

	std::filesystem::remove_all(root);
}
