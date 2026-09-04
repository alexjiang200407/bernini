#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <filesystem>
#include <gamelib/AssetManager.h>

#include "util/GoldenImage.h"
#include "util/RigFixture.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <assetlib_structs/Node.h>
#include <bgl/GeomType.h>
#include <bgl/IScene.h>
#include <bgl/InstanceDesc.h>

#include <assetlib/AssetStore.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Skeleton.h>
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <stdexcept>

// Acquiring a rig as skinned geometry. There is no bake and no freshness rule -- the containers are
// the source -- so what this pins is the sharing, the release, and the one check bgl cannot make for
// itself: that a clip set still matches the rig it names.

namespace
{
	using game::test::DataRoot;
	using game::test::WriteRig;

	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.enableDebugLayer = true;
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		return opts;
	}

	/** Rewrites the .banim with a signature that no longer matches the rig it names. */
	void
	StaleTheClips(const std::filesystem::path& dataRoot)
	{
		auto animations = assetlib::AssetStore(dataRoot).Load<assetlib::AnimationSet>(
			"Derived/Animations/rig.banim");

		// What a reordered rig looks like from the clips' side: same bone count, different identity.
		animations.skeletonSignature ^= 0x9E3779B97F4A7C15ull;

		assetlib::AssetStore(dataRoot).Save(animations, "Derived/Animations/rig.banim");
	}

	/** Rewrites the .bmesh with a signature that no longer matches the rig it names. */
	void
	StaleTheMesh(const std::filesystem::path& dataRoot)
	{
		auto mesh =
			assetlib::AssetStore(dataRoot).Load<assetlib::BMesh>("Derived/Meshes/rig.bmesh");

		mesh.skeletonSignature ^= 0x9E3779B97F4A7C15ull;

		assetlib::AssetStore(dataRoot).Save(mesh, "Derived/Meshes/rig.bmesh");
	}

	/** Repoints the mesh at a second rig, so it and the clips were never cooked as a pair. */
	void
	CookTheMeshAgainstAnotherRig(const std::filesystem::path& dataRoot)
	{
		auto other      = assetlib::Skeleton();
		auto bone       = assetlib::Bone();
		bone.bindPose   = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		bone.parent     = assetlib::c_InvalidIndex;
		bone.nameOffset = other.stringPool.add("pelvis");
		other.bones.push_back(bone);
		other.bones[0].inverseBind = glm::inverse(assetlib::bindPoseModelTransforms(other)[0]);

		const auto store = assetlib::AssetStore(dataRoot);
		store.Save(other, "Derived/Skeletons/other.bskel");

		auto mesh              = store.Load<assetlib::BMesh>("Derived/Meshes/rig.bmesh");
		mesh.skeleton          = "Derived/Skeletons/other.bskel";
		mesh.skeletonSignature = assetlib::skeletonSignature(other);
		store.Save(mesh, "Derived/Meshes/rig.bmesh");
	}
}

TEST_CASE("a rig acquires as skinned geometry, shares, and releases", "[skinned][acquire]")
{
	DataRoot root("bernini_skinned_acquire");
	WriteRig(root.path);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 8);

	auto assets = game::AssetManager(scene, root.path);

	const auto mesh =
		assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim");
	REQUIRE(mesh.geom.IsValid());
	REQUIRE(mesh.geom.geomType == bgl::GeomType::kSkinnedMesh);

	SECTION("the clip table comes back named, so a caller can show it without re-reading the file")
	{
		REQUIRE(mesh.clips.size() == 1);
		CHECK(mesh.clips[0].name == "slide");
		CHECK(mesh.clips[0].frameCount == 2);
		CHECK(mesh.clips[0].sampleRate == 30.0f);
	}

	SECTION("a second acquire shares the upload rather than making another")
	{
		const auto again =
			assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim");
		CHECK(again.geom.handle.index == mesh.geom.handle.index);
		CHECK(again.clips.size() == mesh.clips.size());

		// One release leaves the first acquire's reference standing.
		assets.ReleaseGeom(again.geom);
		CHECK(scene->IsGeomAlive(mesh.geom));
	}

	SECTION("the same mesh can be live as static and skinned at once")
	{
		// Two keyspaces, two uploads.
		const auto staticGeom = assets.AcquireMesh("Derived/Meshes/rig.bmesh");
		CHECK(staticGeom.IsValid());
		CHECK(staticGeom.geomType == bgl::GeomType::kStaticMesh);

		// Both distinct, and both alive at once.
		CHECK(staticGeom.handle.index != mesh.geom.handle.index);
		CHECK(scene->IsGeomAlive(mesh.geom));
		CHECK(scene->IsGeomAlive(staticGeom));
	}

	SECTION("acquiring live geometry with a different clip set is refused")
	{
		game::test::WriteClips(root.path, "Derived/Animations/other.banim", "other", 2.0f, 3);

		CHECK_THROWS_AS(
			assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/other.banim"),
			std::runtime_error);
	}

	SECTION("an instance places and destroys through the manager")
	{
		const auto instance =
			assets.CreateSkinnedInstance(view, mesh.geom, glm::mat4(1.0f), { 0, 0.0f, 1.0f });
		REQUIRE(instance.IsValid());
		CHECK(view->GetInstanceCount() == 1);

		assets.DestroyInstance(view, instance);
		CHECK(view->GetInstanceCount() == 0);
	}

	SECTION("releasing to zero destroys the geometry")
	{
		assets.ReleaseGeom(mesh.geom);
		CHECK_FALSE(scene->IsGeomAlive(mesh.geom));
	}
}

