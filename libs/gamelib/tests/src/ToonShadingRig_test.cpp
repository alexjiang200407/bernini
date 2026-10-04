#include "toon_shading_rig.h"
#include <assetlib/AssetStore.h>
#include <assetlib/import_document.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/BToonShadingRig.h>
#include <assetlib_structs/Skeleton.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/types/InstanceDesc.h>
#include <bgl/types/SkinnedMeshInstanceDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/ToonShadingRigDesc.h>
#include <bgl/types/ToonShadingRigHandle.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstdint>
#include <filesystem>
#include <gamelib/AssetManager.h>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "util/RigFixture.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"

// The toon shading rig a character's `.bimport` names, as gamelib loads it: the document turned
// into the renderer's desc, added once and shared, given to every placement of the mesh, and
// deleted with the last geom holding it. Synthetic rigs only -- the leg rig's bones stand in for a
// head's.

namespace
{
	using game::test::DataRoot;
	using game::test::WriteLegRig;
	using game::test::WriteRig;

	constexpr std::string_view c_RigKey = "Authored/ToonRigs/face.btoonrig";

	bgl::test::GraphicsSetup
	HeadlessOptions()
	{
		auto opts                        = bgl::test::GraphicsSetup();
		opts.gpuContext.enableDebugLayer = true;
		opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
		return opts;
	}

	/** One mirrored edit of one key, its angles in degrees as a document holds them. */
	assetlib::BToonShadingRig
	MakeRig(std::string headBone)
	{
		auto key     = assetlib::ToonShadingRigKey();
		key.light    = glm::vec3(1.0f, 0.0f, 0.0f);
		key.position = glm::vec3(0.0f, 0.0f, 0.1f);
		key.gain     = -0.5f;
		key.rotation = 90.0f;

		auto edit     = assetlib::ToonShadingRigEdit();
		edit.name     = "cheek";
		edit.keys     = { key };
		edit.mirrored = true;

		auto rig                        = assetlib::BToonShadingRig();
		rig.edits                       = { edit };
		rig.faceLight.maxAzimuth        = 60.0f;
		rig.faceLight.minElevation      = -20.0f;
		rig.faceLight.azimuthFadeAmount = 0.6f;
		rig.headBone                    = std::move(headBone);
		rig.headToBone = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.2f, 0.0f));
		return rig;
	}

	/** Writes `rig` and names it from the `.bimport` of `source`. */
	void
	NameRig(
		const std::filesystem::path&     dataRoot,
		std::string_view                 source,
		const assetlib::BToonShadingRig& rig)
	{
		const assetlib::AssetStore store(dataRoot);
		store.Save(rig, std::string(c_RigKey));

		const std::string key      = assetlib::importDocumentKeyFor(source);
		auto              document = store.Load<assetlib::ImportDocument>(key);
		document.toonShadingRig    = std::string(c_RigKey);
		store.Save(document, key);
	}

	assetlib::Skeleton
	LegSkeleton(const std::filesystem::path& dataRoot)
	{
		return assetlib::AssetStore(dataRoot).Load<assetlib::Skeleton>(
			"Derived/Skeletons/leg.glb-0000000000000001.bskel");
	}
}

