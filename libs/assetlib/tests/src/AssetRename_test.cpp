#include <assetlib/asset_refs.h>

#include <assetlib/banim_io.h>
#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/bvat_io.h>
#include <assetlib/skeleton.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BVat.h>
#include <assetlib_structs/Skeleton.h>

#include "RefsSandbox.h"

#include "MountAt.h"

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;

	/** Plans and executes the rename in one step, for the tests about the outcome rather than the plan. */
	RenameResult
	Rename(const DataRoot& root, std::string_view from, std::string_view to)
	{
		return renameAsset(planRename(root.Scan(), from, to), root.Desc());
	}
}

TEST_CASE("Renaming an unreferenced asset moves the file", "[assetrename]")
{
	const DataRoot root("bernini_rename_loose");

	WriteSource(root.path / "textures_src" / "old.ktx2", { { 200, 0, 0, 255 } });

	const RenamePlan plan =
		planRename(root.Scan(), "textures_src/old.ktx2", "textures_src/new.ktx2");

	CHECK(plan.assetType == AssetType::kTexture);
	CHECK(plan.referrers.empty());

	REQUIRE(renameAsset(plan, root.Desc()).status == RenameStatus::kRenamed);

	CHECK_FALSE(fs::exists(root.path / "textures_src" / "old.ktx2"));
	CHECK(fs::exists(root.path / "textures_src" / "new.ktx2"));
}

TEST_CASE("Renaming a material re-points every mesh that names it", "[assetrename]")
{
	// The rule the feature exists for: a rename is never blocked by references, because the references
	// follow. A mesh left naming the old path would be exactly the broken edge deletion works to prevent.
	const DataRoot root("bernini_rename_material");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "old.bmaterial", "textures_src/a.ktx2");

	SaveMesh(root, "one.bmesh", { "Materials/old.bmaterial" });
	SaveMesh(root, "two.bmesh", { "Materials/other.bmaterial", "Materials/old.bmaterial" });

	const RenamePlan plan =
		planRename(root.Scan(), "Materials/old.bmaterial", "Materials/new.bmaterial");

	REQUIRE(renameAsset(plan, root.Desc()).status == RenameStatus::kRenamed);

	CHECK_FALSE(fs::exists(root.path / "Materials" / "old.bmaterial"));
	CHECK(fs::exists(root.path / "Materials" / "new.bmaterial"));

	CHECK(
		loadMeshRefs(root.path / "Meshes" / "one.bmesh").materials ==
		std::vector<std::string>{ "Materials/new.bmaterial" });

	// Only the slot that named it moves; the sibling is untouched.
	CHECK(
		loadMeshRefs(root.path / "Meshes" / "two.bmesh").materials ==
		std::vector<std::string>{ "Materials/other.bmaterial", "Materials/new.bmaterial" });

	SECTION("and the rewritten project has no reference to the old name")
	{
		const AssetRefGraph after = root.Scan();

		CHECK_FALSE(after.IsReferenced("Materials/old.bmaterial"));
		CHECK(ReferrerPaths(after, "Materials/new.bmaterial").size() == 2);
	}
}

TEST_CASE("A rewritten mesh still carries its geometry", "[assetrename]")
{
	// The material chunk is a few hundred bytes of a file that is mostly vertex data, and the rewrite
	// round-trips the whole container -- so what must not change is everything else.
	const DataRoot root("bernini_rename_roundtrip");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "old.bmaterial", "textures_src/a.ktx2");
	SaveMesh(root, "mesh.bmesh", { "Materials/old.bmaterial", "Materials/keep.bmaterial" });

	const BMesh before = load(root.path / "Meshes" / "mesh.bmesh");

	REQUIRE(
		Rename(root, "Materials/old.bmaterial", "Materials/new.bmaterial").status ==
		RenameStatus::kRenamed);

	const BMesh after = load(root.path / "Meshes" / "mesh.bmesh");

	CHECK(
		after.materials ==
		std::vector<std::string>{ "Materials/new.bmaterial", "Materials/keep.bmaterial" });
	CHECK(after.nodes.size() == before.nodes.size());
	CHECK(after.submeshes.size() == before.submeshes.size());
	CHECK(after.stringPool == before.stringPool);
}

