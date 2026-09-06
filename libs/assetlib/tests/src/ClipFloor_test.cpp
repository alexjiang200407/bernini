#include <algorithm>
#include <array>
#include <assetlib/avatar.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/VertexLayout.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

using namespace assetlib;

namespace
{
	/**
	 * A one-bone rig and a mesh welded to it, with clips assembled a frame at a time. Everything a
	 * grounding case needs and nothing else: the floor is a property of where the vertices end up,
	 * so the fixture's job is to put them somewhere awkward.
	 */
	struct Rig
	{
		BMesh        mesh;
		Skeleton     skeleton;
		AnimationSet animations;

		Rig()
		{
			Bone bone{};
			bone.bindPose = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
			bone.inverseBind = glm::mat4(1.0f);
			bone.parent      = c_InvalidIndex;
			bone.nameOffset  = skeleton.stringPool.add("root");
			skeleton.bones.push_back(bone);

			animations.boneCount         = 1;
			animations.skeletonSignature = skeletonSignature(skeleton);
		}

		/**
		 * One vertex welded to `joint`, at `position` in bind space. A joint no bone answers to is
		 * how a case makes the measurement refuse the set.
		 */
		void
		AddVertex(const glm::vec3& position, uint16_t joint = 0)
		{
			const auto write = [&](const auto& value) {
				const auto* bytes = reinterpret_cast<const std::byte*>(&value);
				mesh.vertexData.insert(mesh.vertexData.end(), bytes, bytes + sizeof(value));
			};

			write(position);
			write(std::array<uint16_t, 4>{ { joint, 0, 0, 0 } });
			write(std::array<uint16_t, 4>{ { 65535, 0, 0, 0 } });
			++m_VertexCount;
		}

		/** Closes the vertices added so far into the mesh's single skinned entry. */
		void
		Finish()
		{
			Submesh submesh{};
			submesh.layout.attributeCount = 3;
			submesh.layout.attributes[0]  = { VertexSemantic::kPosition,
				                              VertexFormat::kFloat32x3,
				                              0 };
			submesh.layout.attributes[1]  = { VertexSemantic::kJoints0,
				                              VertexFormat::kUint16x4,
				                              12 };
			submesh.layout.attributes[2]  = { VertexSemantic::kWeights0,
				                              VertexFormat::kUnorm16x4,
				                              20 };
			submesh.layout.stride         = 28;
			submesh.vertexCount           = m_VertexCount;
			submesh.vertexByteOffset      = 0;

			mesh.submeshes.push_back(submesh);
			mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });
		}

		/** Opens a clip; every AddFrame after this belongs to it until the next AddClip. */
		void
		AddClip()
		{
			AnimationClip clip{};
			clip.firstSample = static_cast<uint32_t>(animations.samples.size());
			clip.frameCount  = 0;
			clip.sampleRate  = 30.0f;
			animations.clips.push_back(clip);
		}

		void
		AddFrame(const Transform& pose)
		{
			animations.samples.push_back(pose);
			++animations.clips.back().frameCount;
		}

		[[nodiscard]] static Transform
		At(const glm::vec3& translation,
		   const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
		{
			return Transform{ translation, rotation, glm::vec3(1.0f) };
		}

		/** This rig's one mesh, as the span the grounding seam takes. */
		[[nodiscard]] std::span<const BMesh>
		Meshes() const
		{
			return std::span<const BMesh>(&mesh, 1);
		}

		/** The lowest skinned vertex at `frame` of `clip`, walked the slow, obvious way. */
		[[nodiscard]] float
		LowestAt(uint32_t clip, uint32_t frame) const
		{
			const std::vector<glm::mat4> skinning =
				skinningMatrices(skeleton, poseModelTransforms(skeleton, animations, clip, frame));

			auto lowest = std::numeric_limits<float>::max();
			for (const SkinnedVertex& vertex : skinSubmesh(mesh, mesh.submeshes[0], skinning))
				lowest = std::min(lowest, vertex.position.y);
			return lowest;
		}

		/** The lowest skinned vertex anywhere in `clip`. */
		[[nodiscard]] float
		LowestIn(uint32_t clip) const
		{
			auto lowest = std::numeric_limits<float>::max();
			for (uint32_t frame = 0; frame < animations.clips[clip].frameCount; ++frame)
				lowest = std::min(lowest, LowestAt(clip, frame));
			return lowest;
		}

	private:
		uint32_t m_VertexCount = 0;
	};
}

