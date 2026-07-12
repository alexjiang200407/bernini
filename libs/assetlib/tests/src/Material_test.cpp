#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>

#include <catch2/catch_approx.hpp>

using namespace assetlib;

TEST_CASE("a BMaterial survives a serialize round-trip", "[bmaterial][io]")
{
	BMaterial mat;
	mat.name                 = "brushed_metal";
	mat.pbr.baseColorTexture = "albedo.ktx2";
	mat.pbr.normalTexture    = "";  // absent
	mat.pbr.ormTexture       = "orm.ktx2";
	mat.pbr.baseColorFactor  = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
	mat.pbr.metallicFactor   = 0.75f;
	mat.pbr.roughnessFactor  = 0.25f;

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.name == mat.name);
	REQUIRE(restored.shadingModel == ShadingModel::kPbr);
	REQUIRE(restored.pbr.baseColorTexture == mat.pbr.baseColorTexture);
	REQUIRE(restored.pbr.normalTexture.empty());
	REQUIRE(restored.pbr.ormTexture == mat.pbr.ormTexture);
	REQUIRE(restored.pbr.baseColorFactor.r == Catch::Approx(0.1f));
	REQUIRE(restored.pbr.baseColorFactor.a == Catch::Approx(1.0f));
	REQUIRE(restored.pbr.metallicFactor == Catch::Approx(0.75f));
	REQUIRE(restored.pbr.roughnessFactor == Catch::Approx(0.25f));
}

TEST_CASE("a Loose BMaterial round-trips its mode and routes", "[bmaterial][io]")
{
	BMaterial mat;
	mat.mode                = MaterialMode::kLoose;
	mat.name                = "packed";
	mat.pbr.metallicFactor  = 0.5f;
	mat.pbr.roughnessFactor = 0.4f;
	mat.pbr.routes[0]       = { "albedo.ktx2", 0 };  // base color R
	mat.pbr.routes[1]       = { "albedo.ktx2", 1 };  // base color G
	mat.pbr.routes[5]       = { "packed.ktx2", 2 };  // roughness from packed.B
	mat.pbr.routes[7]       = { "normal.ktx2", 0 };  // normal X

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.mode == MaterialMode::kLoose);
	REQUIRE(restored.pbr.routes.size() == c_LooseChannelCount);
	REQUIRE(restored.pbr.routes[0].texture == "albedo.ktx2");
	REQUIRE(restored.pbr.routes[0].channel == 0);
	REQUIRE(restored.pbr.routes[1].channel == 1);
	REQUIRE(restored.pbr.routes[5].texture == "packed.ktx2");
	REQUIRE(restored.pbr.routes[5].channel == 2);
	REQUIRE(restored.pbr.routes[7].texture == "normal.ktx2");
	REQUIRE(restored.pbr.routes[2].texture.empty());  // an unrouted channel stays empty
	REQUIRE(restored.pbr.metallicFactor == Catch::Approx(0.5f));
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
	mat.mode                 = MaterialMode::kBaked;
	mat.pbr.baseColorTexture = "baked_basecolor.ktx2";

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.editorGraph.empty());
	REQUIRE(restored.pbr.baseColorTexture == "baked_basecolor.ktx2");
}

TEST_CASE("a BMaterial round-trips its bake provenance", "[bmaterial][io]")
{
	BMaterial mat;
	mat.pbr.routes[0]      = { "albedo.ktx2", 0 };
	mat.pbr.routeStamps[0] = { 4096, 1752000000 };
	mat.pbr.routeStamps[8] = { 1, -5 };  // mtime is signed: pre-epoch timestamps must survive

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.pbr.routeStamps[0].size == 4096);
	REQUIRE(restored.pbr.routeStamps[0].mtime == 1752000000);
	REQUIRE(restored.pbr.routeStamps[8].mtime == -5);
	REQUIRE(restored.pbr.routeStamps[3] == SourceStamp{});  // unstamped routes stay zeroed
}

