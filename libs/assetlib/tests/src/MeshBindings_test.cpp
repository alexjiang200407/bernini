#include "CountingFileSystem.h"
#include "RefsSandbox.h"
#include <assetlib/AssetStore.h>
#include <assetlib/ImportIdentity.h>
#include <assetlib/RegenMesh.h>
#include <assetlib/asset_refs.h>
#include <assetlib/bmesh.h>
#include <assetlib/codecs.h>
#include <assetlib/container_info.h>
#include <assetlib/import_document.h>
#include <assetlib/pak.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Grass.h>
#include <assetlib_structs/GrassGeometry.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <core/file/LooseFileSystem.h>
#include <core/file/file.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace assetlib;
using namespace assetlib::test;

TEST_CASE(
	"moving an identified source leaves mesh bytes intact and bindings loadable",
	"[mesh-bindings][assetrename]")
{
	const DataRoot root("bernini-generated-source-move");
	const auto     store    = root.Source();
	auto           document = ImportDocument();
	document.source         = "Authored/Meshes/street.glb";
	document.identity       = { 1, "street.glb" };
	const auto output       = importOutputKey(document.identity, AssetType::kMesh);
	document.outputs        = { output };
	document.bindings       = { { "Road", "Authored/Materials/road.bmaterial" } };
	store.Save(document, importDocumentKeyFor(document.source));
	core::file::write_atomic(root.path / document.source, "fixture source");
	auto mesh                         = MakeMesh({ "Authored/Materials/old.bmaterial" });
	mesh.submeshes.front().nameOffset = mesh.stringPool.add("Road");
	mesh.source.key                   = document.source;
	mesh.source.stamp                 = stampOf(root.path / document.source);
	mesh.source.parametersHash        = parametersHashOf(document);
	store.Save(mesh, output);
	const auto       before   = store.GetFiles().Read(output);
	const auto       loose    = std::make_shared<core::file::LooseFileSystem>(root.path);
	const auto       counting = std::make_shared<CountingFileSystem>(*loose);
	const AssetStore counted(root.path, counting);
	CHECK(
		counted.LoadRegenMeshRefs(output).materials ==
		std::vector<std::string>{ "Authored/Materials/road.bmaterial" });
	CHECK(counting->ReadsOf(document.source) == 0);
	const auto graph = root.Scan();
	CHECK_THROWS(planRename(graph, output, "Derived/Meshes/renamed.bmesh"));
	std::filesystem::create_directories(root.path / "Authored/Meshes/town");
	const auto plan = planRename(graph, document.source, "Authored/Meshes/town/avenue.glb");
	CHECK(plan.outputs.empty());
	REQUIRE(store.RenameAsset(plan).status == RenameStatus::kRenamed);
	const auto movedKey = "Authored/Meshes/town/avenue.bimport";
	const auto moved    = store.Load<ImportDocument>(movedKey);
	CHECK(moved.identity == document.identity);
	CHECK(moved.outputs == document.outputs);
	CHECK_FALSE(store.Exists(document.source));
	CHECK(store.GetFiles().Read(output) == before);
	CHECK_FALSE(store.GeometryIsStale(output));
	CHECK(
		store.LoadRegenMeshRefs(output).materials ==
		std::vector<std::string>{ "Authored/Materials/road.bmaterial" });
	CHECK(
		store.LoadRegenMesh(output).bindings.submeshMaterials ==
		std::vector<std::string>{ "Authored/Materials/road.bmaterial" });
	const auto archive = root.path / "Data.bpak";
	PakWriter  writer(archive);
	writer.Add(movedKey, AssetCodec<ImportDocument>::Serialize(moved), {});
	writer.Add(output, before, { before.size(), 0 });
	writer.Finish();
	const AssetStore packed(root.path, std::make_shared<PakFile>(archive));
	CHECK_FALSE(packed.Exists(moved.source));
	CHECK_FALSE(packed.GeometryIsStale(output));
	CHECK(
		packed.LoadRegenMeshRefs(output).materials ==
		std::vector<std::string>{ "Authored/Materials/road.bmaterial" });
	CHECK(
		packed.LoadRegenMesh(output).bindings.submeshMaterials ==
		std::vector<std::string>{ "Authored/Materials/road.bmaterial" });
	CHECK(store.GetFiles().Read(output) == before);
}

