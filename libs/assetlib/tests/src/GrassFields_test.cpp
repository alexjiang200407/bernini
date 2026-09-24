#include "ImportUnitGroup.h"
#include "MountAt.h"  // IWYU pragma: keep
#include "PointsGltf.h"
#include "TexturedGltf.h"
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/Project.h>
#include <assetlib/RegenGrassFields.h>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>
#include <assetlib/pak.h>
#include <assetlib/reimport.h>
#include <assetlib_structs/BGrass.h>
#include <assetlib_structs/BGrassFields.h>
#include <assetlib_structs/Grass.h>
#include <assetlib_structs/Node.h>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The `.bgrassfields` a mesh import writes beside its `.bmesh`: that it is written only for a source
// with grass, travels with the geometry group through Reimport, rename, the reference scan and pack,
// and takes the `.bimport`'s grass bindings wherever it is loaded.

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;

	constexpr std::string_view c_SourceKey = "Authored/Meshes/street.glb";
	constexpr std::string_view c_GrassKey  = "Derived/Meshes/street.bgrassfields";
	constexpr std::string_view c_LookKey   = "Authored/Grass/verge.bgrass";
	constexpr std::string_view c_Field     = "Street[1]";

	/** A project holding one imported source: `street`, a triangle and a POINTS primitive. */
	struct GrassyProject
	{
		Buffer   buffer;
		Glb      glb;
		Project  project;
		fs::path dataRoot;

		explicit GrassyProject(const char* name) :
			glb(std::format("{}.glb", name).c_str(),
		        StreetDocument(buffer, ShuffledGrid(12)),
		        buffer.bytes),
			project(MakeProject(name))
		{
			dataRoot = project.GetDataDirectory();
			Import();
		}

		void
		Import()
		{
			ImportUnitGroup(
				dataRoot,
				glb.Path(),
				"Authored/Materials/red.bmaterial",
				30.0f,
				{},
				"street");
			project.ReloadStore();
		}

		[[nodiscard]] const AssetStore&
		Store() const
		{
			return project.GetStore();
		}

		/** Binds the grass field to `look` in the source's `.bimport`, and saves the look. */
		void
		Bind(std::string_view field, std::string_view look) const
		{
			auto grass     = BGrass();
			grass.material = "Authored/Materials/red.bmaterial";
			Store().Save(grass, std::string(look));

			const std::string documentKey = importDocumentKeyFor(c_SourceKey);
			ImportDocument    document    = Store().Load<ImportDocument>(documentKey);
			document.bindings.push_back(
				{ .submesh = std::string(field), .material = std::string(look) });
			Store().Save(document, documentKey);
		}

	private:
		static Project
		MakeProject(const char* name)
		{
			const fs::path root = fs::temp_directory_path() / name;
			fs::remove_all(root);
			return Project::Create(root / "Grass.bproj", "Grass");
		}
	};

	[[nodiscard]] std::vector<std::byte>
	ReadBytes(const fs::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		REQUIRE(in.is_open());
		const std::vector<char> chars(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
		auto bytes = std::vector<std::byte>(chars.size());
		std::memcpy(bytes.data(), chars.data(), chars.size());
		return bytes;
	}
}

TEST_CASE(
	"An import of a source with POINTS writes its grass beside the mesh",
	"[grass][container]")
{
	const GrassyProject project("bernini_grass_import");

	REQUIRE(project.Store().Exists(c_GrassKey));
	const ImportDocument document =
		project.Store().Load<ImportDocument>(importDocumentKeyFor(c_SourceKey));
	CHECK(std::ranges::find(document.outputs, c_GrassKey) != document.outputs.end());

	const BGrassFields grass = project.Store().Load<BGrassFields>(std::string(c_GrassKey));
	REQUIRE(grass.names == std::vector<std::string>{ std::string(c_Field) });
	CHECK(grass.clumps.size() == 144);
	CHECK(grass.fields[0].look == c_InvalidIndex);
	CHECK(grass.source.key == c_SourceKey);
	CHECK_FALSE(project.Store().GeometryIsStale(c_GrassKey));
}