TEST_CASE("A clip is moved so its lowest vertex rests on the floor", "[skinning][grounding]")
{
	Rig rig;
	rig.AddVertex(glm::vec3(0.0f, 3.0f, 0.0f));
	rig.AddVertex(glm::vec3(0.0f, 5.0f, 0.0f));
	rig.Finish();

	rig.AddClip();
	rig.AddFrame(Rig::At(glm::vec3(0.0f)));
	rig.AddFrame(Rig::At(glm::vec3(0.0f)));

	REQUIRE(
		measureClipFloors(rig.animations, rig.Meshes(), rig.skeleton).front() ==
		Catch::Approx(3.0f));

	groundClips(rig.animations, rig.Meshes(), rig.skeleton);

	CHECK(rig.LowestIn(0) == Catch::Approx(0.0f).margin(1e-5));
	CHECK(rig.animations.clips[0].groundOffset == Catch::Approx(3.0f));
}

TEST_CASE("A clip authored below the floor is lifted onto it", "[skinning][grounding]")
{
	// The other sign, which is the Coyote's Sleep and Jump_Up: a rig that sinks rather than floats.
	Rig rig;
	rig.AddVertex(glm::vec3(0.0f, -0.5f, 0.0f));
	rig.Finish();

	rig.AddClip();
	rig.AddFrame(Rig::At(glm::vec3(0.0f)));

	groundClips(rig.animations, rig.Meshes(), rig.skeleton);

	CHECK(rig.LowestIn(0) == Catch::Approx(0.0f).margin(1e-5));
	CHECK(rig.animations.clips[0].groundOffset == Catch::Approx(-0.5f));
}

TEST_CASE("Each clip is grounded by its own floor", "[skinning][grounding]")
{
	// The whole point of measuring per clip: one authored ground reference per clip is exactly the
	// defect, so a single rig-wide offset would leave every clip but one wrong.
	Rig rig;
	rig.AddVertex(glm::vec3(0.0f));
	rig.Finish();

	rig.AddClip();
	rig.AddFrame(Rig::At(glm::vec3(0.0f, 10.0f, 0.0f)));

	rig.AddClip();
	rig.AddFrame(Rig::At(glm::vec3(0.0f, -2.0f, 0.0f)));

	groundClips(rig.animations, rig.Meshes(), rig.skeleton);

	CHECK(rig.animations.clips[0].groundOffset == Catch::Approx(10.0f));
	CHECK(rig.animations.clips[1].groundOffset == Catch::Approx(-2.0f));
	CHECK(rig.LowestIn(0) == Catch::Approx(0.0f).margin(1e-5));
	CHECK(rig.LowestIn(1) == Catch::Approx(0.0f).margin(1e-5));
}

TEST_CASE("The floor is the lowest frame, not the first one", "[skinning][grounding]")
{
	// The Coyote's Run opens near the top of its gait cycle, 0.34 above where its planted phase
	// reaches. Referencing frame 0 -- which is what Unity's "Based Upon (at Start)" does -- would
	// drive that phase underground, so the measurement has to see the whole clip.
	Rig rig;
	rig.AddVertex(glm::vec3(0.0f));
	rig.Finish();

	rig.AddClip();
	rig.AddFrame(Rig::At(glm::vec3(0.0f, 8.0f, 0.0f)));
	rig.AddFrame(Rig::At(glm::vec3(0.0f, 2.0f, 0.0f)));
	rig.AddFrame(Rig::At(glm::vec3(0.0f, 5.0f, 0.0f)));

	CHECK(
		measureClipFloors(rig.animations, rig.Meshes(), rig.skeleton).front() ==
		Catch::Approx(2.0f));

	groundClips(rig.animations, rig.Meshes(), rig.skeleton);

	CHECK(rig.LowestAt(0, 0) == Catch::Approx(6.0f));
	CHECK(rig.LowestAt(0, 1) == Catch::Approx(0.0f).margin(1e-5));
}

