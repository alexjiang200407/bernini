#include <assetlib/AssetStore.h>
#include <assetlib/asset_import.h>  // IWYU pragma: keep -- completes MigrateReport's MovedTexture
#include <assetlib/codecs.h>
#include <assetlib/material_bake.h>
#include <assetlib/migrate.h>
#include <assetlib/project_layout.h>
#include <assetlib_structs/BMaterial.h>

#include "MountAt.h"
#include "test_editor_graph.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <core/file/file.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Every material the editor or assetlib writes carries its node graph. A document written without
// one -- by hand, or by a script -- still loads, and is refused by every write, which is how it gets
// named.

using namespace assetlib;
using Catch::Matchers::ContainsSubstring;

namespace
{
	/** A scratch project with nothing in it but what a case writes. */
	struct Scratch
	{
		std::filesystem::path root =
			std::filesystem::temp_directory_path() / "assetlib_material_graph_required";

		Scratch()
		{
			std::filesystem::remove_all(root);
			std::filesystem::create_directories(root / c_MaterialsDirectoryName);
		}

		~Scratch() { std::filesystem::remove_all(root); }

		[[nodiscard]] std::filesystem::path
		PathOf(std::string_view key) const
		{
			return root / key;
		}
	};

	/** A baked material routing one channel, with no graph: a hand-written document's shape. */
	BMaterial
	Graphless()
	{
		auto material                 = BMaterial();
		material.name                 = "handwritten";
		material.pbr.baseColorTexture = "Derived/BakedTextures/basecolor_0.ktx2";
		material.pbr.routes[0]        = { "Derived/SourceTextures/atlas.ktx2", 0 };
		material.pbr.roughnessFactor  = 0.6f;
		return material;
	}

	/** The bytes a hand-written document would hold: the canonical form, minus the graph. */
	std::vector<std::byte>
	GraphlessBytes()
	{
		auto withGraph                     = Graphless();
		withGraph.editorGraph              = std::string(test::c_TestEditorGraph);
		const std::vector<std::byte> bytes = AssetCodec<BMaterial>::Serialize(withGraph);

		auto         text = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		const size_t key  = text.find("\t\"editorGraph\"");
		REQUIRE(key != std::string::npos);
		text.erase(key, text.find('\n', key) - key + 1);

		const auto view = std::as_bytes(std::span(text.data(), text.size()));
		return { view.begin(), view.end() };
	}
}

TEST_CASE("A material without a graph is refused by the codec, by name", "[bmaterial][graph]")
{
	CHECK_THROWS_WITH(
		AssetCodec<BMaterial>::Serialize(Graphless()),
		ContainsSubstring("'handwritten'") && ContainsSubstring("node graph"));

	auto authored        = Graphless();
	authored.editorGraph = std::string(test::c_TestEditorGraph);
	CHECK_NOTHROW(AssetCodec<BMaterial>::Serialize(authored));
}

TEST_CASE("A store save of a material without a graph writes nothing", "[bmaterial][graph]")
{
	const Scratch     scratch;
	const std::string key = KeyIn(c_MaterialsDirectoryName, "handwritten.bmaterial");

	CHECK_THROWS_AS(AssetStore(scratch.root).Save(Graphless(), key), std::runtime_error);
	CHECK_FALSE(std::filesystem::exists(scratch.PathOf(key)));
}

TEST_CASE("A material without a graph still loads", "[bmaterial][graph]")
{
	const BMaterial loaded = AssetCodec<BMaterial>::Deserialize(GraphlessBytes());

	CHECK(loaded.name == "handwritten");
	CHECK(loaded.editorGraph.empty());
	CHECK(loaded.pbr.routes[0].texture == "Derived/SourceTextures/atlas.ktx2");
}

TEST_CASE("The shipping form is the one write without a graph", "[bmaterial][graph]")
{
	auto authored        = Graphless();
	authored.editorGraph = std::string(test::c_TestEditorGraph);

	const BMaterial shipped = AssetCodec<BMaterial>::Deserialize(serializeStripped(authored));

	CHECK(shipped.editorGraph.empty());
	CHECK(shipped.pbr.routes[0].texture.empty());
	CHECK(shipped.pbr.baseColorTexture == authored.pbr.baseColorTexture);

	// Stripping is what makes it shippable, so a material with nothing baked is still refused.
	auto unbaked                 = authored;
	unbaked.pbr.baseColorTexture = {};
	CHECK_THROWS_AS(serializeStripped(unbaked), std::runtime_error);
}

TEST_CASE("migrate names a material without a graph and leaves it alone", "[migrate][graph]")
{
	const Scratch     scratch;
	const std::string key   = KeyIn(c_MaterialsDirectoryName, "handwritten.bmaterial");
	const auto        bytes = GraphlessBytes();
	core::file::write_atomic(scratch.PathOf(key), bytes);

	const bool dryRun = GENERATE(true, false);
	CAPTURE(dryRun);

	const MigrateReport report = AssetStore(scratch.root).Migrate(dryRun);

	const auto named = std::ranges::find_if(report.files, [&](const MigratedFile& file) {
		return file.path == scratch.PathOf(key);
	});
	REQUIRE(named != report.files.end());
	CHECK(named->outcome == MigratedFile::Outcome::kFailed);
	CHECK_THAT(named->message, ContainsSubstring("node graph"));

	CHECK(core::file::read_file_bytes(scratch.PathOf(key).string()) == bytes);
}