TEST_CASE("a BMaterial carries both its sources and its baked triplet", "[bmaterial][io]")
{
	// The coexistence the format exists for: the bake fills the triplet without discarding the
	// routes that produced it, so the material can still be reopened and re-baked.
	BMaterial mat;
	mat.mode                 = MaterialMode::kBaked;
	mat.pbr.baseColorTexture = "mat_basecolor.ktx2";
	mat.pbr.ormTexture       = "mat_orm.ktx2";
	mat.pbr.routes[0]        = { "src/albedo.ktx2", 0 };
	mat.pbr.routeStamps[0]   = { 64, 7 };

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.mode == MaterialMode::kBaked);
	REQUIRE(restored.pbr.baseColorTexture == "mat_basecolor.ktx2");
	REQUIRE(restored.pbr.routes[0].texture == "src/albedo.ktx2");
	REQUIRE(restored.pbr.routeStamps[0].size == 64);
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
	mat.mode                 = MaterialMode::kBaked;
	mat.pbr.baseColorTexture = "mat_basecolor.ktx2";
	mat.pbr.routes[0]        = { "albedo.ktx2", 0 };

	SECTION("a material with no routes is never stale")
	{
		BMaterial imported;
		imported.pbr.baseColorTexture = "tex0.ktx2";
		REQUIRE_FALSE(bakeIsStale(imported, dir));
	}

	SECTION("routed but unstamped means it was never baked") { REQUIRE(bakeIsStale(mat, dir)); }

	SECTION("a matching stamp is fresh")
	{
		mat.pbr.routeStamps[0] = stampOf(source);
		REQUIRE_FALSE(bakeIsStale(mat, dir));
	}

	SECTION("a source that changed size is stale")
	{
		mat.pbr.routeStamps[0] = stampOf(source);
		{
			std::ofstream out(source, std::ios::binary);
			out << "aaaaaaaa";  // different size
		}
		REQUIRE(bakeIsStale(mat, dir));
	}

	SECTION("a deleted source is stale, not silently unchanged")
	{
		mat.pbr.routeStamps[0] = stampOf(source);
		std::filesystem::remove(source);
		REQUIRE(bakeIsStale(mat, dir));
	}

	SECTION("fresh sources but no bake output is stale")
	{
		mat.pbr.routeStamps[0]   = stampOf(source);
		mat.pbr.baseColorTexture = "";
		REQUIRE(bakeIsStale(mat, dir));
	}

	std::filesystem::remove_all(dir);
}

TEST_CASE("deserializeMaterial rejects every version but the current one", "[bmaterial][io]")
{
	// Exactly one version is readable, deliberately: nothing has shipped, so an out-of-date file is
	// re-cooked rather than decoded by a second reader that would have to be kept correct forever. The
	// check has to be loud, because the alternative to rejecting an old stream is not "it still works"
	// -- it is reading its bytes with the current layout and producing a material made of garbage.
	//
	// v5 is named explicitly: it is the layout v6 replaced, and every asset in the repo used to be one.
	const auto streamOfVersion = [](uint16_t version) {
		std::vector<std::byte> bytes;
		const auto             putPod = [&](auto value) {
			const auto* p = reinterpret_cast<const std::byte*>(&value);
			bytes.insert(bytes.end(), p, p + sizeof(value));
		};
		const auto putStr = [&](const std::string& s) {
			putPod(static_cast<uint32_t>(s.size()));
			const auto* p = reinterpret_cast<const std::byte*>(s.data());
			bytes.insert(bytes.end(), p, p + s.size());
		};

		putPod(static_cast<uint32_t>(0x54414D42u));  // magic
		putPod(version);                             // versionMajor
		putPod(static_cast<uint16_t>(0));            // versionMinor

		// A plausible old body. It should never be decoded, whatever it holds.
		putPod(glm::vec4(0.2f, 0.3f, 0.4f, 1.0f));
		putPod(0.6f);
		putPod(0.7f);
		putStr("old");
		putStr("base.ktx2");
		putStr("");
		putStr("orm.ktx2");

		return bytes;
	};

	REQUIRE_THROWS_AS(deserializeMaterial(streamOfVersion(1)), std::runtime_error);
	REQUIRE_THROWS_AS(deserializeMaterial(streamOfVersion(5)), std::runtime_error);
	REQUIRE_THROWS_AS(deserializeMaterial(streamOfVersion(7)), std::runtime_error);
}

