#include <array>
#include <assetlib/bmesh.h>
#include <assetlib/codecs.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>

#include "MountAt.h"
#include <assetlib_structs/Node.h>
#include <assetlib_structs/VertexLayout.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/hash.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace assetlib;

namespace
{
	Transform
	IdentityTransform() noexcept
	{
		return Transform{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
	}

	/** A three-bone chain: hips -> spine -> head, each offset one unit up from its parent. */
	Skeleton
	MakeChain()
	{
		Skeleton skeleton;

		const std::array<const char*, 3> names = { { "hips", "spine", "head" } };
		for (uint32_t i = 0; i < names.size(); ++i)
		{
			Bone bone{};
			bone.bindPose               = IdentityTransform();
			bone.bindPose.translation.y = 1.0f;
			bone.inverseBind =
				glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -static_cast<float>(i + 1), 0.0f));
			bone.parent     = i == 0 ? c_InvalidIndex : i - 1;
			bone.nameOffset = skeleton.stringPool.add(names[i]);
			skeleton.bones.push_back(bone);
		}

		return skeleton;
	}

	/** A mesh whose one submesh carries joint indices, cooked against `skeleton`. */
	BMesh
	MakeSkinnedMesh(const Skeleton& skeleton)
	{
		BMesh mesh;

		Submesh submesh{};
		submesh.layout.attributeCount = 1;
		submesh.layout.attributes[0]  = { VertexSemantic::kJoints0, VertexFormat::kUint16x4, 0 };
		mesh.submeshes                = { submesh };

		mesh.skeleton          = "Derived/Skeletons/chain.bskel";
		mesh.skeletonSignature = skeletonSignature(skeleton);
		mesh.skeletonBoneNames = skeletonBoneNames(skeleton);
		return mesh;
	}

	/** One clip of `frames` poses over the chain, translating bone 0 along +Z by `distance`. */
	AnimationSet
	MakeClipSet(const Skeleton& skeleton, uint32_t frames, float distance)
	{
		AnimationSet animations;
		animations.boneCount         = static_cast<uint32_t>(skeleton.bones.size());
		animations.skeletonSignature = skeletonSignature(skeleton);
		animations.skeletonBoneNames = skeletonBoneNames(skeleton);
		animations.skeleton          = "Derived/Animations/walk.bskel";

		AnimationClip clip{};
		clip.nameOffset  = animations.stringPool.add("walk");
		clip.firstSample = 0;
		clip.frameCount  = frames;
		clip.sampleRate  = 30.0f;
		clip.duration    = static_cast<float>(frames - 1) / 30.0f;

		for (uint32_t f = 0; f < frames; ++f)
		{
			for (uint32_t b = 0; b < animations.boneCount; ++b)
			{
				Transform pose = skeleton.bones[b].bindPose;
				if (b == 0)
					pose.translation.z =
						distance * static_cast<float>(f) / static_cast<float>(frames - 1);
				animations.samples.push_back(pose);
			}
		}

		clip.rootMotion      = glm::vec3(0.0f, 0.0f, distance);
		clip.locomotionSpeed = distance / clip.duration;
		animations.clips.push_back(clip);
		return animations;
	}
}

TEST_CASE("A skeleton survives a container round-trip", "[skeleton][io]")
{
	const auto skeleton = MakeChain();
	const auto restored =
		AssetCodec<Skeleton>::Deserialize(AssetCodec<Skeleton>::Serialize(skeleton));

	REQUIRE(restored.bones.size() == skeleton.bones.size());
	CHECK(restored.stringPool == skeleton.stringPool);

	for (size_t i = 0; i < skeleton.bones.size(); ++i)
	{
		CHECK(restored.bones[i].parent == skeleton.bones[i].parent);
		CHECK(restored.bones[i].nameOffset == skeleton.bones[i].nameOffset);
		CHECK(restored.bones[i].inverseBind == skeleton.bones[i].inverseBind);
		CHECK(restored.bones[i].bindPose.translation == skeleton.bones[i].bindPose.translation);
	}

	CHECK(skeletonSignature(restored) == skeletonSignature(skeleton));
}

