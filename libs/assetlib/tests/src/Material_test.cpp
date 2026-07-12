#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>

#include <catch2/catch_approx.hpp>

using namespace assetlib;

TEST_CASE("a BMaterial survives a serialize round-trip", "[bmaterial][io]")
{
	BMaterial mat;
	mat.name             = "brushed_metal";
	mat.baseColorTexture = "albedo.ktx2";
	mat.normalTexture    = "";  // absent
	mat.ormTexture       = "orm.ktx2";
	mat.baseColorFactor  = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
	mat.metallicFactor   = 0.75f;
	mat.roughnessFactor  = 0.25f;

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.name == mat.name);
	REQUIRE(restored.baseColorTexture == mat.baseColorTexture);
	REQUIRE(restored.normalTexture.empty());
	REQUIRE(restored.ormTexture == mat.ormTexture);
	REQUIRE(restored.baseColorFactor.r == Catch::Approx(0.1f));
	REQUIRE(restored.baseColorFactor.a == Catch::Approx(1.0f));
	REQUIRE(restored.metallicFactor == Catch::Approx(0.75f));
	REQUIRE(restored.roughnessFactor == Catch::Approx(0.25f));
}

TEST_CASE("a Loose BMaterial round-trips its mode and routes", "[bmaterial][io]")
{
	BMaterial mat;
	mat.mode            = MaterialMode::kLoose;
	mat.name            = "packed";
	mat.metallicFactor  = 0.5f;
	mat.roughnessFactor = 0.4f;
	mat.routes[0]       = { "albedo.ktx2", 0 };  // base color R
	mat.routes[1]       = { "albedo.ktx2", 1 };  // base color G
	mat.routes[5]       = { "packed.ktx2", 2 };  // roughness from packed.B
	mat.routes[7]       = { "normal.ktx2", 0 };  // normal X

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.mode == MaterialMode::kLoose);
	REQUIRE(restored.routes.size() == c_LooseChannelCount);
	REQUIRE(restored.routes[0].texture == "albedo.ktx2");
	REQUIRE(restored.routes[0].channel == 0);
	REQUIRE(restored.routes[1].channel == 1);
	REQUIRE(restored.routes[5].texture == "packed.ktx2");
	REQUIRE(restored.routes[5].channel == 2);
	REQUIRE(restored.routes[7].texture == "normal.ktx2");
	REQUIRE(restored.routes[2].texture.empty());  // an unrouted channel stays empty
	REQUIRE(restored.metallicFactor == Catch::Approx(0.5f));
}

TEST_CASE("a BMaterial round-trips its editor graph", "[bmaterial][io]")
{
	// The graph is an opaque blob to assetlib: it must survive byte-for-byte, embedded quotes,
	// braces, newlines and all.
	BMaterial mat;
	mat.mode        = MaterialMode::kLoose;
	mat.name        = "graphed";
	mat.editorGraph = R"({"nodes":[{"id":0,"internal-data":{"model-name":"Texture"}}],"c":[]})"
					  "\n{\"trailing\":\"line\"}";

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.editorGraph == mat.editorGraph);
	REQUIRE(restored.mode == MaterialMode::kLoose);
	REQUIRE(restored.name == "graphed");
}

TEST_CASE("a BMaterial with no editor graph round-trips an empty one", "[bmaterial][io]")
{
	// The exported/baked form: the authoring graph has been stripped.
	BMaterial mat;
	mat.mode             = MaterialMode::kBaked;
	mat.baseColorTexture = "baked_basecolor.ktx2";

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.editorGraph.empty());
	REQUIRE(restored.baseColorTexture == "baked_basecolor.ktx2");
}

TEST_CASE("a BMaterial round-trips its bake provenance", "[bmaterial][io]")
{
	BMaterial mat;
	mat.routes[0]      = { "albedo.ktx2", 0 };
	mat.routeStamps[0] = { 4096, 1752000000 };
	mat.routeStamps[8] = { 1, -5 };  // mtime is signed: pre-epoch timestamps must survive

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.routeStamps[0].size == 4096);
	REQUIRE(restored.routeStamps[0].mtime == 1752000000);
	REQUIRE(restored.routeStamps[8].mtime == -5);
	REQUIRE(restored.routeStamps[3] == SourceStamp{});  // unstamped routes stay zeroed
}

