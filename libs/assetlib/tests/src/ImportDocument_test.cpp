#include <assetlib/import_document.h>

#include <assetlib/asset_refs.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib_structs/BMaterial.h>
#include <catch2/catch_test_macros.hpp>
#include <core/file/file.h>

#include "RefsSandbox.h"

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;

	void
	WriteText(const fs::path& file, std::string_view text)
	{
		fs::create_directories(file.parent_path());
		core::file::write_atomic(file, std::as_bytes(std::span(text.data(), text.size())));
	}
}

TEST_CASE("an import document round-trips, canonically", "[importdoc]")
{
	ImportDocument document;
	document.sampleRate = 60.0f;
	document.bindings   = { { "kirk[1]", "Materials/kirk/teeth.bmaterial" },
		                    { "kirk[0]", "Materials/kirk/skin.bmaterial" } };

	const std::string text = serializeImportDocument(document);
	ImportDocument    read = deserializeImportDocument(text);

	CHECK(read.sampleRate == 60.0f);
	REQUIRE(read.bindings.size() == 2);
	// An object's keys come back sorted; the order a caller pushed them in is not part of the document.
	CHECK(read.bindings[0] == MaterialBinding{ "kirk[0]", "Materials/kirk/skin.bmaterial" });
	CHECK(read.bindings[1] == MaterialBinding{ "kirk[1]", "Materials/kirk/teeth.bmaterial" });

	SECTION("identical documents serialize to identical bytes, whatever the construction order")
	{
		ImportDocument reversed;
		reversed.sampleRate = 60.0f;
		reversed.bindings   = { { "kirk[0]", "Materials/kirk/skin.bmaterial" },
			                    { "kirk[1]", "Materials/kirk/teeth.bmaterial" } };
		CHECK(serializeImportDocument(reversed) == text);
	}

	SECTION("a serialize-deserialize-serialize cycle is byte-stable")
	{
		CHECK(serializeImportDocument(read) == text);
	}
}

TEST_CASE(
	"keys a reader does not know survive the round-trip, in the half they arrived in",
	"[importdoc]")
{
	const std::string_view text = R"({
	"bindings": { "cube[0]": "Materials/a.bmaterial" },
	"futureKnob": { "nested": [1, 2, 3] },
	"parameters": { "sampleRate": 24.0, "tangentMode": "mikkt" }
})";

	const ImportDocument document = deserializeImportDocument(text);
	CHECK(document.sampleRate == 24.0f);
	CHECK(document.extraJson.find("futureKnob") != std::string::npos);
	// An unknown *parameter* stays in the parameter half -- the subtree the cache key hashes --
	// so a newer branch's knob keys even through a reader that has never heard of it.
	CHECK(document.extraParametersJson.find("tangentMode") != std::string::npos);
	CHECK(document.extraJson.find("tangentMode") == std::string::npos);

	const std::string rewritten = serializeImportDocument(document);
	CHECK(rewritten.find("futureKnob") != std::string::npos);
	CHECK(rewritten.find("\"tangentMode\"") != std::string::npos);

	// And the unknown keys still deserialize to the same document after the rewrite.
	CHECK(deserializeImportDocument(rewritten) == document);
}

TEST_CASE("an empty or defaulted document reads back defaulted", "[importdoc]")
{
	const ImportDocument document = deserializeImportDocument("{}");
	CHECK(document.sampleRate == c_DefaultSampleRate);
	CHECK(document.bindings.empty());
}

TEST_CASE("a malformed import document is refused with its reason", "[importdoc]")
{
	CHECK_THROWS(deserializeImportDocument("not json"));
	CHECK_THROWS(deserializeImportDocument("[1, 2]"));
	CHECK_THROWS(deserializeImportDocument(R"({ "parameters": [] })"));
	CHECK_THROWS(deserializeImportDocument(R"({ "parameters": { "sampleRate": "fast" } })"));
	CHECK_THROWS(deserializeImportDocument(R"({ "parameters": { "sampleRate": 0 } })"));
	CHECK_THROWS(deserializeImportDocument(R"({ "bindings": [1] })"));
	CHECK_THROWS(deserializeImportDocument(R"({ "bindings": { "cube[0]": 7 } })"));

	// Two bindings for one submesh cannot exist in the object form a document stores; the in-memory
	// form can hold them, and serializing refuses rather than letting one silently win.
	ImportDocument colliding;
	colliding.bindings = { { "cube[0]", "Materials/a.bmaterial" },
		                   { "cube[0]", "Materials/b.bmaterial" } };
	CHECK_THROWS(serializeImportDocument(colliding));
}

TEST_CASE("the document lives beside its source, one key from the other", "[importdoc]")
{
	CHECK(importDocumentKeyFor("meshes_src/kirk.glb") == "meshes_src/kirk.bimport");
	CHECK(importedSourceKeyFor("meshes_src/kirk.bimport") == "meshes_src/kirk.glb");
	CHECK_THROWS(importDocumentKeyFor("meshes_src/no_extension"));
}

TEST_CASE(
	"the scan reads a .bimport: it holds its source and its materials",
	"[importdoc][assetrefs]")
{
	const DataRoot root("bernini_importdoc_scan");

	BMaterial material;
	material.name = "skin";
	core::file::write_atomic(
		root.path / "Materials" / "skin.bmaterial",
		serializeMaterial(material));

	ImportDocument document;
	document.bindings = { { "kirk[0]", "Materials/skin.bmaterial" } };
	WriteText(root.path / "meshes_src" / "kirk.bimport", serializeImportDocument(document));
	WriteText(root.path / "meshes_src" / "kirk.glb", "not really a glb");

	const AssetRefGraph graph = root.Scan();

	REQUIRE(graph.ReferrersOf("meshes_src/kirk.glb").size() == 1);
	CHECK(
		graph.ReferrersOf("meshes_src/kirk.glb")[0] ==
		AssetRef{ "meshes_src/kirk.bimport", "meshes_src/kirk.glb", RefKind::kImportedSource });

	bool documentHoldsMaterial = false;
	for (const AssetRef& ref : graph.ReferrersOf("Materials/skin.bmaterial"))
		documentHoldsMaterial |=
			ref.referrer == "meshes_src/kirk.bimport" && ref.kind == RefKind::kSubmeshMaterial;
	CHECK(documentHoldsMaterial);
}