TEST_CASE("A skeleton whose bones are not topologically sorted will not load", "[skeleton][io]")
{
	// The whole point of the ordering is that a runtime walks the hierarchy in one forward pass with
	// no check of its own -- so a file that breaks it has to be rejected here, not tolerated. A parent
	// after its child would otherwise be read one frame stale, silently.
	auto skeleton = MakeChain();
	std::swap(skeleton.bones[0], skeleton.bones[1]);

	const auto bytes = AssetCodec<Skeleton>::Serialize(skeleton);
	CHECK_THROWS_AS(AssetCodec<Skeleton>::Deserialize(bytes), std::runtime_error);
}

TEST_CASE("A skeleton's signature covers its bones' names and parents", "[skeleton]")
{
	const auto skeleton = MakeChain();
	const auto original = skeletonSignature(skeleton);

	SECTION("re-authoring the rest pose does not invalidate a clip")
	{
		// A clip's indices do not depend on where a bone sits, so treating a moved bone as a new rig
		// would make every rest-pose tweak a re-cook of every clip set.
		auto moved                          = skeleton;
		moved.bones[1].bindPose.translation = glm::vec3(5.0f, 6.0f, 7.0f);
		CHECK(skeletonSignature(moved) == original);
	}

	SECTION("renaming a bone does")
	{
		auto renamed                = skeleton;
		renamed.bones[1].nameOffset = renamed.stringPool.add("chest");
		CHECK(skeletonSignature(renamed) != original);
	}

	SECTION("re-parenting a bone does")
	{
		auto reparented            = skeleton;
		reparented.bones[2].parent = 0;
		CHECK(skeletonSignature(reparented) != original);
	}

	SECTION("and inserting one does, which is what makes a stale clip set detectable")
	{
		auto inserted = skeleton;
		Bone extra{};
		extra.bindPose   = IdentityTransform();
		extra.parent     = 0;
		extra.nameOffset = inserted.stringPool.add("tail");
		inserted.bones.push_back(extra);
		CHECK(skeletonSignature(inserted) != original);
	}
}

TEST_CASE("skeletonBoneNames answers in bone order", "[skeleton]")
{
	const auto skeleton = MakeChain();
	const auto names    = skeletonBoneNames(skeleton);

	REQUIRE(names.size() == skeleton.bones.size());
	for (size_t i = 0; i < names.size(); ++i)
		CHECK(names[i] == skeleton.stringPool.at(skeleton.bones[i].nameOffset));
}

namespace
{
	/** A rig built from `{name, parent}` pairs, in the order given. */
	Skeleton
	MakeRig(const std::vector<std::pair<const char*, uint32_t>>& bones)
	{
		Skeleton skeleton;
		for (const auto& [name, parent] : bones)
		{
			Bone bone{};
			bone.bindPose   = IdentityTransform();
			bone.parent     = parent;
			bone.nameOffset = skeleton.stringPool.add(name);
			skeleton.bones.push_back(bone);
		}
		return skeleton;
	}
}

TEST_CASE("the factored signature is the one already on disk", "[skeleton][canary]")
{
	// Every .bskel, .banim and .bmesh stores a signature computed by the loop below. Factoring it
	// into hashBones must not have moved the value by a bit: nothing compares a stored signature
	// against a recomputed one in this suite -- both sides of every other check are computed fresh,
	// so they would agree with each other while disagreeing with every file already written.
	const auto skeleton = MakeChain();

	uint64_t expected = core::hash_seed();
	for (const Bone& bone : skeleton.bones)
	{
		expected = core::hash_string(skeleton.stringPool.at(bone.nameOffset), expected);
		expected = core::hash_pod(bone.parent, expected);
	}

	CHECK(skeletonSignature(skeleton) == expected);
}