TEST_CASE("a clip set cooked against a since-changed rig is refused", "[skinned][acquire]")
{
	DataRoot root("bernini_skinned_stale");
	WriteRig(root.path);
	StaleTheClips(root.path);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene  = gfx->CreateScene(bgl::SceneDesc());
	auto assets = game::AssetManager(scene, root.path);

	// bgl cannot make this check -- computing a skeleton's signature needs assetlib, which it does
	// not link -- and its own bone-count check passes here, because a reordered rig has the same
	// number of bones. Caught, the clips animate the wrong joints; uncaught, they animate silently.
	CHECK_THROWS_AS(
		assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim"),
		std::runtime_error);
}

TEST_CASE("a mesh cooked against a since-changed rig is refused", "[skinned][acquire]")
{
	DataRoot root("bernini_skinned_stale_mesh");
	WriteRig(root.path);
	StaleTheMesh(root.path);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene  = gfx->CreateScene(bgl::SceneDesc());
	auto assets = game::AssetManager(scene, root.path);

	// The other half of the same hazard, and the one nothing checked: a cache key holds only its
	// own bake token, so re-cooking the rig leaves this mesh current. Uncaught, its joint indices
	// address the wrong bones and the rig draws as a heap with no error anywhere.
	CHECK_THROWS_AS(
		assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim"),
		std::runtime_error);
}

TEST_CASE("a mesh and a clip set that were never a pair are refused", "[skinned][acquire]")
{
	DataRoot root("bernini_skinned_unpaired");
	WriteRig(root.path);
	CookTheMeshAgainstAnotherRig(root.path);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene  = gfx->CreateScene(bgl::SceneDesc());
	auto assets = game::AssetManager(scene, root.path);

	// The acquire poses the mesh with the palette the *clips* name, so a mesh cooked against some
	// other rig is skinned by bones it never addressed -- with the same bone count, and so with
	// nothing else to notice it.
	CHECK_THROWS_AS(
		assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim"),
		std::runtime_error);
}

TEST_CASE("a skinned acquire that cannot stand leaves nothing behind", "[skinned][acquire]")
{
	DataRoot root("bernini_skinned_acquire_refuse");

	// A loose material: the skinned pipeline has no loose variant, so AddSkinnedMeshGeom refuses --
	// after the acquire has already taken its material, which is the unwind under test.
	WriteRig(root.path, true);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene  = gfx->CreateScene(bgl::SceneDesc());
	auto assets = game::AssetManager(scene, root.path);

	CHECK_THROWS_AS(
		assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim"),
		bgl::SceneError);

	// The unwind gave the material reference back: acquiring it now must count 1, not 2 -- a leaked
	// reference from the failed acquire is exactly what this catches.
	const auto material = assets.AcquireMaterial("Authored/Materials/skin.bmaterial");
	CHECK(assets.MaterialRefCount(material) == 1);
	assets.ReleaseMaterial(material);
}

