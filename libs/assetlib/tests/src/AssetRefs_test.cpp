#include <assetlib/asset_refs.h>
#include <assetlib/bmesh.h>

#include <assetlib/skinning.h>
#include <assetlib/texture_prune.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/Skeleton.h>

#include "MountAt.h"
#include "RefsSandbox.h"
#include "bmesh_texture.h"
#include "mounted_io.h"
#include <assetlib/AssetStore.h>

#ifdef _WIN32
#	define NOMINMAX
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;
}

TEST_CASE("loadMeshRefs reads what a full load would, without the geometry", "[assetrefs]")
{
	// The scan surveys every mesh in the project, and a .bmesh is mostly vertex data -- so it seeks to
	// the reference chunks instead of deserializing. This pins the cheap reader against the real one; a
	// change to the container that broke it would otherwise surface as an asset silently losing its
	// references.
	const DataRoot root("bernini_refs_matpaths");

	const std::vector<std::string> materials = { "Authored/Materials/a.bmaterial",
		                                         "Authored/Materials/nested/b.bmaterial",
		                                         "Authored/Materials/a.bmaterial" };
	SaveMesh(root, "mesh.bmesh", materials, "Derived/Meshes/rig.bskel");

	const fs::path file = root.path / "Derived/Meshes" / "mesh.bmesh";

	CHECK(loadMeshRefs(file).materials == LoadAt<BMesh>(file).materials);
	CHECK(loadMeshRefs(file).materials == materials);
	CHECK(loadMeshRefs(file).skeleton == "Derived/Meshes/rig.bskel");

	SECTION("a mesh that names neither yields none, and is not an error")
	{
		SaveMesh(root, "bare.bmesh", {});
		CHECK(loadMeshRefs(root.path / "Derived/Meshes" / "bare.bmesh").materials.empty());
		CHECK(loadMeshRefs(root.path / "Derived/Meshes" / "bare.bmesh").skeleton.empty());
	}

	SECTION("a file that is not a mesh is an error, not an empty list")
	{
		std::ofstream(root.path / "Derived/Meshes" / "junk.bmesh", std::ios::binary)
			<< "not a mesh";
		CHECK_THROWS_AS(
			loadMeshRefs(root.path / "Derived/Meshes" / "junk.bmesh"),
			std::runtime_error);
	}
}

TEST_CASE("A material references both the maps it baked and the sources it routes", "[assetrefs]")
{
	// The two kinds are not interchangeable: the triplet is what the renderer samples, the routes are
	// what a re-bake reads. Deleting either breaks the material, so both are edges.
	const DataRoot root("bernini_refs_material");

	WriteSource(root.path / "Derived/SourceTextures" / "albedo.ktx2", { { 200, 0, 0, 255 } });
	const BMaterial material =
		BakeAndSave(root, "mat.bmaterial", "Derived/SourceTextures/albedo.ktx2");

	const AssetRefGraph graph = root.Scan();

	REQUIRE(graph.materialsScanned == 1);

	SECTION("the baked map is referenced by the material that wrote it")
	{
		const auto referrers = graph.ReferrersOf(material.pbr.baseColorTexture);

		REQUIRE(referrers.size() == 1);
		CHECK(referrers[0].referrer == "Authored/Materials/mat.bmaterial");
		CHECK(referrers[0].kind == RefKind::kBakedMap);
	}

	SECTION("so is the source the channel routes from")
	{
		const auto referrers = graph.ReferrersOf("Derived/SourceTextures/albedo.ktx2");

		REQUIRE(referrers.size() == 1);
		CHECK(referrers[0].referrer == "Authored/Materials/mat.bmaterial");
		CHECK(referrers[0].kind == RefKind::kChannelRoute);
	}

	SECTION("neither can be deleted while the material names it")
	{
		for (const std::string& texture :
		     { material.pbr.baseColorTexture, std::string("Derived/SourceTextures/albedo.ktx2") })
		{
			INFO("texture: " << texture);

			const DeletionPlan plan = planDeletion(graph, texture);

			CHECK_FALSE(plan.Allowed());
			CHECK(root.Source().DeleteAsset(plan).status == DeletionStatus::kRefused);
			CHECK(fs::exists(root.path / texture));
		}
	}
}