TEST_CASE("skeletonRemap accepts a rig that only grew", "[skeleton][remap]")
{
	const auto cooked    = MakeChain();  // hips -> spine -> head
	const auto names     = skeletonBoneNames(cooked);
	const auto signature = skeletonSignature(cooked);

	SECTION("the same rig maps every bone to itself")
	{
		const auto remap = skeletonRemap(names, signature, cooked);
		REQUIRE(remap.has_value());
		CHECK(*remap == std::vector<uint32_t>{ 0, 1, 2 });
	}

	SECTION("a bone appended at the end leaves the old indices alone")
	{
		const auto grown =
			MakeRig({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "head", 1 }, { "prop", 0 } });

		const auto remap = skeletonRemap(names, signature, grown);
		REQUIRE(remap.has_value());
		CHECK(*remap == std::vector<uint32_t>{ 0, 1, 2 });
	}

	SECTION("a bone inserted mid-hierarchy shifts them")
	{
		// What a socket added to the spine looks like once the depth-first walk has placed it.
		const auto grown =
			MakeRig({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "grip", 1 }, { "head", 1 } });

		const auto remap = skeletonRemap(names, signature, grown);
		REQUIRE(remap.has_value());
		CHECK(*remap == std::vector<uint32_t>{ 0, 1, 3 });
	}

	SECTION("a corrective inserted *between* two bones is an append, not a reparent")
	{
		// head's parent is now `twist`, which the cooked rig never had -- so the test that matters
		// is the nearest ancestor it did have, which is still spine.
		const auto grown =
			MakeRig({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "twist", 1 }, { "head", 2 } });

		const auto remap = skeletonRemap(names, signature, grown);
		REQUIRE(remap.has_value());
		CHECK(*remap == std::vector<uint32_t>{ 0, 1, 3 });
	}

	SECTION("a reorder is a bijection by name and maps through")
	{
		// Needs a branching rig: a chain has exactly one topological order, so there is nothing to
		// permute. Bone order is seeded from `skin.joints` order, so an exporter version change
		// swaps two siblings with no rig edit at all -- every parent is still the same bone by
		// name, so the reconstruction holds and the clips are not stranded.
		const auto forked = MakeRig({ { "hips", c_InvalidIndex }, { "armL", 0 }, { "armR", 0 } });
		const auto forkedNames     = skeletonBoneNames(forked);
		const auto forkedSignature = skeletonSignature(forked);

		const auto swapped = MakeRig({ { "hips", c_InvalidIndex }, { "armR", 0 }, { "armL", 0 } });
		REQUIRE(skeletonSignature(swapped) != forkedSignature);

		const auto remap = skeletonRemap(forkedNames, forkedSignature, swapped);
		REQUIRE(remap.has_value());
		CHECK(*remap == std::vector<uint32_t>{ 0, 2, 1 });
	}
}

TEST_CASE("skeletonRemap refuses what genuinely lost its target", "[skeleton][remap]")
{
	const auto cooked    = MakeChain();
	const auto names     = skeletonBoneNames(cooked);
	const auto signature = skeletonSignature(cooked);

	SECTION("a rename has no bone to resolve to")
	{
		const auto renamed = MakeRig({ { "hips", c_InvalidIndex }, { "chest", 0 }, { "head", 1 } });
		CHECK_FALSE(skeletonRemap(names, signature, renamed).has_value());
	}

	SECTION("a deletion likewise")
	{
		const auto deleted = MakeRig({ { "hips", c_InvalidIndex }, { "head", 0 } });
		CHECK_FALSE(skeletonRemap(names, signature, deleted).has_value());
	}

	SECTION("a reparent resolves every name and must still be refused")
	{
		// The case that pins ADR-4: head moved off spine and onto hips. Name resolution alone
		// accepts this and poses the rig wrongly with nothing to show for it.
		const auto reparented =
			MakeRig({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "head", 0 } });
		CHECK_FALSE(skeletonRemap(names, signature, reparented).has_value());
	}

	SECTION("two bones of one name leave 'which bone' unanswerable")
	{
		const auto ambiguous =
			MakeRig({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "head", 1 }, { "spine", 0 } });
		CHECK_FALSE(skeletonRemap(names, signature, ambiguous).has_value());
	}

	SECTION("a container written before the name list existed carries nothing to resolve")
	{
		CHECK_FALSE(skeletonRemap({}, signature, cooked).has_value());
	}
}