TEST_CASE("a skinned acquire passes its posed box down to the geom", "[skinned][acquire]")
{
	DataRoot root("bernini_skinned_acquire_bounds");
	WriteRig(root.path);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene  = gfx->CreateScene(bgl::SceneDesc());
	auto assets = game::AssetManager(scene, root.path);

	// The one box AddSkinnedMeshGeom refuses, which is what makes the forwarding observable from
	// out here: the acquire has no accessor for the sphere it built, but a box that cannot build
	// one throws, and it can only throw if the box arrived.
	const auto inverted =
		assetlib::Bounds{ glm::vec3(1.0f, -1.0f, -1.0f), glm::vec3(-1.0f, 1.0f, 1.0f) };

	CHECK_THROWS_AS(
		assets.AcquireSkinnedMesh(
			"Derived/Meshes/rig.bmesh",
			"Derived/Animations/rig.banim",
			0,
			inverted),
		bgl::SceneError);

	// Measured here instead, and the walk produces a box that stands: the fixture's quad spans
	// x in [-1, 1] and its one clip slides the root to x = 1, so the pose reaches x = 2.
	const auto mesh =
		assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim");
	REQUIRE(mesh.geom.IsValid());

	// A shared acquire never looks at the argument -- not even to validate it. The sphere belongs
	// to the geom, which already exists, so the box that would have been refused above is ignored.
	const auto shared = assets.AcquireSkinnedMesh(
		"Derived/Meshes/rig.bmesh",
		"Derived/Animations/rig.banim",
		0,
		inverted);
	CHECK(shared.geom.handle.index == mesh.geom.handle.index);

	assets.ReleaseGeom(shared.geom);
	assets.ReleaseGeom(mesh.geom);
}

TEST_CASE(
	"a skinned acquire culls by the .banim's baked box instead of measuring",
	"[skinned][acquire]")
{
	DataRoot root("bernini_skinned_acquire_baked");
	WriteRig(root.path);

	// The bake an import writes, replayed by hand -- except the box is the one box the scene
	// refuses. An acquire that throws on it can only have read the bake; one that measures gets
	// the valid box the walk always produces, and stands.
	const auto store    = assetlib::AssetStore(root.path);
	const auto mesh     = store.Load<assetlib::BMesh>("Derived/Meshes/rig.bmesh");
	const auto skeleton = store.Load<assetlib::Skeleton>("Derived/Skeletons/rig.bskel");

	auto animations = store.Load<assetlib::AnimationSet>("Derived/Animations/rig.banim");
	animations.posedBoxes.push_back(
		assetlib::PosedBox{ assetlib::posedBoundsSignature(mesh, skeleton),
	                        glm::vec3(1.0f, -1.0f, -1.0f),
	                        glm::vec3(-1.0f, 1.0f, 1.0f),
	                        0 });
	assetlib::AssetStore(root.path).Save(animations, "Derived/Animations/rig.banim");

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene  = gfx->CreateScene(bgl::SceneDesc());
	auto assets = game::AssetManager(scene, root.path);

	CHECK_THROWS_AS(
		assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim"),
		bgl::SceneError);

	// A caller's own box still outranks the bake.
	const auto valid = assetlib::Bounds{ glm::vec3(-2.0f), glm::vec3(2.0f) };
	const auto own   = assets.AcquireSkinnedMesh(
		"Derived/Meshes/rig.bmesh",
		"Derived/Animations/rig.banim",
		0,
		valid);
	REQUIRE(own.geom.IsValid());
	assets.ReleaseGeom(own.geom);
}

TEST_CASE("two meshes on one clip set share a single uploaded rig", "[skinned][acquire]")
{
	DataRoot root("bernini_skinned_acquire_shared_rig");
	WriteRig(root.path);

	// A second slot mesh against the same rig -- what a modular unit is: one skeleton, one clip
	// set, several meshes.
	{
		const auto source =
			assetlib::AssetStore(root.path).Load<assetlib::BMesh>("Derived/Meshes/rig.bmesh");
		assetlib::AssetStore(root.path).Save(source, "Derived/Meshes/slot.bmesh");
	}

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene  = gfx->CreateScene(bgl::SceneDesc());
	auto assets = game::AssetManager(scene, root.path);

	const auto body =
		assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim");
	const auto piece =
		assets.AcquireSkinnedMesh("Derived/Meshes/slot.bmesh", "Derived/Animations/rig.banim");

	REQUIRE(body.geom.IsValid());
	REQUIRE(piece.geom.IsValid());

	// Two geoms, because they are two meshes -- the sharing is of the rig, not of the geometry.
	CHECK(body.geom.handle.index != piece.geom.handle.index);

	// One rig beneath them. Had each geom uploaded its own, releasing the first would delete a rig
	// the second is still skinned to, which bgl refuses -- so this release would throw.
	CHECK_NOTHROW(assets.ReleaseGeom(body.geom));
	CHECK_NOTHROW(assets.ReleaseGeom(piece.geom));

	// And the rig went with the last geom holding it: acquiring again stands a fresh one up.
	const auto again =
		assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim");
	CHECK(again.geom.IsValid());
	assets.ReleaseGeom(again.geom);
}

