#include "CacheTamper.h"
#include "ImportUnitGroup.h"
#include "MountAt.h"  // IWYU pragma: keep
#include "PointsGltf.h"
#include "TexturedGltf.h"
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/Project.h>
#include <assetlib/RegenMesh.h>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>
#include <assetlib/migrate.h>
#include <assetlib/pak.h>
#include <assetlib/reimport.h>
#include <assetlib_structs/BGrass.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Grass.h>
#include <assetlib_structs/Node.h>
#include <catch2/catch_test_macros.hpp>
#include <core/file/IFileSystem.h>
#include <core/file/file.h>
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

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;

	constexpr std::string_view c_SourceKey = "Authored/Meshes/street.glb";
	constexpr std::string_view c_GrassKey  = "Derived/Meshes/street.bgrassfields";
	constexpr std::string_view c_MeshKey   = "Derived/Meshes/street.bmesh";
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

		// Spelled out: the .glb it holds deletes them anyway, and MSVC's /Wall makes an implicitly
		// deleted copy or move an error.
		GrassyProject(const GrassyProject&) = delete;
		GrassyProject(GrassyProject&&)      = delete;
		GrassyProject&
		operator=(const GrassyProject&) = delete;
		GrassyProject&
		operator=(GrassyProject&&) = delete;
		~GrassyProject()           = default;

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

}

TEST_CASE("A mesh import stores its POINTS geometry inside the mesh", "[grass][container]")
{
	const GrassyProject project("bernini_grass_embedded");
	CHECK_FALSE(project.Store().Exists(c_GrassKey));
	const auto mesh = project.Store().Load<BMesh>(c_MeshKey);
	REQUIRE(mesh.grassFields.fields.size() == 1);
	CHECK(mesh.grassFields.fields[0].name == c_Field);
	CHECK(mesh.grassFields.fields[0].field.look == 0);
	CHECK(mesh.grassFields.clumps.size() == 144);
	const auto document = project.Store().Load<ImportDocument>(importDocumentKeyFor(c_SourceKey));
	CHECK(std::ranges::find(document.outputs, c_GrassKey) == document.outputs.end());
	SECTION("reimport restores geometry from the copied source")
	{
		const auto before = project.Store().GetFiles().Read(c_MeshKey);
		fs::remove(project.dataRoot / c_MeshKey);
		CHECK(project.Store().Reimport(false).GetFailedCount() == 0);
		CHECK(project.Store().GetFiles().Read(c_MeshKey) == before);
	}
}

TEST_CASE(
	"Grass bindings resolve from the sidecar in loose and packed stores",
	"[grass][container][pack]")
{
	const GrassyProject project("bernini_grass_sidecar");
	project.Bind(c_Field, c_LookKey);
	const auto before = project.Store().GetFiles().Read(c_MeshKey);
	const auto loaded = project.Store().LoadRegenMesh(c_MeshKey);
	CHECK(loaded.bindings.grassLooks == std::vector<std::string>{ std::string(c_LookKey) });
	CHECK_FALSE(project.Store().GeometryIsStale(c_MeshKey));
	const auto archive = project.dataRoot.parent_path() / "Data.bpak";
	(void)project.Store().Pack(PackDesc{ archive });
	const AssetStore packed(project.dataRoot, std::make_shared<PakFile>(archive));
	CHECK_FALSE(packed.Exists(c_SourceKey));
	CHECK_FALSE(packed.Exists(c_GrassKey));
	CHECK(packed.Exists(importDocumentKeyFor(c_SourceKey)));
	const auto shipped = packed.LoadRegenMesh(c_MeshKey);
	CHECK(shipped.bindings.grassLooks == loaded.bindings.grassLooks);
	CHECK(shipped.mesh.grassFields.clumps.size() == 144);
	CHECK(project.Store().GetFiles().Read(c_MeshKey) == before);
	project.Bind("Gone[4]", "Authored/Grass/old.bgrass");
	CHECK(
		project.Store().LoadRegenMesh(c_MeshKey).unboundBindings ==
		std::vector<std::string>{ "Gone[4]" });
	CHECK_THROWS(project.Store().Pack(PackDesc{ archive }));
}