// The property the prune rests on: the conservative box that orders the frames must never bound a
// frame above its own vertices, or the frame holding the real minimum is dropped unvisited and the
// rig is planted too high.
TEST_CASE("The measured floor is exactly the lowest vertex", "[skinning][grounding]")
{
	// A vertex far off the bone's origin, rotated a different amount every frame -- which is where
	// an axis-aligned box swept by a rotation is loosest, so the bound and the truth diverge most.
	Rig rig;
	rig.AddVertex(glm::vec3(4.0f, 0.0f, 0.0f));
	rig.AddVertex(glm::vec3(0.0f, 0.0f, 4.0f));
	rig.Finish();

	rig.AddClip();
	for (uint32_t frame = 0; frame < 24; ++frame)
	{
		const float angle = static_cast<float>(frame) * 0.31f;
		rig.AddFrame(
			Rig::At(
				glm::vec3(0.0f, 20.0f, 0.0f),
				glm::angleAxis(angle, glm::normalize(glm::vec3(0.3f, 1.0f, 0.7f)))));
	}

	auto brute = std::numeric_limits<float>::max();
	for (uint32_t frame = 0; frame < rig.animations.clips[0].frameCount; ++frame)
		brute = std::min(brute, rig.LowestAt(0, frame));

	CHECK(
		measureClipFloors(rig.animations, rig.Meshes(), rig.skeleton).front() ==
		Catch::Approx(brute));
}

TEST_CASE("Grounding a clip twice does not sink it further", "[skinning][grounding]")
{
	// A cook runs over an already-cooked .banim -- assetlib_cli bakebounds does exactly that -- so
	// a pass that could not see its own previous work would drive the rig down once per run.
	Rig rig;
	rig.AddVertex(glm::vec3(0.0f, 6.0f, 0.0f));
	rig.Finish();

	rig.AddClip();
	rig.AddFrame(Rig::At(glm::vec3(0.0f)));

	groundClips(rig.animations, rig.Meshes(), rig.skeleton);
	groundClips(rig.animations, rig.Meshes(), rig.skeleton);

	CHECK(rig.LowestIn(0) == Catch::Approx(0.0f).margin(1e-5));
	CHECK(rig.animations.clips[0].groundOffset == Catch::Approx(6.0f));
}

TEST_CASE("Grounding leaves root motion and locomotion speed alone", "[skinning][grounding]")
{
	// Both describe how a clip travels, and a constant vertical shift changes neither -- but they
	// are measured at import before grounding runs, so nothing recomputes them afterwards.
	Rig rig;
	rig.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
	rig.Finish();

	rig.AddClip();
	rig.AddFrame(Rig::At(glm::vec3(0.0f)));
	rig.AddFrame(Rig::At(glm::vec3(9.0f, 0.0f, 0.0f)));

	rig.animations.clips[0].rootMotion      = glm::vec3(9.0f, 0.0f, 0.0f);
	rig.animations.clips[0].locomotionSpeed = 13.5f;
	rig.animations.clips[0].duration        = 0.667f;

	groundClips(rig.animations, rig.Meshes(), rig.skeleton);

	CHECK(rig.animations.clips[0].rootMotion.x == Catch::Approx(9.0f));
	CHECK(rig.animations.clips[0].rootMotion.y == Catch::Approx(0.0f));
	CHECK(rig.animations.clips[0].locomotionSpeed == Catch::Approx(13.5f));
}

