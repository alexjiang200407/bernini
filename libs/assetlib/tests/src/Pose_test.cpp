#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Skeleton.h>

#include <catch2/catch_approx.hpp>

using namespace assetlib;

namespace
{
	Transform
	Rest() noexcept
	{
		return Transform{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
	}

	/**
	 * Two bones, deliberately awkward: the child is offset *and* rotated *and* scaled in its bind
	 * pose, so a walk that composes in the wrong order or drops a term is visibly wrong rather than
	 * accidentally right. A chain of identities would pass almost any implementation.
	 */
	Skeleton
	TwoBoneRig()
	{
		Skeleton skeleton;

		Bone root{};
		root.bindPose   = { glm::vec3(1.0f, 2.0f, 3.0f),
			                glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
			                glm::vec3(2.0f) };
		root.parent     = c_InvalidIndex;
		root.nameOffset = skeleton.stringPool.add("root");
		skeleton.bones.push_back(root);

		Bone child{};
		child.bindPose   = { glm::vec3(0.0f, 1.0f, 0.0f),
			                 glm::angleAxis(glm::radians(-45.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			                 glm::vec3(0.5f) };
		child.parent     = 0;
		child.nameOffset = skeleton.stringPool.add("child");
		skeleton.bones.push_back(child);

		// The inverse bind is the inverse of the bone's *model-space* bind transform, which is what
		// makes a rest pose skin to identity.
		const auto bind = bindPoseModelTransforms(skeleton);
		for (size_t i = 0; i < skeleton.bones.size(); ++i)
			skeleton.bones[i].inverseBind = glm::inverse(bind[i]);

		return skeleton;
	}

	/** A clip whose frames are exactly `poses`, one entry per frame, each a whole-rig pose. */
	AnimationSet
	ClipOf(const Skeleton& skeleton, const std::vector<std::vector<Transform>>& poses)
	{
		AnimationSet animations;
		animations.boneCount         = static_cast<uint32_t>(skeleton.bones.size());
		animations.skeletonSignature = skeletonSignature(skeleton);

		AnimationClip clip{};
		clip.nameOffset  = animations.stringPool.add("clip");
		clip.firstSample = 0;
		clip.frameCount  = static_cast<uint32_t>(poses.size());
		clip.sampleRate  = 30.0f;
		clip.duration    = static_cast<float>(poses.size() - 1) / 30.0f;
		animations.clips.push_back(clip);

		for (const std::vector<Transform>& pose : poses)
			animations.samples.insert(animations.samples.end(), pose.begin(), pose.end());

		return animations;
	}

	glm::vec3
	TranslationOf(const glm::mat4& matrix) noexcept
	{
		return glm::vec3(matrix[3]);
	}

	void
	CheckMatrix(const glm::mat4& got, const glm::mat4& want)
	{
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r) CHECK(got[c][r] == Catch::Approx(want[c][r]).margin(1e-5));
	}
}

// The property the whole bake rests on: a frame whose pose is the bind pose must skin every vertex
// to exactly where it started. If this drifts, every posed frame is wrong by the same drift and
// nothing downstream can tell.
TEST_CASE("A bind-pose frame skins to identity", "[pose][skinning]")
{
	const Skeleton skeleton = TwoBoneRig();

	const std::vector<Transform> bindPose   = { skeleton.bones[0].bindPose,
		                                        skeleton.bones[1].bindPose };
	const AnimationSet           animations = ClipOf(skeleton, { bindPose });

	const auto model    = poseModelTransforms(skeleton, animations, 0, 0);
	const auto skinning = skinningMatrices(skeleton, model);

	REQUIRE(skinning.size() == 2);
	for (const glm::mat4& matrix : skinning) CheckMatrix(matrix, glm::mat4(1.0f));
}

// The same, through the door a caller actually uses: the model transforms of a rest-pose frame are
// the bind pose, so the two entry points cannot drift apart.
TEST_CASE("A bind-pose frame evaluates to the bind pose", "[pose]")
{
	const Skeleton skeleton = TwoBoneRig();

	const std::vector<Transform> bindPose   = { skeleton.bones[0].bindPose,
		                                        skeleton.bones[1].bindPose };
	const AnimationSet           animations = ClipOf(skeleton, { bindPose });

	const auto bind = bindPoseModelTransforms(skeleton);
	const auto pose = poseModelTransforms(skeleton, animations, 0, 0);

	REQUIRE(pose.size() == bind.size());
	for (size_t i = 0; i < pose.size(); ++i) CheckMatrix(pose[i], bind[i]);
}

// Closed form rather than a golden matrix: the expected value is derived here from the TRS channels
// the clip drives, so the test states the rule instead of recording whatever the code produced.
TEST_CASE("A pose composes parent before child", "[pose]")
{
	const Skeleton skeleton = TwoBoneRig();

	const Transform rootPose{ glm::vec3(5.0f, 0.0f, 0.0f),
		                      glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
		                      glm::vec3(1.0f) };
	const Transform childPose{ glm::vec3(0.0f, 2.0f, 0.0f),
		                       glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		                       glm::vec3(1.0f) };

	const AnimationSet animations = ClipOf(skeleton, { { rootPose, childPose } });
	const auto         model      = poseModelTransforms(skeleton, animations, 0, 0);

	CheckMatrix(model[0], toMatrix(rootPose));
	CheckMatrix(model[1], toMatrix(rootPose) * toMatrix(childPose));

	// Spelled out for the one that matters: the root turns +Y onto -X, so the child's local +2 along
	// Y lands 2 to the left of the root's own translation.
	const glm::vec3 childOrigin = TranslationOf(model[1]);
	CHECK(childOrigin.x == Catch::Approx(3.0f).margin(1e-5));
	CHECK(childOrigin.y == Catch::Approx(0.0f).margin(1e-5));
	CHECK(childOrigin.z == Catch::Approx(0.0f).margin(1e-5));
}

TEST_CASE("Each frame of a clip evaluates on its own", "[pose]")
{
	const Skeleton skeleton = TwoBoneRig();

	const Transform still = Rest();
	Transform       moved = Rest();
	moved.translation     = glm::vec3(0.0f, 10.0f, 0.0f);

	const AnimationSet animations =
		ClipOf(skeleton, { { still, still }, { moved, still }, { still, moved } });

	// Frame-major: reading frame 1 must not read frame 0's bone 1, which is the indexing mistake
	// that produces a rig one bone out of step.
	CHECK(
		TranslationOf(poseModelTransforms(skeleton, animations, 0, 1)[0]).y ==
		Catch::Approx(10.0f));
	CHECK(
		TranslationOf(poseModelTransforms(skeleton, animations, 0, 2)[0]).y == Catch::Approx(0.0f));
	CHECK(
		TranslationOf(poseModelTransforms(skeleton, animations, 0, 2)[1]).y ==
		Catch::Approx(10.0f));
}

TEST_CASE("Pose evaluation refuses what it cannot address", "[pose]")
{
	const Skeleton     skeleton   = TwoBoneRig();
	const AnimationSet animations = ClipOf(skeleton, { { Rest(), Rest() } });

	CHECK_THROWS_AS(poseModelTransforms(skeleton, animations, 1, 0), std::runtime_error);
	CHECK_THROWS_AS(poseModelTransforms(skeleton, animations, 0, 1), std::runtime_error);

	SECTION("a clip set cooked against a different rig")
	{
		AnimationSet mismatched = animations;
		mismatched.boneCount    = 3;
		CHECK_THROWS_AS(poseModelTransforms(skeleton, mismatched, 0, 0), std::runtime_error);
	}

	// A file may claim a sample range it does not carry; the pool is the authority, not the header.
	SECTION("a clip whose samples are not in the pool")
	{
		AnimationSet truncated = animations;
		truncated.samples.pop_back();
		CHECK_THROWS_AS(poseModelTransforms(skeleton, truncated, 0, 0), std::runtime_error);
	}

	SECTION("model transforms that are not one per bone")
	{
		const std::vector<glm::mat4> tooFew(1, glm::mat4(1.0f));
		CHECK_THROWS_AS(skinningMatrices(skeleton, tooFew), std::runtime_error);
	}
}

namespace
{
	/** A second clip appended to `animations`, so a blend has two to draw from. */
	uint32_t
	AppendClip(
		AnimationSet&                              animations,
		std::string_view                           name,
		const std::vector<std::vector<Transform>>& poses)
	{
		AnimationClip clip{};
		clip.nameOffset  = animations.stringPool.add(name);
		clip.firstSample = static_cast<uint32_t>(animations.samples.size());
		clip.frameCount  = static_cast<uint32_t>(poses.size());
		clip.sampleRate  = 30.0f;
		clip.duration    = static_cast<float>(poses.size() - 1) / 30.0f;
		animations.clips.push_back(clip);

		for (const std::vector<Transform>& pose : poses)
			animations.samples.insert(animations.samples.end(), pose.begin(), pose.end());

		return static_cast<uint32_t>(animations.clips.size() - 1);
	}

	Transform
	TurnedAboutZ(float degrees, const glm::vec3& translation = glm::vec3(0.0f)) noexcept
	{
		return Transform{ translation,
			              glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 0.0f, 1.0f)),
			              glm::vec3(1.0f) };
	}

