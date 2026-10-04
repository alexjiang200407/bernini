#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>
#include <assetlib/pak.h>
#include <assetlib/toon_shading_rig.h>
#include <assetlib_structs/BToonShadingRig.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "MountAt.h"
#include "RefsSandbox.h"

#include <catch2/catch_test_macros.hpp>

// The `.btoonrig`: the document, what it refuses and keeps, its head bone resolved by name, and
// the `.bimport` edge that names it -- what deletion, rename and pack see.

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	constexpr std::string_view c_RigKey      = "Authored/ToonRigs/face.btoonrig";
	constexpr std::string_view c_DocumentKey = "Authored/Meshes/head.bimport";

	/** Every value off its default, so a field the codec drops cannot pass for one it kept. */
	BToonShadingRig
	MakeRig()
	{
		auto key            = ToonShadingRigKey();
		key.light           = glm::vec3(0.6f, 0.2f, 0.77f);
		key.position        = glm::vec3(0.01f, 0.03f, 0.09f);
		key.gain            = -0.85f;
		key.size            = 0.115f;
		key.anisotropy      = 0.55f;
		key.sharpness       = 1.0f;
		key.bend            = 0.25f;
		key.bulge           = -0.5f;
		key.rotation        = 80.2f;
		key.radius          = 0.07f;
		key.normalSmoothing = 0.4f;

		auto edit         = ToonShadingRigEdit();
		edit.name         = "nose";
		edit.keys         = { key, key };
		edit.keys[1].gain = -0.2f;
		edit.keySharpness = 12.0f;
		edit.mirrored     = true;

		auto rig            = BToonShadingRig();
		rig.edits           = { edit };
		rig.faceLight       = { .minElevation      = -10.0f,
			                    .maxElevation      = 25.0f,
			                    .maxAzimuth        = 45.0f,
			                    .azimuthFadeStart  = 35.0f,
			                    .azimuthFadeEnd    = 80.0f,
			                    .azimuthFadeAmount = 0.75f };
		rig.headBone        = "neck_top";
		rig.headToBone      = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.1f, 0.02f)) *
		                      glm::rotate(glm::mat4(1.0f), 0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
		rig.headRadius      = 0.14f;
		rig.fadeStartPixels = 120.0f;
		rig.fadeEndPixels   = 40.0f;
		return rig;
	}

	std::string
	Text(const BToonShadingRig& rig)
	{
		const std::vector<std::byte> bytes = AssetCodec<BToonShadingRig>::Serialize(rig);
		return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	BToonShadingRig
	Parse(const std::string_view text)
	{
		return AssetCodec<BToonShadingRig>::Deserialize(
			std::as_bytes(std::span(text.data(), text.size())));
	}

	/** Two bones, `root` and a `head` child, enough to resolve a name against. */
	Skeleton
	MakeSkeleton()
	{
		auto skeleton = Skeleton();
		for (const char* name : { "root", "head" })
		{
			auto bone     = Bone();
			bone.bindPose = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
			bone.parent   = skeleton.bones.empty() ? c_InvalidIndex : 0;
			bone.nameOffset = skeleton.stringPool.add(name);
			skeleton.bones.push_back(bone);
		}
		return skeleton;
	}

	/** A character's document naming the rig. */
	void
	WriteDocument(const DataRoot& root)
	{
		auto document           = ImportDocument();
		document.source         = "Authored/Meshes/head.glb";
		document.toonShadingRig = std::string(c_RigKey);
		root.Source().Save(document, std::string(c_DocumentKey));
	}
}

TEST_CASE("A toon shading rig round-trips through its document", "[toonshadingrig][codec]")
{
	const BToonShadingRig rig  = MakeRig();
	const BToonShadingRig back = Parse(Text(rig));
	CHECK(back == rig);

	// Canonical: the document a load writes back is the document it read.
	CHECK(Text(back) == Text(rig));
}

TEST_CASE("A toon shading rig document is canonical JSON", "[toonshadingrig][codec]")
{
	const std::string text = Text(MakeRig());

	// Sorted, tab-indented, one trailing newline: the shape every authored document shares.
	const auto json = nlohmann::json::parse(text);
	CHECK(text == json.dump(1, '\t') + '\n');

	// Floats at their shortest decimal, so a hand-typed value survives a save as typed.
	CHECK(text.find("80.2,") != std::string::npos);
	CHECK(text.find("80.19") == std::string::npos);

	// headToBone is four rows, the translation down the last column as a matrix is written.
	const auto& rows = json.at("headToBone");
	REQUIRE(rows.size() == 4);
	CHECK(rows[1][3].get<float>() == 0.1f);
	CHECK(rows[3] == nlohmann::json::array({ 0, 0, 0, 1 }));
}