TEST_CASE("A texture no material names can be deleted", "[assetrefs]")
{
	const DataRoot root("bernini_refs_unused");

	WriteSource(root.path / "Derived/SourceTextures" / "orphan.ktx2", { { 0, 200, 0, 255 } });

	const AssetRefGraph graph = root.Scan();

	CHECK(graph.ReferrersOf("Derived/SourceTextures/orphan.ktx2").empty());

	const DeletionPlan plan = planDeletion(graph, "Derived/SourceTextures/orphan.ktx2");
	REQUIRE(plan.Allowed());
	REQUIRE(plan.assetType == AssetType::kTexture);

	CHECK(root.Source().DeleteAsset(plan).status == DeletionStatus::kDeleted);
	CHECK_FALSE(fs::exists(root.path / "Derived/SourceTextures" / "orphan.ktx2"));
}

TEST_CASE("A material a mesh names cannot be deleted", "[assetrefs]")
{
	const DataRoot root("bernini_refs_meshmaterial");

	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "used.bmaterial", "Derived/SourceTextures/a.ktx2");
	BakeAndSave(root, "loose.bmaterial", "Derived/SourceTextures/a.ktx2");

	SaveMesh(root, "mesh.bmesh", { "Authored/Materials/used.bmaterial" });

	const AssetRefGraph graph = root.Scan();

	REQUIRE(graph.meshesScanned == 1);

	SECTION("the one it names is held")
	{
		const auto referrers = graph.ReferrersOf("Authored/Materials/used.bmaterial");

		REQUIRE(referrers.size() == 1);
		CHECK(referrers[0].referrer == "Derived/Meshes/mesh.bmesh");
		CHECK(referrers[0].kind == RefKind::kSubmeshMaterial);

		CHECK_FALSE(planDeletion(graph, "Authored/Materials/used.bmaterial").Allowed());
	}

	SECTION("the one no mesh names is not")
	{
		const DeletionPlan plan = planDeletion(graph, "Authored/Materials/loose.bmaterial");

		REQUIRE(plan.Allowed());
		REQUIRE(plan.assetType == AssetType::kMaterial);
		CHECK(root.Source().DeleteAsset(plan).status == DeletionStatus::kDeleted);
	}
}

TEST_CASE("A skeleton cannot be deleted while a mesh skins to it", "[assetrefs][skeleton]")
{
	// A skeleton is held by two different kinds of asset, and by neither of the edges the graph had
	// before. Deleting one out from under a mesh leaves joint indices that resolve to nothing.
	const DataRoot root("bernini_refs_skeleton");
	fs::create_directories(root.path / "Derived/Animations");

	Skeleton skeleton;
	skeleton.bones.push_back(
		Bone{ Transform{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) },
	          glm::mat4(1.0f),
	          c_InvalidIndex,
	          0 });
	StoreAt(root.path).Save(skeleton, "Derived/Animations/rig.bskel");

	AnimationSet animations;
	animations.boneCount         = 1;
	animations.skeleton          = "Derived/Animations/rig.bskel";
	animations.skeletonSignature = skeletonSignature(skeleton);
	StoreAt(root.path).Save(animations, "Derived/Animations/walk.banim");

	SaveMesh(root, "mesh.bmesh", {}, "Derived/Animations/rig.bskel");

	const AssetRefGraph graph = root.Scan();
	CHECK(graph.clipSetsScanned == 1);

	const DeletionPlan plan = planDeletion(graph, "Derived/Animations/rig.bskel");

	REQUIRE_FALSE(plan.Allowed());
	REQUIRE(plan.assetType == AssetType::kSkeleton);
	CHECK(
		ReferrerPaths(graph, "Derived/Animations/rig.bskel") ==
		std::vector<std::string>{ "Derived/Animations/walk.banim", "Derived/Meshes/mesh.bmesh" });
	CHECK(root.Source().DeleteAsset(plan).status == DeletionStatus::kRefused);

	SECTION("and a clip set is deletable, because nothing references one")
	{
		const DeletionPlan clips = planDeletion(graph, "Derived/Animations/walk.banim");

		REQUIRE(clips.Allowed());
		REQUIRE(clips.assetType == AssetType::kAnimation);
		CHECK(root.Source().DeleteAsset(clips).status == DeletionStatus::kDeleted);

		// The skeleton it named outlives it, for the reason a mesh's materials do.
		CHECK(fs::exists(root.path / "Derived/Animations" / "rig.bskel"));
	}
}

