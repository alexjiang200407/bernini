#include "Windows/AnimationEditor/AnimationEditorWindow.h"
#include "Windows/AnimationEditor/TimelineScrubber.h"
#include "Windows/AnimationEditor/animation_draws.h"

#include <assetlib/skeleton.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>

#include <catch2/catch_approx.hpp>

// The transport glue behind the panel, pinned without a window: which mesh entries play as VAT
// and which stand static, the clip-table conversion, and the timeline's tick mapping.

namespace
{
	// One submesh: skinned means it carries joint indices.
	uint32_t
	AddEntry(assetlib::BMesh& mesh, bool skinned)
	{
		auto submesh                  = assetlib::Submesh();
		submesh.indexType             = assetlib::IndexType::kUint16;
		submesh.layout.attributeCount = 1;
		submesh.layout.attributes[0].semantic =
			skinned ? assetlib::VertexSemantic::kJoints0 : assetlib::VertexSemantic::kPosition;

		const auto first = static_cast<uint32_t>(mesh.submeshes.size());
		mesh.submeshes.push_back(submesh);
		mesh.meshes.push_back({ .firstSubmesh = first, .submeshCount = 1, .nameOffset = 0 });
		return static_cast<uint32_t>(mesh.meshes.size() - 1);
	}

	void
	AddNode(assetlib::BMesh& mesh, uint32_t meshIndex)
	{
		auto node           = assetlib::Node();
		node.localTransform = { glm::vec3(0.0f),
			                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			                    glm::vec3(1.0f) };
		node.parent         = assetlib::c_InvalidIndex;
		node.firstChild     = assetlib::c_InvalidIndex;
		node.nextSibling    = assetlib::c_InvalidIndex;
		node.mesh           = meshIndex;
		node.nameOffset     = 0;
		mesh.nodes.push_back(node);
	}
}

TEST_CASE("A skinned entry plays as VAT; a static one stands beside it", "[animation]")
{
	auto mesh     = assetlib::BMesh();
	mesh.skeleton = "Skeletons/rig.bskel";

	const uint32_t skinned = AddEntry(mesh, true);
	const uint32_t prop    = AddEntry(mesh, false);
	AddNode(mesh, skinned);
	AddNode(mesh, prop);

	const auto plan = editor::PlanAnimationDraws(mesh);

	REQUIRE(plan.animated.size() == 1);
	CHECK(plan.animated[0].meshIndex == skinned);
	REQUIRE(plan.statics.size() == 1);
	CHECK(plan.statics[0].meshIndex == prop);
}

TEST_CASE("The clip table converts field for field", "[animation]")
{
	const auto infos = editor::ToClipInfos(
		std::array{ game::ClipInfo{ "walk", 24, 30.0f, 0.767f, true },
	                game::ClipInfo{ "die", 12, 15.0f, 0.733f, false } });

	REQUIRE(infos.size() == 2);
	CHECK(infos[0].name == "walk");
	CHECK(infos[0].frameCount == 24);
	CHECK(infos[0].sampleRate == 30.0f);
	CHECK(infos[0].loop);
	CHECK(infos[1].name == "die");
	CHECK_FALSE(infos[1].loop);
}

TEST_CASE("Timeline ticks round-trip the clock inside the period", "[animation]")
{
	CHECK(AnimationEditorWindow::TimelineTicks(0.0f, 2.0f, 1000) == 0);
	CHECK(AnimationEditorWindow::TimelineTicks(1.0f, 2.0f, 1000) == 500);
	CHECK(AnimationEditorWindow::TimelineTicks(2.0f, 2.0f, 1000) == 1000);
	CHECK(AnimationEditorWindow::TimelineTicks(5.0f, 2.0f, 1000) == 1000);  // clamped
	CHECK(AnimationEditorWindow::TimelineTicks(1.0f, 0.0f, 1000) == 0);     // no clips

	CHECK(AnimationEditorWindow::TimelineSeconds(500, 2.0f, 1000) == Catch::Approx(1.0f));
	CHECK(AnimationEditorWindow::TimelineSeconds(1000, 2.0f, 1000) == Catch::Approx(2.0f));
	CHECK(AnimationEditorWindow::TimelineSeconds(2000, 2.0f, 1000) == Catch::Approx(2.0f));
	CHECK(AnimationEditorWindow::TimelineSeconds(500, 0.0f, 1000) == 0.0f);

	const int ticks = AnimationEditorWindow::TimelineTicks(0.733f, 2.2f, 1000);
	CHECK(
		AnimationEditorWindow::TimelineSeconds(ticks, 2.2f, 1000) ==
		Catch::Approx(0.733f).margin(0.0023f));  // one tick of slack
}