TEST_CASE("Renaming a texture re-points the material that routes it", "[assetrename]")
{
	// A material names its textures twice -- the routes a re-bake reads and the triplet its last bake
	// wrote -- and the rename must catch whichever of them names the file.
	const DataRoot root("bernini_rename_texture");

	WriteSource(root.path / "textures_src" / "old.ktx2", { { 200, 0, 0, 255 } });
	const BMaterial baked = BakeAndSave(root, "mat.bmaterial", "textures_src/old.ktx2");

	SECTION("a routed source")
	{
		REQUIRE(
			Rename(root, "textures_src/old.ktx2", "textures_src/new.ktx2").status ==
			RenameStatus::kRenamed);

		const BMaterial material = loadMaterial(root.path / "Materials" / "mat.bmaterial");

		CHECK(material.pbr.routes[0].texture == "textures_src/new.ktx2");
		CHECK(root.Scan().broken.empty());
	}

	SECTION("a baked map")
	{
		const std::string renamed = "Textures/renamed_basecolor.ktx2";

		REQUIRE(Rename(root, baked.pbr.baseColorTexture, renamed).status == RenameStatus::kRenamed);

		const BMaterial material = loadMaterial(root.path / "Materials" / "mat.bmaterial");

		CHECK(material.pbr.baseColorTexture == renamed);
		CHECK(root.Scan().broken.empty());
	}
}

TEST_CASE("Renaming an environment part re-points its whole family", "[assetrename]")
{
	const DataRoot    root("bernini_rename_env");
	const Environment e = WriteEnvironment(root);

	SECTION("the .bsky a .benv composes")
	{
		REQUIRE(Rename(root, e.sky, "Sky/dawn.bsky").status == RenameStatus::kRenamed);

		CHECK(loadEnv(root.path / e.env).sky == "Sky/dawn.bsky");
		CHECK(root.Scan().broken.empty());
	}

	SECTION("the radiance three routes across two containers read")
	{
		REQUIRE(
			Rename(root, e.skySource, "textures_src/dawn.ktx2").status == RenameStatus::kRenamed);

		CHECK(loadSky(root.path / e.sky).sky.source == "textures_src/dawn.ktx2");

		const BEnvLighting lighting = loadEnvLighting(root.path / e.lighting);
		CHECK(lighting.prefilter.source == "textures_src/dawn.ktx2");
		CHECK(lighting.irradiance.source == "textures_src/dawn.ktx2");

		CHECK(root.Scan().broken.empty());
	}
}

TEST_CASE("Renaming a directory re-points every reference into it", "[assetrename]")
{
	const DataRoot root("bernini_rename_dir");

	// One reference in from outside, one wholly inside: the outside one is rewritten where it stands,
	// the inside one is rewritten and then moves with the directory.
	WriteSource(root.path / "textures_src" / "kirk" / "tex0.ktx2", { { 200, 0, 0, 255 } });
	WriteSource(root.path / "textures_src" / "kirk" / "tex1.ktx2", { { 0, 200, 0, 255 } });
	BakeAndSave(root, "outside.bmaterial", "textures_src/kirk/tex0.ktx2");

	BMaterial inside;
	inside.pbr.routes[0] = { "textures_src/kirk/tex1.ktx2", 0 };
	fs::create_directories(root.path / "textures_src" / "kirk");
	saveMaterial(inside, root.path / "textures_src" / "kirk" / "inside.bmaterial");

	const RenamePlan plan = planRename(root.Scan(), "textures_src/kirk", "textures_src/spock");

	CHECK(plan.IsDirectory());

	REQUIRE(renameAsset(plan, root.Desc()).status == RenameStatus::kRenamed);

	CHECK_FALSE(fs::exists(root.path / "textures_src" / "kirk"));
	CHECK(fs::exists(root.path / "textures_src" / "spock" / "tex0.ktx2"));

	CHECK(
		loadMaterial(root.path / "Materials" / "outside.bmaterial").pbr.routes[0].texture ==
		"textures_src/spock/tex0.ktx2");
	CHECK(
		loadMaterial(root.path / "textures_src" / "spock" / "inside.bmaterial")
			.pbr.routes[0]
			.texture == "textures_src/spock/tex1.ktx2");

	CHECK(root.Scan().broken.empty());
}