TEST_CASE("A mesh is always deletable, and its materials outlive it", "[assetrefs]")
{
	// The rule the feature exists for. A material is a shareable asset that a mesh happens to name --
	// not a part of it -- so deleting the mesh must not take it down, and nothing references a .bmesh
	// for it to be blocked by.
	const DataRoot root("bernini_refs_meshdelete");

	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 200, 0, 0, 255 } });
	const BMaterial material = BakeAndSave(root, "mat.bmaterial", "Derived/SourceTextures/a.ktx2");

	SaveMesh(root, "mesh.bmesh", { "Authored/Materials/mat.bmaterial" });

	const AssetRefGraph graph = root.Scan();
	const DeletionPlan  plan  = planDeletion(graph, "Derived/Meshes/mesh.bmesh");

	REQUIRE(plan.Allowed());
	REQUIRE(plan.assetType == AssetType::kMesh);
	REQUIRE(root.Source().DeleteAsset(plan).status == DeletionStatus::kDeleted);

	CHECK_FALSE(fs::exists(root.path / "Derived/Meshes" / "mesh.bmesh"));

	// Nothing else was touched: not the material, and not the maps it baked.
	CHECK(fs::exists(root.path / "Authored/Materials" / "mat.bmaterial"));
	CHECK(fs::exists(root.path / material.pbr.baseColorTexture));
	CHECK(fs::exists(root.path / "Derived/SourceTextures" / "a.ktx2"));

	SECTION("and the material it freed can then be deleted in its own right")
	{
		const AssetRefGraph after = root.Scan();

		CHECK(after.ReferrersOf("Authored/Materials/mat.bmaterial").empty());
		CHECK(planDeletion(after, "Authored/Materials/mat.bmaterial").Allowed());
	}
}

TEST_CASE("A mesh naming one material twice is one blocker, not two", "[assetrefs]")
{
	// attachMaterial splits a shared slot rather than repointing its siblings, so a .bmesh legitimately
	// names one material from two slots -- Test Project's tree_alpha_test.bmesh does. Reporting the same
	// mesh twice would be a lie about how much is holding the material.
	const DataRoot root("bernini_refs_dedup");

	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "leaf.bmaterial", "Derived/SourceTextures/a.ktx2");

	SaveMesh(
		root,
		"tree.bmesh",
		{ "Authored/Materials/leaf.bmaterial",
	      "Authored/Materials/wood.bmaterial",
	      "Authored/Materials/leaf.bmaterial" });

	const AssetRefGraph graph = root.Scan();

	CHECK(
		ReferrerPaths(graph, "Authored/Materials/leaf.bmaterial") ==
		std::vector<std::string>{ "Derived/Meshes/tree.bmesh" });
	CHECK(planDeletion(graph, "Authored/Materials/leaf.bmaterial").blockers.size() == 1);
}

TEST_CASE("A material routing one texture into two channels is one blocker", "[assetrefs]")
{
	// The same collapse on the other edge: an ORM map feeds roughness and metallic from one file.
	const DataRoot root("bernini_refs_dedup_routes");

	WriteSource(root.path / "Derived/SourceTextures" / "orm.ktx2", { { 10, 60, 90, 255 } });

	BMaterial material;
	material.pbr.routes[channelIndex(PbrChannel::kRoughness)] = { "Derived/SourceTextures/orm.ktx2",
		                                                          1 };
	material.pbr.routes[channelIndex(PbrChannel::kMetallic)]  = { "Derived/SourceTextures/orm.ktx2",
		                                                          2 };
	StoreAt(root.path).Save(material, "Authored/Materials/mat.bmaterial");

	const AssetRefGraph graph = root.Scan();

	CHECK(
		ReferrerPaths(graph, "Derived/SourceTextures/orm.ktx2") ==
		std::vector<std::string>{ "Authored/Materials/mat.bmaterial" });
}

TEST_CASE("A baked map two materials share is blocked by both", "[assetrefs]")
{
	// Baked maps are content-hashed, so two materials routing a group identically converge on one file.
	// A user told only one of them is holding it would delete the other's map and not know.
	const DataRoot root("bernini_refs_shared");

	WriteSource(root.path / "Derived/SourceTextures" / "shared.ktx2", { { 10, 60, 90, 255 } });

	const BMaterial first =
		BakeAndSave(root, "first.bmaterial", "Derived/SourceTextures/shared.ktx2");
	const BMaterial second =
		BakeAndSave(root, "second.bmaterial", "Derived/SourceTextures/shared.ktx2");

	REQUIRE(first.pbr.baseColorTexture == second.pbr.baseColorTexture);

	const AssetRefGraph graph = root.Scan();

	CHECK(
		ReferrerPaths(graph, first.pbr.baseColorTexture) ==
		std::vector<std::string>{ "Authored/Materials/first.bmaterial",
	                              "Authored/Materials/second.bmaterial" });
	CHECK(planDeletion(graph, first.pbr.baseColorTexture).blockers.size() == 2);
}