TEST_CASE("The scrubber's press-to-tick mapping spans the groove exactly", "[animation]")
{
	// The handle's center travels [radius, width - radius]; presses outside clamp to the ends.
	CHECK(TimelineScrubber::ValueForX(0, 200, 1000) == 0);
	CHECK(TimelineScrubber::ValueForX(200, 200, 1000) == 1000);
	CHECK(TimelineScrubber::ValueForX(100, 200, 1000) == 500);
	CHECK(TimelineScrubber::ValueForX(-50, 200, 1000) == 0);
	CHECK(TimelineScrubber::ValueForX(500, 200, 1000) == 1000);

	// Degenerate widths never divide by zero.
	CHECK(TimelineScrubber::ValueForX(5, 0, 1000) == 0);
	CHECK(TimelineScrubber::ValueForX(5, 10, 1000) == 0);
}

TEST_CASE("A load's tier-dependent steps all follow from the source", "[animation][source]")
{
	using editor::AnimationSource;
	using editor::PlanAnimationLoad;

	SECTION("the VAT tier bakes, frames by the bake's box, and a bake can answer its refusal")
	{
		const auto steps = PlanAnimationLoad(AnimationSource::kVat, /*hasAnimations*/ true);
		CHECK(steps.bakeVat);
		CHECK(steps.offerBakeOnRefusal);
	}

	SECTION("the skinned tier does none of them")
	{
		// The bake is the one that matters: it is seconds of CPU skinning for a texture pair the
		// skinned path never samples, and it would also make the preview need a bakeable material.
		const auto steps = PlanAnimationLoad(AnimationSource::kSkinned, /*hasAnimations*/ true);
		CHECK_FALSE(steps.bakeVat);
		CHECK_FALSE(steps.offerBakeOnRefusal);
	}

	SECTION("with no clip file, neither tier does anything")
	{
		// Nothing to play: the mesh stands in its bind pose as static geometry, so a bake and a bake
		// offer are both meaningless -- including on the VAT tier, which would otherwise bake.
		for (const AnimationSource source : { AnimationSource::kSkinned, AnimationSource::kVat })
		{
			const auto steps = PlanAnimationLoad(source, /*hasAnimations*/ false);
			CHECK_FALSE(steps.bakeVat);
			CHECK_FALSE(steps.offerBakeOnRefusal);
		}
	}
}

