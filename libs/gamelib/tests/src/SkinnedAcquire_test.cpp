#include <gamelib/AssetManager.h>

#include "util/RigFixture.h"
#include "util/TestOptions.h"

#include <assetlib/AssetStore.h>
#include <assetlib/banim_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/skeleton.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Skeleton.h>
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>

// Acquiring a rig as skinned geometry. Unlike the VAT acquire there is no bake and no freshness
// rule -- the containers are the source -- so what this pins instead is the sharing, the release,
// and the one check bgl cannot make for itself: that a clip set still matches the rig it names.

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
		auto animations = assetlib::loadAnimations(dataRoot / "Animations/rig.banim");

		// What a reordered rig looks like from the clips' side: same bone count, different identity.
		animations.skeletonSignature ^= 0x9E3779B97F4A7C15ull;

		assetlib::saveAnimations(animations, dataRoot / "Animations/rig.banim");
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

	const auto mesh = assets.AcquireSkinnedMesh("Meshes/rig.bmesh", "Animations/rig.banim");
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
		const auto again = assets.AcquireSkinnedMesh("Meshes/rig.bmesh", "Animations/rig.banim");
		CHECK(again.geom.handle.index == mesh.geom.handle.index);
		CHECK(again.clips.size() == mesh.clips.size());

		// One release leaves the first acquire's reference standing.
		assets.ReleaseGeom(again.geom);
		CHECK(scene->IsGeomAlive(mesh.geom));
	}

	SECTION("the same mesh can be live as static, VAT and skinned at once")
	{
		// Three keyspaces, three uploads. The editor's Animation panel is the caller that needs it:
		// it holds a rig as skinned and as VAT together so the two can be compared.
		const auto staticGeom = assets.AcquireMesh("Meshes/rig.bmesh");
		CHECK(staticGeom.IsValid());
		CHECK(staticGeom.geomType == bgl::GeomType::kStaticMesh);

		const auto vat = assets.AcquireVatMesh("Meshes/rig.bmesh", "Animations/rig.banim");
		CHECK(vat.geom.IsValid());
		CHECK(vat.geom.geomType == bgl::GeomType::kVatMesh);

		// All three distinct, and all three alive at once.
		CHECK(staticGeom.handle.index != mesh.geom.handle.index);
		CHECK(vat.geom.handle.index != mesh.geom.handle.index);
		CHECK(vat.geom.handle.index != staticGeom.handle.index);
		CHECK(scene->IsGeomAlive(mesh.geom));
		CHECK(scene->IsGeomAlive(staticGeom));
		CHECK(scene->IsGeomAlive(vat.geom));
	}

	SECTION("acquiring live geometry with a different clip set is refused")
	{
		game::test::WriteClips(root.path, "Animations/other.banim", "other", 2.0f, 3);

		CHECK_THROWS_AS(
			assets.AcquireSkinnedMesh("Meshes/rig.bmesh", "Animations/other.banim"),
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
		assets.AcquireSkinnedMesh("Meshes/rig.bmesh", "Animations/rig.banim"),
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
		assets.AcquireSkinnedMesh("Meshes/rig.bmesh", "Animations/rig.banim"),
		bgl::SceneError);

	// The unwind gave the material reference back: acquiring it now must count 1, not 2 -- a leaked
	// reference from the failed acquire is exactly what this catches.
	const auto material = assets.AcquireMaterial("Materials/skin.bmaterial");
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
		assets.AcquireSkinnedMesh("Meshes/rig.bmesh", "Animations/rig.banim", 0, inverted),
		bgl::SceneError);

	// Measured here instead, and the walk produces a box that stands: the fixture's quad spans
	// x in [-1, 1] and its one clip slides the root to x = 1, so the pose reaches x = 2.
	const auto mesh = assets.AcquireSkinnedMesh("Meshes/rig.bmesh", "Animations/rig.banim");
	REQUIRE(mesh.geom.IsValid());

	// A shared acquire never looks at the argument -- not even to validate it. The sphere belongs
	// to the geom, which already exists, so the box that would have been refused above is ignored.
	const auto shared =
		assets.AcquireSkinnedMesh("Meshes/rig.bmesh", "Animations/rig.banim", 0, inverted);
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
	const auto mesh     = store.LoadMesh("Meshes/rig.bmesh");
	const auto skeleton = store.LoadSkeleton("Skeletons/rig.bskel");

	auto animations = store.LoadAnimations("Animations/rig.banim");
	animations.posedBoxes.push_back(
		assetlib::PosedBox{ assetlib::posedBoundsSignature(mesh, skeleton),
	                        glm::vec3(1.0f, -1.0f, -1.0f),
	                        glm::vec3(-1.0f, 1.0f, 1.0f),
	                        0 });
	assetlib::saveAnimations(animations, root.path / "Animations/rig.banim");

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene  = gfx->CreateScene(bgl::SceneDesc());
	auto assets = game::AssetManager(scene, root.path);

	CHECK_THROWS_AS(
		assets.AcquireSkinnedMesh("Meshes/rig.bmesh", "Animations/rig.banim"),
		bgl::SceneError);

	// A caller's own box still outranks the bake.
	const auto valid = assetlib::Bounds{ glm::vec3(-2.0f), glm::vec3(2.0f) };
	const auto own =
		assets.AcquireSkinnedMesh("Meshes/rig.bmesh", "Animations/rig.banim", 0, valid);
	REQUIRE(own.geom.IsValid());
	assets.ReleaseGeom(own.geom);
}