TEST_CASE("Deleting a material leaves its maps for the prune to sweep", "[assetrefs]")
{
	// Deletion is not cascading, and does not need to be: the maps a deleted material alone named are
	// exactly what FindUnusedBakedTextures already collects. The two features compose rather than
	// duplicate.
	const DataRoot root("bernini_refs_compose");

	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 200, 0, 0, 255 } });
	const BMaterial material = BakeAndSave(root, "mat.bmaterial", "Derived/SourceTextures/a.ktx2");

	const DeletionPlan plan = planDeletion(root.Scan(), "Authored/Materials/mat.bmaterial");
	REQUIRE(root.Source().DeleteAsset(plan).status == DeletionStatus::kDeleted);

	CHECK(fs::exists(root.path / material.pbr.baseColorTexture));

	const auto swept = AssetStore(root.path).FindUnusedBakedTextures();

	REQUIRE(swept.unused.size() == 1);
	CHECK(swept.unused.front().path == material.pbr.baseColorTexture);
}

TEST_CASE("A referrer that cannot be read stops the scan", "[assetrefs]")
{
	// The fail-safe. A material or mesh we cannot parse is one whose edges we cannot see, and we would
	// then let the user delete straight through them. Refusing to answer is the only safe answer.
	const DataRoot root("bernini_refs_corrupt");

	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "good.bmaterial", "Derived/SourceTextures/a.ktx2");

	SECTION("an unreadable material")
	{
		std::ofstream(root.path / "Authored/Materials" / "broken.bmaterial", std::ios::binary)
			<< "not a material";

		CHECK_THROWS_AS(root.Scan(), std::runtime_error);
	}

	SECTION("an unreadable mesh")
	{
		std::ofstream(root.path / "Derived/Meshes" / "broken.bmesh", std::ios::binary)
			<< "not a mesh";

		CHECK_THROWS_AS(root.Scan(), std::runtime_error);
	}
}

// A mount enumerates files, so an empty folder is invisible to the scan -- but it is still there on
// the writable layer, and removing one is an ordinary thing to do in the Content Explorer.
TEST_CASE("an empty directory is still a deletable target", "[assetrefs]")
{
	const DataRoot root("bernini_refs_empty_dir");
	std::filesystem::create_directories(root.path / "Authored/Materials/empty");

	const DeletionPlan plan = planDeletion(root.Scan(), "Authored/Materials/empty");

	CHECK(plan.IsDirectory());
	CHECK(plan.contents.empty());
	CHECK(plan.Allowed());
}

TEST_CASE("planDeletion refuses a file that is not an asset", "[assetrefs]")
{
	const DataRoot      root("bernini_refs_kind");
	const AssetRefGraph graph = root.Scan();

	CHECK_THROWS_AS(planDeletion(graph, "Levels/level.txt"), std::runtime_error);

	SECTION("and assetTypeFromExtension knows the three that are, whatever their case")
	{
		CHECK(assetTypeFromExtension("Derived/Meshes/a.bmesh") == AssetType::kMesh);
		CHECK(assetTypeFromExtension("Authored/Materials/a.bmaterial") == AssetType::kMaterial);
		CHECK(assetTypeFromExtension("Derived/BakedTextures/a.ktx2") == AssetType::kTexture);
		CHECK(assetTypeFromExtension("Derived/BakedTextures/A.KTX2") == AssetType::kTexture);

		CHECK(assetTypeFromExtension("game.bproj") == std::nullopt);
		CHECK(assetTypeFromExtension("notes.txt") == std::nullopt);
		CHECK(
			assetTypeFromExtension("Derived/Meshes") ==
			std::nullopt);  // a directory is not an asset
	}
}