TEST_CASE("a manager torn down over a surviving scene leaves it usable", "[skinned][acquire]")
{
	DataRoot root("bernini_skinned_acquire_teardown");
	WriteRig(root.path);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(bgl::SceneDesc());

	// The editor's shape: one scene outliving the manager over it, which is rebuilt on every project
	// switch. The geom is left held on purpose, so the destructor is what hands it and its rig back
	// -- and it must do so in that order, since bgl refuses a rig a geom is still skinned to. Were
	// the order wrong, DeleteRig would throw, the destructor would swallow it, and both would be
	// stranded.
	//
	// It does not pin the freeing itself: a rig the destructor forgot is invisible from here, since
	// nothing on IScene reports one and gamelib_tests cannot reach bgl::Scene. That half is held by
	// reading, not by this case.
	{
		auto       assets = game::AssetManager(scene, root.path);
		const auto mesh =
			assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim");
		REQUIRE(mesh.geom.IsValid());
	}

	auto       second = game::AssetManager(scene, root.path);
	const auto again =
		second.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim");
	CHECK(again.geom.IsValid());
	second.ReleaseGeom(again.geom);
}

TEST_CASE("a two-slot unit draws off one rig's bone anim table", "[skinned][acquire][render]")
{
	DataRoot root("bernini_skinned_acquire_crowd");
	WriteRig(root.path);

	// The second slot of a modular unit: same skeleton, same clips, its own mesh.
	{
		const auto source =
			assetlib::AssetStore(root.path).Load<assetlib::BMesh>("Derived/Meshes/rig.bmesh");
		assetlib::AssetStore(root.path).Save(source, "Derived/Meshes/slot.bmesh");
	}

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 256;
	targetDesc.height   = 256;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 8;
	sceneDesc.initialSubmeshes            = 8;
	sceneDesc.initialMeshlets             = 64;
	sceneDesc.initialVertexBufferByteSize = 65536;
	sceneDesc.initialIndices              = 1024;
	sceneDesc.initialPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto assets = game::AssetManager(scene, root.path);

	const auto body =
		assets.AcquireSkinnedMesh("Derived/Meshes/rig.bmesh", "Derived/Animations/rig.banim");
	const auto piece =
		assets.AcquireSkinnedMesh("Derived/Meshes/slot.bmesh", "Derived/Animations/rig.banim");
	REQUIRE(body.geom.IsValid());
	REQUIRE(piece.geom.IsValid());

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 10.0f),
			glm::vec3(0.0f, 0.0f, 9.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 1.0f, 0.5f, 100.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(256.0f, 256.0f);

	// Both slots on one clock and one pose, which is what a unit assembled from several meshes is.
	// Frame 1 of the fixture's clip slides the rig to x = 1, and the two sources must put it in the
	// same place -- the bone anim table is a different route to the same pose, not a different pose.
	const auto drawUnit = [&](bgl::PoseSource source, const char* png) {
		auto desc   = bgl::SkinnedInstanceDesc();
		desc.clip   = 0;
		desc.phase  = 1.0f;
		desc.rate   = 0.0f;
		desc.source = source;

		// Offset, so the second slot occupies pixels the first does not. Placed coincident they
		// would be one silhouette, and a slot that drew nothing at all would pass every check below.
		const auto a = assets.CreateSkinnedInstance(view, body.geom, glm::mat4(1.0f), desc);
		const auto b = assets.CreateSkinnedInstance(
			view,
			piece.geom,
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.0f, 0.0f)),
			desc);

		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, png);

		assets.DestroyInstance(view, b);
		assets.DestroyInstance(view, a);
	};

	const auto* palettePng = "assets/golden/crowd_unit_palette.got.png";
	const auto* tablePng   = "assets/golden/crowd_unit_table.got.png";

	drawUnit(bgl::PoseSource::kPerInstance, palettePng);
	drawUnit(bgl::PoseSource::kBoneAnimTable, tablePng);

	// The same pose by a different route, over the whole frame: a slot in the wrong place, a lost
	// normal or a mis-addressed frame all have to show up.
	CHECK(bgl::test::FrameDelta(palettePng, tablePng, 0, 0, 256, 256) < 1e-6f);

	// And both slots are on screen, so the comparison above is not of two empty frames -- nor of two
	// frames missing the same slot. The body alone must differ from the pair.
	const auto* emptyPng = "assets/golden/crowd_unit_empty.got.png";
	gfx->DrawFrame(target, job);
	gfx->ScreenshotPng(target, emptyPng);
	CHECK(bgl::test::FrameDelta(emptyPng, tablePng, 0, 0, 256, 256) > 1e-3f);

	const auto* bodyOnlyPng = "assets/golden/crowd_unit_body_only.got.png";
	{
		auto desc   = bgl::SkinnedInstanceDesc();
		desc.phase  = 1.0f;
		desc.rate   = 0.0f;
		desc.source = bgl::PoseSource::kBoneAnimTable;

		const auto only = assets.CreateSkinnedInstance(view, body.geom, glm::mat4(1.0f), desc);
		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, bodyOnlyPng);
		assets.DestroyInstance(view, only);
	}

	CHECK(bgl::test::FrameDelta(bodyOnlyPng, tablePng, 0, 0, 256, 256) > 1e-3f);
}