TEST_CASE("Grass look renames change sidecars without rewriting geometry", "[grass][assetrename]")
{
	const GrassyProject project("bernini_grass_look_move");
	project.Bind(c_Field, c_LookKey);
	const auto before = project.Store().GetFiles().Read(c_MeshKey);
	const auto plan =
		planRename(AssetRefGraph::Scan(project.Store()), c_LookKey, "Authored/Grass/meadow.bgrass");
	REQUIRE(project.Store().RenameAsset(plan).status == RenameStatus::kRenamed);
	CHECK(
		project.Store().LoadRegenMesh(c_MeshKey).bindings.grassLooks ==
		std::vector<std::string>{ "Authored/Grass/meadow.bgrass" });
	CHECK(project.Store().GetFiles().Read(c_MeshKey) == before);
}

TEST_CASE(
	"Migration rebuilds legacy grass from the source and removes its retired output",
	"[grass][migrate]")
{
	const GrassyProject project("bernini_grass_legacy_migrate");
	project.Bind(c_Field, c_LookKey);
	auto document = project.Store().Load<ImportDocument>(importDocumentKeyFor(c_SourceKey));
	document.outputs.emplace_back(c_GrassKey);
	project.Store().Save(document, importDocumentKeyFor(c_SourceKey));
	core::file::write_atomic(project.dataRoot / c_GrassKey, "unreadable retired cache");
	test::TamperHeaderByte(project.dataRoot / c_MeshKey, test::c_TokenOffset);
	const auto before = project.Store().GetFiles().Read(c_MeshKey);
	CHECK(project.Store().Migrate(true).Count(MigratedFile::Outcome::kFailed) == 0);
	CHECK(project.Store().Exists(c_GrassKey));
	CHECK(project.Store().GetFiles().Read(c_MeshKey) == before);
	const auto result = project.Store().Migrate(false);
	CHECK(result.Count(MigratedFile::Outcome::kFailed) == 0);
	CHECK_FALSE(project.Store().Exists(c_GrassKey));
	document = project.Store().Load<ImportDocument>(importDocumentKeyFor(c_SourceKey));
	CHECK(std::ranges::find(document.outputs, c_GrassKey) == document.outputs.end());
	const auto mesh = project.Store().LoadRegenMesh(document.GetMeshOutput());
	CHECK(mesh.mesh.grassFields.clumps.size() == 144);
	CHECK(mesh.bindings.grassLooks == std::vector<std::string>{ std::string(c_LookKey) });
	const auto settled = project.Store().Migrate(false);
	CHECK(settled.Count(MigratedFile::Outcome::kFailed) == 0);
	CHECK(settled.Count(MigratedFile::Outcome::kRewritten) == 0);
}

TEST_CASE("Embedded grass refuses invalid field and chunk ranges", "[grass][codec]")
{
	const GrassyProject project("bernini_grass_ranges");
	auto                mesh = project.Store().Load<BMesh>(c_MeshKey);
	SECTION("field mesh index") { mesh.grassFields.fields[0].field.mesh = 999; }
	SECTION("field look index") { mesh.grassFields.fields[0].field.look = 999; }
	SECTION("chunk range") { mesh.grassFields.fields[0].field.firstChunk = 999; }
	SECTION("clump range") { mesh.grassFields.chunks[0].firstClump = 999; }
	SECTION("empty name") { mesh.grassFields.fields[0].name.clear(); }
	SECTION("duplicate name")
	{
		auto repeated       = mesh.grassFields.fields[0];
		repeated.field.look = 1;
		mesh.grassFields.fields.push_back(repeated);
	}
	CHECK_THROWS(AssetCodec<BMesh>::Serialize(mesh));
}
