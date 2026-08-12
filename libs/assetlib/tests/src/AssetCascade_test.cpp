#include <assetlib/asset_refs.h>

#include <assetlib/bmaterial_io.h>
#include <assetlib_structs/BMaterial.h>

#include "RefsSandbox.h"

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;
}

TEST_CASE("Cascade deleting a mesh takes what it alone was holding alive", "[assetcascade]")
{
	// The transitive rule, end to end: the mesh frees its material, and the material -- once in the
	// deleted set -- frees the baked map and the source it alone named.
	const DataRoot root("bernini_cascade_mesh");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	const BMaterial material = BakeAndSave(root, "mat.bmaterial", "textures_src/a.ktx2");
	SaveMesh(root, "mesh.bmesh", { "Materials/mat.bmaterial" });

	const DeletionPlan plan = planCascadeDeletion(root.Scan(), "Meshes/mesh.bmesh");

	REQUIRE(plan.Allowed());

	auto expected = std::vector<std::string>{ "Materials/mat.bmaterial",
		                                      material.pbr.baseColorTexture,
		                                      "textures_src/a.ktx2" };
	std::ranges::sort(expected);
	CHECK(plan.cascade == expected);

	REQUIRE(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);

	CHECK_FALSE(fs::exists(root.path / "Meshes" / "mesh.bmesh"));
	CHECK_FALSE(fs::exists(root.path / "Materials" / "mat.bmaterial"));
	CHECK_FALSE(fs::exists(root.path / material.pbr.baseColorTexture));
	CHECK_FALSE(fs::exists(root.path / "textures_src" / "a.ktx2"));
}

TEST_CASE("What something outside the deletion still references survives it", "[assetcascade]")
{
	const DataRoot root("bernini_cascade_shared");

	WriteSource(root.path / "textures_src" / "shared.ktx2", { { 200, 0, 0, 255 } });

	SECTION("a material another mesh names")
	{
		BakeAndSave(root, "mat.bmaterial", "textures_src/shared.ktx2");
		SaveMesh(root, "gone.bmesh", { "Materials/mat.bmaterial" });
		SaveMesh(root, "stays.bmesh", { "Materials/mat.bmaterial" });

		const DeletionPlan plan = planCascadeDeletion(root.Scan(), "Meshes/gone.bmesh");

		CHECK(plan.cascade.empty());

		REQUIRE(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);
		CHECK(fs::exists(root.path / "Materials" / "mat.bmaterial"));
	}

	SECTION("a source another material routes")
	{
		// Both materials route the same source, and their identical bakes converge on one
		// content-hashed map -- so the survivor holds the source *and* the baked file, and the
		// cascade of the deleted mesh stops at its material.
		BakeAndSave(root, "gone.bmaterial", "textures_src/shared.ktx2");
		const BMaterial stays = BakeAndSave(root, "stays.bmaterial", "textures_src/shared.ktx2");
		SaveMesh(root, "mesh.bmesh", { "Materials/gone.bmaterial" });

		const DeletionPlan plan = planCascadeDeletion(root.Scan(), "Meshes/mesh.bmesh");

		CHECK(plan.cascade == std::vector<std::string>{ "Materials/gone.bmaterial" });

		REQUIRE(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);
		CHECK(fs::exists(root.path / "textures_src" / "shared.ktx2"));
		CHECK(fs::exists(root.path / stays.pbr.baseColorTexture));
	}
}