namespace
{
	/** MakeClipSet with a second clip behind it, so a nonzero `firstSample` is exercised. */
	AnimationSet
	MakeTwoClipSet(const Skeleton& skeleton, const uint32_t frames)
	{
		auto animations = MakeClipSet(skeleton, frames, 2.0f);

		AnimationClip second{};
		second.nameOffset  = animations.stringPool.add("idle");
		second.firstSample = static_cast<uint32_t>(animations.samples.size());
		second.frameCount  = frames;
		second.sampleRate  = 30.0f;
		second.duration    = static_cast<float>(frames - 1) / 30.0f;

		for (uint32_t f = 0; f < frames; ++f)
			for (uint32_t b = 0; b < animations.boneCount; ++b)
			{
				Transform pose = skeleton.bones[b].bindPose;
				if (b == 0)
					pose.translation.x = static_cast<float>(f);
				animations.samples.push_back(pose);
			}

		animations.clips.push_back(second);
		return animations;
	}
}

TEST_CASE("remapAnimations poses a grown rig exactly as the cooked one posed", "[skeleton][remap]")
{
	const auto cooked = MakeChain();  // hips -> spine -> head
	auto       clips  = MakeClipSet(cooked, 4, 2.0f);

	// The rig with a socket hung off the spine, which the depth-first walk places before head --
	// so head moves from index 2 to 3 and every clip's samples now name the wrong bones.
	auto grown =
		MakeRig({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "grip", 1 }, { "head", 1 } });
	grown.bones[2].bindPose.translation = glm::vec3(9.0f, 9.0f, 9.0f);

	REQUIRE_FALSE(animationsMatchSkeleton(clips, grown));

	// What the cooked pairing produced, before anything is touched.
	auto before = std::vector<std::vector<glm::mat4>>();
	for (uint32_t frame = 0; frame < clips.clips[0].frameCount; ++frame)
		before.push_back(poseModelTransforms(cooked, clips, 0, frame));

	REQUIRE(remapAnimations(clips, grown));
	CHECK(animationsMatchSkeleton(clips, grown));
	CHECK(clips.boneCount == 4);

	for (uint32_t frame = 0; frame < clips.clips[0].frameCount; ++frame)
	{
		const auto after = poseModelTransforms(grown, clips, 0, frame);
		REQUIRE(after.size() == 4);

		// Every surviving bone lands where it did, at its new index.
		CHECK(after[0] == before[frame][0]);  // hips
		CHECK(after[1] == before[frame][1]);  // spine
		CHECK(after[3] == before[frame][2]);  // head, moved 2 -> 3
	}

	SECTION("and the added bone holds its bind pose in every frame")
	{
		// Its *local* sample, not its model transform: a grip hung off the spine has to travel
		// with the spine, so what stays at bind is the bone's own offset from its parent.
		const Transform& bind = grown.bones[2].bindPose;
		for (uint32_t frame = 0; frame < clips.clips[0].frameCount; ++frame)
		{
			const Transform& sample = clips.samples[frame * clips.boneCount + 2];
			CHECK(sample.translation == bind.translation);
			CHECK(sample.rotation == bind.rotation);
			CHECK(sample.scale == bind.scale);
		}
	}
}

TEST_CASE("remapAnimations keeps the frame each clip starts on", "[skeleton][remap]")
{
	// findPlantWeights reads a clip's start as `firstSample / boneCount`, so a re-stride that
	// renumbered frames would leave every baked weight addressing the wrong pose. Two clips,
	// because a lone clip starts at 0 and 0 survives any arithmetic -- including wrong arithmetic.
	const auto cooked = MakeChain();
	auto       clips  = MakeTwoClipSet(cooked, 4);

	REQUIRE(clips.clips.size() == 2);
	REQUIRE(clips.clips[1].firstSample > 0);

	const uint32_t firstStart  = clips.clips[0].firstSample / clips.boneCount;
	const uint32_t secondStart = clips.clips[1].firstSample / clips.boneCount;
	REQUIRE(secondStart == 4);

	auto before = std::vector<std::vector<glm::mat4>>();
	for (uint32_t frame = 0; frame < clips.clips[1].frameCount; ++frame)
		before.push_back(poseModelTransforms(cooked, clips, 1, frame));

	const auto grown =
		MakeRig({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "grip", 1 }, { "head", 1 } });

	REQUIRE(remapAnimations(clips, grown));

	CHECK(clips.clips[0].firstSample % clips.boneCount == 0);
	CHECK(clips.clips[1].firstSample % clips.boneCount == 0);
	CHECK(clips.clips[0].firstSample / clips.boneCount == firstStart);
	CHECK(clips.clips[1].firstSample / clips.boneCount == secondStart);

	// The offset itself moved, since the stride did -- what held is the frame it names.
	CHECK(clips.clips[1].firstSample == secondStart * clips.boneCount);

	// And the second clip still poses what it posed, at the bones' new indices.
	for (uint32_t frame = 0; frame < clips.clips[1].frameCount; ++frame)
	{
		const auto after = poseModelTransforms(grown, clips, 1, frame);
		CHECK(after[0] == before[frame][0]);
		CHECK(after[1] == before[frame][1]);
		CHECK(after[3] == before[frame][2]);
	}
}