TEST_CASE("One asset is one key, however its path is spelled", "[assetrefs]")
{
	const DataRoot root("bernini_refs_normalize");

	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "Derived/SourceTextures/a.ktx2");

	const AssetRefGraph graph = root.Scan();

	// The path a file browser hands over need not be spelled the way the bake wrote it.
	CHECK(graph.IsReferenced("Derived/SourceTextures/a.ktx2"));
	CHECK(graph.IsReferenced("./Derived/SourceTextures/a.ktx2"));
	CHECK(graph.IsReferenced("Derived/Meshes/../SourceTextures/a.ktx2"));
}

TEST_CASE("An asset deleted behind the editor's back is not fatal", "[assetrefs]")
{
	// The data root is shared with the user's file manager. A texture removed there leaves the material
	// naming a file that is gone -- and if that stopped the scan, one stray deletion would make every
	// deletion in the project impossible.
	const DataRoot root("bernini_refs_external_texture");

	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 200, 0, 0, 255 } });
	WriteSource(root.path / "Derived/SourceTextures" / "b.ktx2", { { 0, 200, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "Derived/SourceTextures/a.ktx2");

	fs::remove(root.path / "Derived/SourceTextures" / "a.ktx2");

	const AssetRefGraph graph = root.Scan();

	SECTION("the dangling reference is reported")
	{
		REQUIRE(graph.broken.size() == 1);
		CHECK(graph.broken.front().target == "Derived/SourceTextures/a.ktx2");
		CHECK(graph.broken.front().referrer == "Authored/Materials/mat.bmaterial");
	}

	SECTION("and the material's other edges are still known")
	{
		// The referrer parsed; only its target was missing. Its baked map is still held.
		CHECK(graph.materialsScanned == 1);
		CHECK_FALSE(graph.broken.empty());
		CHECK(planDeletion(graph, "Derived/SourceTextures/b.ktx2").Allowed());
	}

	SECTION("restoring the texture does not un-reference it")
	{
		// Blocking is keyed on the edge, not on whether the file happened to exist when we looked.
		WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 200, 0, 0, 255 } });

		CHECK_FALSE(planDeletion(root.Scan(), "Derived/SourceTextures/a.ktx2").Allowed());
	}
}

TEST_CASE("A mesh deleted behind the editor's back stops blocking its materials", "[assetrefs]")
{
	// The behaviour a cached graph would get wrong: it would refuse the deletion, naming a mesh that is
	// no longer there. Rebuilding from disk is what makes the answer true rather than merely fresh.
	const DataRoot root("bernini_refs_external_mesh");

	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "Derived/SourceTextures/a.ktx2");
	SaveMesh(root, "mesh.bmesh", { "Authored/Materials/mat.bmaterial" });

	REQUIRE_FALSE(planDeletion(root.Scan(), "Authored/Materials/mat.bmaterial").Allowed());

	fs::remove(root.path / "Derived/Meshes" / "mesh.bmesh");

	CHECK(planDeletion(root.Scan(), "Authored/Materials/mat.bmaterial").Allowed());
}

TEST_CASE("An asset already gone counts as deleted", "[assetrefs]")
{
	// The user may well have deleted it from a file manager since the scan. That is the outcome they
	// asked for, not a failure to report.
	const DataRoot root("bernini_refs_vanished");

	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 200, 0, 0, 255 } });

	const DeletionPlan plan = planDeletion(root.Scan(), "Derived/SourceTextures/a.ktx2");
	REQUIRE(plan.Allowed());

	fs::remove(root.path / "Derived/SourceTextures" / "a.ktx2");

	CHECK(root.Source().DeleteAsset(plan).status == DeletionStatus::kDeleted);
}

