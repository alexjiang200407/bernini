#include <assetlib/codecs.h>
#include <assetlib/import_document.h>

#include <assetlib/asset_import.h>
#include <assetlib/asset_refs.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Node.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <core/file/LooseFileSystem.h>
#include <core/file/file.h>

#include "RefsSandbox.h"

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;

	// The container a person edits by hand, so these cases are written against its text. Its bytes
	// are that text verbatim -- see AssetCodec<ImportDocument> -- which is what makes the pair below
	// a reinterpretation rather than a second encoding.
	std::string
	DocumentText(const ImportDocument& document)
	{
		const std::vector<std::byte> bytes = AssetCodec<ImportDocument>::Serialize(document);
		return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	ImportDocument
	DocumentFrom(std::string_view text)
	{
		return AssetCodec<ImportDocument>::Deserialize(
			std::as_bytes(std::span(text.data(), text.size())));
	}

	void
	WriteText(const fs::path& file, std::string_view text)
	{
		fs::create_directories(file.parent_path());
		core::file::write_atomic(file, std::as_bytes(std::span(text.data(), text.size())));
	}
}

TEST_CASE("an import document records where its textures went", "[importdoc]")
{
	ImportDocument document;
	document.textureDir = "Derived/SourceTextures/kirk";

	document.textureStamp = { 4096, 0xfeedfacecafebeefull };

	const ImportDocument read = DocumentFrom(DocumentText(document));
	CHECK(read.textureDir == "Derived/SourceTextures/kirk");
	CHECK(read.textureStamp == document.textureStamp);

	SECTION("an import that extracted no textures writes no key at all")
	{
		// So a document for such an import stays byte-identical to one written before the key
		// existed, which is what keeps migrate's byte-compare from reporting every project once.
		const std::string none = DocumentText(ImportDocument());
		CHECK(none.find("texture") == std::string::npos);
		CHECK(DocumentFrom(none).textureDir.empty());
		CHECK(DocumentFrom(none).textureStamp == SourceStamp());
	}

	SECTION("a document written before the key existed reads as no folder")
	{
		const auto old = DocumentFrom(R"({"parameters":{"sampleRate":30.0}})");
		CHECK(old.textureDir.empty());
		CHECK(old.textureStamp == SourceStamp());
	}

	SECTION("it is outside the parameters, so it does not key the cache")
	{
		// Where the textures went does not change what the importer computes, and a cache key that
		// moved with it would stale every mesh in the project over a folder.
		ImportDocument elsewhere = document;
		elsewhere.textureDir     = "Derived/SourceTextures/somewhere_else";
		CHECK(parametersHashOf(elsewhere) == parametersHashOf(document));
	}

	SECTION("a non-string folder is refused rather than defaulted")
	{
		CHECK_THROWS(DocumentFrom(R"({"textureDir":7})"));
		CHECK_THROWS(DocumentFrom(R"({"textureStampHash":"beef"})"));
	}
}

TEST_CASE("an import document round-trips, canonically", "[importdoc]")
{
	ImportDocument document;
	document.sampleRate = 60.0f;
	document.bindings   = { { "kirk[1]", "Authored/Materials/kirk/teeth.bmaterial" },
		                    { "kirk[0]", "Authored/Materials/kirk/skin.bmaterial" } };

	const std::string text = DocumentText(document);
	ImportDocument    read = DocumentFrom(text);

	CHECK(read.sampleRate == 60.0f);
	REQUIRE(read.bindings.size() == 2);
	// An object's keys come back sorted; the order a caller pushed them in is not part of the document.
	CHECK(
		read.bindings[0] == MaterialBinding{ "kirk[0]", "Authored/Materials/kirk/skin.bmaterial" });
	CHECK(
		read.bindings[1] ==
		MaterialBinding{ "kirk[1]", "Authored/Materials/kirk/teeth.bmaterial" });

	SECTION("identical documents serialize to identical bytes, whatever the construction order")
	{
		ImportDocument reversed;
		reversed.sampleRate = 60.0f;
		reversed.bindings   = { { "kirk[0]", "Authored/Materials/kirk/skin.bmaterial" },
			                    { "kirk[1]", "Authored/Materials/kirk/teeth.bmaterial" } };
		CHECK(DocumentText(reversed) == text);
	}

	SECTION("a serialize-deserialize-serialize cycle is byte-stable")
	{
		CHECK(DocumentText(read) == text);
	}
}

TEST_CASE(
	"keys a reader does not know survive the round-trip, in the half they arrived in",
	"[importdoc]")
{
	const std::string_view text = R"({
	"bindings": { "cube[0]": "Authored/Materials/a.bmaterial" },
	"futureKnob": { "nested": [1, 2, 3] },
	"parameters": { "sampleRate": 24.0, "tangentMode": "mikkt" }
})";

	const ImportDocument document = DocumentFrom(text);
	CHECK(document.sampleRate == 24.0f);
	CHECK(document.extraJson.find("futureKnob") != std::string::npos);
	// An unknown *parameter* stays in the parameter half -- the subtree the cache key hashes --
	// so a newer branch's knob keys even through a reader that has never heard of it.
	CHECK(document.extraParametersJson.find("tangentMode") != std::string::npos);
	CHECK(document.extraJson.find("tangentMode") == std::string::npos);

	const std::string rewritten = DocumentText(document);
	CHECK(rewritten.find("futureKnob") != std::string::npos);
	CHECK(rewritten.find("\"tangentMode\"") != std::string::npos);

	// And the unknown keys still deserialize to the same document after the rewrite.
	CHECK(DocumentFrom(rewritten) == document);
}