TEST_CASE("A toon shading rig document becomes the renderer's desc", "[toonshadingrig]")
{
	const DataRoot root("bernini_toonrig_desc");
	WriteLegRig(root.path);
	const assetlib::Skeleton skeleton = LegSkeleton(root.path);

	SECTION("angles go from degrees to radians, and the rest across as written")
	{
		const bgl::ToonShadingRigDesc desc =
			game::ToonShadingRigDescOf(MakeRig(""), nullptr, game::ToonShadingRigPose::kPosed);

		CHECK(desc.faceLight.maxAzimuth == Catch::Approx(std::numbers::pi_v<float> / 3.0f));
		CHECK(desc.faceLight.minElevation == Catch::Approx(-std::numbers::pi_v<float> / 9.0f));
		CHECK(desc.faceLight.azimuthFadeAmount == 0.6f);
		CHECK(desc.faceLight.maxElevation == Catch::Approx(bgl::FaceLightDesc().maxElevation));
		REQUIRE(desc.edits.size() == 1);
		CHECK(desc.edits[0].mirrored);
		REQUIRE(desc.edits[0].keys.size() == 1);
		CHECK(desc.edits[0].keys[0].rotation == Catch::Approx(std::numbers::pi_v<float> / 2.0f));
		CHECK(desc.edits[0].keys[0].gain == -0.5f);
		CHECK(desc.headRadius == bgl::ToonShadingRigDesc().headRadius);
		CHECK_FALSE(desc.headBoneIndex.has_value());
	}

	SECTION("a posed head bone resolves by name to its index in the mesh's rig")
	{
		const bgl::ToonShadingRigDesc desc = game::ToonShadingRigDescOf(
			MakeRig("ankle"),
			&skeleton,
			game::ToonShadingRigPose::kPosed);
		CHECK(desc.headBoneIndex == std::optional<uint32_t>(2));
		CHECK(desc.headToBone == MakeRig("").headToBone);
	}

	SECTION("in the bind pose the bone's bind transform is folded into the head's frame")
	{
		const bgl::ToonShadingRigDesc desc = game::ToonShadingRigDescOf(
			MakeRig("ankle"),
			&skeleton,
			game::ToonShadingRigPose::kBindPose);
		CHECK_FALSE(desc.headBoneIndex.has_value());

		// The ankle binds at (0, 0.1, 0); the head sits 0.2 above it.
		const glm::vec4 origin = desc.headToBone * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		CHECK(origin.y == Catch::Approx(0.3f));
		CHECK(
			desc.headToBone ==
			assetlib::bindPoseModelTransforms(skeleton)[2] * MakeRig("").headToBone);
	}

	SECTION("a bone the rig does not carry, or no rig at all, is refused")
	{
		CHECK_THROWS(
			game::ToonShadingRigDescOf(
				MakeRig("skull"),
				&skeleton,
				game::ToonShadingRigPose::kPosed));
		CHECK_THROWS(
			game::ToonShadingRigDescOf(
				MakeRig("ankle"),
				nullptr,
				game::ToonShadingRigPose::kPosed));
	}
}

TEST_CASE(
	"A character's toon shading rig is added once and given to every placement",
	"[toonshadingrig][skinned]")
{
	const DataRoot root("bernini_toonrig_skinned");
	WriteLegRig(root.path);
	NameRig(root.path, "Authored/Meshes/leg.glb", MakeRig("ankle"));

	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene  = gfx->CreateScene(bgl::SceneDesc());
	auto view   = gfx->CreateSceneView(scene, 8);
	auto assets = game::AssetManager(scene, root.path);

	const auto mesh =
		assets.AcquireSkinnedMesh("Authored/Meshes/leg.glb", "Authored/Meshes/leg.glb");
	const auto place = [&] {
		return assets.CreateSkinnedInstance(
			view,
			bgl::SkinnedMeshInstanceDesc().SetGeom(mesh.geom).SetPlayback(
				bgl::SkinnedPlaybackDesc::FromClip(0)));
	};
	const auto first  = place();
	const auto second = place();

	const bgl::ToonShadingRigHandle rig = view->GetToonShadingRig(first);
	REQUIRE(rig.IsValid());
	CHECK(view->GetToonShadingRig(second) == rig);

	// One upload, held by the one geom both placements share.
	CHECK(assets.ToonShadingRigRefCount(rig) == 1);

	SECTION("a shared acquire shares the rig too")
	{
		const auto again =
			assets.AcquireSkinnedMesh("Authored/Meshes/leg.glb", "Authored/Meshes/leg.glb");
		CHECK(again.geom.handle.index == mesh.geom.handle.index);
		CHECK(assets.ToonShadingRigRefCount(rig) == 1);
		assets.ReleaseGeom(again.geom);
	}

	SECTION("the rig is deleted with the last geom holding it")
	{
		assets.DestroyInstance(view, first);
		CHECK(assets.ToonShadingRigRefCount(rig) == 1);
		assets.DestroyInstance(view, second);
		CHECK(assets.ToonShadingRigRefCount(rig) == 1);

		assets.ReleaseGeom(mesh.geom);
		CHECK(assets.ToonShadingRigRefCount(rig) == 0);

		// Already deleted by the manager: had it been left in the scene, nothing would refuse this.
		CHECK_THROWS(scene->DeleteToonShadingRig(rig));
	}
}