TEST_CASE("A mesh with nothing skinned is left where it was authored", "[skinning][grounding]")
{
	// No bone moves these vertices, so there is no pose to ground them against and no clip whose
	// samples grounding could move. Leaving them alone is the only answer that does not invent one.
	Rig rig;
	rig.Finish();

	rig.AddClip();
	rig.AddFrame(Rig::At(glm::vec3(0.0f, 4.0f, 0.0f)));

	groundClips(rig.animations, rig.Meshes(), rig.skeleton);

	CHECK(rig.animations.clips[0].groundOffset == Catch::Approx(0.0f));
	CHECK(rig.animations.samples[0].translation.y == Catch::Approx(4.0f));
}

TEST_CASE("An authored floor overrules the measured one", "[skinning][grounding]")
{
	// The Coyote's Land is why this exists: its lowest frame is the impact compression, so the
	// measurement plants that frame and leaves the settled stance above the floor. The author names
	// the height the clip actually stands at, which is what describe prints for every clip.
	Rig rig;
	rig.AddVertex(glm::vec3(0.0f));
	rig.Finish();

	rig.AddClip();
	rig.animations.clips[0].nameOffset = rig.animations.stringPool.add("Land");
	rig.AddFrame(Rig::At(glm::vec3(0.0f, 1.0f, 0.0f)));   // the settled stance
	rig.AddFrame(Rig::At(glm::vec3(0.0f, -3.0f, 0.0f)));  // the impact, which the measurement finds

	const std::array authored = { ClipFloor{ "Land", 1.0f } };
	groundClips(rig.animations, rig.Meshes(), rig.skeleton, authored);

	CHECK(rig.LowestAt(0, 0) == Catch::Approx(0.0f).margin(1e-5));
	CHECK(rig.LowestAt(0, 1) == Catch::Approx(-4.0f));
	CHECK(rig.animations.clips[0].groundOffset == Catch::Approx(1.0f));

	SECTION("and a second pass does not re-measure what the author overruled")
	{
		// A cook grounds twice -- once against the mesh beside the clips, once against the project's
		// meshes for the rig. An overruled clip is deliberately not resting on the floor at its
		// lowest frame, so a second pass that lost the override would plant that frame and undo it.
		groundClips(rig.animations, rig.Meshes(), rig.skeleton, authored);

		CHECK(rig.LowestAt(0, 0) == Catch::Approx(0.0f).margin(1e-5));
		CHECK(rig.animations.clips[0].groundOffset == Catch::Approx(1.0f));
	}
}

TEST_CASE("An authored floor for a clip that is not there is ignored", "[skinning][grounding]")
{
	Rig rig;
	rig.AddVertex(glm::vec3(0.0f, 2.0f, 0.0f));
	rig.Finish();

	rig.AddClip();
	rig.animations.clips[0].nameOffset = rig.animations.stringPool.add("Idle");
	rig.AddFrame(Rig::At(glm::vec3(0.0f)));

	const std::array authored = { ClipFloor{ "Sprint", 99.0f } };
	groundClips(rig.animations, rig.Meshes(), rig.skeleton, authored);

	CHECK(rig.animations.clips[0].groundOffset == Catch::Approx(2.0f));
}

TEST_CASE("A rig stands on whichever of its meshes hangs lowest", "[skinning][grounding]")
{
	// A clip set is not the property of one mesh: a body and a separately imported cloak share a rig
	// and are drawn as one character, and a clips-only import has no mesh of its own at all -- it is
	// grounded against the ones already in the project. Taking the first mesh found would make the
	// floor depend on directory walk order.
	Rig body;
	body.AddVertex(glm::vec3(0.0f, 5.0f, 0.0f));
	body.Finish();
	body.AddClip();
	body.AddFrame(Rig::At(glm::vec3(0.0f)));

	Rig cloak;
	cloak.AddVertex(glm::vec3(0.0f, 2.0f, 0.0f));
	cloak.Finish();

	const std::array<BMesh, 2> meshes = { body.mesh, cloak.mesh };
	groundClips(body.animations, meshes, body.skeleton);

	CHECK(body.animations.clips[0].groundOffset == Catch::Approx(2.0f));
	CHECK(body.LowestIn(0) == Catch::Approx(3.0f));
}

