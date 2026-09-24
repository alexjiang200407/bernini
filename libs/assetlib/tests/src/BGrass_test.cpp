#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>
#include <assetlib_structs/BGrass.h>
#include <assetlib_structs/BMaterial.h>
#include <core/glm.h>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "MountAt.h"
#include "RefsSandbox.h"

#include <catch2/catch_test_macros.hpp>

// The `.bgrass`: the document itself, what it refuses, what it keeps that it does not know, and the
// two operations its stored reference to a `.bmaterial` has to survive -- a reference scan and a
// rename of the material it names.

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	constexpr std::string_view c_MaterialKey = "Authored/Materials/verge.bmaterial";
	constexpr std::string_view c_GrassKey    = "Authored/Grass/verge.bgrass";

	/** Every value off its default, so a field the codec drops cannot pass for one it kept. */
	BGrass
	MakeGrass()
	{
		auto grass     = BGrass();
		grass.material = std::string(c_MaterialKey);

		grass.blade    = { .minHeight    = 0.21f,
			               .maxHeight    = 0.63f,
			               .rootWidth    = 0.041f,
			               .tipWidth     = 0.25f,
			               .curvature    = 0.45f,
			               .lean         = 0.12f,
			               .nearSegments = 6,
			               .farSegments  = 2 };
		grass.clump    = { .bladesPerClump = 11, .radius = 0.2f };
		grass.density  = { .fadeStart = 12.5f, .fadeEnd = 75.0f, .widening = 1.5f };
		grass.response = { .stiffness = 0.35f, .gustResponse = 0.8f };
		grass.lighting = { .rootOcclusion     = 0.7f,
			               .normalRounding    = 0.6f,
			               .groundNormalNear  = 0.1f,
			               .groundNormalFar   = 0.9f,
			               .translucencyColor = glm::vec3(0.8f, 1.0f, 0.4f),
			               .translucency      = 0.3f };
		grass.color    = { .rootTint  = glm::vec3(0.4f, 0.5f, 0.2f),
			               .tipTint   = glm::vec3(0.9f, 1.0f, 0.6f),
			               .variation = 0.25f };
		return grass;
	}

	std::string
	Text(const BGrass& grass)
	{
		const std::vector<std::byte> bytes = AssetCodec<BGrass>::Serialize(grass);
		return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	BGrass
	Parse(const std::string_view text)
	{
		return AssetCodec<BGrass>::Deserialize(std::as_bytes(std::span(text.data(), text.size())));
	}
}

TEST_CASE("A grass look round-trips through its document", "[grass][codec]")
{
	const BGrass grass = MakeGrass();
	const BGrass back  = Parse(Text(grass));
	CHECK(back == grass);

	// Canonical: the document a load writes back is the document it read.
	CHECK(Text(back) == Text(grass));
}

TEST_CASE("A grass document that omits a key takes the default", "[grass][codec]")
{
	CHECK(Parse("{}") == BGrass());
	CHECK(Parse(R"({"blade": {}})") == BGrass());

	auto lean       = BGrass();
	lean.blade.lean = 0.5f;
	CHECK(Parse(R"({"blade": {"lean": 0.5}})") == lean);
}

TEST_CASE("A grass document keeps the keys it does not know, at any depth", "[grass][codec]")
{
	const BGrass read = Parse(R"({
		"futureTopLevel": [1, 2],
		"blade": { "lean": 0.4, "futureBladeKey": "kept" },
		"futureGroup": { "x": 1 }
	})");
	CHECK(read.blade.lean == 0.4f);

	BGrass edited      = read;
	edited.blade.lean  = 0.1f;
	edited.material    = std::string(c_MaterialKey);
	const auto written = nlohmann::json::parse(Text(edited));

	CHECK(written["futureTopLevel"] == nlohmann::json::array({ 1, 2 }));
	CHECK(written["futureGroup"]["x"] == 1);
	CHECK(written["blade"]["futureBladeKey"] == "kept");
	CHECK(written["blade"]["lean"].get<float>() == 0.1f);
	CHECK(written["material"] == c_MaterialKey);
}

TEST_CASE("A grass document refuses a value of the wrong type", "[grass][codec]")
{
	CHECK_THROWS(Parse("[]"));
	CHECK_THROWS(Parse(R"({"material": 5})"));
	CHECK_THROWS(Parse(R"({"blade": 3})"));
	CHECK_THROWS(Parse(R"({"blade": {"minHeight": "tall"}})"));
	CHECK_THROWS(Parse(R"({"blade": {"nearSegments": -1}})"));
	CHECK_THROWS(Parse(R"({"lighting": {"translucencyColor": [1, 1]}})"));
}

TEST_CASE("A grass document leaves range checks to the renderer", "[grass][codec]")
{
	// The renderer states the ranges where it creates the look; a second copy here would be a
	// second place to keep them, and an out-of-range value must still open in a text editor's
	// round trip so it can be fixed.
	auto grass            = BGrass();
	grass.blade.maxHeight = -1.0f;
	CHECK(Parse(Text(grass)).blade.maxHeight == -1.0f);
}

TEST_CASE("A grass look is authored, so the store refuses it under Derived", "[grass][codec]")
{
	const DataRoot root("bernini_grass_origin");
	CHECK_NOTHROW(StoreAt(root.path).Save(MakeGrass(), std::string(c_GrassKey)));
	CHECK_THROWS(StoreAt(root.path).Save(MakeGrass(), "Derived/Grass/verge.bgrass"));
}

TEST_CASE("The reference scan reads the material a grass look names", "[grass][assetrefs]")
{
	const DataRoot root("bernini_grass_refs");
	StoreAt(root.path).Save(BMaterial(), std::string(c_MaterialKey));
	StoreAt(root.path).Save(MakeGrass(), std::string(c_GrassKey));

	const AssetRefGraph graph = root.Scan();
	CHECK(graph.grassLooksScanned == 1);
	CHECK(graph.broken.empty());

	const std::vector<AssetRef> named = graph.ReferencesOf(c_GrassKey);
	REQUIRE(named.size() == 1);
	CHECK(named[0].target == c_MaterialKey);
	CHECK(named[0].kind == RefKind::kGrassMaterial);

	// What a deletion of the material consults, so the look holds it alive.
	CHECK(
		ReferrerPaths(graph, c_MaterialKey) == std::vector<std::string>{ std::string(c_GrassKey) });
}

TEST_CASE("Renaming a material rewrites the grass look that names it", "[grass][assetrename]")
{
	const DataRoot root("bernini_grass_rename");
	StoreAt(root.path).Save(BMaterial(), std::string(c_MaterialKey));
	StoreAt(root.path).Save(MakeGrass(), std::string(c_GrassKey));

	const RenamePlan plan =
		planRename(root.Scan(), c_MaterialKey, "Authored/Materials/meadow.bmaterial");
	REQUIRE(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);

	const BGrass after = StoreAt(root.path).Load<BGrass>(std::string(c_GrassKey));
	CHECK(after.material == "Authored/Materials/meadow.bmaterial");

	// Everything but the path is untouched.
	BGrass expected   = MakeGrass();
	expected.material = after.material;
	CHECK(after == expected);
}