namespace
{
	/**
	 * A three-bone rig: a root, a head `headAt` from it, and one face bone `facing` from the head --
	 * the shape every character rig has, where the face is built out of the head's children.
	 */
	assetlib::Skeleton
	RigFacing(
		const glm::vec3& facing,
		const glm::vec3& headAt   = glm::vec3(0.0f, 5.0f, 0.0f),
		const char*      headName = "Head")
	{
		auto skeleton = assetlib::Skeleton();

		auto root        = assetlib::Bone();
		root.bindPose    = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		root.inverseBind = glm::mat4(1.0f);
		root.parent      = assetlib::c_InvalidIndex;
		root.nameOffset  = skeleton.stringPool.add("Pelvis");
		skeleton.bones.push_back(root);

		auto head        = assetlib::Bone();
		head.bindPose    = { headAt, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		head.inverseBind = glm::mat4(1.0f);
		head.parent      = 0;
		head.nameOffset  = skeleton.stringPool.add(headName);
		skeleton.bones.push_back(head);

		auto nose        = assetlib::Bone();
		nose.bindPose    = { facing, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		nose.inverseBind = glm::mat4(1.0f);
		nose.parent      = 1;
		nose.nameOffset  = skeleton.stringPool.add("Nose");
		skeleton.bones.push_back(nose);

		return skeleton;
	}

	/** One clip of one frame holding `skeleton` in its bind pose. */
	assetlib::AnimationSet
	RestClip(const assetlib::Skeleton& skeleton)
	{
		auto animations              = assetlib::AnimationSet();
		animations.boneCount         = static_cast<uint32_t>(skeleton.bones.size());
		animations.skeletonSignature = assetlib::skeletonSignature(skeleton);

		auto clip        = assetlib::AnimationClip();
		clip.firstSample = 0;
		clip.frameCount  = 1;
		clip.sampleRate  = 30.0f;
		animations.clips.push_back(clip);

		for (const assetlib::Bone& bone : skeleton.bones)
			animations.samples.push_back(bone.bindPose);

		return animations;
	}
}

TEST_CASE("The opening camera faces whichever axis a rig's head is on", "[animation][camera]")
{
	using editor::RestFacingYaw;

	// The orbit camera puts its eye at (sin(yaw), _, cos(yaw)), so these are the yaws that put the
	// eye where the face points -- which is the whole reason a constant cannot do this job. The test
	// coyote faces +X; glTF's own convention is +Z.
	SECTION("a rig facing +Z, which is glTF's convention")
	{
		const auto skeleton = RigFacing(glm::vec3(0.0f, 0.0f, 5.0f));
		const auto yaw      = RestFacingYaw(skeleton, RestClip(skeleton));
		REQUIRE(yaw.has_value());
		CHECK(*yaw == Catch::Approx(0.0f).margin(1e-4));
	}

	SECTION("a rig facing +X, which is what the test coyote does")
	{
		const auto skeleton = RigFacing(glm::vec3(5.0f, 0.0f, 0.0f));
		const auto yaw      = RestFacingYaw(skeleton, RestClip(skeleton));
		REQUIRE(yaw.has_value());
		CHECK(*yaw == Catch::Approx(glm::half_pi<float>()).margin(1e-4));
	}

	SECTION("a rig facing -Z")
	{
		const auto skeleton = RigFacing(glm::vec3(0.0f, 0.0f, -5.0f));
		const auto yaw      = RestFacingYaw(skeleton, RestClip(skeleton));
		REQUIRE(yaw.has_value());
		CHECK(std::abs(*yaw) == Catch::Approx(glm::pi<float>()).margin(1e-4));
	}

	SECTION("a reared rig, whose head is above its root rather than in front of it")
	{
		// The case that put the camera behind the coyote: its idle clip stands it up, so the head
		// sits 45 units above the pelvis and 4 to the *wrong* side. Read root-to-head and the camera
		// lands on the tail; read where the head looks and it lands on the face.
		const auto skeleton =
			RigFacing(glm::vec3(43.0f, 14.0f, 0.0f), glm::vec3(-4.0f, 45.0f, 0.0f));
		const auto yaw = RestFacingYaw(skeleton, RestClip(skeleton));
		REQUIRE(yaw.has_value());
		CHECK(*yaw == Catch::Approx(glm::half_pi<float>()).margin(1e-4));
	}

	SECTION("a head looking straight up says nothing")
	{
		const auto skeleton = RigFacing(glm::vec3(0.0f, 5.0f, 0.0f));
		CHECK_FALSE(RestFacingYaw(skeleton, RestClip(skeleton)).has_value());
	}

	SECTION("a rig that does not name a head says nothing")
	{
		const auto skeleton =
			RigFacing(glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f, 5.0f, 0.0f), "Bone_02");
		CHECK_FALSE(RestFacingYaw(skeleton, RestClip(skeleton)).has_value());
	}

	SECTION("the shortest 'head' wins, so a forehead does not beat the head")
	{
		auto skeleton =
			RigFacing(glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f, 5.0f, 0.0f), "Coyote Head");

		// Named after the head bone but longer, and with a child the other way: picking it would
		// point the camera at the rig's back.
		auto forehead        = assetlib::Bone();
		forehead.bindPose    = { glm::vec3(0.0f, 1.0f, 0.0f),
			                     glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			                     glm::vec3(1.0f) };
		forehead.inverseBind = glm::mat4(1.0f);
		forehead.parent      = 1;
		forehead.nameOffset  = skeleton.stringPool.add("Coyote L Forehead");
		skeleton.bones.push_back(forehead);

		auto behind        = assetlib::Bone();
		behind.bindPose    = { glm::vec3(-40.0f, 0.0f, 0.0f),
			                   glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			                   glm::vec3(1.0f) };
		behind.inverseBind = glm::mat4(1.0f);
		behind.parent      = 3;
		behind.nameOffset  = skeleton.stringPool.add("Tuft");
		skeleton.bones.push_back(behind);

		const auto yaw = RestFacingYaw(skeleton, RestClip(skeleton));
		REQUIRE(yaw.has_value());
		CHECK(*yaw > 0.0f);
	}
}