TEST_CASE("A toon shading rig naming a bone the rig lacks is left off", "[toonshadingrig][skinned]")
{
	const DataRoot root("bernini_toonrig_missing_bone");
	WriteLegRig(root.path);
	NameRig(root.path, "Authored/Meshes/leg.glb", MakeRig("skull"));

	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene  = gfx->CreateScene(bgl::SceneDesc());
	auto view   = gfx->CreateSceneView(scene, 8);
	auto assets = game::AssetManager(scene, root.path);

	// As an avatar naming a missing bone leaves the rig unplanted: the character still loads and
	// draws, cel shaded, and the log says why.
	const auto mesh =
		assets.AcquireSkinnedMesh("Authored/Meshes/leg.glb", "Authored/Meshes/leg.glb");
	REQUIRE(mesh.geom.IsValid());

	const auto instance = assets.CreateSkinnedInstance(
		view,
		bgl::SkinnedMeshInstanceDesc().SetGeom(mesh.geom).SetPlayback(
			bgl::SkinnedPlaybackDesc::FromClip(0)));
	CHECK_FALSE(view->GetToonShadingRig(instance).IsValid());
}

TEST_CASE("A static mesh carries a toon shading rig", "[toonshadingrig][static]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 8);

	SECTION("one naming no head bone takes the placement's frame")
	{
		const DataRoot root("bernini_toonrig_static");
		WriteRig(root.path);
		NameRig(root.path, "Authored/Meshes/rig.glb", MakeRig(""));

		auto       assets = game::AssetManager(scene, root.path);
		const auto geom   = assets.AcquireMesh("Authored/Meshes/rig.glb");
		const auto instance =
			assets.CreateInstance(view, bgl::StaticMeshInstanceDesc().SetGeom(geom));

		const bgl::ToonShadingRigHandle rig = view->GetToonShadingRig(instance);
		REQUIRE(rig.IsValid());
		CHECK(assets.ToonShadingRigRefCount(rig) == 1);

		assets.DestroyInstance(view, instance);
		assets.ReleaseGeom(geom);
		CHECK(assets.ToonShadingRigRefCount(rig) == 0);
	}

	SECTION("one naming a head bone holds it where the bind put it")
	{
		const DataRoot root("bernini_toonrig_static_bone");
		WriteLegRig(root.path);
		NameRig(root.path, "Authored/Meshes/leg.glb", MakeRig("ankle"));

		auto       assets = game::AssetManager(scene, root.path);
		const auto geom   = assets.AcquireMesh("Authored/Meshes/leg.glb");
		const auto instance =
			assets.CreateInstance(view, bgl::StaticMeshInstanceDesc().SetGeom(geom));
		CHECK(view->GetToonShadingRig(instance).IsValid());

		// Static and skinned are two uploads of one document: their descs differ in the bone.
		const auto skinned =
			assets.AcquireSkinnedMesh("Authored/Meshes/leg.glb", "Authored/Meshes/leg.glb");
		const auto placed = assets.CreateSkinnedInstance(
			view,
			bgl::SkinnedMeshInstanceDesc()
				.SetGeom(skinned.geom)
				.SetPlayback(bgl::SkinnedPlaybackDesc::FromClip(0)));
		CHECK(view->GetToonShadingRig(placed).IsValid());
		CHECK_FALSE(view->GetToonShadingRig(placed) == view->GetToonShadingRig(instance));
	}
}
