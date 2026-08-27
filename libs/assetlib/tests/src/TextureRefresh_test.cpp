#include <assetlib/AssetStore.h>

#include <assetlib/asset_import.h>
#include <assetlib/import_document.h>
#include <assetlib/migrate.h>
#include <assetlib/project_layout.h>
#include <core/file/file.h>

#include "ImportUnitGroup.h"

#include <catch2/matchers/catch_matchers_string.hpp>

using namespace assetlib;
using Catch::Matchers::ContainsSubstring;

namespace
{
	namespace fs = std::filesystem;

	constexpr std::string_view c_TextureDir = "textures_src/unit";

	struct Project
	{
		fs::path root;

		// Named per test case, because `just test` shards the suite across concurrent processes
		// and two cases sharing a directory would wipe each other's project.
		explicit Project(const char* name) : root(fs::temp_directory_path() / name)
		{
			fs::remove_all(root);
			fs::create_directories(root);
		}

		~Project() { fs::remove_all(root); }

		[[nodiscard]] AssetStore
		Store() const
		{
			return AssetStore(root);
		}

		[[nodiscard]] ImportDocument
		Document() const
		{
			return loadImportDocument(root / "meshes_src" / "unit.bimport");
		}

		[[nodiscard]] std::vector<std::string>
		Textures() const
		{
			auto names = std::vector<std::string>();
			for (const auto& entry : fs::directory_iterator(root / c_TextureDir))
				names.push_back(entry.path().filename().string());
			std::ranges::sort(names);
			return names;
		}
	};

	/**
	 * Rewrites the source in place with one of its glTF image names altered, standing in for a
	 * re-export from the DCC. The replacement is the same length, so every offset the GLB's chunk
	 * headers state stays true and the file is still a file tinygltf reads.
	 */
	void
	RenameImageInSource(const fs::path& glb, std::string_view from, std::string_view to)
	{
		REQUIRE(from.size() == to.size());

		std::vector<std::byte> bytes = core::file::read_file_bytes(glb.string());
		auto text = std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());

		const size_t at = text.find(from);
		REQUIRE(at != std::string_view::npos);
		std::memcpy(bytes.data() + at, to.data(), to.size());

		core::file::write_atomic(glb, bytes);
	}
}

TEST_CASE("A source that has not changed has nothing to refresh", "[refresh][textures]")
{
	Project project("assetlib_texture_refresh_unchanged_test");
	test::ImportUnitGroup(
		project.root,
		"assets/apples.glb",
		"Materials/red.bmaterial",
		30.0f,
		c_TextureDir);

	CHECK(project.Store().GetStaleImportedTextureSources().empty());
}

TEST_CASE("A folder still holding a numbered texture is stale", "[refresh][textures]")
{
	// The one staleness a stamp cannot see. `textureStamp` answers whether the *source* moved; an
	// unnamed image used to be named after its position and is now named after its content, and a
	// folder holding one of the old names is a folder the current extract would not write. Without
	// this the rule would change and no project would ever re-extract under it.
	Project project("assetlib_texture_refresh_numbered_test");
	test::ImportUnitGroup(
		project.root,
		"assets/apples.glb",
		"Materials/red.bmaterial",
		30.0f,
		c_TextureDir);

	const fs::path folder = project.root / c_TextureDir;
	REQUIRE(project.Store().GetStaleImportedTextureSources().empty());

	// A file the old rule would have written, and the same bytes as one the current rule did.
	fs::copy_file(folder / "Apple1_u1_v1_diffuse.ktx2", folder / "tex0.ktx2");

	CHECK(
		project.Store().GetStaleImportedTextureSources() ==
		std::vector<std::string>{ "meshes_src/unit.glb" });

	SECTION("and the refresh follows it onto the file holding those bytes")
	{
		const TextureRefresh refresh =
			project.Store().RefreshImportedTextures("meshes_src/unit.glb");

		REQUIRE(refresh.moved.size() == 1);
		CHECK(refresh.moved[0].from == std::string(c_TextureDir) + "/tex0.ktx2");
		CHECK(refresh.moved[0].to == std::string(c_TextureDir) + "/Apple1_u1_v1_diffuse.ktx2");
		CHECK(refresh.superseded.empty());
		CHECK_FALSE(fs::exists(folder / "tex0.ktx2"));

		// And having followed it, the folder is what the rule says it is.
		CHECK(project.Store().GetStaleImportedTextureSources().empty());
	}
}

