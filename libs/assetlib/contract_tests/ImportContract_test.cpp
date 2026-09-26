#include "ImportClient.h"
#include "ImportHost.h"
#include <assetlib/AssetStore.h>
#include <assetlib/ImportIdentity.h>
#include <assetlib/RegenMesh.h>
#include <assetlib/ResolvedImport.h>
#include <assetlib/asset_refs.h>
#include <assetlib/import_document.h>
#include <assetlib_structs/Grass.h>
#include <assetlib_structs/Mesh.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <core/glm.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace assetlib::test
{
	namespace
	{
		constexpr auto c_Source = "Authored/Meshes/street.glb";
		constexpr auto c_Output = "Derived/Meshes/street.glb-3f9a1c7e0b24d5a6.bmesh";

		std::shared_ptr<ImportHost>
		makeHost()
		{
			auto host         = std::make_shared<ImportHost>();
			auto document     = newMeshDocument(c_Source);
			document.bindings = { { "Road", "Authored/Materials/road.bmaterial" },
				                  { "Verge", "Authored/Grass/verge.bgrass" } };
			document.skeleton = "Derived/Skeletons/shared.glb-0000000000000001.bskel";
			host->imports[{ c_Source, AssetType::kMesh }] = { "Authored/Meshes/street.bimport",
				                                              c_Output,
				                                              document };
			auto& geometry                                = host->meshes[c_Output];
			geometry.mesh.meshes.push_back(Mesh{ .firstSubmesh = 0, .submeshCount = 2 });
			geometry.mesh.submeshes.resize(2);
			geometry.mesh.grassFields.names = { "Verge" };
			geometry.mesh.grassFields.fields.push_back(GrassField{ 0, 0, 0, 1 });
			geometry.mesh.grassFields.chunks.push_back(GrassChunk{ glm::vec3(0), 1, 0, 1, 1 });
			geometry.mesh.grassFields.clumps.push_back(
				GrassClump{ glm::vec3(0), 1, glm::vec3(0, 1, 0), glm::u8vec4(255) });
			geometry.bindings.submeshMaterials  = { "Authored/Materials/road.bmaterial", "" };
			geometry.bindings.materialOverrides = {
				{ 0, "wet", "Authored/Materials/wet.bmaterial" }
			};
			geometry.bindings.skeleton   = document.skeleton;
			geometry.bindings.grassLooks = { "Authored/Grass/verge.bgrass" };
			geometry.unboundBindings     = { "RemovedSubmesh" };
			return host;
		}
	}

	TEST_CASE(
		"an import client carries one identity through its document and destinations",
		"[import-contract]")
	{
		const auto document = newMeshDocument(c_Source);
		CHECK(document.identity == ImportIdentity{ 0x3f9a1c7e0b24d5a6ull, "street.glb" });
		CHECK(document.source == c_Source);
		CHECK(document.outputs == std::vector<std::string>{ c_Output });
		CHECK(document.textureDir == "Derived/SourceTextures/street.glb-3f9a1c7e0b24d5a6");
		CHECK(ImportDocument().identity.id == 0);
	}

	TEST_CASE(
		"source resolution returns owned data and bindings independent of the host lifetime",
		"[import-contract]")
	{
		auto                      loaded = LoadedImport();
		std::weak_ptr<ImportHost> lifetime;
		{
			auto host      = makeHost();
			host->readOnly = GENERATE(false, true);
			lifetime       = host;
			const AssetStore store(std::filesystem::path{}, host);
			CHECK(store.IsReadOnly() == host->readOnly);
			loaded = loadMeshSource(store, c_Source);
			host->imports.clear();
			host->meshes.clear();
		}
		CHECK(lifetime.expired());
		CHECK(loaded.import.documentKey == "Authored/Meshes/street.bimport");
		CHECK(loaded.import.outputKey == c_Output);
		CHECK(loaded.import.document.identity.label == "street.glb");
		REQUIRE(loaded.geometry.bindings.submeshMaterials.size() == 2);
		CHECK(loaded.geometry.bindings.submeshMaterials[0] == "Authored/Materials/road.bmaterial");
		CHECK(loaded.geometry.bindings.submeshMaterials[1].empty());
		REQUIRE(loaded.geometry.bindings.materialOverrides.size() == 1);
		CHECK(loaded.geometry.bindings.materialOverrides[0].name == "wet");
		CHECK(loaded.geometry.bindings.materialOverrides[0].submesh == 0);
		CHECK(
			loaded.geometry.bindings.materialOverrides[0].material ==
			"Authored/Materials/wet.bmaterial");
		CHECK(loaded.geometry.bindings.skeleton == loaded.import.document.skeleton);
		CHECK(loaded.geometry.unboundBindings == std::vector<std::string>{ "RemovedSubmesh" });
		const auto& grass = loaded.geometry.mesh.grassFields;
		REQUIRE(grass.fields.size() == 1);
		CHECK(grass.names == std::vector<std::string>{ "Verge" });
		CHECK(
			loaded.geometry.bindings.grassLooks.at(grass.fields[0].look) ==
			"Authored/Grass/verge.bgrass");
		REQUIRE(grass.chunks.size() == 1);
		CHECK(grass.chunks[0].clumpCount == 1);
		REQUIRE(grass.clumps.size() == 1);
		CHECK(grass.clumps[0].heightScale == 1);
	}

	TEST_CASE(
		"a client consumes a new binding snapshot without mutating cooked geometry",
		"[import-contract]")
	{
		auto             host = makeHost();
		const AssetStore store(std::filesystem::path{}, host);
		const auto       before                             = loadMeshSource(store, c_Source);
		host->meshes[c_Output].bindings.submeshMaterials[0] = "Authored/Materials/new.bmaterial";
		host->meshes[c_Output].bindings.grassLooks[0].clear();
		const auto after = loadMeshSource(store, c_Source);
		CHECK(before.geometry.bindings.submeshMaterials[0] == "Authored/Materials/road.bmaterial");
		CHECK(after.geometry.bindings.submeshMaterials[0] == "Authored/Materials/new.bmaterial");
		CHECK(after.geometry.bindings.grassLooks[0].empty());
		CHECK(
			before.geometry.mesh.grassFields.clumps.size() ==
			after.geometry.mesh.grassFields.clumps.size());
	}

	TEST_CASE("lookup and load failures reach the source client", "[import-contract]")
	{
		auto             host = makeHost();
		const AssetStore store(std::filesystem::path{}, host);
		SECTION("missing source")
		{
			CHECK_THROWS_WITH(
				loadMeshSource(store, "Authored/Meshes/missing.glb"),
				"contract: no such output");
		}
		SECTION("a bound skeleton is not a produced output")
		{
			CHECK_THROWS_WITH(
				store.ResolveImport(c_Source, AssetType::kSkeleton),
				"contract: no such output");
		}
		SECTION("missing cooked output")
		{
			host->meshes.clear();
			CHECK_THROWS_WITH(loadMeshSource(store, c_Source), "contract: missing cooked mesh");
		}
		SECTION("the host refuses a stale group")
		{
			host->failure = "stale group";
			CHECK_THROWS_WITH(loadMeshSource(store, c_Source), "stale group");
		}
	}
}