// The plant half of the acquire: the avatar found by convention beside the rig, its legs handed to
// bgl with a sole fitted off the mesh, and the weights read off the .banim or measured. Observed
// through the public API alone -- gamelib cannot open bgl's scene -- so what bgl *does* with the
// desc is the evidence: a chain it cannot walk is refused naming the bone, and weights of the
// wrong length are refused, and neither happens when the desc is right.

namespace
{
	using game::test::WriteLegAvatar;
	using game::test::WriteLegRig;

	game::AssetManager::SkinnedMesh
	AcquireLeg(game::AssetManager& assets)
	{
		return assets.AcquireSkinnedMesh(
			"Derived/Meshes/leg.bmesh",
			"Derived/Animations/leg.banim");
	}
}

TEST_CASE("a rig with no avatar acquires as before", "[skinned][acquire][skinnedacquire]")
{
	DataRoot root("bernini_skinned_acquire_no_avatar");
	WriteLegRig(root.path);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene  = gfx->CreateScene(bgl::SceneDesc());
	auto assets = game::AssetManager(scene, root.path);

	const auto leg = AcquireLeg(assets);
	REQUIRE(leg.geom.IsValid());
	assets.ReleaseGeom(leg.geom);
}

TEST_CASE("a rig with an avatar hands bgl its legs", "[skinned][acquire][skinnedacquire]")
{
	DataRoot root("bernini_skinned_acquire_avatar");
	WriteLegRig(root.path);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene  = gfx->CreateScene(bgl::SceneDesc());
	auto assets = game::AssetManager(scene, root.path);

	SECTION("a direct chain is accepted")
	{
		WriteLegAvatar(root.path, { { "hip", "knee", "ankle", "toe" } });

		const auto leg = AcquireLeg(assets);
		REQUIRE(leg.geom.IsValid());
		assets.ReleaseGeom(leg.geom);
	}

	SECTION("a chain the pose pass cannot walk is refused by bgl, naming the bone")
	{
		// hip -> ankle skips the knee. Only bgl refuses this -- resolveAvatar deliberately does
		// not -- so a refusal here is the proof the legs reached it.
		WriteLegAvatar(root.path, { { "hip", "ankle", "toe", "toe" } });

		CHECK_THROWS_WITH(AcquireLeg(assets), Catch::Matchers::ContainsSubstring("'ankle'"));
	}

	SECTION("an avatar naming a bone the rig lacks plants nothing rather than failing the rig")
	{
		WriteLegAvatar(root.path, { { "hip", "knee", "ankle", "hoof" } });

		const auto leg = AcquireLeg(assets);
		REQUIRE(leg.geom.IsValid());
		assets.ReleaseGeom(leg.geom);
	}
}

TEST_CASE(
	"a stale plant signature is measured rather than trusted",
	"[skinned][acquire][skinnedacquire]")
{
	DataRoot root("bernini_skinned_acquire_stale_plant");
	WriteLegRig(root.path);
	WriteLegAvatar(root.path, { { "hip", "knee", "ankle", "toe" } });

	// Weights that were measured against something else -- one leg, but a signature nothing here
	// produces, and a byte count that is not two frames of one leg. Trusted, bgl would refuse
	// the count; measured, the count is right and the rig stands.
	{
		const auto store      = assetlib::AssetStore(root.path);
		auto       animations = store.Load<assetlib::AnimationSet>("Derived/Animations/leg.banim");
		animations.plantWeights.signature = 0xDEADBEEFull;
		animations.plantWeights.legCount  = 1;
		animations.plantWeights.weights   = { 255, 255, 255 };
		store.Save(animations, "Derived/Animations/leg.banim");
	}

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene  = gfx->CreateScene(bgl::SceneDesc());
	auto assets = game::AssetManager(scene, root.path);

	const auto leg = AcquireLeg(assets);
	REQUIRE(leg.geom.IsValid());
	assets.ReleaseGeom(leg.geom);
}