	void
	CheckPose(std::span<const glm::mat4> got, std::span<const glm::mat4> want)
	{
		REQUIRE(got.size() == want.size());
		for (size_t i = 0; i < got.size(); ++i) CheckMatrix(got[i], want[i]);
	}
}

// The weighted form at one integer frame is the integer form, at any weight: the two entry points
// are the same reference, and a GPU gate may use either.
TEST_CASE("A single sample at an integer frame is that frame", "[pose][blend]")
{
	const Skeleton skeleton = TwoBoneRig();

	Transform moved   = Rest();
	moved.translation = glm::vec3(0.0f, 10.0f, 0.0f);
	const AnimationSet animations =
		ClipOf(skeleton, { { Rest(), Rest() }, { moved, Rest() }, { TurnedAboutZ(90.0f), moved } });

	for (const float weight : { 1.0f, 0.25f, 7.0f })
		for (uint32_t frame = 0; frame < 3; ++frame)
		{
			const BlendSample sample{ 0, static_cast<float>(frame), weight };
			CheckPose(
				poseModelTransforms(skeleton, animations, std::span(&sample, 1)),
				poseModelTransforms(skeleton, animations, 0, frame));
		}
}

TEST_CASE("A clip blended with itself is itself", "[pose][blend]")
{
	const Skeleton     skeleton   = TwoBoneRig();
	const AnimationSet animations = ClipOf(
		skeleton,
		{ { TurnedAboutZ(10.0f), Rest() },
	      { TurnedAboutZ(70.0f, glm::vec3(4.0f, 0.0f, 0.0f)), Rest() } });

	SECTION("at an integer frame, whatever the weights")
	{
		const std::vector<BlendSample> samples = { { 0, 1.0f, 0.3f }, { 0, 1.0f, 0.7f } };
		CheckPose(
			poseModelTransforms(skeleton, animations, samples),
			poseModelTransforms(skeleton, animations, 0, 1));

		// Weights are normalized, not assumed to sum to one.
		const std::vector<BlendSample> unnormalized = { { 0, 1.0f, 2.0f }, { 0, 1.0f, 2.0f } };
		CheckPose(
			poseModelTransforms(skeleton, animations, unnormalized),
			poseModelTransforms(skeleton, animations, 0, 1));
	}

	SECTION("at a fractional frame")
	{
		const BlendSample              one     = { 0, 0.5f, 1.0f };
		const std::vector<BlendSample> samples = { { 0, 0.5f, 0.2f }, { 0, 0.5f, 0.8f } };
		CheckPose(
			poseModelTransforms(skeleton, animations, samples),
			poseModelTransforms(skeleton, animations, std::span(&one, 1)));
	}
}