TEST_CASE("A refused measurement still honours an authored floor", "[skinning][grounding]")
{
	// The two are independent: an explicit floor is a subtraction, and needs no pose measured to
	// apply. Losing every override in a file because one mesh in it will not skin would discard
	// author intent for a reason that has nothing to do with the clip they named.
	Rig rig;
	rig.AddVertex(glm::vec3(0.0f, 3.0f, 0.0f), 7);
	rig.Finish();

	rig.AddClip();
	rig.animations.clips[0].nameOffset = rig.animations.stringPool.add("Land");
	rig.AddFrame(Rig::At(glm::vec3(0.0f)));

	REQUIRE_THROWS(measureClipFloors(rig.animations, rig.Meshes(), rig.skeleton));

	const std::array authored = { ClipFloor{ "Land", 2.0f } };
	groundClips(rig.animations, rig.Meshes(), rig.skeleton, authored);

	CHECK(rig.animations.clips[0].groundOffset == Catch::Approx(2.0f));
	CHECK(rig.animations.samples[0].translation.y == Catch::Approx(-2.0f));

	SECTION("and a clip it says nothing about is left where it was authored")
	{
		rig.AddClip();
		rig.animations.clips[1].nameOffset = rig.animations.stringPool.add("Idle");
		rig.AddFrame(Rig::At(glm::vec3(0.0f, 5.0f, 0.0f)));

		groundClips(rig.animations, rig.Meshes(), rig.skeleton, authored);

		CHECK(rig.animations.clips[1].groundOffset == Catch::Approx(0.0f));
		CHECK(rig.animations.samples[1].translation.y == Catch::Approx(5.0f));
	}
}

namespace
{
	/**
	 * A hip-knee-ankle-toe chain with a sole under the ankle and a belly slung beneath the hip, so
	 * the lowest vertex is deliberately *not* a foot. That is the whole case the sole floor exists
	 * for: the test Dog is shaped this way, and grounding it on its lowest vertex left every planted
	 * foot hovering.
	 */
	struct LegRig
	{
		BMesh                       mesh;
		Skeleton                    skeleton;
		AnimationSet                animations;
		std::vector<AvatarLegChain> legs;

		static constexpr float c_SoleY  = 0.0f;
		static constexpr float c_BellyY = -0.3f;

		explicit LegRig()
		{
			const std::array<const char*, 4> names = { { "hip", "knee", "ankle", "toe" } };
			const std::array<glm::vec3, 4>   bind  = { { glm::vec3(0.0f, 2.0f, 0.0f),
				                                         glm::vec3(0.0f, 1.0f, 0.0f),
				                                         glm::vec3(0.0f, 0.2f, 0.0f),
				                                         glm::vec3(0.2f, 0.2f, 0.0f) } };

			for (uint32_t i = 0; i < 4; ++i)
			{
				const glm::vec3 parent = i == 0 ? glm::vec3(0.0f) : bind[i - 1];

				Bone bone{};
				bone.bindPose    = { bind[i] - parent,
					                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
					                 glm::vec3(1.0f) };
				bone.inverseBind = glm::translate(glm::mat4(1.0f), -bind[i]);
				bone.parent      = i == 0 ? c_InvalidIndex : i - 1;
				bone.nameOffset  = skeleton.stringPool.add(names[i]);
				skeleton.bones.push_back(bone);
			}

			// The sole: two vertices under the ankle and one under the toe, all on one plane.
			AddVertex(glm::vec3(-0.05f, c_SoleY, -0.05f), 2);
			AddVertex(glm::vec3(0.05f, c_SoleY, 0.05f), 2);
			AddVertex(glm::vec3(0.2f, c_SoleY, 0.0f), 3);

			// And the belly, hanging below every one of them off the hip.
			AddVertex(glm::vec3(0.0f, c_BellyY, 0.0f), 0);
			Finish();

			animations.boneCount         = 4;
			animations.skeletonSignature = skeletonSignature(skeleton);
			legs.push_back(
				{ .hipBoneIndex = 0, .kneeBoneIndex = 1, .ankleBoneIndex = 2, .toeBoneIndex = 3 });

			// One clip of one frame, every bone at its bind pose.
			AnimationClip clip{};
			clip.firstSample = 0;
			clip.frameCount  = 1;
			clip.sampleRate  = 30.0f;
			animations.clips.push_back(clip);
			for (const Bone& bone : skeleton.bones) animations.samples.push_back(bone.bindPose);
		}

