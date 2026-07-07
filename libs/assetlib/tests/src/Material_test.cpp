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
