#include <assetlib/avatar.h>
#include <assetlib/project_layout.h>
#include <assetlib/rebake_bounds.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Skeleton.h>

#include "RefsSandbox.h"

#include "MountAt.h"
#include <catch2/catch_approx.hpp>

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;

	/** One bone at identity, so the posed box is readable straight off the samples. */
	Skeleton
	MakeRig()
	{
		Skeleton skeleton;

		auto bone        = Bone();
		bone.bindPose    = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		bone.inverseBind = glm::mat4(1.0f);
		bone.parent      = c_InvalidIndex;
		bone.nameOffset  = skeleton.stringPool.add("root");
		skeleton.bones.push_back(bone);
		return skeleton;
	}

	/** A 2-frame clip: still, then the root at x = 50. */
	AnimationSet
	MakeClips(const Skeleton& skeleton)
	{
		auto animations              = AnimationSet();
		animations.skeleton          = "Derived/Skeletons/rig.bskel";
		animations.skeletonSignature = skeletonSignature(skeleton);
		animations.boneCount         = 1;

		auto clip        = AnimationClip();
		clip.firstSample = 0;
		clip.frameCount  = 2;
		clip.sampleRate  = 30.0f;
		animations.clips.push_back(clip);

		animations.samples.push_back(
			{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
		animations.samples.push_back(
			{ glm::vec3(50.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
		return animations;
	}

	/** Two vertices at opposite corners of a unit box, both welded to bone 0. */
	BMesh
	MakeSkinnedMesh(const glm::vec3& corner)
	{
		BMesh mesh;

		auto submesh                  = Submesh();
		submesh.layout.attributeCount = 3;
		submesh.layout.attributes[0]  = { VertexSemantic::kPosition, VertexFormat::kFloat32x3, 0 };
		submesh.layout.attributes[1]  = { VertexSemantic::kJoints0, VertexFormat::kUint16x4, 12 };
		submesh.layout.attributes[2]  = { VertexSemantic::kWeights0, VertexFormat::kUnorm16x4, 20 };
		submesh.layout.stride         = 28;

		for (const glm::vec3& position : { -corner, corner })
		{
			const size_t base = mesh.vertexData.size();
			mesh.vertexData.resize(base + submesh.layout.stride);

			const std::array<uint16_t, 4> joints{};
			const std::array<uint16_t, 4> weights{ { 65535, 0, 0, 0 } };

			std::byte* at = mesh.vertexData.data() + base;
			std::memcpy(at, &position, sizeof(position));
			std::memcpy(at + 12, joints.data(), sizeof(joints));
			std::memcpy(at + 20, weights.data(), sizeof(weights));
			++submesh.vertexCount;
		}

		submesh.aabbMin = -corner;
		submesh.aabbMax = corner;
		mesh.submeshes.push_back(submesh);
		mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });
		mesh.skeleton = "Derived/Skeletons/rig.bskel";
		return mesh;
	}

	void
	WriteProject(const DataRoot& root)
	{
		const Skeleton skeleton = MakeRig();

		fs::create_directories(root.path / "Derived/Meshes");
		fs::create_directories(root.path / "Derived/Skeletons");
		fs::create_directories(root.path / "Derived/Animations");
		StoreAt(root.path).Save(MakeSkinnedMesh(glm::vec3(1.0f)), "Derived/Meshes/rig.bmesh");
		StoreAt(root.path).Save(skeleton, "Derived/Skeletons/rig.bskel");
		SaveAt(MakeClips(skeleton), root.path / "Derived/Animations/rig.banim");
	}
}

TEST_CASE("The rebake writes the box a load then finds", "[rebake]")
{
	const DataRoot root("bernini_rebake_bounds");
	WriteProject(root);

	SECTION("a dry run reports the work and touches nothing")
	{
		const RebakeBoundsReport preview = AssetStore(root.path).RebakePosedBounds(true);
		CHECK(preview.Count(RebakedFile::Outcome::kRebaked) == 1);
		CHECK(StoreAt(root.path)
		          .Load<AnimationSet>("Derived/Animations/rig.banim")
		          .posedBoxes.empty());
	}

	SECTION("the real run bakes, and a second run rewrites nothing")
	{
		const RebakeBoundsReport report = AssetStore(root.path).RebakePosedBounds(false);
		CHECK(report.Count(RebakedFile::Outcome::kRebaked) == 1);
		CHECK(report.Count(RebakedFile::Outcome::kFailed) == 0);

		const std::optional<Bounds> baked = findPosedBounds(
			StoreAt(root.path).Load<AnimationSet>("Derived/Animations/rig.banim"),
			StoreAt(root.path).Load<BMesh>("Derived/Meshes/rig.bmesh"),
			StoreAt(root.path).Load<Skeleton>("Derived/Skeletons/rig.bskel"))[0];

		REQUIRE(baked.has_value());
		CHECK(baked->min.x == Catch::Approx(-1.0f));
		CHECK(baked->max.x == Catch::Approx(51.0f));

		const RebakeBoundsReport again = AssetStore(root.path).RebakePosedBounds(false);
		CHECK(again.Count(RebakedFile::Outcome::kCurrent) == 1);
		CHECK(again.Count(RebakedFile::Outcome::kRebaked) == 0);
	}

	SECTION("an avatar authored after the bake backfills the plant weights")
	{
		// Task 8's own workflow: the rig is cooked, and the avatar is authored afterwards. Without
		// this the boxes read as current, the weights are never measured, and the only ways to get
		// them are a re-import or paying the whole frame walk on every load.
		(void)AssetStore(root.path).RebakePosedBounds(false);

		fs::create_directories(root.path / c_AvatarsDirectoryName);
		auto avatar = Avatar();
		avatar.legs.push_back({ "root", "root", "root", "root" });
		StoreAt(root.path).Save(avatar, "Authored/Skeletons/rig.bavatar");

		const RebakeBoundsReport after = AssetStore(root.path).RebakePosedBounds(false);
		CHECK(after.Count(RebakedFile::Outcome::kRebaked) == 1);

		const AnimationSet read =
			StoreAt(root.path).Load<AnimationSet>("Derived/Animations/rig.banim");
		CHECK_FALSE(read.plantWeights.Empty());
		CHECK(read.plantWeights.legCount == 1);

		// And it settles: a second run has nothing left to measure.
		const RebakeBoundsReport again = AssetStore(root.path).RebakePosedBounds(false);
		CHECK(again.Count(RebakedFile::Outcome::kCurrent) == 1);
	}

	SECTION("a mesh re-authored since the bake makes its clip set stale again")
	{
		(void)AssetStore(root.path).RebakePosedBounds(false);
		StoreAt(root.path).Save(MakeSkinnedMesh(glm::vec3(3.0f)), "Derived/Meshes/rig.bmesh");

		const RebakeBoundsReport preview = AssetStore(root.path).RebakePosedBounds(true);
		CHECK(preview.Count(RebakedFile::Outcome::kRebaked) == 1);
	}

	SECTION("a clip set no mesh skins to is reported, not guessed at")
	{
		fs::remove(root.path / "Derived/Meshes/rig.bmesh");

		const RebakeBoundsReport report = AssetStore(root.path).RebakePosedBounds(false);
		CHECK(report.Count(RebakedFile::Outcome::kOrphaned) == 1);
		CHECK(StoreAt(root.path)
		          .Load<AnimationSet>("Derived/Animations/rig.banim")
		          .posedBoxes.empty());
	}
}
