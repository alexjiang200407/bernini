#include <assetlib/banim_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/skeleton.h>

#include <catch2/catch_approx.hpp>

using namespace assetlib;

namespace
{
	Transform
	identityTransform() noexcept
	{
		return Transform{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
	}

	uint32_t
	addName(std::vector<char>& pool, const char* name)
	{
		const auto offset = static_cast<uint32_t>(pool.size());
		for (const char* c = name; *c != '\0'; ++c) pool.push_back(*c);
		pool.push_back('\0');
		return offset;
	}

	/** A three-bone chain: hips -> spine -> head, each offset one unit up from its parent. */
	Skeleton
	makeChain()
	{
		Skeleton skeleton;
		skeleton.stringPool.push_back('\0');

		const std::array<const char*, 3> names = { { "hips", "spine", "head" } };
		for (uint32_t i = 0; i < names.size(); ++i)
		{
			Bone bone{};
			bone.bindPose               = identityTransform();
			bone.bindPose.translation.y = 1.0f;
			bone.inverseBind =
				glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -static_cast<float>(i + 1), 0.0f));
			bone.parent     = i == 0 ? c_InvalidIndex : i - 1;
			bone.nameOffset = addName(skeleton.stringPool, names[i]);
			skeleton.bones.push_back(bone);
		}

		return skeleton;
	}

	/** One clip of `frames` poses over the chain, translating bone 0 along +Z by `distance`. */
	AnimationSet
	makeClipSet(const Skeleton& skeleton, uint32_t frames, float distance)
	{
		AnimationSet animations;
		animations.stringPool.push_back('\0');
		animations.boneCount         = static_cast<uint32_t>(skeleton.bones.size());
		animations.skeletonSignature = skeletonSignature(skeleton);
		animations.skeleton          = "Animations/walk.bskel";

		AnimationClip clip{};
		clip.nameOffset  = addName(animations.stringPool, "walk");
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
	const auto skeleton = makeChain();
	const auto restored = deserializeSkeleton(serializeSkeleton(skeleton));

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
	auto skeleton = makeChain();
	std::swap(skeleton.bones[0], skeleton.bones[1]);

	const auto bytes = serializeSkeleton(skeleton);
	CHECK_THROWS_AS(deserializeSkeleton(bytes), std::runtime_error);
}

TEST_CASE("A skeleton's signature covers its bones' names and parents", "[skeleton]")
{
	const auto skeleton = makeChain();
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
		renamed.bones[1].nameOffset = addName(renamed.stringPool, "chest");
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
		extra.bindPose   = identityTransform();
		extra.parent     = 0;
		extra.nameOffset = addName(inserted.stringPool, "tail");
		inserted.bones.push_back(extra);
		CHECK(skeletonSignature(inserted) != original);
	}
}

TEST_CASE("A clip set survives a container round-trip", "[animation][io]")
{
	const auto skeleton   = makeChain();
	const auto animations = makeClipSet(skeleton, 31, 2.0f);
	const auto restored   = deserializeAnimations(serializeAnimations(animations));

	CHECK(restored.skeleton == animations.skeleton);
	CHECK(restored.skeletonSignature == animations.skeletonSignature);
	CHECK(restored.boneCount == animations.boneCount);
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
	const auto skeleton   = makeChain();
	const auto animations = makeClipSet(skeleton, 31, 2.0f);

	const auto path = std::filesystem::temp_directory_path() / "bernini_banim_refs.banim";
	saveAnimations(animations, path);

	CHECK(loadAnimationSkeletonPath(path) == animations.skeleton);
	CHECK(loadAnimations(path).skeleton == animations.skeleton);

	std::filesystem::remove(path);
}

TEST_CASE("A clip set cooked against another rig is detected", "[animation]")
{
	const auto skeleton   = makeChain();
	const auto animations = makeClipSet(skeleton, 31, 2.0f);

	CHECK(animationsMatchSkeleton(animations, skeleton));

	SECTION("a reordered rig no longer matches")
	{
		// The clips' bone indices now name different bones, and nothing about the pose they produce
		// says so -- this comparison is the only thing that can.
		auto reordered                = skeleton;
		reordered.bones[1].nameOffset = addName(reordered.stringPool, "chest");
		CHECK_FALSE(animationsMatchSkeleton(animations, reordered));
	}

	SECTION("nor does one with a different bone count")
	{
		auto shorter = skeleton;
		shorter.bones.pop_back();
		CHECK_FALSE(animationsMatchSkeleton(animations, shorter));
	}
}

TEST_CASE("A clip that samples past its pool will not load", "[animation][io]")
{
	const auto skeleton            = makeChain();
	auto       animations          = makeClipSet(skeleton, 31, 2.0f);
	animations.clips[0].frameCount = 40;

	const auto bytes = serializeAnimations(animations);
	CHECK_THROWS_AS(deserializeAnimations(bytes), std::runtime_error);
}

TEST_CASE("A bind pose resolves to model space in one forward pass", "[skeleton]")
{
	// Each bone sits one unit above its parent, so the chain's third bone is three units up. This is
	// only true because parent < index; a walk over an unsorted skeleton would read a parent that had
	// not been computed yet.
	const auto model = bindPoseModelTransforms(makeChain());

	REQUIRE(model.size() == 3);
	CHECK(model[0][3].y == Catch::Approx(1.0f));
	CHECK(model[1][3].y == Catch::Approx(2.0f));
	CHECK(model[2][3].y == Catch::Approx(3.0f));
}

TEST_CASE("findBone names a bone by its pooled name", "[skeleton]")
{
	const auto skeleton = makeChain();

	CHECK(findBone(skeleton, "hips") == 0);
	CHECK(findBone(skeleton, "head") == 2);
	CHECK(findBone(skeleton, "tail") == c_InvalidIndex);
}