// Closed form: nlerp at t = 0.5 lies on the bisector of the two rotations, so halfway between 0
// and 90 degrees is exactly 45 -- the one point where nlerp and slerp agree, which is what makes it
// derivable by hand rather than recorded from the code.
TEST_CASE("Halfway between two frames is the half angle", "[pose][blend]")
{
	const Skeleton     skeleton   = TwoBoneRig();
	const AnimationSet animations = ClipOf(
		skeleton,
		{ { TurnedAboutZ(0.0f), Rest() },
	      { TurnedAboutZ(90.0f, glm::vec3(4.0f, 0.0f, 0.0f)), Rest() } });

	const BlendSample sample{ 0, 0.5f, 1.0f };
	const auto        model = poseModelTransforms(skeleton, animations, std::span(&sample, 1));

	CheckMatrix(model[0], toMatrix(TurnedAboutZ(45.0f, glm::vec3(2.0f, 0.0f, 0.0f))));
}

TEST_CASE("Two clips blended equally meet at the half angle", "[pose][blend]")
{
	const Skeleton skeleton = TwoBoneRig();

	AnimationSet   animations = ClipOf(skeleton, { { TurnedAboutZ(0.0f), TurnedAboutZ(20.0f) } });
	const uint32_t turned     = AppendClip(
		animations,
		"turned",
		{ { TurnedAboutZ(90.0f, glm::vec3(4.0f, 0.0f, 0.0f)), TurnedAboutZ(-40.0f) } });

	const Transform wantRoot  = TurnedAboutZ(45.0f, glm::vec3(2.0f, 0.0f, 0.0f));
	const Transform wantChild = TurnedAboutZ(-10.0f);

	SECTION("blended local to the parent, then walked")
	{
		const std::vector<BlendSample> samples = { { 0, 0.0f, 0.5f }, { turned, 0.0f, 0.5f } };
		const auto                     model   = poseModelTransforms(skeleton, animations, samples);

		CheckMatrix(model[0], toMatrix(wantRoot));
		CheckMatrix(model[1], toMatrix(wantRoot) * toMatrix(wantChild));
	}

	// q and -q are one rotation. A pair straddling the hemisphere boundary must blend the short
	// way round, which is the flip against the running sum.
	SECTION("whichever sign the second clip's quaternion was stored with")
	{
		for (Transform& pose : animations.samples) pose.rotation = -pose.rotation;

		const std::vector<BlendSample> samples = { { 0, 0.0f, 0.5f }, { turned, 0.0f, 0.5f } };
		const auto                     model   = poseModelTransforms(skeleton, animations, samples);

		CheckMatrix(model[0], toMatrix(wantRoot));
		CheckMatrix(model[1], toMatrix(wantRoot) * toMatrix(wantChild));
	}

	// Off the midpoint nlerp is not the angle's lerp; the rule is the normalized weighted sum, and
	// the expectation is derived from that rule, not from the code.
	SECTION("and unequally, by the normalized weighted sum")
	{
		const std::vector<BlendSample> samples = { { 0, 0.0f, 0.25f }, { turned, 0.0f, 0.75f } };
		const auto                     model   = poseModelTransforms(skeleton, animations, samples);

		const glm::quat q0 = TurnedAboutZ(0.0f).rotation;
		const glm::quat q1 = TurnedAboutZ(90.0f).rotation;
		const Transform want{ glm::vec3(3.0f, 0.0f, 0.0f),
			                  glm::normalize(q0 * 0.25f + q1 * 0.75f),
			                  glm::vec3(1.0f) };
		CheckMatrix(model[0], toMatrix(want));
	}
}