TEST_CASE("An edited source's textures are re-extracted over the routes", "[refresh][textures]")
{
	Project project("assetlib_texture_refresh_edited_test");
	test::ImportUnitGroup(
		project.root,
		"assets/apples.glb",
		"Materials/red.bmaterial",
		30.0f,
		c_TextureDir);

	const std::vector<std::string> imported = project.Textures();
	REQUIRE(
		imported ==
		std::vector<std::string>{ "Apple1_u1_v1_diffuse.ktx2", "Apple2_u1_v1_diffuse.ktx2" });

	// Apple1's image is renamed and Apple2's is not, so one texture moves to a new file and the
	// other must land back on the file a material already routes at.
	RenameImageInSource(
		project.root / "meshes_src" / "unit.glb",
		"Apple1_u1_v1_diffuse",
		"Apple9_u1_v1_diffuse");

	REQUIRE(
		project.Store().GetStaleImportedTextureSources() ==
		std::vector<std::string>{ "meshes_src/unit.glb" });

	const TextureRefresh refresh = project.Store().RefreshImportedTextures("meshes_src/unit.glb");

	CHECK(refresh.textureDir == c_TextureDir);
	CHECK(
		refresh.written ==
		std::vector<std::string>{ "textures_src/unit/Apple2_u1_v1_diffuse.ktx2",
	                              "textures_src/unit/Apple9_u1_v1_diffuse.ktx2" });

	// Apple2 kept its file, so every material routed at it is now drawing the re-exported pixels
	// with no route edit -- which is the whole point of naming by image. Apple1's old file held the
	// same bytes as exactly one file this extract wrote, which is the image having been renamed
	// rather than removed, so it is followed: the file moves and the routes move with it.
	CHECK(refresh.superseded.empty());
	CHECK(refresh.moved.size() == 1);
	CHECK(refresh.moved[0].from == "textures_src/unit/Apple1_u1_v1_diffuse.ktx2");
	CHECK(refresh.moved[0].to == "textures_src/unit/Apple9_u1_v1_diffuse.ktx2");
	CHECK_FALSE(fs::exists(project.root / c_TextureDir / "Apple1_u1_v1_diffuse.ktx2"));

	SECTION("and the source is no longer reported stale")
	{
		CHECK(project.Store().GetStaleImportedTextureSources().empty());
		CHECK(project.Document().textureStamp != SourceStamp());
	}

	SECTION("while the folder it wrote into is untouched")
	{
		CHECK(project.Document().textureDir == c_TextureDir);
	}
}

TEST_CASE("An import that recorded no texture folder refuses the refresh", "[refresh][textures]")
{
	// Every project imported before the folder was recorded is this case, and guessing the folder
	// is how one import comes to overwrite another's textures.
	Project project("assetlib_texture_refresh_nofolder_test");
	test::ImportUnitGroup(project.root, "assets/apples.glb");

	CHECK(project.Store().GetStaleImportedTextureSources().empty());
	CHECK_THROWS_WITH(
		project.Store().RefreshImportedTextures("meshes_src/unit.glb"),
		ContainsSubstring("records no texture folder"));
}

TEST_CASE("A source that is gone refuses the refresh rather than staling", "[refresh][textures]")
{
	Project project("assetlib_texture_refresh_nosource_test");
	test::ImportUnitGroup(
		project.root,
		"assets/apples.glb",
		"Materials/red.bmaterial",
		30.0f,
		c_TextureDir);
	fs::remove(project.root / "meshes_src" / "unit.glb");

	// What cannot be compared is not evidence of a change: a project whose sources were not
	// checked out must not report every asset stale.
	CHECK(project.Store().GetStaleImportedTextureSources().empty());
	CHECK_THROWS_WITH(
		project.Store().RefreshImportedTextures("meshes_src/unit.glb"),
		ContainsSubstring("is not in this project"));
}

TEST_CASE("migrate re-extracts a moved source's textures", "[refresh][textures][migrate]")
{
	Project project("assetlib_texture_refresh_migrate_test");
	test::ImportUnitGroup(
		project.root,
		"assets/apples.glb",
		"Materials/red.bmaterial",
		30.0f,
		c_TextureDir);

	RenameImageInSource(
		project.root / "meshes_src" / "unit.glb",
		"Apple1_u1_v1_diffuse",
		"Apple9_u1_v1_diffuse");

	SECTION("a dry run writes nothing and says the document would move")
	{
		const MigrateReport preview = project.Store().Migrate(true);

		CHECK(std::ranges::any_of(preview.files, [](const MigratedFile& file) {
			return file.path.filename() == "unit.bimport" &&
			       file.outcome == MigratedFile::Outcome::kRewritten;
		}));
		CHECK(
			project.Textures() ==
			std::vector<std::string>{ "Apple1_u1_v1_diffuse.ktx2", "Apple2_u1_v1_diffuse.ktx2" });
		CHECK(preview.supersededTextures.empty());
	}

	SECTION("the real run extracts, reports what it left, and settles")
	{
		const MigrateReport report = project.Store().Migrate(false);

		CHECK(report.Count(MigratedFile::Outcome::kFailed) == 0);

		// The renamed image is followed rather than left beside its replacement, so the folder holds
		// what the source holds and no material is left routed at a file the extract stopped writing.
		CHECK(report.supersededTextures.empty());
		CHECK(report.movedTextures.size() == 1);
		CHECK(
			project.Textures() ==
			std::vector<std::string>{ "Apple2_u1_v1_diffuse.ktx2", "Apple9_u1_v1_diffuse.ktx2" });

		// Running it twice rewrites nothing the second time, which is migrate's whole contract.
		const MigrateReport again = project.Store().Migrate(false);
		CHECK(again.Count(MigratedFile::Outcome::kRewritten) == 0);
		CHECK(again.supersededTextures.empty());
		CHECK(again.movedTextures.empty());
	}
}