TEST_CASE(
	"A toon shading rig document that omits a key takes the default",
	"[toonshadingrig][codec]")
{
	CHECK(Parse("{}") == BToonShadingRig());
	CHECK(Parse(R"({"faceLight": {}})") == BToonShadingRig());

	const BToonShadingRig sparse = Parse(R"({
		"edits": [ { "keys": [ { "light": [1, 0, 0], "position": [0, 0, 0.1] } ] } ],
		"faceLight": { "maxAzimuth": 45 }
	})");
	REQUIRE(sparse.edits.size() == 1);
	CHECK(sparse.edits[0].keySharpness == 10.0f);
	CHECK_FALSE(sparse.edits[0].mirrored);
	CHECK(sparse.edits[0].name.empty());
	REQUIRE(sparse.edits[0].keys.size() == 1);
	CHECK(sparse.edits[0].keys[0].size == ToonShadingRigKey().size);
	CHECK(sparse.edits[0].keys[0].light == glm::vec3(1.0f, 0.0f, 0.0f));
	CHECK(sparse.faceLight.maxAzimuth == 45.0f);
	CHECK(sparse.faceLight.minElevation == ToonFaceLight().minElevation);
	CHECK(sparse.headBone.empty());
	CHECK(sparse.headToBone == glm::mat4(1.0f));
}

TEST_CASE(
	"A toon shading rig document keeps the keys it does not know, at any depth",
	"[toonshadingrig][codec]")
{
	const BToonShadingRig read = Parse(R"({
		"futureTopLevel": [1, 2],
		"faceLight": { "maxAzimuth": 40, "futureFaceKey": "kept" },
		"edits": [ {
			"futureEditKey": 3,
			"keys": [ { "light": [0, 0, 1], "position": [0, 0, 0], "futureKeyKey": { "x": 1 } } ]
		} ]
	})");

	BToonShadingRig edited       = read;
	edited.headRadius            = 0.2f;
	edited.edits[0].keys[0].gain = -0.5f;
	const auto written           = nlohmann::json::parse(Text(edited));

	CHECK(written.at("futureTopLevel") == nlohmann::json::array({ 1, 2 }));
	CHECK(written.at("faceLight").at("futureFaceKey").get<std::string>() == "kept");
	CHECK(written.at("faceLight").at("maxAzimuth").get<float>() == 40.0f);
	CHECK(written.at("edits")[0].at("futureEditKey").get<int>() == 3);
	CHECK(written.at("edits")[0].at("keys")[0].at("futureKeyKey").at("x").get<int>() == 1);
	CHECK(written.at("edits")[0].at("keys")[0].at("gain").get<float>() == -0.5f);
	CHECK(written.at("headRadius").get<float>() == 0.2f);
}

TEST_CASE("A toon shading rig document refuses what it cannot read", "[toonshadingrig][codec]")
{
	SECTION("a value of the wrong type")
	{
		CHECK_THROWS(Parse("[]"));
		CHECK_THROWS(Parse(R"({"headBone": 5})"));
		CHECK_THROWS(Parse(R"({"headRadius": "big"})"));
		CHECK_THROWS(Parse(R"({"faceLight": 3})"));
		CHECK_THROWS(Parse(R"({"faceLight": {"maxAzimuth": "wide"}})"));
		CHECK_THROWS(Parse(R"({"edits": {}})"));
		CHECK_THROWS(Parse(R"({"edits": [3]})"));
		CHECK_THROWS(Parse(R"({"edits": [{"keys": 1}]})"));
		CHECK_THROWS(Parse(R"({"edits": [{"keys": [], "mirrored": 1}]})"));
		CHECK_THROWS(Parse(R"({"edits": [{"keys": [{"light": [0, 1], "position": [0, 0, 0]}]}]})"));
		CHECK_THROWS(Parse(
			R"({"edits": [{"keys": [{"light": [0, 0, 1], "position": [0, 0, 0], "gain": true}]}]})"));
	}

	SECTION("a matrix that is not four rows of four")
	{
		CHECK_THROWS(Parse(R"({"headToBone": [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0]]})"));
		CHECK_THROWS(
			Parse(R"({"headToBone": [[1, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]})"));
	}

	SECTION("a required field that is missing")
	{
		CHECK_THROWS(Parse(R"({"edits": [{"mirrored": true}]})"));
		CHECK_THROWS(Parse(R"({"edits": [{"keys": [{"position": [0, 0, 0]}]}]})"));
		CHECK_THROWS(Parse(R"({"edits": [{"keys": [{"light": [0, 0, 1]}]}]})"));
	}

	SECTION("a head bone that names nothing")
	{
		// Absent is the placement's frame; an empty name would be a second spelling of it.
		CHECK_THROWS(Parse(R"({"headBone": ""})"));
	}
}

TEST_CASE("A toon shading rig leaves range checks to the renderer", "[toonshadingrig][codec]")
{
	auto rig                   = BToonShadingRig();
	rig.headRadius             = -1.0f;
	rig.edits                  = { ToonShadingRigEdit() };
	const BToonShadingRig back = Parse(Text(rig));
	CHECK(back.headRadius == -1.0f);
	CHECK(back.edits.size() == 1);
	CHECK(back.edits[0].keys.empty());
}