TEST_CASE("An asset two cascading referrers share goes when both do", "[assetcascade]")
{
	// The all-referrers rule, not a sole-referrer shortcut: the source is held twice, but both
	// holders are themselves in the deleted set, so nothing outside it is left pointing at anything.
	const DataRoot root("bernini_cascade_diamond");

	WriteSource(root.path / "textures_src" / "shared.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "a.bmaterial", "textures_src/shared.ktx2");
	BakeAndSave(root, "b.bmaterial", "textures_src/shared.ktx2");
	SaveMesh(root, "mesh.bmesh", { "Materials/a.bmaterial", "Materials/b.bmaterial" });

	const DeletionPlan plan = planCascadeDeletion(root.Scan(), "Meshes/mesh.bmesh");

	REQUIRE(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);

	CHECK_FALSE(fs::exists(root.path / "Materials" / "a.bmaterial"));
	CHECK_FALSE(fs::exists(root.path / "Materials" / "b.bmaterial"));
	CHECK_FALSE(fs::exists(root.path / "textures_src" / "shared.ktx2"));
}

TEST_CASE("A plain deletion still takes the target alone", "[assetcascade]")
{
	// planDeletion is what every existing caller uses, so the cascade must be something a caller
	// asks for by name rather than something that starts happening to them.
	const DataRoot root("bernini_cascade_default");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "textures_src/a.ktx2");
	SaveMesh(root, "mesh.bmesh", { "Materials/mat.bmaterial" });

	const DeletionPlan plan = planDeletion(root.Scan(), "Meshes/mesh.bmesh");

	CHECK(plan.cascade.empty());

	REQUIRE(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);
	CHECK(fs::exists(root.path / "Materials" / "mat.bmaterial"));
}

TEST_CASE("A blocked deletion plans no cascade", "[assetcascade]")
{
	// Nothing is freed by a deletion that cannot happen, and a cascade list on a refused plan would
	// read as a promise of what Delete would take.
	const DataRoot root("bernini_cascade_blocked");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "textures_src/a.ktx2");

	const DeletionPlan plan = planCascadeDeletion(root.Scan(), "textures_src/a.ktx2");

	CHECK_FALSE(plan.Allowed());
	CHECK(plan.cascade.empty());
	CHECK(deleteAsset(plan, root.Desc()).status == DeletionStatus::kRefused);
}

TEST_CASE("A directory cascade counts every referrer under it as deleted", "[assetcascade]")
{
	// Two meshes in the folder both name the material: no single file frees it, but the whole folder
	// does. A material a mesh outside the folder also names stays exactly where it was.
	const DataRoot root("bernini_cascade_dir");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	WriteSource(root.path / "textures_src" / "b.ktx2", { { 0, 200, 0, 255 } });
	BakeAndSave(root, "freed.bmaterial", "textures_src/a.ktx2");
	BakeAndSave(root, "held.bmaterial", "textures_src/b.ktx2");

	fs::create_directories(root.path / "Meshes" / "props");
	save(MakeMesh({ "Materials/freed.bmaterial" }), root.path / "Meshes" / "props" / "a.bmesh");
	save(
		MakeMesh({ "Materials/freed.bmaterial", "Materials/held.bmaterial" }),
		root.path / "Meshes" / "props" / "b.bmesh");
	SaveMesh(root, "outside.bmesh", { "Materials/held.bmaterial" });

	const DeletionPlan plan = planCascadeDeletion(root.Scan(), "Meshes/props");

	REQUIRE(plan.Allowed());
	REQUIRE(plan.IsDirectory());

	REQUIRE(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);

	CHECK_FALSE(fs::exists(root.path / "Meshes" / "props"));
	CHECK_FALSE(fs::exists(root.path / "Materials" / "freed.bmaterial"));
	CHECK(fs::exists(root.path / "Materials" / "held.bmaterial"));
	CHECK(fs::exists(root.path / "textures_src" / "b.ktx2"));
}

TEST_CASE("A cascade file already gone counts as deleted", "[assetcascade]")
{
	// The same stance the target takes: the user may well have removed it in a file manager since the
	// scan, and that is the outcome they asked for.
	const DataRoot root("bernini_cascade_vanished");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "textures_src/a.ktx2");
	SaveMesh(root, "mesh.bmesh", { "Materials/mat.bmaterial" });

	const DeletionPlan plan = planCascadeDeletion(root.Scan(), "Meshes/mesh.bmesh");

	REQUIRE_FALSE(plan.cascade.empty());
	fs::remove(root.path / "Materials" / "mat.bmaterial");

	CHECK(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);
}