TEST_CASE("remapAnimations refuses a clip that does not start on a frame", "[skeleton][remap]")
{
	// The condition findPlantWeights already refuses a clip for. Rewriting one would divide the
	// remainder away and land it in the middle of another clip's frames.
	const auto cooked = MakeChain();
	auto       clips  = MakeTwoClipSet(cooked, 4);
	clips.clips[1].firstSample += 1;

	const auto grown =
		MakeRig({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "grip", 1 }, { "head", 1 } });

	CHECK_FALSE(remapAnimations(clips, grown));
	CHECK(clips.boneCount == cooked.bones.size());
}

TEST_CASE("remapAnimations leaves a clip set it cannot resolve alone", "[skeleton][remap]")
{
	const auto cooked   = MakeChain();
	const auto original = MakeClipSet(cooked, 4, 2.0f);

	const auto reparented = MakeRig({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "head", 0 } });

	auto clips = original;
	CHECK_FALSE(remapAnimations(clips, reparented));

	// Not partially rewritten: a refusal that had already re-strided the pool would leave the
	// caller a container that matches neither rig.
	CHECK(clips.boneCount == original.boneCount);
	REQUIRE(clips.samples.size() == original.samples.size());
	for (size_t i = 0; i < clips.samples.size(); ++i)
	{
		CHECK(clips.samples[i].translation == original.samples[i].translation);
		CHECK(clips.samples[i].rotation == original.samples[i].rotation);
		CHECK(clips.samples[i].scale == original.samples[i].scale);
	}
	CHECK(clips.skeletonSignature == original.skeletonSignature);
	CHECK(clips.clips[0].firstSample == original.clips[0].firstSample);
}

TEST_CASE("A clip set survives a container round-trip", "[animation][io]")
{
	const auto skeleton   = MakeChain();
	const auto animations = MakeClipSet(skeleton, 31, 2.0f);
	const auto restored =
		AssetCodec<AnimationSet>::Deserialize(AssetCodec<AnimationSet>::Serialize(animations));

	CHECK(restored.skeleton == animations.skeleton);
	CHECK(restored.skeletonSignature == animations.skeletonSignature);
	CHECK(restored.boneCount == animations.boneCount);
	CHECK(restored.skeletonBoneNames == animations.skeletonBoneNames);
	CHECK(restored.stringPool == animations.stringPool);
	REQUIRE(restored.clips.size() == 1);
	REQUIRE(restored.samples.size() == animations.samples.size());

	CHECK(restored.clips[0].frameCount == 31);
	CHECK(restored.clips[0].duration == Catch::Approx(1.0f));
	CHECK(restored.clips[0].locomotionSpeed == Catch::Approx(2.0f));

	// Frame-major, so bone 0 of the last frame is where the pool's last pose *starts*, not ends.
	const size_t lastPose =
		static_cast<size_t>(restored.clips[0].frameCount - 1) * restored.boneCount;
	CHECK(restored.samples[lastPose].translation.z == Catch::Approx(2.0f));
}