TEST_CASE("A source with no POINTS writes no grass file", "[grass][container]")
{
	const fs::path root = fs::temp_directory_path() / "bernini_grass_none";
	fs::remove_all(root);
	Project project = Project::Create(root / "Grass.bproj", "Grass");
	ImportUnitGroup(project.GetDataDirectory(), TexturedGltfPath());
	project.ReloadStore();

	const ImportDocument document =
		project.GetStore().Load<ImportDocument>(importDocumentKeyFor("Authored/Meshes/unit.glb"));
	for (const std::string& output : document.outputs)
		CHECK_FALSE(output.ends_with(c_GrassFieldsExtension));
	CHECK_FALSE(project.GetStore().Exists("Derived/Meshes/unit.bgrassfields"));
}

TEST_CASE("Reimport puts a deleted grass file back, byte for byte", "[grass][container][reimport]")
{
	const GrassyProject project("bernini_grass_reimport");
	const fs::path      file   = project.dataRoot / c_GrassKey;
	const auto          before = ReadBytes(file);

	fs::remove(file);
	const ReimportReport report = project.Store().Reimport(false);
	CHECK(report.GetFailedCount() == 0);

	REQUIRE(fs::exists(file));
	CHECK(ReadBytes(file) == before);
}

TEST_CASE(
	"A document's grass bindings reach the fields wherever they are loaded",
	"[grass][container]")
{
	const GrassyProject project("bernini_grass_bind");
	project.Bind(c_Field, c_LookKey);
	project.Bind("Gone[4]", "Authored/Grass/old.bgrass");

	const RegenGrassFields current = project.Store().LoadRegenGrassFields(c_GrassKey);
	REQUIRE(current.fields.looks == std::vector<std::string>{ std::string(c_LookKey) });
	CHECK(current.fields.fields[0].look == 0);
	CHECK(current.unboundBindings == std::vector<std::string>{ "Gone[4]" });

	// A binding is a document edit, not a parameter: nothing re-cooks for it.
	CHECK_FALSE(project.Store().GeometryIsStale(c_GrassKey));
}

TEST_CASE("Re-importing a source keeps the grass bindings authored since", "[grass][container]")
{
	GrassyProject project("bernini_grass_rebind");
	project.Bind(c_Field, c_LookKey);

	project.Import();

	const ImportDocument document =
		project.Store().Load<ImportDocument>(importDocumentKeyFor(c_SourceKey));
	const auto kept = std::ranges::find_if(document.bindings, [](const MaterialBinding& binding) {
		return binding.submesh == c_Field;
	});
	REQUIRE(kept != document.bindings.end());
	CHECK(kept->material == c_LookKey);
}

TEST_CASE(
	"Renaming a look rewrites the grass file and the document that name it",
	"[grass][container][assetrename]")
{
	const GrassyProject project("bernini_grass_rename");
	project.Bind(c_Field, c_LookKey);

	// The file stores its looks once it has been written with the bindings over it.
	project.Store().Save(
		project.Store().LoadRegenGrassFields(c_GrassKey).fields,
		std::string(c_GrassKey));

	const RenamePlan plan =
		planRename(AssetRefGraph::Scan(project.Store()), c_LookKey, "Authored/Grass/meadow.bgrass");
	REQUIRE(project.Store().RenameAsset(plan).status == RenameStatus::kRenamed);

	const BGrassFields grass = project.Store().Load<BGrassFields>(std::string(c_GrassKey));
	CHECK(grass.looks == std::vector<std::string>{ "Authored/Grass/meadow.bgrass" });

	const ImportDocument document =
		project.Store().Load<ImportDocument>(importDocumentKeyFor(c_SourceKey));
	CHECK(std::ranges::any_of(document.bindings, [](const MaterialBinding& binding) {
		return binding.material == "Authored/Grass/meadow.bgrass";
	}));
}

TEST_CASE("The reference scan reads the looks a grass file stores", "[grass][container][assetrefs]")
{
	const GrassyProject project("bernini_grass_scan");
	project.Bind(c_Field, c_LookKey);
	project.Store().Save(
		project.Store().LoadRegenGrassFields(c_GrassKey).fields,
		std::string(c_GrassKey));

	const AssetRefGraph graph = AssetRefGraph::Scan(project.Store());
	CHECK(graph.grassFieldsScanned == 1);

	const std::vector<AssetRef> named = graph.ReferencesOf(c_GrassKey);
	REQUIRE(named.size() == 1);
	CHECK(named[0].target == c_LookKey);
	CHECK(named[0].kind == RefKind::kFieldGrass);
}

