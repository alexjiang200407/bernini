#include "RefsSandbox.h"
#include <assetlib/AssetStore.h>
#include <assetlib/ImportIdentity.h>
#include <assetlib/ResolvedImport.h>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>
#include <assetlib/pak.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace assetlib;
using namespace assetlib::test;

TEST_CASE("import identities distinguish sources sharing a filename", "[import-identity]")
{
	std::unordered_set<uint64_t> ids;
	for (int i = 0; i < 128; ++i)
	{
		const auto source =
			i % 2 == 0 ? "Authored/Meshes/town/crate.glb" : "Authored/Meshes/props/crate.glb";
		const auto identity = makeImportIdentity(source);
		CHECK(identity.id != 0);
		CHECK(identity.label == "crate.glb");
		CHECK(ids.insert(identity.id).second);
	}
}

TEST_CASE("derived keys use a frozen label and all 64 identity bits", "[import-identity]")
{
	const ImportIdentity identity{ 0xfedcba9876543210ull, "street.glb" };
	CHECK(
		importOutputKey(identity, AssetType::kMesh) ==
		"Derived/Meshes/street.glb-fedcba9876543210.bmesh");
	CHECK(
		importOutputKey(identity, AssetType::kSkeleton) ==
		"Derived/Skeletons/street.glb-fedcba9876543210.bskel");
	CHECK(
		importOutputKey(identity, AssetType::kAnimation) ==
		"Derived/Animations/street.glb-fedcba9876543210.banim");
	CHECK(
		importOutputKey(identity, AssetType::kSky) ==
		"Derived/Sky/street.glb-fedcba9876543210.bsky");
	CHECK(
		importOutputKey(identity, AssetType::kEnvLighting) ==
		"Derived/EnvLighting/street.glb-fedcba9876543210.benvl");
	CHECK(importTextureDirectory(identity) == "Derived/SourceTextures/street.glb-fedcba9876543210");
	CHECK(
		importOutputKey({ 1, "street.glb" }, AssetType::kMesh) ==
		"Derived/Meshes/street.glb-0000000000000001.bmesh");
	CHECK_THROWS(importOutputKey(identity, AssetType::kMaterial));
	CHECK_THROWS(importOutputKey({}, AssetType::kMesh));
	CHECK_THROWS(importTextureDirectory({ 1, "../street.glb" }));
	CHECK_THROWS(makeImportIdentity("../../street.glb"));
}

TEST_CASE("sidecars persist identity without hashing it as a cook parameter", "[import-identity]")
{
	auto       document      = ImportDocument();
	const auto parameterHash = parametersHashOf(document);
	document.identity        = { 0xfedcba9876543210ull, "street.glb" };
	document.extraJson       = R"({"identity":{"future":true}})";
	const auto bytes         = AssetCodec<ImportDocument>::Serialize(document);
	const auto read          = AssetCodec<ImportDocument>::Deserialize(bytes);
	CHECK(read.identity == document.identity);
	CHECK(read.extraJson == document.extraJson);
	CHECK(parametersHashOf(read) == parameterHash);
	CHECK(AssetCodec<ImportDocument>::Serialize(read) == bytes);
	const auto legacy =
		AssetCodec<ImportDocument>::Deserialize(AssetCodec<ImportDocument>::Serialize({}));
	CHECK(legacy.identity == ImportIdentity{});
}

TEST_CASE("invalid authored identities are refused", "[import-identity]")
{
	const auto json = GENERATE(
		R"({"identity":{}})",
		R"({"identity":{"id":"0000000000000000","label":"x.glb"}})",
		R"({"identity":{"id":"0000000000000001","label":"../x.glb"}})",
		R"({"identity":{"id":"FEDCBA9876543210","label":"x.glb"}})",
		R"({"identity":{"id":1,"label":"x.glb"}})",
		R"({"identity":{"id":"1","label":"x.glb"}})");
	const auto text = std::string_view(json);
	CHECK_THROWS(
		AssetCodec<ImportDocument>::Deserialize(
			std::as_bytes(std::span(text.data(), text.size()))));
}

TEST_CASE(
	"source lookup needs only the mounted sidecar and returns an owned snapshot",
	"[import-identity]")
{
	const DataRoot root("bernini-source-resolution");
	const auto     source   = "Authored/Meshes/street.glb";
	const auto     key      = importDocumentKeyFor(source);
	auto           document = ImportDocument();
	document.source         = source;
	document.identity       = { 1, "street.glb" };
	document.outputs        = { importOutputKey(document.identity, AssetType::kMesh) };
	document.bindings       = { { "Road", "Authored/Materials/road.bmaterial" } };
	const auto loose        = root.Source();
	loose.Save(document, key);
	ResolvedImport snapshot;
	if (GENERATE(false, true))
	{
		const auto archive = root.path / "Data.bpak";
		PakWriter  writer(archive);
		writer.Add(key, AssetCodec<ImportDocument>::Serialize(document), {});
		writer.Finish();
		const AssetStore packed(root.path, std::make_shared<PakFile>(archive));
		CHECK_FALSE(packed.Exists(source));
		snapshot = packed.ResolveImport(source, AssetType::kMesh);
	}
	else
	{
		CHECK_FALSE(loose.Exists(source));
		snapshot = loose.ResolveImport(source, AssetType::kMesh);
	}
	CHECK(snapshot.outputKey == document.outputs.front());
	CHECK(snapshot.documentKey == key);
	document.bindings.clear();
	loose.Save(document, key);
	REQUIRE(snapshot.document.bindings.size() == 1);
	CHECK(snapshot.document.bindings.front().material == "Authored/Materials/road.bmaterial");
	CHECK(loose.ResolveImport(source, AssetType::kMesh).document.bindings.empty());
}

TEST_CASE("source lookup refuses mismatched or ambiguous ownership", "[import-identity]")
{
	const DataRoot root("bernini-invalid-source-resolution");
	const auto     store    = root.Source();
	const auto     source   = "Authored/Meshes/street.glb";
	auto           document = ImportDocument();
	document.source         = source;
	document.identity       = { 1, "street.glb" };
	document.outputs        = { importOutputKey(document.identity, AssetType::kMesh) };
	SECTION("missing sidecar")
	{
		CHECK_THROWS(store.ResolveImport(source, AssetType::kMesh));
		return;
	}
	SECTION("source mismatch") { document.source = "Authored/Meshes/other.glb"; }
	SECTION("unmigrated document") { document.identity = {}; }
	SECTION("wrong name") { document.outputs = { "Derived/Meshes/other.bmesh" }; }
	SECTION("duplicate kind") { document.outputs.push_back(document.outputs.front()); }
	SECTION("no produced output") { document.outputs.clear(); }
	store.Save(document, importDocumentKeyFor(source));
	CHECK_THROWS(store.ResolveImport(source, AssetType::kMesh));
}