TEST_CASE("a BMaterial carries both its sources and its baked triplet", "[bmaterial][io]")
{
	// The coexistence the format exists for: the bake fills the triplet without discarding the
	// routes that produced it, so the material can still be reopened and re-baked.
	BMaterial mat;
	mat.mode             = MaterialMode::kBaked;
	mat.baseColorTexture = "mat_basecolor.ktx2";
	mat.ormTexture       = "mat_orm.ktx2";
	mat.routes[0]        = { "src/albedo.ktx2", 0 };
	mat.routeStamps[0]   = { 64, 7 };

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.mode == MaterialMode::kBaked);
	REQUIRE(restored.baseColorTexture == "mat_basecolor.ktx2");
	REQUIRE(restored.routes[0].texture == "src/albedo.ktx2");
	REQUIRE(restored.routeStamps[0].size == 64);
}

TEST_CASE("stampOf measures a file and zeroes a missing one", "[bmaterial][bake]")
{
	const auto dir = std::filesystem::temp_directory_path() / "bernini_stamp_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);

	const auto file = dir / "src.bin";
	{
		std::ofstream out(file, std::ios::binary);
		out << "hello";
	}

	const SourceStamp stamp = stampOf(file);
	REQUIRE(stamp.size == 5);
	REQUIRE(stamp.mtime != 0);
	REQUIRE(stampOf(file) == stamp);  // stable across calls

	REQUIRE(stampOf(dir / "absent.bin") == SourceStamp{});

	std::filesystem::remove_all(dir);
}

TEST_CASE("bakeIsStale compares routed sources against their stamps", "[bmaterial][bake]")
{
	const auto dir = std::filesystem::temp_directory_path() / "bernini_stale_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);

	const auto source = dir / "albedo.ktx2";
	{
		std::ofstream out(source, std::ios::binary);
		out << "aaaa";
	}

	BMaterial mat;
	mat.mode             = MaterialMode::kBaked;
	mat.baseColorTexture = "mat_basecolor.ktx2";
	mat.routes[0]        = { "albedo.ktx2", 0 };

	SECTION("a material with no routes is never stale")
	{
		BMaterial imported;
		imported.baseColorTexture = "tex0.ktx2";
		REQUIRE_FALSE(bakeIsStale(imported, dir));
	}

	SECTION("routed but unstamped means it was never baked") { REQUIRE(bakeIsStale(mat, dir)); }

	SECTION("a matching stamp is fresh")
	{
		mat.routeStamps[0] = stampOf(source);
		REQUIRE_FALSE(bakeIsStale(mat, dir));
	}

	SECTION("a source that changed size is stale")
	{
		mat.routeStamps[0] = stampOf(source);
		{
			std::ofstream out(source, std::ios::binary);
			out << "aaaaaaaa";  // different size
		}
		REQUIRE(bakeIsStale(mat, dir));
	}

	SECTION("a deleted source is stale, not silently unchanged")
	{
		mat.routeStamps[0] = stampOf(source);
		std::filesystem::remove(source);
		REQUIRE(bakeIsStale(mat, dir));
	}

	SECTION("fresh sources but no bake output is stale")
	{
		mat.routeStamps[0]   = stampOf(source);
		mat.baseColorTexture = "";
		REQUIRE(bakeIsStale(mat, dir));
	}

	std::filesystem::remove_all(dir);
}

TEST_CASE("deserializeMaterial rejects an outdated stream", "[bmaterial][io]")
{
	// Only the current major version loads; an older file is re-baked, not migrated. The check has to
	// be loud, because the alternative to rejecting a v1 stream is not "it still works" -- it is
	// reading v1's bytes with the current layout and producing a material made of garbage.
	//
	// Hand-build a v1 .bmaterial byte stream (predates the mode + routes fields).
	std::vector<std::byte> v1;
	const auto             putPod = [&](auto value) {
		const auto* p = reinterpret_cast<const std::byte*>(&value);
		v1.insert(v1.end(), p, p + sizeof(value));
	};
	const auto putStr = [&](const std::string& s) {
		putPod(static_cast<uint32_t>(s.size()));
		const auto* p = reinterpret_cast<const std::byte*>(s.data());
		v1.insert(v1.end(), p, p + s.size());
	};

	putPod(static_cast<uint32_t>(0x54414D42u));  // magic
	putPod(static_cast<uint16_t>(1));            // versionMajor = 1
	putPod(static_cast<uint16_t>(0));            // versionMinor
	putPod(glm::vec4(0.2f, 0.3f, 0.4f, 1.0f));   // baseColorFactor
	putPod(0.6f);                                // metallicFactor
	putPod(0.7f);                                // roughnessFactor
	putStr("old");                               // name
	putStr("base.ktx2");                         // baseColorTexture
	putStr("");                                  // normalTexture
	putStr("orm.ktx2");                          // ormTexture

	REQUIRE_THROWS_AS(deserializeMaterial(v1), std::runtime_error);
}