TEST_CASE(
	"Pack carries the grass file, and refuses a binding its source lost",
	"[grass][container][pack]")
{
	const GrassyProject project("bernini_grass_pack");
	project.Bind(c_Field, c_LookKey);

	const fs::path archive = project.dataRoot.parent_path() / "Data.bpak";
	static_cast<void>(project.Store().Pack(PackDesc{ archive }));

	const AssetStore   shipped(project.dataRoot, std::make_shared<PakFile>(archive));
	const BGrassFields packed = shipped.Load<BGrassFields>(std::string(c_GrassKey));
	CHECK(packed.looks == std::vector<std::string>{ std::string(c_LookKey) });

	project.Bind("Gone[4]", "Authored/Grass/old.bgrass");
	CHECK_THROWS(project.Store().Pack(PackDesc{ archive }));
}

TEST_CASE("A grass file refuses ranges it cannot back", "[grass][container][codec]")
{
	auto grass   = BGrassFields();
	grass.fields = {
		GrassField{ .mesh = 0, .look = c_InvalidIndex, .firstChunk = 0, .chunkCount = 1 }
	};
	grass.names  = { "Street[1]" };
	grass.chunks = { GrassChunk{ .boundingCenter = glm::vec3(0),
		                         .boundingRadius = 1.0f,
		                         .firstClump     = 0,
		                         .clumpCount     = 1,
		                         .maxHeightScale = 1.0f } };
	grass.clumps = { GrassClump{ .position    = glm::vec3(0),
		                         .heightScale = 1.0f,
		                         .normal      = glm::vec3(0, 1, 0),
		                         .color       = glm::u8vec4(255) } };

	const BGrassFields back =
		AssetCodec<BGrassFields>::Deserialize(AssetCodec<BGrassFields>::Serialize(grass));
	CHECK(back.names == grass.names);
	CHECK(back.clumps.size() == 1);

	SECTION("a look slot past the looks")
	{
		grass.fields[0].look = 0;
		CHECK_THROWS(AssetCodec<BGrassFields>::Serialize(grass));
	}

	SECTION("a chunk past the chunks")
	{
		grass.fields[0].chunkCount = 2;
		CHECK_THROWS(AssetCodec<BGrassFields>::Serialize(grass));
	}

	SECTION("a clump past the clumps")
	{
		grass.chunks[0].clumpCount = 2;
		CHECK_THROWS(AssetCodec<BGrassFields>::Serialize(grass));
	}

	SECTION("names that do not match the fields")
	{
		grass.names.clear();
		CHECK_THROWS(AssetCodec<BGrassFields>::Serialize(grass));
	}
}

TEST_CASE(
	"Deleting a grass file drops it from its document; a look it names is held alive",
	"[grass][container][assetdelete]")
{
	const GrassyProject project("bernini_grass_delete");
	project.Bind(c_Field, c_LookKey);
	project.Store().Save(
		project.Store().LoadRegenGrassFields(c_GrassKey).fields,
		std::string(c_GrassKey));

	// The look is referenced by the file (and the document's binding), so it cannot go first.
	CHECK_FALSE(planDeletion(AssetRefGraph::Scan(project.Store()), c_LookKey).Allowed());

	const DeletionPlan plan = planDeletion(AssetRefGraph::Scan(project.Store()), c_GrassKey);
	REQUIRE(plan.Allowed());
	CHECK(plan.producers == std::vector<std::string>{ importDocumentKeyFor(c_SourceKey) });
	REQUIRE(project.Store().DeleteAsset(plan).status == DeletionStatus::kDeleted);

	CHECK_FALSE(project.Store().Exists(c_GrassKey));
	const ImportDocument document =
		project.Store().Load<ImportDocument>(importDocumentKeyFor(c_SourceKey));
	CHECK(std::ranges::find(document.outputs, c_GrassKey) == document.outputs.end());
}