TEST_CASE("planRename refuses what a rename must never do", "[assetrename]")
{
	const DataRoot root("bernini_rename_refuse");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	WriteSource(root.path / "textures_src" / "b.ktx2", { { 0, 200, 0, 255 } });

	const AssetRefGraph graph = root.Scan();

	SECTION("changing what kind of asset a file is")
	{
		CHECK_THROWS_AS(
			planRename(graph, "textures_src/a.ktx2", "textures_src/a.bmaterial"),
			std::runtime_error);
	}

	SECTION("overwriting a file that already exists")
	{
		CHECK_THROWS_AS(
			planRename(graph, "textures_src/a.ktx2", "textures_src/b.ktx2"),
			std::runtime_error);
	}

	SECTION("renaming to the name it already has")
	{
		CHECK_THROWS_AS(
			planRename(graph, "textures_src/a.ktx2", "textures_src/a.ktx2"),
			std::runtime_error);
	}

	SECTION("renaming something that does not exist")
	{
		CHECK_THROWS_AS(
			planRename(graph, "textures_src/ghost.ktx2", "textures_src/new.ktx2"),
			std::runtime_error);
	}

	SECTION("renaming a file of no kind the project tracks")
	{
		std::ofstream(root.path / "notes.txt") << "hello";
		CHECK_THROWS_AS(planRename(graph, "notes.txt", "notes2.txt"), std::runtime_error);
	}

	SECTION("renaming into a directory that does not exist")
	{
		CHECK_THROWS_AS(
			planRename(graph, "textures_src/a.ktx2", "textures_src/ghost/a.ktx2"),
			std::runtime_error);
	}

	SECTION("moving a directory inside itself")
	{
		CHECK_THROWS_AS(
			planRename(graph, "textures_src", "textures_src/inner"),
			std::runtime_error);
	}

	SECTION("reaching outside the data root, from either end")
	{
		CHECK_THROWS_AS(planRename(graph, "../a.ktx2", "textures_src/a.ktx2"), std::runtime_error);
		CHECK_THROWS_AS(planRename(graph, "textures_src/a.ktx2", "../a.ktx2"), std::runtime_error);

		// An absolute path is not "inside" anything: operator/ would let it replace the root outright.
		CHECK_THROWS_AS(
			planRename(graph, "textures_src/a.ktx2", "/outside/a.ktx2"),
			std::runtime_error);
		CHECK_THROWS_AS(
			planRename(graph, "/outside/a.ktx2", "textures_src/a.ktx2"),
			std::runtime_error);
	}
}

TEST_CASE("A referrer that stopped parsing fails the rename, and is not touched", "[assetrename]")
{
	// The scan reads a referrer once, at plan time; by execution it may be locked, gone, or corrupted
	// behind the editor's back. That is weather, not a crash: the rename reports kFailed with nothing
	// moved and nothing rewritten.
	const DataRoot root("bernini_rename_badreferrer");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "textures_src/a.ktx2");

	const RenamePlan plan = planRename(root.Scan(), "textures_src/a.ktx2", "textures_src/new.ktx2");

	std::ofstream(root.path / "Materials" / "mat.bmaterial", std::ios::binary) << "not a material";

	const RenameResult result = renameAsset(plan, root.Desc());

	CHECK(result.status == RenameStatus::kFailed);
	CHECK_FALSE(result.error.empty());
	CHECK(fs::exists(root.path / "textures_src" / "a.ktx2"));
	CHECK_FALSE(fs::exists(root.path / "textures_src" / "new.ktx2"));
}

TEST_CASE("A rename whose file vanished fails without touching the referrers", "[assetrename]")
{
	// The data root is shared with the user's file manager, and a deletion shrugs at a file already
	// gone -- but a rename cannot: rewriting the referrers with nothing to move would break every one.
	const DataRoot root("bernini_rename_vanished");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "textures_src/a.ktx2");

	const RenamePlan plan = planRename(root.Scan(), "textures_src/a.ktx2", "textures_src/new.ktx2");

	fs::remove(root.path / "textures_src" / "a.ktx2");

	const RenameResult result = renameAsset(plan, root.Desc());

	CHECK(result.status == RenameStatus::kFailed);
	CHECK_FALSE(result.error.empty());

	// The material still says what it said.
	CHECK(
		loadMaterial(root.path / "Materials" / "mat.bmaterial").pbr.routes[0].texture ==
		"textures_src/a.ktx2");
}

TEST_CASE("A destination taken since the plan fails the rename", "[assetrename]")
{
	const DataRoot root("bernini_rename_taken");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });

	const RenamePlan plan = planRename(root.Scan(), "textures_src/a.ktx2", "textures_src/new.ktx2");

	WriteSource(root.path / "textures_src" / "new.ktx2", { { 0, 200, 0, 255 } });

	CHECK(renameAsset(plan, root.Desc()).status == RenameStatus::kFailed);
	CHECK(fs::exists(root.path / "textures_src" / "a.ktx2"));
}