TEST_CASE("an empty or defaulted document reads back defaulted", "[importdoc]")
{
	const ImportDocument document = DocumentFrom("{}");
	CHECK(document.sampleRate == c_DefaultSampleRate);
	CHECK(document.bindings.empty());
}

TEST_CASE("a malformed import document is refused with its reason", "[importdoc]")
{
	CHECK_THROWS(DocumentFrom("not json"));
	CHECK_THROWS(DocumentFrom("[1, 2]"));
	CHECK_THROWS(DocumentFrom(R"({ "parameters": [] })"));
	CHECK_THROWS(DocumentFrom(R"({ "parameters": { "sampleRate": "fast" } })"));
	CHECK_THROWS(DocumentFrom(R"({ "parameters": { "sampleRate": 0 } })"));
	CHECK_THROWS(DocumentFrom(R"({ "bindings": [1] })"));
	CHECK_THROWS(DocumentFrom(R"({ "bindings": { "cube[0]": 7 } })"));

	// Two bindings for one submesh cannot exist in the object form a document stores; the in-memory
	// form can hold them, and serializing refuses rather than letting one silently win.
	ImportDocument colliding;
	colliding.bindings = { { "cube[0]", "Authored/Materials/a.bmaterial" },
		                   { "cube[0]", "Authored/Materials/b.bmaterial" } };
	CHECK_THROWS(DocumentText(colliding));
}

TEST_CASE("the document lives beside its source, one key from the other", "[importdoc]")
{
	CHECK(importDocumentKeyFor("Authored/Meshes/kirk.glb") == "Authored/Meshes/kirk.bimport");
	CHECK(importedSourceKeyFor("Authored/Meshes/kirk.bimport") == "Authored/Meshes/kirk.glb");
	CHECK_THROWS(importDocumentKeyFor("Authored/Meshes/no_extension"));
}

TEST_CASE(
	"the scan reads a .bimport: it holds its source and its materials",
	"[importdoc][assetrefs]")
{
	const DataRoot root("bernini_importdoc_scan");

	BMaterial material;
	material.name = "skin";
	core::file::write_atomic(
		root.path / "Authored/Materials" / "skin.bmaterial",
		AssetCodec<BMaterial>::Serialize(material));

	ImportDocument document;
	document.bindings = { { "kirk[0]", "Authored/Materials/skin.bmaterial" } };
	WriteText(root.path / "Authored/Meshes" / "kirk.bimport", DocumentText(document));
	WriteText(root.path / "Authored/Meshes" / "kirk.glb", "not really a glb");

	const AssetRefGraph graph = root.Scan();

	REQUIRE(graph.ReferrersOf("Authored/Meshes/kirk.glb").size() == 1);
	CHECK(
		graph.ReferrersOf("Authored/Meshes/kirk.glb")[0] ==
		AssetRef{ "Authored/Meshes/kirk.bimport",
	              "Authored/Meshes/kirk.glb",
	              RefKind::kImportedSource });

	bool documentHoldsMaterial = false;
	for (const AssetRef& ref : graph.ReferrersOf("Authored/Materials/skin.bmaterial"))
		documentHoldsMaterial |=
			ref.referrer == "Authored/Meshes/kirk.bimport" && ref.kind == RefKind::kSubmeshMaterial;
	CHECK(documentHoldsMaterial);
}

namespace
{
	BMesh
	NamedMesh(
		const std::vector<std::pair<std::string, uint32_t>>& submeshes,
		const std::vector<std::string>&                      materials)
	{
		BMesh mesh;
		mesh.materials = materials;
		for (const auto& [name, material] : submeshes)
		{
			Submesh submesh{};
			submesh.material   = material;
			submesh.nameOffset = mesh.stringPool.add(name);
			mesh.submeshes.push_back(submesh);
		}
		return mesh;
	}
}

TEST_CASE("an import records the bindings the mesh carries", "[importdoc]")
{
	const DataRoot root("bernini_importdoc_write");
	WriteText(root.path / "kirk.glb", "the source");

	const BMesh mesh = NamedMesh(
		{ { "kirk[0]", 0 }, { "kirk[1]", 1 }, { "props", c_InvalidIndex } },
		{ "Authored/Materials/skin.bmaterial", "Authored/Materials/teeth.bmaterial" });

	const ImportTarget target{ "kirk", 24.0f, "Derived/SourceTextures/kirk" };
	const AssetStore   store(root.path);
	const SourceRef    ref = store.CopyImportedSource(root.path / "kirk.glb", target);
	CHECK(ref.key == "Authored/Meshes/kirk.glb");
	CHECK(ref.stamp.size > 0);
	store.WriteImportedDocument(target, &mesh);

	CHECK(fs::exists(root.path / "Authored/Meshes/kirk.glb"));
	const ImportDocument document =
		loadImportDocument(core::file::LooseFileSystem(root.path), "Authored/Meshes/kirk.bimport");
	CHECK(document.sampleRate == 24.0f);
	REQUIRE(document.bindings.size() == 2);  // the unbound submesh records nothing
	CHECK(
		document.bindings[0] == MaterialBinding{ "kirk[0]", "Authored/Materials/skin.bmaterial" });
	CHECK(
		document.bindings[1] == MaterialBinding{ "kirk[1]", "Authored/Materials/teeth.bmaterial" });
}