		[[nodiscard]] std::span<const BMesh>
		Meshes() const
		{
			return std::span<const BMesh>(&mesh, 1);
		}

		/** Where the sole plane sits after whatever grounding has been applied. */
		[[nodiscard]] float
		SoleY() const
		{
			const std::vector<SolePlane> soles = solePlanes(Meshes(), skeleton, legs);
			const std::vector<glm::mat4> pose  = poseModelTransforms(skeleton, animations, 0, 0);
			return (pose[legs[0].ankleBoneIndex] * glm::vec4(soles[0].point, 1.0f)).y;
		}

	private:
		void
		AddVertex(const glm::vec3& position, uint16_t joint)
		{
			const auto write = [&](const auto& value) {
				const auto* bytes = reinterpret_cast<const std::byte*>(&value);
				mesh.vertexData.insert(mesh.vertexData.end(), bytes, bytes + sizeof(value));
			};
			write(position);
			write(std::array<uint16_t, 4>{ { joint, 0, 0, 0 } });
			write(std::array<uint16_t, 4>{ { 65535, 0, 0, 0 } });
			++m_VertexCount;
		}

		void
		Finish()
		{
			Submesh submesh{};
			submesh.layout.attributeCount = 3;
			submesh.layout.attributes[0]  = { VertexSemantic::kPosition,
				                              VertexFormat::kFloat32x3,
				                              0 };
			submesh.layout.attributes[1]  = { VertexSemantic::kJoints0,
				                              VertexFormat::kUint16x4,
				                              12 };
			submesh.layout.attributes[2]  = { VertexSemantic::kWeights0,
				                              VertexFormat::kUnorm16x4,
				                              20 };
			submesh.layout.stride         = 28;
			submesh.vertexCount           = m_VertexCount;
			submesh.vertexByteOffset      = 0;
			mesh.submeshes.push_back(submesh);
			mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });
		}

		uint32_t m_VertexCount = 0;
	};
}

TEST_CASE("A rig that authored legs is grounded on its soles", "[skinning][grounding]")
{
	SECTION("without an avatar the belly is the floor, and the foot ends up above it")
	{
		LegRig rig;
		REQUIRE(rig.SoleY() == Catch::Approx(LegRig::c_SoleY).margin(1e-5));

		groundClips(rig.animations, rig.Meshes(), rig.skeleton);

		// Moved by the belly, so the sole is left hanging by exactly the belly's clearance -- the
		// gap a plant then preserves, because it measures departure from y = 0.
		CHECK(rig.SoleY() == Catch::Approx(LegRig::c_SoleY - LegRig::c_BellyY).margin(1e-5));
		CHECK(rig.SoleY() > 0.01f);
	}

	SECTION("with the legs given the sole is the floor, and the foot rests on it")
	{
		LegRig rig;
		groundClips(rig.animations, rig.Meshes(), rig.skeleton, {}, rig.legs);

		CHECK(rig.SoleY() == Catch::Approx(0.0f).margin(1e-5));
	}

	SECTION("an authored floor still overrules the soles")
	{
		LegRig                         rig;
		const std::array<ClipFloor, 1> authored = { { ClipFloor{ .clip = "", .floor = 0.5f } } };

		groundClips(rig.animations, rig.Meshes(), rig.skeleton, authored, rig.legs);

		CHECK(rig.SoleY() == Catch::Approx(-0.5f).margin(1e-5));
	}
}