TEST_CASE("deserializeMaterial rejects a stream with bad magic", "[bmaterial][io]")
{
	const std::array<std::byte, 8> garbage{};
	REQUIRE_THROWS_AS(deserializeMaterial(garbage), std::runtime_error);
}

TEST_CASE("saveMaterial / loadMaterial round-trips through a file", "[bmaterial][io]")
{
	BMaterial mat;
	mat.name             = "leaf";
	mat.baseColorTexture = "tex0.ktx2";
	mat.baseColorFactor  = glm::vec4(1.0f, 0.5f, 0.25f, 1.0f);
	mat.metallicFactor   = 0.0f;
	mat.roughnessFactor  = 0.9f;

	const auto path = std::filesystem::temp_directory_path() / "bmaterial_roundtrip_test.bmaterial";
	saveMaterial(mat, path);
	const auto restored = loadMaterial(path);
	std::filesystem::remove(path);

	REQUIRE(restored.name == "leaf");
	REQUIRE(restored.baseColorTexture == "tex0.ktx2");
	REQUIRE(restored.normalTexture.empty());
	REQUIRE(restored.roughnessFactor == Catch::Approx(0.9f));
}

TEST_CASE("bake writes a loadable .bmesh and its textures, and no materials", "[bmesh][bake]")
{
	const std::filesystem::path glb = "assets/suzanne.glb";
	REQUIRE(std::filesystem::exists(glb));

	const auto import = loadFromGltf(glb);
	REQUIRE(
		import.materials.size() >= 1);  // the glTF has materials; the bake must not carry them over

	const auto outDir = std::filesystem::temp_directory_path() / "bake_suzanne_test";
	std::filesystem::remove_all(outDir);
	bake(import, outDir, "suzanne");

	REQUIRE(std::filesystem::exists(outDir / "suzanne.bmesh"));

	// The textures do come across: they are what a material, once authored, routes at.
	for (size_t i = 0; i < import.textures.size(); ++i)
		REQUIRE(std::filesystem::exists(outDir / ("tex" + std::to_string(i) + ".ktx2")));

	// The glTF's PBR materials do not. Nothing is written for them, and -- this is the part that used
	// to be wrong -- the container does not name files that were never written: every submesh comes out
	// unassigned rather than pointing at a matN.bmaterial that does not exist.
	REQUIRE_FALSE(std::filesystem::exists(outDir / "mat0.bmaterial"));

	const auto mesh = load(outDir / "suzanne.bmesh");
	REQUIRE(mesh.materials.empty());
	REQUIRE_FALSE(mesh.submeshes.empty());

	for (const Submesh& submesh : mesh.submeshes) REQUIRE(submesh.material == c_InvalidIndex);

	std::filesystem::remove_all(outDir);
}

TEST_CASE("attachMaterial binds a material to an imported submesh", "[bmesh][bmaterial][attach]")
{
	// The other half of the contract: an import leaves the submeshes unassigned, and this is how they
	// get a material -- what the material editor calls when a material is saved.
	const std::filesystem::path glb = "assets/suzanne.glb";
	REQUIRE(std::filesystem::exists(glb));

	auto mesh = toBMesh(loadFromGltf(glb));
	REQUIRE(mesh.materials.empty());
	REQUIRE_FALSE(mesh.submeshes.empty());

	REQUIRE(attachMaterial(mesh, 0, "Materials/suzanne.bmaterial"));

	REQUIRE(mesh.materials.size() == 1);
	REQUIRE(mesh.materials[0] == "Materials/suzanne.bmaterial");
	REQUIRE(mesh.submeshes[0].material == 0);

	// Attaching the same material again is a no-op, not a duplicate slot.
	REQUIRE_FALSE(attachMaterial(mesh, 0, "Materials/suzanne.bmaterial"));
	REQUIRE(mesh.materials.size() == 1);
}