TEST_CASE("Renaming a skeleton re-points the whole rig that hangs off it", "[assetrename]")
{
	const DataRoot root("bernini_rename_rig");

	// The smallest rig a .bvat can be baked from -- one bone, one skinned vertex, one single-frame
	// clip -- so every kind of skeleton referrer exists to be re-pointed: the mesh names it, the
	// clip set names it, and the bake stamps all three.
	auto skeleton = Skeleton();

	auto bone       = Bone();
	bone.bindPose   = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
	bone.parent     = c_InvalidIndex;
	bone.nameOffset = skeleton.stringPool.add("root");
	skeleton.bones.push_back(bone);
	skeleton.bones[0].inverseBind = glm::inverse(bindPoseModelTransforms(skeleton)[0]);

	fs::create_directories(root.path / "Skeletons");
	saveSkeleton(skeleton, root.path / "Skeletons/rig.bskel");

	auto animations              = AnimationSet();
	animations.skeleton          = "Skeletons/rig.bskel";
	animations.skeletonSignature = skeletonSignature(skeleton);
	animations.boneCount         = 1;

	auto clip        = AnimationClip();
	clip.nameOffset  = animations.stringPool.add("rest");
	clip.firstSample = 0;
	clip.frameCount  = 1;
	clip.sampleRate  = 30.0f;
	animations.clips.push_back(clip);
	animations.samples.push_back(bone.bindPose);

	fs::create_directories(root.path / "Animations");
	saveAnimations(animations, root.path / "Animations/rig.banim");

	auto mesh = BMesh();

	auto submesh                  = Submesh();
	submesh.layout.attributeCount = 3;
	submesh.layout.attributes[0]  = { VertexSemantic::kPosition, VertexFormat::kFloat32x3, 0 };
	submesh.layout.attributes[1]  = { VertexSemantic::kJoints0, VertexFormat::kUint16x4, 12 };
	submesh.layout.attributes[2]  = { VertexSemantic::kWeights0, VertexFormat::kUnorm16x4, 20 };
	submesh.layout.stride         = 28;

	const glm::vec3               position(1.0f, 0.0f, 0.0f);
	const std::array<uint16_t, 4> joints  = { { 0, 0, 0, 0 } };
	const std::array<uint16_t, 4> weights = { { 65535, 0, 0, 0 } };
	mesh.vertexData.resize(28);
	std::memcpy(mesh.vertexData.data(), &position, 12);
	std::memcpy(mesh.vertexData.data() + 12, joints.data(), 8);
	std::memcpy(mesh.vertexData.data() + 20, weights.data(), 8);
	submesh.vertexCount = 1;
	mesh.submeshes.push_back(submesh);

	mesh.meshes   = { Mesh{ 0, 1, 0 } };
	mesh.skeleton = "Skeletons/rig.bskel";
	save(mesh, root.path / "Meshes/rig.bmesh");

	const fs::path baked = root.path / vatPathFor("Meshes/rig.bmesh", "Animations/rig.banim");
	saveVat(bakeVat(VatBakeDesc{ root.path, "Meshes/rig.bmesh", "Animations/rig.banim" }), baked);

	REQUIRE(
		Rename(root, "Skeletons/rig.bskel", "Skeletons/hero.bskel").status ==
		RenameStatus::kRenamed);

	CHECK(loadMeshRefs(root.path / "Meshes/rig.bmesh").skeleton == "Skeletons/hero.bskel");
	CHECK(loadAnimationSkeletonPath(root.path / "Animations/rig.banim") == "Skeletons/hero.bskel");

	// A skeleton rename does not change the bake's derived name, so the file stays put.
	const VatRefs refs = loadVatRefs(baked);
	CHECK(refs.skeleton == "Skeletons/hero.bskel");
	CHECK(refs.mesh == "Meshes/rig.bmesh");

	// The stamps record size and mtime, both of which a rename preserves: the rewritten .bvat is
	// still fresh, not a re-bake waiting to happen.
	CHECK_FALSE(vatIsStale(loadVatTables(baked), MountAt(root.path)));

	// An input only the .bvat references follows too -- and this one is part of the derived name,
	// so the bake moves to where the runtime will now look, still fresh.
	REQUIRE(
		Rename(root, "Animations/rig.banim", "Animations/hero.banim").status ==
		RenameStatus::kRenamed);

	const fs::path moved = root.path / vatPathFor("Meshes/rig.bmesh", "Animations/hero.banim");
	CHECK_FALSE(fs::exists(baked));
	REQUIRE(fs::exists(moved));
	CHECK(loadVatRefs(moved).animations == "Animations/hero.banim");
	CHECK_FALSE(vatIsStale(loadVatTables(moved), MountAt(root.path)));
}
