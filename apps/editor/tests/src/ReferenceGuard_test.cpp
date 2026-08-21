#include "VersionControl/reference_guard.h"

#include <assetlib/bmaterial_io.h>
#include <assetlib_structs/BMaterial.h>

#include <QFile>

namespace
{
	namespace fs = std::filesystem;

	/** A scratch data root holding the smallest assets that make a real reference. */
	struct DataRoot
	{
		fs::path path;

		explicit DataRoot(std::string_view name) :
			path(fs::temp_directory_path() / ("bernini_guard_" + std::string(name)))
		{
			std::error_code ec;
			fs::remove_all(path, ec);
			fs::create_directories(path / "Materials");
			fs::create_directories(path / "textures_src");
		}

		~DataRoot()
		{
			std::error_code ec;
			fs::remove_all(path, ec);
		}

		DataRoot(const DataRoot&) = delete;
		DataRoot&
		operator=(const DataRoot&) = delete;

		/** A stub is enough: the scan reads referrers, and a texture is only ever a target. */
		void
		WriteTexture(std::string_view relative) const
		{
			QFile out(QString::fromStdWString((path / relative).generic_wstring()));
			REQUIRE(out.open(QIODevice::WriteOnly));
			REQUIRE(out.write("not really a texture") > 0);
		}

		/** A real `.bmaterial`, because the scan reads every material whole. */
		void
		WriteMaterial(std::string_view name, const std::vector<std::string>& routedFrom) const
		{
			assetlib::BMaterial material;
			for (size_t slot = 0; slot < routedFrom.size(); ++slot)
			{
				material.pbr.routes[slot] = { routedFrom[slot], 0 };
			}
			assetlib::saveMaterial(material, path / "Materials" / name);
		}
	};
}

TEST_CASE("An asset nothing else needs can be deleted", "[vcs]")
{
	const DataRoot root("orphan");
	root.WriteTexture("textures_src/orphan.ktx2");

	CHECK(
		editor::FindAssetsStillInUse(root.path, { root.path / "textures_src/orphan.ktx2" })
			.empty());
}

// The case the whole guard exists for: neither artist is wrong on their own, and only the tree they
// would produce between them is.
TEST_CASE("An asset another still routes from cannot be deleted, and both are named", "[vcs]")
{
	const DataRoot root("still_used");
	root.WriteTexture("textures_src/albedo.ktx2");
	root.WriteMaterial("coyote.bmaterial", { "textures_src/albedo.ktx2" });

	const auto inUse =
		editor::FindAssetsStillInUse(root.path, { root.path / "textures_src/albedo.ktx2" });

	REQUIRE(inUse.size() == 1);
	// Resolved, because the guard resolves before it compares -- see AssetKey.
	CHECK(inUse.front().asset == fs::weakly_canonical(root.path / "textures_src/albedo.ktx2"));
	CHECK(inUse.front().neededBy == fs::weakly_canonical(root.path / "Materials/coyote.bmaterial"));
}

TEST_CASE("An asset can be deleted when what needs it is deleted too", "[vcs]")
{
	const DataRoot root("both_go");
	root.WriteTexture("textures_src/albedo.ktx2");
	root.WriteMaterial("coyote.bmaterial", { "textures_src/albedo.ktx2" });

	CHECK(
		editor::FindAssetsStillInUse(
			root.path,
			{ root.path / "textures_src/albedo.ktx2", root.path / "Materials/coyote.bmaterial" })
			.empty());
}

TEST_CASE("Every asset that would be left pointing at nothing is named", "[vcs]")
{
	const DataRoot root("two_referrers");
	root.WriteTexture("textures_src/albedo.ktx2");
	root.WriteMaterial("coyote.bmaterial", { "textures_src/albedo.ktx2" });
	root.WriteMaterial("squirrel.bmaterial", { "textures_src/albedo.ktx2" });

	const auto inUse =
		editor::FindAssetsStillInUse(root.path, { root.path / "textures_src/albedo.ktx2" });

	CHECK(inUse.size() == 2);
}

// A repository holds more than a project: a .gitignore is not an asset and has no references.
TEST_CASE("A file outside the project's assets is not something to guard", "[vcs]")
{
	const DataRoot root("outside");
	root.WriteTexture("textures_src/albedo.ktx2");
	root.WriteMaterial("coyote.bmaterial", { "textures_src/albedo.ktx2" });

	CHECK(
		editor::FindAssetsStillInUse(root.path, { root.path.parent_path() / ".gitignore" })
			.empty());
}

// Proved by pointing at a data root that is not there: constructing a store over it would throw, so
// a pass that reaches the scan cannot answer at all.
#ifndef _WIN32
// The two roots reach the guard from different places: git reports one with its symlinks already
// resolved, the project supplies the other as the user typed it. Compared as written, a data root
// behind a symlink puts every asset outside itself -- and a guard that finds nothing permits
// everything, which is the one way a safety net fails silently.
TEST_CASE("A data root reached through a symlink still guards its assets", "[vcs]")
{
	const DataRoot root("symlinked");
	root.WriteTexture("textures_src/albedo.ktx2");
	root.WriteMaterial("coyote.bmaterial", { "textures_src/albedo.ktx2" });

	const fs::path  link = fs::temp_directory_path() / "bernini_guard_symlink";
	std::error_code ec;
	fs::remove(link, ec);
	fs::create_directory_symlink(root.path, link, ec);
	REQUIRE_FALSE(ec);

	// The root as a symlink, the asset as its real path: the mismatch the guard has to survive.
	const auto inUse =
		editor::FindAssetsStillInUse(link, { root.path / "textures_src/albedo.ktx2" });

	CHECK(inUse.size() == 1);
	fs::remove(link, ec);
}
#endif

TEST_CASE("An update that deletes nothing does not read the project at all", "[vcs]")
{
	const fs::path  absent = fs::temp_directory_path() / "bernini_guard_absent";
	std::error_code ec;
	fs::remove_all(absent, ec);

	CHECK(editor::FindAssetsStillInUse(absent, {}).empty());
}