TEST_CASE("A directory is held only from outside it", "[assetrefs]")
{
	// The rule that makes a folder deletable at all. Everything inside goes together, so an edge with
	// both ends inside is not holding anything back -- only an edge reaching in from outside is.
	const DataRoot root("bernini_refs_dir");

	// A self-contained folder: its material routes from its own texture, and nothing else names either.
	WriteSource(
		root.path / "Derived/SourceTextures" / "kirk" / "tex0.ktx2",
		{ { 200, 0, 0, 255 } });
	fs::create_directories(root.path / "Authored/Materials" / "kirk");
	BakeAndSave(root, "kirk/Body.bmaterial", "Derived/SourceTextures/kirk/tex0.ktx2");

	SECTION("a folder whose references are all internal deletes, and takes them with it")
	{
		// Materials/kirk names a texture *outside* itself, which is an edge pointing out, not in. That
		// texture stays -- the same rule that keeps a deleted mesh's materials.
		const DeletionPlan plan = planDeletion(root.Scan(), "Authored/Materials/kirk");

		REQUIRE(plan.Allowed());
		REQUIRE(plan.IsDirectory());
		CHECK(
			plan.contents == std::vector<std::string>{ "Authored/Materials/kirk/Body.bmaterial" });

		REQUIRE(root.Source().DeleteAsset(plan).status == DeletionStatus::kDeleted);

		CHECK_FALSE(fs::exists(root.path / "Authored/Materials" / "kirk"));
		CHECK(fs::exists(root.path / "Derived/SourceTextures" / "kirk" / "tex0.ktx2"));
	}

	SECTION("a folder something outside routes from does not")
	{
		const DeletionPlan plan = planDeletion(root.Scan(), "Derived/SourceTextures/kirk");

		REQUIRE_FALSE(plan.Allowed());
		REQUIRE(plan.blockers.size() == 1);
		CHECK(plan.blockers.front().referrer == "Authored/Materials/kirk/Body.bmaterial");
		CHECK(plan.blockers.front().kind == RefKind::kChannelRoute);

		CHECK(root.Source().DeleteAsset(plan).status == DeletionStatus::kRefused);
		CHECK(fs::exists(root.path / "Derived/SourceTextures" / "kirk" / "tex0.ktx2"));
	}

	SECTION("and a folder holding the mesh is never held, because nothing names a mesh")
	{
		SaveMesh(root, "kirk.bmesh", { "Authored/Materials/kirk/Body.bmaterial" });

		CHECK(planDeletion(root.Scan(), "Derived/Meshes").Allowed());
	}
}

TEST_CASE("Deleting a directory takes every file under it, tracked or not", "[assetrefs]")
{
	// remove_all does not ask what a file is for, so the plan must not either: a README the user dropped
	// in the folder goes with it, and the count it is warned with has to include that.
	const DataRoot root("bernini_refs_dir_contents");

	WriteSource(
		root.path / "Derived/SourceTextures" / "props" / "tex0.ktx2",
		{ { 200, 0, 0, 255 } });
	WriteSource(
		root.path / "Derived/SourceTextures" / "props" / "nested" / "tex1.ktx2",
		{ { 0, 9, 0, 255 } });
	std::ofstream(root.path / "Derived/SourceTextures" / "props" / "notes.txt")
		<< "source: some_dcc_tool";

	const DeletionPlan plan = planDeletion(root.Scan(), "Derived/SourceTextures/props");

	REQUIRE(plan.Allowed());
	CHECK(
		plan.contents == std::vector<std::string>{ "Derived/SourceTextures/props/nested/tex1.ktx2",
	                                               "Derived/SourceTextures/props/notes.txt",
	                                               "Derived/SourceTextures/props/tex0.ktx2" });

	REQUIRE(root.Source().DeleteAsset(plan).status == DeletionStatus::kDeleted);
	CHECK_FALSE(fs::exists(root.path / "Derived/SourceTextures" / "props"));
}

TEST_CASE("A directory is held by a reference into any depth of it", "[assetrefs]")
{
	// Deleting Derived/SourceTextures would take kirk/tex0.ktx2 with it, so the material two levels
	// down still
	// holds the whole tree. A check that only looked at the folder's immediate children would miss it.
	const DataRoot root("bernini_refs_dir_deep");

	WriteSource(
		root.path / "Derived/SourceTextures" / "kirk" / "tex0.ktx2",
		{ { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "Derived/SourceTextures/kirk/tex0.ktx2");

	const DeletionPlan plan = planDeletion(root.Scan(), "Derived/SourceTextures");

	REQUIRE_FALSE(plan.Allowed());
	CHECK(plan.blockers.front().target == "Derived/SourceTextures/kirk/tex0.ktx2");
}

TEST_CASE("The data root itself is not something inside the data root", "[assetrefs]")
{
	const DataRoot root("bernini_refs_dir_escape");

	for (const char* path : { "", ".", "..", "../Meshes", "/outside/x.ktx2" })
	{
		INFO("path: " << path);
		CHECK_THROWS_AS(planDeletion(root.Scan(), path), std::runtime_error);
	}
}

#ifdef _WIN32
TEST_CASE("An asset held open cannot be deleted, and says so", "[assetrefs]")
{
	// Not hypothetical: the editor decodes .ktx2 thumbnails on a thread pool, and Windows will not
	// unlink a file that is open. Reporting a success we did not achieve would leave the user staring
	// at a file they were told was gone.
	const DataRoot root("bernini_refs_locked");

	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 200, 0, 0, 255 } });

	const DeletionPlan plan = planDeletion(root.Scan(), "Derived/SourceTextures/a.ktx2");
	REQUIRE(plan.Allowed());

	const fs::path file   = root.path / "Derived/SourceTextures" / "a.ktx2";
	const HANDLE   handle = ::CreateFileW(
		file.wstring().c_str(),
		GENERIC_READ,
		0,  // no sharing: exactly what an in-flight decode holds
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	REQUIRE(handle != INVALID_HANDLE_VALUE);

	const DeletionResult result = root.Source().DeleteAsset(plan);

	::CloseHandle(handle);

	CHECK(result.status == DeletionStatus::kFailed);
	CHECK_FALSE(result.error.empty());
	CHECK(fs::exists(file));

	// And once the reader lets go, the same plan goes through.
	CHECK(root.Source().DeleteAsset(plan).status == DeletionStatus::kDeleted);
}
#endif