TEST_CASE(
	"A toon shading rig is authored, so the store refuses it under Derived",
	"[toonshadingrig][codec]")
{
	const DataRoot root("bernini_toonrig_origin");
	CHECK_NOTHROW(StoreAt(root.path).Save(MakeRig(), std::string(c_RigKey)));
	CHECK_THROWS(StoreAt(root.path).Save(MakeRig(), "Derived/ToonRigs/face.btoonrig"));
	CHECK(StoreAt(root.path).Load<BToonShadingRig>(std::string(c_RigKey)) == MakeRig());
}

TEST_CASE("A toon shading rig's head bone resolves by name", "[toonshadingrig]")
{
	const Skeleton skeleton = MakeSkeleton();

	auto rig = BToonShadingRig();
	CHECK_FALSE(resolveHeadBone(rig, skeleton).has_value());

	rig.headBone = "head";
	CHECK(resolveHeadBone(rig, skeleton) == std::optional<uint32_t>(1));

	rig.headBone = "skull";
	CHECK_THROWS(resolveHeadBone(rig, skeleton));
}

TEST_CASE(
	"An import document names its toon shading rig outside its parameters",
	"[toonshadingrig][codec]")
{
	auto document           = ImportDocument();
	document.source         = "Authored/Meshes/head.glb";
	const uint64_t before   = parametersHashOf(document);
	document.toonShadingRig = std::string(c_RigKey);

	// Naming a rig changes nothing the importer computes, so no cache entry goes stale over it.
	CHECK(parametersHashOf(document) == before);

	const std::vector<std::byte> bytes = AssetCodec<ImportDocument>::Serialize(document);
	const auto                   json  = nlohmann::json::parse(
		std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
	CHECK(json.at("toonShadingRig").get<std::string>() == c_RigKey);
	CHECK(AssetCodec<ImportDocument>::Deserialize(bytes) == document);

	// Omitted when there is none, so a document without one is byte-identical to before the key.
	document.toonShadingRig.clear();
	const std::vector<std::byte> bare = AssetCodec<ImportDocument>::Serialize(document);
	CHECK(
		std::string(reinterpret_cast<const char*>(bare.data()), bare.size())
			.find("toonShadingRig") == std::string::npos);
}

TEST_CASE(
	"The reference scan holds a toon shading rig alive through the import naming it",
	"[toonshadingrig][assetrefs]")
{
	const DataRoot root("bernini_toonrig_refs");
	StoreAt(root.path).Save(MakeRig(), std::string(c_RigKey));
	WriteDocument(root);

	const AssetRefGraph graph = root.Scan();

	auto toRig = std::vector<AssetRef>();
	for (const AssetRef& ref : graph.ReferencesOf(c_DocumentKey))
		if (ref.target == c_RigKey)
			toRig.push_back(ref);
	REQUIRE(toRig.size() == 1);
	CHECK(toRig[0].kind == RefKind::kToonShadingRig);
	CHECK(isStoredRef(RefKind::kToonShadingRig));

	// What a deletion of the rig consults: the character holds it, so it is refused.
	CHECK(ReferrerPaths(graph, c_RigKey) == std::vector<std::string>{ std::string(c_DocumentKey) });
	CHECK_FALSE(planDeletion(graph, c_RigKey).Allowed());

	// A rig names no file, so it holds nothing alive itself.
	CHECK(graph.ReferencesOf(c_RigKey).empty());
}

TEST_CASE(
	"Renaming a toon shading rig rewrites the import naming it",
	"[toonshadingrig][assetrename]")
{
	const DataRoot root("bernini_toonrig_rename");
	StoreAt(root.path).Save(MakeRig(), std::string(c_RigKey));
	WriteDocument(root);

	constexpr std::string_view c_Renamed = "Authored/ToonRigs/hero.btoonrig";
	const RenamePlan           plan      = planRename(root.Scan(), c_RigKey, c_Renamed);
	REQUIRE(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);

	const auto document = StoreAt(root.path).Load<ImportDocument>(std::string(c_DocumentKey));
	CHECK(document.toonShadingRig == c_Renamed);
	CHECK(StoreAt(root.path).Load<BToonShadingRig>(std::string(c_Renamed)) == MakeRig());
	CHECK_FALSE(StoreAt(root.path).Exists(std::string(c_RigKey)));
}

TEST_CASE("Pack ships a toon shading rig verbatim", "[toonshadingrig][pack]")
{
	const DataRoot root("bernini_toonrig_pack");
	StoreAt(root.path).Save(MakeRig(), std::string(c_RigKey));
	const std::vector<std::byte> bytes = StoreAt(root.path).GetFiles().Read(c_RigKey);

	static_cast<void>(StoreAt(root.path).Pack(PackDesc{ root.path / "Data.bpak" }));

	const PakFile pak(root.path / "Data.bpak");
	REQUIRE(pak.Exists(c_RigKey));
	CHECK(pak.Read(c_RigKey) == bytes);
}
