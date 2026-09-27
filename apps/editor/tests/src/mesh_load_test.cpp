#include <QTemporaryDir>
#include <assetlib/AssetStore.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>
#include <assetlib_structs/BMesh.h>
#include <catch2/catch_test_macros.hpp>
#include <editor_sdk/mesh_load.h>
#include <filesystem>
#include <string>
#include <vector>

TEST_CASE("The editor mesh loader returns an owned binding snapshot", "[meshload]")
{
	const QTemporaryDir directory;
	REQUIRE(directory.isValid());
	const auto                 root = std::filesystem::path(directory.path().toStdString());
	const assetlib::AssetStore store(root);
	auto                       document = assetlib::ImportDocument();
	document.source                     = "Authored/Meshes/road.glb";
	document.bindings                   = { { "Road", "Authored/Materials/asphalt.bmaterial" } };
	const auto documentKey              = assetlib::importDocumentKeyFor(document.source);
	store.Save(document, documentKey);
	auto mesh                                = assetlib::BMesh();
	mesh.submeshes.emplace_back().nameOffset = mesh.stringPool.add("Road");
	mesh.source.key                          = document.source;
	mesh.source.parametersHash               = assetlib::parametersHashOf(document);
	const auto meshKey                       = "Derived/Meshes/road.bmesh";
	store.Save(mesh, meshKey);
	const auto before = store.GetFiles().Read(meshKey);
	const auto loaded = editor::LoadMeshThroughSeam(store, root / meshKey);
	CHECK(
		loaded.bindings.submeshMaterials ==
		std::vector<std::string>{ document.bindings[0].material });
	document.bindings[0].material = "Authored/Materials/snow.bmaterial";
	store.Save(document, documentKey);
	const auto rebound = editor::LoadMeshThroughSeam(store, root / meshKey);
	CHECK(rebound.bindings.submeshMaterials[0] == document.bindings[0].material);
	CHECK(loaded.bindings.submeshMaterials[0] == "Authored/Materials/asphalt.bmaterial");
	CHECK(store.GetFiles().Read(meshKey) == before);
}