// The hole this closes: the graph knew .bmesh, .bmaterial and .ktx2 and nothing else, so a baked
// environment map came back unreferenced and the editor -- which gates deletion on this -- would have
// let a live one go.
TEST_CASE("A baked environment map is held by the container that baked it", "[assetrefs]")
{
	const DataRoot    root("bernini_refs_env_baked");
	const Environment e = WriteEnvironment(root);

	const AssetRefGraph graph = root.Scan();

	CHECK(graph.environmentsScanned == 3);
	CHECK(graph.broken.empty());

	for (const std::string* map : { &e.skyBaked, &e.prefilter, &e.irradiance })
	{
		INFO("baked map: " << *map);
		CHECK(graph.IsReferenced(*map));
		CHECK_FALSE(planDeletion(graph, *map).Allowed());
	}

	// Named by the container that wrote it, and reported as such rather than as some other edge.
	const std::span<const AssetRef> holders = graph.ReferrersOf(e.skyBaked);
	REQUIRE(holders.size() == 1);
	CHECK(holders.front().referrer == e.sky);
	CHECK(holders.front().kind == RefKind::kBakedMap);
}

// The source is what a re-bake reads, so it is held for the same reason a material's routed source is.
TEST_CASE("An environment source is held by what bakes from it", "[assetrefs]")
{
	const DataRoot    root("bernini_refs_env_source");
	const Environment e = WriteEnvironment(root);

	const AssetRefGraph graph = root.Scan();

	CHECK(graph.IsReferenced(e.skySource));
	CHECK_FALSE(planDeletion(graph, e.skySource).Allowed());

	// One store, three routes across two containers -- and the sky and the lighting are both named,
	// each once, because a referrer reported twice would read as two blockers to the user.
	const std::span<const AssetRef> holders = graph.ReferrersOf(e.skySource);
	CHECK(holders.size() == 2);
	CHECK(std::ranges::all_of(holders, [](const AssetRef& ref) {
		return ref.kind == RefKind::kEnvSource;
	}));
}

// A `.benv` holds no pixels, so composing the pair is the only thing it does -- and the only thing
// keeping either half from being deleted out from under it.
TEST_CASE("A .benv holds the sky and the lighting it composes", "[assetrefs]")
{
	const DataRoot    root("bernini_refs_env_parts");
	const Environment e = WriteEnvironment(root);

	const AssetRefGraph graph = root.Scan();

	for (const std::string* part : { &e.sky, &e.lighting })
	{
		INFO("part: " << *part);

		const std::span<const AssetRef> holders = graph.ReferrersOf(*part);
		REQUIRE(holders.size() == 1);
		CHECK(holders.front().referrer == e.env);
		CHECK(holders.front().kind == RefKind::kEnvironmentPart);

		CHECK_FALSE(planDeletion(graph, *part).Allowed());
	}

	// Nothing names the .benv, so it is the one end of the chain that can go -- and taking it leaves
	// the pair behind, exactly as deleting a mesh leaves its materials.
	CHECK(planDeletion(graph, e.env).Allowed());
}