TEST_CASE("packed mesh loads refuse mismatched sidecar parameters", "[mesh-bindings]")
{
	const DataRoot root("bernini-packed-binding-mismatch");
	auto           document    = ImportDocument();
	document.source            = "Authored/Meshes/street.glb";
	auto mesh                  = MakeMesh({});
	mesh.source.key            = document.source;
	mesh.source.parametersHash = parametersHashOf(document);
	document.sampleRate        = 60;
	const auto archive         = root.path / "Data.bpak";
	PakWriter  writer(archive);
	writer.Add(
		importDocumentKeyFor(document.source),
		AssetCodec<ImportDocument>::Serialize(document),
		{});
	const auto meshBytes = AssetCodec<BMesh>::Serialize(mesh);
	writer.Add("Derived/Meshes/street.bmesh", meshBytes, { meshBytes.size(), 0 });
	writer.Finish();
	const AssetStore packed(root.path, std::make_shared<PakFile>(archive));
	CHECK_THROWS(packed.LoadRegenMesh("Derived/Meshes/street.bmesh"));
	CHECK(packed.GeometryIsStale("Derived/Meshes/street.bmesh"));
}

TEST_CASE("loose and packed mesh loads resolve the same owned binding snapshot", "[mesh-bindings]")
{
	const DataRoot root("bernini-mesh-bindings");
	const auto     store       = root.Source();
	auto           document    = ImportDocument();
	document.source            = "Authored/Meshes/street.glb";
	document.identity          = { 1, "street.glb" };
	const auto output          = importOutputKey(document.identity, AssetType::kMesh);
	document.outputs           = { output };
	document.bindings          = { { "Road", "Authored/Materials/road.bmaterial" },
		                           { "Pavement", "Authored/Materials/pavement.bmaterial" },
		                           { "Verge", "Authored/Grass/verge.bgrass" } };
	document.materialOverrides = { { "Road", "wet", "Authored/Materials/wet.bmaterial" } };
	document.skeleton          = "Derived/Skeletons/shared.bskel";
	auto mesh =
		MakeMesh({ "Authored/Materials/old.bmaterial", "Authored/Materials/old.bmaterial" });
	mesh.submeshes[0].nameOffset = mesh.stringPool.add("Road");
	mesh.submeshes[1].nameOffset = mesh.stringPool.add("Pavement");
	mesh.submeshes[1].material   = mesh.submeshes[0].material;
	mesh.grassFields.fields      = { NamedGrassField{ "Verge", GrassField{ 0, 0, 0, 1 } } };
	mesh.grassFields.chunks      = { GrassChunk{ .clumpCount = 1 } };
	mesh.grassFields.clumps.resize(1);
	mesh.source.key            = document.source;
	mesh.source.parametersHash = parametersHashOf(document);
	const auto key             = importDocumentKeyFor(document.source);
	store.Save(document, key);
	store.Save(mesh, output);
	RegenMesh loaded;
	if (GENERATE(false, true))
	{
		const auto archive = root.path / "Data.bpak";
		PakWriter  writer(archive);
		writer.Add(key, AssetCodec<ImportDocument>::Serialize(document), {});
		const auto meshBytes = AssetCodec<BMesh>::Serialize(mesh);
		writer.Add(output, meshBytes, { meshBytes.size(), 0 });
		writer.Finish();
		const AssetStore packed(root.path, std::make_shared<PakFile>(archive));
		loaded = packed.LoadRegenMesh(output);
	}
	else
	{
		loaded = store.LoadRegenMesh(output);
	}
	CHECK(
		loaded.bindings.submeshMaterials ==
		std::vector<std::string>{ "Authored/Materials/road.bmaterial",
	                              "Authored/Materials/pavement.bmaterial" });
	REQUIRE(loaded.bindings.materialOverrides.size() == 1);
	CHECK(loaded.bindings.materialOverrides[0].submesh == 0);
	CHECK(loaded.bindings.materialOverrides[0].name == "wet");
	CHECK(loaded.bindings.materialOverrides[0].material == "Authored/Materials/wet.bmaterial");
	CHECK(loaded.bindings.skeleton == document.skeleton);
	CHECK(loaded.bindings.grassLooks == std::vector<std::string>{ "Authored/Grass/verge.bgrass" });
	const auto before = store.GetFiles().Read(output);
	document.bindings.clear();
	document.materialOverrides.clear();
	store.Save(document, key);
	const auto rebound = store.LoadRegenMesh(output);
	CHECK(rebound.bindings.submeshMaterials == std::vector<std::string>(2));
	CHECK(rebound.bindings.grassLooks == std::vector<std::string>(1));
	CHECK(rebound.bindings.materialOverrides.empty());
	CHECK(loaded.bindings.submeshMaterials[0] == "Authored/Materials/road.bmaterial");
	CHECK(store.GetFiles().Read(output) == before);
}