TEST_CASE("A blend refuses what it cannot evaluate", "[pose][blend]")
{
	const Skeleton     skeleton   = TwoBoneRig();
	const AnimationSet animations = ClipOf(skeleton, { { Rest(), Rest() }, { Rest(), Rest() } });

	const auto blend = [&](std::vector<BlendSample> samples) {
		return poseModelTransforms(skeleton, animations, samples);
	};

	CHECK_THROWS_AS(blend({}), std::runtime_error);
	CHECK_THROWS_AS(blend({ { 0, 0.0f, 0.0f }, { 0, 1.0f, 0.0f } }), std::runtime_error);
	CHECK_THROWS_AS(blend({ { 0, 0.0f, 1.0f }, { 0, 1.0f, -0.5f } }), std::runtime_error);
	CHECK_THROWS_AS(blend({ { 0, 0.0f, 1.0f }, { 0, 1.0f, INFINITY } }), std::runtime_error);
	CHECK_THROWS_AS(blend({ { 0, 0.0f, std::nanf("") } }), std::runtime_error);
	CHECK_THROWS_AS(blend({ { 1, 0.0f, 1.0f } }), std::runtime_error);

	SECTION("a frame past the clip's last, or not a number")
	{
		CHECK_NOTHROW(blend({ { 0, 1.0f, 1.0f } }));
		CHECK_THROWS_AS(blend({ { 0, 1.5f, 1.0f } }), std::runtime_error);
		CHECK_THROWS_AS(blend({ { 0, -0.5f, 1.0f } }), std::runtime_error);
		CHECK_THROWS_AS(blend({ { 0, std::nanf(""), 1.0f } }), std::runtime_error);
	}

	SECTION("a clip that holds no frames")
	{
		AnimationSet empty        = animations;
		empty.clips[0].frameCount = 0;
		const BlendSample sample{ 0, 0.0f, 1.0f };
		CHECK_THROWS_AS(
			poseModelTransforms(skeleton, empty, std::span(&sample, 1)),
			std::runtime_error);
	}

	SECTION("a clip set cooked against a different rig")
	{
		AnimationSet mismatched = animations;
		mismatched.boneCount    = 3;
		const BlendSample sample{ 0, 0.0f, 1.0f };
		CHECK_THROWS_AS(
			poseModelTransforms(skeleton, mismatched, std::span(&sample, 1)),
			std::runtime_error);
	}
}
