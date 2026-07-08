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

TEST_CASE("deserializeMaterial reads a v1 stream as a Baked material", "[bmaterial][io]")
{
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

	const auto mat = deserializeMaterial(v1);

	REQUIRE(mat.mode == MaterialMode::kBaked);
	REQUIRE(mat.name == "old");
	REQUIRE(mat.baseColorTexture == "base.ktx2");
	REQUIRE(mat.normalTexture.empty());
	REQUIRE(mat.ormTexture == "orm.ktx2");
	REQUIRE(mat.metallicFactor == Catch::Approx(0.6f));
	// A v1 file carries no routes; they default to unrouted.
	for (const auto& route : mat.routes) REQUIRE(route.texture.empty());
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
		REQUIRE(material.metallicFactor == Catch::Approx(import.materials[i].metallicFactor));
		REQUIRE(material.roughnessFactor == Catch::Approx(import.materials[i].roughnessFactor));

		// Any texture a material references must be a file bake actually emitted.
		for (const std::string& tex :
		     { material.baseColorTexture, material.normalTexture, material.ormTexture })
		{
			if (!tex.empty())
				REQUIRE(std::filesystem::exists(outDir / tex));
		}
	}

	std::filesystem::remove_all(outDir);
}