// Before this, planDeletion threw on one: a .bsky was "a file of no kind this project stores anything
// about", so the editor could not even ask the question.
TEST_CASE("The environment containers are assets the project knows", "[assetrefs]")
{
	CHECK(assetTypeFromExtension("Authored/Environments/forest.benv") == AssetType::kEnvironment);
	CHECK(assetTypeFromExtension("Derived/Sky/forest.bsky") == AssetType::kSky);
	CHECK(assetTypeFromExtension("Derived/EnvLighting/forest.benvl") == AssetType::kEnvLighting);

	// The suffix decides, case-insensitively, as it does for every other kind.
	CHECK(assetTypeFromExtension("Derived/Sky/FOREST.BSKY") == AssetType::kSky);

	// .benvl must not be read as a .benv with something after it.
	CHECK(assetTypeFromExtension("a.benvl") != AssetType::kEnvironment);
}

// Fatal on purpose, as it is for a mesh or a material: an environment we cannot read is one whose
// maps we cannot see, and we would then delete one out from under it.
TEST_CASE("An unreadable environment stops the scan rather than being skipped", "[assetrefs]")
{
	const DataRoot root("bernini_refs_env_unreadable");
	WriteEnvironment(root);

	std::ofstream(root.path / "Derived/Sky" / "broken.bsky", std::ios::binary) << "not a sky";

	REQUIRE_THROWS_AS(root.Scan(), std::runtime_error);
}

// The UI runtime reads its documents through the mount like every other asset, so the project has
// to know the three kinds: an extension it does not claim is one `pack` drops and `planDeletion`
// refuses to reason about at all.
TEST_CASE("The UI documents, styles and fonts are assets the project knows", "[assetrefs]")
{
	CHECK(assetTypeFromExtension("Authored/UI/menu.rml") == AssetType::kUiDocument);
	CHECK(assetTypeFromExtension("Authored/UI/menu.rcss") == AssetType::kUiStyle);
	CHECK(assetTypeFromExtension("Authored/Fonts/ui.ttf") == AssetType::kFont);

	// The suffix decides, case-insensitively, as it does for every other kind.
	CHECK(assetTypeFromExtension("Authored/UI/MENU.RML") == AssetType::kUiDocument);

	// .rcss must not be read as a .rml with something after it, nor either as a container.
	CHECK(assetTypeFromExtension("a.rcss") != AssetType::kUiDocument);
	CHECK_FALSE(containerKindForExtension(".rml").has_value());
	CHECK_FALSE(containerKindForExtension(".ttf").has_value());
}

TEST_CASE("A UI document is a leaf: nothing holds it, and it holds nothing", "[assetrefs]")
{
	const DataRoot root("bernini_refs_ui");

	fs::create_directories(root.path / "Authored/UI");
	fs::create_directories(root.path / "Authored/Fonts");
	std::ofstream(root.path / "Authored/UI/menu.rml") << "<rml><body>menu</body></rml>";
	std::ofstream(root.path / "Authored/UI/menu.rcss") << "body { color: #fff; }";
	std::ofstream(root.path / "Authored/Fonts/ui.ttf") << "not really a font";

	const AssetRefGraph graph = root.Scan();

	// The graph does not read a foreign kind, so a document's @import is not an edge -- the runtime
	// resolves that itself, and nothing here claims otherwise.
	for (const std::string& key : { std::string("Authored/UI/menu.rml"),
	                                std::string("Authored/UI/menu.rcss"),
	                                std::string("Authored/Fonts/ui.ttf") })
	{
		INFO("asset: " << key);
		CHECK(graph.ReferrersOf(key).empty());

		const DeletionPlan plan = planDeletion(graph, key);
		REQUIRE(plan.Allowed());
		CHECK(root.Source().DeleteAsset(plan).status == DeletionStatus::kDeleted);
		CHECK_FALSE(fs::exists(root.path / key));
	}
}

// ADR-8: gamelib's Rml::SystemInterface::JoinPath refuses an escape through this, rather than
// re-rolling the rule -- so it has to be reachable from outside assetlib.
TEST_CASE("The escape check is public, and refuses what leaves the data root", "[assetrefs]")
{
	CHECK_NOTHROW(requireInsideDataRoot("ui", normalizePath("Authored/UI/menu.rml")));
	CHECK_NOTHROW(requireInsideDataRoot("ui", normalizePath("Authored/UI/../Fonts/ui.ttf")));

	for (const std::string_view escape : { "../outside.rml", "/etc/passwd", "..", "" })
	{
		INFO("path: " << escape);
		CHECK_THROWS_AS(requireInsideDataRoot("ui", normalizePath(escape)), std::runtime_error);
	}
}