TEST_CASE("The skeleton path is readable without the samples", "[animation][io]")
{
	// A whole-project reference scan reads this and nothing else -- the samples are megabytes.
	const auto skeleton   = MakeChain();
	const auto animations = MakeClipSet(skeleton, 31, 2.0f);

	const auto path = std::filesystem::temp_directory_path() / "bernini_banim_refs.banim";
	SaveAt(animations, path);

	CHECK(loadAnimationSkeletonPath(path) == animations.skeleton);
	CHECK(LoadAt<AnimationSet>(path).skeleton == animations.skeleton);

	std::filesystem::remove(path);
}

TEST_CASE("A clip set cooked against another rig is detected", "[animation]")
{
	const auto skeleton   = MakeChain();
	const auto animations = MakeClipSet(skeleton, 31, 2.0f);

	CHECK(animationsMatchSkeleton(animations, skeleton));

	SECTION("a reordered rig no longer matches")
	{
		// The clips' bone indices now name different bones, and nothing about the pose they produce
		// says so -- this comparison is the only thing that can.
		auto reordered                = skeleton;
		reordered.bones[1].nameOffset = reordered.stringPool.add("chest");
		CHECK_FALSE(animationsMatchSkeleton(animations, reordered));
	}

	SECTION("nor does one with a different bone count")
	{
		auto shorter = skeleton;
		shorter.bones.pop_back();
		CHECK_FALSE(animationsMatchSkeleton(animations, shorter));
	}
}

TEST_CASE("A skinned mesh cooked against another rig is detected", "[skeleton][bmesh]")
{
	const auto skeleton = MakeChain();
	const auto mesh     = MakeSkinnedMesh(skeleton);

	CHECK(meshMatchesSkeleton(mesh, skeleton));

	SECTION("a reordered rig no longer matches")
	{
		// The mesh's joint indices now name different bones. Each container's cache key holds only
		// its own bake token, so re-cooking the rig leaves this mesh current -- this comparison is
		// the only thing standing between that and a silently mis-skinned draw.
		auto reordered                = skeleton;
		reordered.bones[1].nameOffset = reordered.stringPool.add("chest");
		CHECK_FALSE(meshMatchesSkeleton(mesh, reordered));
	}

	SECTION("nor does a rig it was never paired with")
	{
		auto other = skeleton;
		other.bones.pop_back();
		CHECK_FALSE(meshMatchesSkeleton(mesh, other));
	}

	SECTION("a mesh that carries no joints matches any rig")
	{
		// Nothing to misname: a static attachment may name a rig it hangs off without addressing
		// its bones, so refusing it would refuse a legal pairing.
		auto attachment                                       = mesh;
		attachment.submeshes[0].layout.attributes[0].semantic = VertexSemantic::kPosition;
		attachment.skeletonSignature                          = 0;

		auto reordered                = skeleton;
		reordered.bones[1].nameOffset = reordered.stringPool.add("chest");
		CHECK(meshMatchesSkeleton(attachment, reordered));
	}
}

TEST_CASE("A clip that samples past its pool will not load", "[animation][io]")
{
	const auto skeleton            = MakeChain();
	auto       animations          = MakeClipSet(skeleton, 31, 2.0f);
	animations.clips[0].frameCount = 40;

	const auto bytes = AssetCodec<AnimationSet>::Serialize(animations);
	CHECK_THROWS_AS(AssetCodec<AnimationSet>::Deserialize(bytes), std::runtime_error);
}

TEST_CASE("A bind pose resolves to model space in one forward pass", "[skeleton]")
{
	// Each bone sits one unit above its parent, so the chain's third bone is three units up. This is
	// only true because parent < index; a walk over an unsorted skeleton would read a parent that had
	// not been computed yet.
	const auto model = bindPoseModelTransforms(MakeChain());

	REQUIRE(model.size() == 3);
	CHECK(model[0][3].y == Catch::Approx(1.0f));
	CHECK(model[1][3].y == Catch::Approx(2.0f));
	CHECK(model[2][3].y == Catch::Approx(3.0f));
}

TEST_CASE("findBone names a bone by its pooled name", "[skeleton]")
{
	const auto skeleton = MakeChain();

	CHECK(findBone(skeleton, "hips") == 0);
	CHECK(findBone(skeleton, "head") == 2);
	CHECK_FALSE(findBone(skeleton, "tail").has_value());
}