TEST_CASE("a source that is not self-contained is refused", "[importdoc]")
{
	const DataRoot root("bernini_importdoc_gltf");
	WriteText(root.path / "kirk.gltf", "{}");

	CHECK_THROWS_WITH(
		AssetStore(root.path).CopyImportedSource(
			root.path / "kirk.gltf",
			ImportTarget{ "kirk", 30.0f, {} }),
		Catch::Matchers::ContainsSubstring("export as .glb"));
}

TEST_CASE("colliding submesh names are refused before anything is written", "[importdoc]")
{
	const BMesh colliding = NamedMesh(
		{ { "cube", 0 }, { "cube", 1 } },
		{ "Authored/Materials/a.bmaterial", "Authored/Materials/b.bmaterial" });
	CHECK_THROWS(requireUniqueSubmeshNames(colliding));

	const BMesh unique = NamedMesh(
		{ { "cube", 0 }, { "sphere", 1 } },
		{ "Authored/Materials/a.bmaterial", "Authored/Materials/b.bmaterial" });
	CHECK_NOTHROW(requireUniqueSubmeshNames(unique));
}

TEST_CASE("a document records the rig it binds and the outputs it produced", "[importdoc]")
{
	ImportDocument document;
	document.skeleton = "Derived/Skeletons/kirk.bskel";
	document.outputs  = { "Derived/Meshes/kirk.bmesh", "Derived/Animations/kirk.banim" };

	const ImportDocument read = DocumentFrom(DocumentText(document));
	CHECK(read.skeleton == "Derived/Skeletons/kirk.bskel");

	// Sorted on write, so two imports that wrote the same set in different orders are one byte
	// sequence -- the same rule every other key in this document follows.
	CHECK(
		read.outputs ==
		std::vector<std::string>{ "Derived/Animations/kirk.banim", "Derived/Meshes/kirk.bmesh" });

	SECTION("neither is written when empty, so a document from before them is byte-identical")
	{
		const std::string text = DocumentText(ImportDocument());
		CHECK_THAT(text, !Catch::Matchers::ContainsSubstring("skeleton"));
		CHECK_THAT(text, !Catch::Matchers::ContainsSubstring("outputs"));
	}

	SECTION("neither keys the cache, so recording them cannot stale a container")
	{
		CHECK(parametersHashOf(document) == parametersHashOf(ImportDocument()));
	}
}

TEST_CASE("a document refuses a skeleton or outputs of the wrong shape", "[importdoc]")
{
	CHECK_THROWS_WITH(
		DocumentFrom(R"({"skeleton": 7})"),
		Catch::Matchers::ContainsSubstring("'skeleton' is not a string"));

	CHECK_THROWS_WITH(
		DocumentFrom(R"({"outputs": "Derived/Meshes/kirk.bmesh"})"),
		Catch::Matchers::ContainsSubstring("'outputs' is not an array"));

	CHECK_THROWS_WITH(
		DocumentFrom(R"({"outputs": [7]})"),
		Catch::Matchers::ContainsSubstring("'outputs' holds a non-string entry"));
}
TEST_CASE("an authored clip floor round-trips as a parameter", "[importdoc][grounding]")
{
	ImportDocument document;
	document.clipFloors = { { "Run", 0.151f }, { "Land", -0.02f } };

	const std::string    text = DocumentText(document);
	const ImportDocument read = DocumentFrom(text);

	REQUIRE(read.clipFloors.size() == 2);
	CHECK(read.clipFloors[0] == ClipFloor{ "Land", -0.02f });
	CHECK(read.clipFloors[1] == ClipFloor{ "Run", 0.151f });

	// A parameter, not a binding: it changes the samples the cook writes, so the entry it wrote must
	// go stale when it is edited. That is what makes an override take effect at all.
	ImportDocument edited      = read;
	edited.clipFloors[0].floor = -0.03f;
	CHECK(parametersHashOf(edited) != parametersHashOf(read));

	SECTION("a document authoring none hashes as it did before the key existed")
	{
		// Writing an empty object would stale every container in every project on this change alone.
		const ImportDocument none;
		CHECK(DocumentText(none).find("clipFloor") == std::string::npos);
		CHECK(parametersHashOf(none) == parametersHashOf(DocumentFrom("{}")));
	}

	SECTION("two grounds for one clip are refused")
	{
		ImportDocument twice;
		twice.clipFloors = { { "Run", 0.0f }, { "Run", 1.0f } };
		CHECK_THROWS(DocumentText(twice));
	}
}