TEST_CASE("deserializeMaterial rejects an unknown shading model", "[bmaterial][io]")
{
	// A tag the reader does not know is a payload it cannot decode: rejected, never guessed at.
	std::vector<std::byte> bytes;
	const auto             putPod = [&](auto value) {
		const auto* p = reinterpret_cast<const std::byte*>(&value);
		bytes.insert(bytes.end(), p, p + sizeof(value));
	};

	putPod(static_cast<uint32_t>(0x54414D42u));  // magic
	putPod(static_cast<uint16_t>(6));            // the current version...
	putPod(static_cast<uint16_t>(0));
	putPod(static_cast<uint32_t>(999));  // ...but a shading model from the future

	REQUIRE_THROWS_AS(deserializeMaterial(bytes), std::runtime_error);
}

TEST_CASE("deserializeMaterial rejects a stream with bad magic", "[bmaterial][io]")
{
	const std::array<std::byte, 8> garbage{};
	REQUIRE_THROWS_AS(deserializeMaterial(garbage), std::runtime_error);
}

TEST_CASE("saveMaterial / loadMaterial round-trips through a file", "[bmaterial][io]")
{
	BMaterial mat;
	mat.name                 = "leaf";
	mat.pbr.baseColorTexture = "tex0.ktx2";
	mat.pbr.baseColorFactor  = glm::vec4(1.0f, 0.5f, 0.25f, 1.0f);
	mat.pbr.metallicFactor   = 0.0f;
	mat.pbr.roughnessFactor  = 0.9f;

	const auto path = std::filesystem::temp_directory_path() / "bmaterial_roundtrip_test.bmaterial";
	saveMaterial(mat, path);
	const auto restored = loadMaterial(path);
	std::filesystem::remove(path);

	REQUIRE(restored.name == "leaf");
	REQUIRE(restored.shadingModel == ShadingModel::kPbr);
	REQUIRE(restored.pbr.baseColorTexture == "tex0.ktx2");
	REQUIRE(restored.pbr.normalTexture.empty());
	REQUIRE(restored.pbr.roughnessFactor == Catch::Approx(0.9f));
}

TEST_CASE("bake writes a loadable .bmesh with sidecar materials", "[bmesh][bmaterial][bake]")
{
	const std::filesystem::path glb = "assets/suzanne.glb";
	REQUIRE(std::filesystem::exists(glb));

	const auto import = loadFromGltf(glb);
	REQUIRE(import.materials.size() >= 1);

	const auto outDir = std::filesystem::temp_directory_path() / "bake_suzanne_test";
	std::filesystem::remove_all(outDir);
	bake(import, outDir, "suzanne");

	// The container plus one .bmaterial per material are written to disk.
	REQUIRE(std::filesystem::exists(outDir / "suzanne.bmesh"));
	for (size_t i = 0; i < import.materials.size(); ++i)
		REQUIRE(std::filesystem::exists(outDir / ("mat" + std::to_string(i) + ".bmaterial")));

	// The loaded container references those material files by the same relative paths, and each one
	// loads and carries the import's factors.
	const auto mesh = load(outDir / "suzanne.bmesh");
	REQUIRE(mesh.materials.size() == import.materials.size());

	for (size_t i = 0; i < mesh.materials.size(); ++i)
	{
		const auto material = loadMaterial(outDir / mesh.materials[i]);

		// glTF is metallic-roughness, so an import is always a PBR material.
		REQUIRE(material.shadingModel == ShadingModel::kPbr);
		REQUIRE(material.pbr.metallicFactor == Catch::Approx(import.materials[i].metallicFactor));
		REQUIRE(material.pbr.roughnessFactor == Catch::Approx(import.materials[i].roughnessFactor));

		// Any texture a material references must be a file bake actually emitted.
		for (const std::string& tex :
		     { material.pbr.baseColorTexture, material.pbr.normalTexture, material.pbr.ormTexture })
		{
			if (!tex.empty())
				REQUIRE(std::filesystem::exists(outDir / tex));
		}
	}

	std::filesystem::remove_all(outDir);
}
