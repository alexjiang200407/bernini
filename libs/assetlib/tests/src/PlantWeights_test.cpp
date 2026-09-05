#include <algorithm>
#include <array>
#include <assetlib/avatar.h>
#include <assetlib/codecs.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/VertexLayout.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The cook half of foot planting: the sole fitted off the mesh, and the per-frame weight measured
// off the walk. Both are derived, so what these pin is that the derivation says what a person
// looking at the clip would say -- a foot on the floor and still is planted, one in the air or
// sliding is not.

using namespace assetlib;

namespace
{
	// hip -> knee -> ankle -> toe, hanging straight down from a pelvis at y = 2. A leg and not a
	// chain of arbitrary bones, because the sole fit is over the vertices the last two carry.
	constexpr uint32_t c_Bones = 5;

	const std::array<glm::vec3, c_Bones> c_Bind = { {
		glm::vec3(0.0f, 2.0f, 0.0f),  // 0 pelvis
		glm::vec3(0.0f, 2.0f, 0.0f),  // 1 hip
		glm::vec3(0.0f, 1.0f, 0.0f),  // 2 knee
		glm::vec3(0.0f, 0.1f, 0.0f),  // 3 ankle -- the foot's own height above the sole
		glm::vec3(0.2f, 0.1f, 0.0f),  // 4 toe
	} };

	/**
	 * A leg rig whose sole sits on y = 0 at bind pose -- or two, the second the mirror of the
	 * first across z, hanging off the same pelvis, for the cases where what one foot does has to
	 * be told from what the other does.
	 */
	struct Leg
	{
		BMesh        mesh;
		Skeleton     skeleton;
		AnimationSet animations;

		explicit Leg(const bool twoLegs = false) : m_Legs(twoLegs ? 2 : 1)
		{
			for (uint32_t i = 0; i < BoneCount(); ++i)
			{
				const uint32_t  parent = ParentOf(i);
				const glm::vec3 at     = BindOf(i);
				const glm::vec3 origin =
					parent == c_InvalidIndex ? glm::vec3(0.0f) : BindOf(parent);

				auto bone     = Bone();
				bone.bindPose = { at - origin, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
				bone.inverseBind = glm::translate(glm::mat4(1.0f), -at);
				bone.parent      = parent;
				bone.nameOffset  = skeleton.stringPool.add(std::format("Bone{}", i));
				skeleton.bones.push_back(bone);
			}

			animations.boneCount         = BoneCount();
			animations.skeletonSignature = skeletonSignature(skeleton);
		}

		[[nodiscard]] uint32_t
		BoneCount() const
		{
			return 1 + 4 * m_Legs;
		}

		// Bone 0 is the pelvis; leg `l` is bones 1 + 4l .. 4 + 4l, the second mirrored in z.
		[[nodiscard]] static uint32_t
		ParentOf(const uint32_t bone)
		{
			return bone == 0 ? c_InvalidIndex : ((bone - 1) % 4 == 0 ? 0 : bone - 1);
		}

		[[nodiscard]] static glm::vec3
		BindOf(const uint32_t bone)
		{
			if (bone == 0)
				return c_Bind[0];
			const glm::vec3 at = c_Bind[1 + (bone - 1) % 4];
			return (bone - 1) / 4 == 0 ? at : glm::vec3(at.x, at.y, at.z + 0.5f);
		}

		/**
		 * A sole quad on `y`, plus one vertex up the shin so the fit has an upper half to exclude.
		 * `tilt` raises the toe end, which is what a fitted plane must see and a flat one cannot.
		 */
		void
		AddFoot(float y = 0.0f, float tilt = 0.0f)
		{
			for (uint32_t leg = 0; leg < m_Legs; ++leg)
			{
				const float z = leg == 0 ? 0.0f : 0.5f;
				for (const glm::vec3 corner : { glm::vec3(-0.1f, y, z - 0.1f),
				                                glm::vec3(-0.1f, y, z + 0.1f),
				                                glm::vec3(0.3f, y + tilt, z - 0.1f),
				                                glm::vec3(0.3f, y + tilt, z + 0.1f) })
					AddVertex(corner, static_cast<uint16_t>(3 + 4 * leg));
			}

			// Weighted to the knee, so it is neither in the fit's input nor in its output.
			AddVertex(glm::vec3(0.0f, 1.0f, 0.0f), 2);
		}

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
			auto submesh                  = Submesh();
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

			mesh.submeshes.push_back(submesh);
			mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });
		}

		void
		AddClip(std::string_view name = "clip")
		{
			auto clip        = AnimationClip();
			clip.firstSample = static_cast<uint32_t>(animations.samples.size());
			clip.frameCount  = 0;
			clip.sampleRate  = 30.0f;
			clip.nameOffset  = animations.stringPool.add(name);
			animations.clips.push_back(clip);
		}

		/** One frame with the whole rig moved by `root`; every bone keeps its bind pose. */
		void
		AddFrame(const glm::vec3& root)
		{
			AddFrame(root, glm::vec3(0.0f), glm::vec3(0.0f));
		}

		/** One frame with the rig at `root` and each leg moved on its own by its hip. */
		void
		AddFrame(const glm::vec3& root, const glm::vec3& left, const glm::vec3& right)
		{
			for (uint32_t i = 0; i < BoneCount(); ++i)
			{
				const uint32_t  parent = ParentOf(i);
				const glm::vec3 origin =
					parent == c_InvalidIndex ? glm::vec3(0.0f) : BindOf(parent);

				auto pose = Transform{ BindOf(i) - origin,
					                   glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
					                   glm::vec3(1.0f) };
				if (i == 0)
					pose.translation += root;
				else if (i == 1)
					pose.translation += left;
				else if (i == 5)
					pose.translation += right;

				animations.samples.push_back(pose);
			}
			++animations.clips.back().frameCount;
		}

		[[nodiscard]] std::span<const BMesh>
		Meshes() const
		{
			return std::span<const BMesh>(&mesh, 1);
		}

		[[nodiscard]] std::vector<AvatarLegChain>
		Chains() const
		{
			auto out = std::vector<AvatarLegChain>();
			for (uint32_t leg = 0; leg < m_Legs; ++leg)
				out.push_back({ 1 + 4 * leg, 2 + 4 * leg, 3 + 4 * leg, 4 + 4 * leg });
			return out;
		}

		/** The chains as the avatar the plant measures with; `weights` scale clips by name. */
		[[nodiscard]] ResolvedAvatar
		Plant(std::vector<ClipPlantWeight> weights = {}) const
		{
			return { Chains(), std::move(weights) };
		}

	private:
		uint32_t m_Legs        = 1;
		uint32_t m_VertexCount = 0;
	};

	/** The weights of the one leg, frame by frame, as floats. */
	std::vector<float>
	WeightsOf(const std::vector<uint8_t>& bytes)
	{
		auto out = std::vector<float>();
		for (const uint8_t byte : bytes) out.push_back(float(byte) / 255.0f);
		return out;
	}
}

TEST_CASE("A sole plane is fitted to the underside of the foot", "[skinning][plant]")
{
	Leg leg;
	leg.AddFoot();
	leg.Finish();

	const std::vector<SolePlane> soles = solePlanes(leg.Meshes(), leg.skeleton, leg.Chains());
	REQUIRE(soles.size() == 1);

	// Ankle-local: the ankle sits 0.1 above the sole at bind, so the plane is 0.1 below its origin.
	CHECK(soles[0].point.y == Catch::Approx(-0.1f).margin(1e-4));
	CHECK(soles[0].normal.x == Catch::Approx(0.0f).margin(1e-4));
	CHECK(soles[0].normal.y == Catch::Approx(1.0f).margin(1e-4));
	CHECK(soles[0].normal.z == Catch::Approx(0.0f).margin(1e-4));

	SECTION("a sole on an incline tilts the plane, which is the whole reason it is fitted")
	{
		// The pad rises 0.004 over its 0.4 of x -- within the band, so the whole of it is fitted,
		// and a plane through the lowest vertex alone would report flat and plant this foot on
		// its heel. Rising 0.004 over 0.4 is atan(0.01), and the normal leans back by that much.
		Leg tilted;
		tilted.AddFoot(0.0f, 0.004f);
		tilted.Finish();

		const std::vector<SolePlane> fitted =
			solePlanes(tilted.Meshes(), tilted.skeleton, tilted.Chains());

		CHECK(fitted[0].normal.x == Catch::Approx(-std::sin(std::atan(0.01f))).margin(1e-4));
		CHECK(fitted[0].normal.y > 0.99f);
	}

	SECTION("the instep is not the sole")
	{
		// A foot whose front rises 8 cm is not a tilted sole, it is a pad with an instep above it
		// -- and here the pad is the two heel corners, which is a line, not a plane. The fit does
		// not invent a tilt from a line: it falls back to flat through the ankle, exactly as it
		// would for a foot no mesh carries.
		Leg arched;
		arched.AddFoot(0.0f, 0.08f);
		arched.Finish();

		const std::vector<SolePlane> fitted =
			solePlanes(arched.Meshes(), arched.skeleton, arched.Chains());

		CHECK(fitted[0].normal.y == Catch::Approx(1.0f).margin(1e-4));
	}

	SECTION("a leg no mesh carries gets the flat plane through its joint, not a fit of nothing")
	{
		Leg bare;
		bare.AddVertex(glm::vec3(0.0f, 1.0f, 0.0f), 2);
		bare.Finish();

		const std::vector<SolePlane> flat = solePlanes(bare.Meshes(), bare.skeleton, bare.Chains());

		CHECK(flat[0].normal.y == Catch::Approx(1.0f).margin(1e-4));
	}
}

TEST_CASE("A foot on the floor and still is planted", "[skinning][plant]")
{
	Leg leg;
	leg.AddFoot();
	leg.Finish();

	// Eight frames standing still on the floor.
	leg.AddClip();
	for (int i = 0; i < 8; ++i) leg.AddFrame(glm::vec3(0.0f));

	const std::vector<SolePlane> soles = solePlanes(leg.Meshes(), leg.skeleton, leg.Chains());
	const std::vector<float>     w =
		WeightsOf(measurePlantWeights(leg.animations, leg.skeleton, leg.Plant(), soles));

	REQUIRE(w.size() == 8);

	// Planted throughout, ends included. The clip never lifts the foot, so there is no transition to
	// ramp against -- and a looping idle that dipped on its own last frame would put a pop exactly
	// on the loop point, which is the artefact the ramp exists to remove.
	for (size_t i = 0; i < w.size(); ++i) CHECK(w[i] == Catch::Approx(1.0f).margin(0.01));
}

TEST_CASE("A plant is ramped in and out of every transition inside a clip", "[skinning][plant]")
{
	Leg leg;
	leg.AddFoot();
	leg.Finish();

	// Airborne, down for nine frames, airborne again: two real transitions, so both ends ramp.
	leg.AddClip();
	for (int i = 0; i < 3; ++i) leg.AddFrame(glm::vec3(0.05f * float(i), 0.4f, 0.0f));
	for (int i = 0; i < 9; ++i) leg.AddFrame(glm::vec3(0.1f, 0.0f, 0.0f));
	for (int i = 0; i < 3; ++i) leg.AddFrame(glm::vec3(0.1f + 0.05f * float(i + 1), 0.4f, 0.0f));

	const std::vector<SolePlane> soles = solePlanes(leg.Meshes(), leg.skeleton, leg.Chains());
	const std::vector<float>     w =
		WeightsOf(measurePlantWeights(leg.animations, leg.skeleton, leg.Plant(), soles));

	REQUIRE(w.size() == 15);

	// The foot is on the floor over 3..11, but the run is 3..10: frame 11's slide is measured across
	// the frames either side of it, and frame 12 is already carrying the foot away. A foot on its
	// way up is not planted, which is exactly what the slide test is for.
	//
	// The ramp is three frames counted from the one the foot is not down in: nothing at 2, half
	// at 3, whole from 4 -- and the same going out. A weight, not a flag, which is the whole of
	// ADR-5's ramp.
	CHECK(w[2] == Catch::Approx(0.0f).margin(0.01));
	CHECK(w[3] == Catch::Approx(0.5f).margin(0.01));
	for (size_t i = 4; i <= 9; ++i) CHECK(w[i] == Catch::Approx(1.0f).margin(0.01));
	CHECK(w[10] == Catch::Approx(0.5f).margin(0.01));
	CHECK(w[11] == Catch::Approx(0.0f).margin(0.01));
}

TEST_CASE("A foot off the floor or sliding along it is not planted", "[skinning][plant]")
{
	const std::vector<AvatarLegChain> chains = Leg().Chains();
	const ResolvedAvatar              plant  = Leg().Plant();

	SECTION("lifted clear of the floor")
	{
		Leg leg;
		leg.AddFoot();
		leg.Finish();

		leg.AddClip();
		for (int i = 0; i < 8; ++i) leg.AddFrame(glm::vec3(0.0f, 0.5f, 0.0f));

		const std::vector<SolePlane> soles = solePlanes(leg.Meshes(), leg.skeleton, chains);
		const std::vector<uint8_t>   w =
			measurePlantWeights(leg.animations, leg.skeleton, plant, soles);

		CHECK(std::ranges::all_of(w, [](uint8_t byte) { return byte == 0; }));
	}

	SECTION("dragged along it, unlike the stance -- which height alone would call planted")
	{
		Leg leg;
		leg.AddFoot();
		leg.Finish();

		// On the floor throughout: still for six frames, then dragged 10 cm a frame. The stance
		// is the still foot -- it is most of what is on the floor -- and the drag is not it.
		leg.AddClip();
		for (int i = 0; i < 6; ++i) leg.AddFrame(glm::vec3(0.0f));
		for (int i = 1; i <= 2; ++i) leg.AddFrame(glm::vec3(0.1f * float(i), 0.0f, 0.0f));

		const std::vector<SolePlane> soles = solePlanes(leg.Meshes(), leg.skeleton, chains);
		const std::vector<float>     w =
			WeightsOf(measurePlantWeights(leg.animations, leg.skeleton, plant, soles));

		// Frame 5 is where the motion is measured to have begun -- its window reaches the first
		// dragged frame -- so the run is 0..4, ramping out at its end.
		CHECK(w[2] == Catch::Approx(1.0f).margin(0.01));
		CHECK(w[4] == Catch::Approx(0.5f).margin(0.01));
		for (size_t i = 5; i < 8; ++i) CHECK(w[i] == Catch::Approx(0.0f).margin(0.01));
	}

	SECTION("sliding uniformly is the stance of a clip played in place, and is planted")
	{
		// A clip played in place keeps its root still and slides the standing foot back under it
		// at the stride, which is what a game plays. Every floor-level sample moves alike, so that
		// motion *is* the stance, and a foot moving with it is down. Stillness was the first rule
		// here, and planted nothing in any such clip.
		Leg leg;
		leg.AddFoot();
		leg.Finish();

		leg.AddClip();
		for (int i = 0; i < 8; ++i) leg.AddFrame(glm::vec3(-0.1f * float(i), 0.0f, 0.0f));

		const std::vector<SolePlane> soles = solePlanes(leg.Meshes(), leg.skeleton, chains);
		const std::vector<float>     w =
			WeightsOf(measurePlantWeights(leg.animations, leg.skeleton, plant, soles));

		for (size_t i = 0; i < 8; ++i) CHECK(w[i] == Catch::Approx(1.0f).margin(0.01));
	}

	SECTION("two feet that disagree do not average into a motion neither made")
	{
		// Two legs, both on the floor, one moving along x and the other along z. A per-axis
		// median over the pool of both takes its x from one and its z from the other and names a
		// diagonal no foot moves along -- and then neither foot matches it, and nothing plants.
		// The stance is a motion some foot made, and that foot is planted.
		Leg both(true);
		both.AddFoot();
		both.Finish();

		both.AddClip();
		for (int i = 0; i < 8; ++i)
			both.AddFrame(
				glm::vec3(0.0f),
				glm::vec3(0.05f * float(i), 0.0f, 0.0f),
				glm::vec3(0.0f, 0.0f, 0.05f * float(i)));

		const std::vector<SolePlane> soles =
			solePlanes(both.Meshes(), both.skeleton, both.Chains());
		const std::vector<float> w =
			WeightsOf(measurePlantWeights(both.animations, both.skeleton, both.Plant(), soles));

		REQUIRE(w.size() == 16);
		const bool leftPlanted  = w[2 * 3 + 0] == Catch::Approx(1.0f).margin(0.01);
		const bool rightPlanted = w[2 * 3 + 1] == Catch::Approx(1.0f).margin(0.01);
		CHECK(leftPlanted != rightPlanted);
	}

	SECTION("hovering at its stillest is not standing")
	{
		// A rig sitting with its feet off the ground: the lowest a sole gets is where it hovers,
		// and a floor that far above the ground the clip was rested on is no floor at all.
		Leg leg;
		leg.AddFoot();
		leg.Finish();

		leg.AddClip();
		for (int i = 0; i < 8; ++i) leg.AddFrame(glm::vec3(0.0f, 0.2f, 0.0f));

		const std::vector<SolePlane> soles = solePlanes(leg.Meshes(), leg.skeleton, chains);
		const std::vector<uint8_t>   w =
			measurePlantWeights(leg.animations, leg.skeleton, plant, soles);

		CHECK(std::ranges::all_of(w, [](uint8_t byte) { return byte == 0; }));
	}

	SECTION("a standing foot lifted by a toe that dipped through the floor still plants")
	{
		// What groundClips does to a walk: the clip's lowest vertex is a toe tip punching through
		// mid-swing, so resting that on zero leaves the standing foot a few centimetres up. The
		// floor the plant measures against is the clip's own lowest sole, so it is found anyway.
		Leg leg;
		leg.AddFoot();
		leg.Finish();

		leg.AddClip();
		for (int i = 0; i < 8; ++i) leg.AddFrame(glm::vec3(0.0f, 0.07f, 0.0f));

		const std::vector<SolePlane> soles = solePlanes(leg.Meshes(), leg.skeleton, chains);
		const std::vector<float>     w =
			WeightsOf(measurePlantWeights(leg.animations, leg.skeleton, plant, soles));

		for (size_t i = 0; i < 8; ++i) CHECK(w[i] == Catch::Approx(1.0f).margin(0.01));
	}

	SECTION("a foot standing higher than the other stands at its own floor")
	{
		// A cocked pelvis: the Dog idles with its right foot three centimetres above its left,
		// every frame. Judged against the left foot's floor the right never plants, and on a
		// slope one foot follows the ground while the other hangs where the clip left it.
		Leg both(true);
		both.AddFoot();
		both.Finish();

		both.AddClip();
		for (int i = 0; i < 8; ++i)
			both.AddFrame(glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.03f, 0.0f));

		const std::vector<SolePlane> soles =
			solePlanes(both.Meshes(), both.skeleton, both.Chains());
		const std::vector<float> w =
			WeightsOf(measurePlantWeights(both.animations, both.skeleton, both.Plant(), soles));

		REQUIRE(w.size() == 16);
		for (size_t i = 0; i < 16; ++i) CHECK(w[i] == Catch::Approx(1.0f).margin(0.01));
	}

	SECTION("a foot held well above the other is up, not standing higher")
	{
		// A knee held up through a crouch: the foot's own lowest is where it hovers, and a floor
		// that far above the standing foot's is no floor.
		Leg both(true);
		both.AddFoot();
		both.Finish();

		both.AddClip();
		for (int i = 0; i < 8; ++i)
			both.AddFrame(glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.08f, 0.0f));

		const std::vector<SolePlane> soles =
			solePlanes(both.Meshes(), both.skeleton, both.Chains());
		const std::vector<float> w =
			WeightsOf(measurePlantWeights(both.animations, both.skeleton, both.Plant(), soles));

		REQUIRE(w.size() == 16);
		for (size_t i = 0; i < 8; ++i)
		{
			CHECK(w[2 * i + 0] == Catch::Approx(1.0f).margin(0.01));
			CHECK(w[2 * i + 1] == Catch::Approx(0.0f).margin(0.01));
		}
	}
}

TEST_CASE("A step plants only while the foot is down", "[skinning][plant]")
{
	Leg leg;
	leg.AddFoot();
	leg.Finish();

	// Down for eight frames, then lifted and carried forward for four.
	leg.AddClip();
	for (int i = 0; i < 8; ++i) leg.AddFrame(glm::vec3(0.0f));
	for (int i = 0; i < 4; ++i) leg.AddFrame(glm::vec3(0.05f * float(i + 1), 0.4f, 0.0f));

	const std::vector<SolePlane> soles = solePlanes(leg.Meshes(), leg.skeleton, leg.Chains());
	const std::vector<float>     w =
		WeightsOf(measurePlantWeights(leg.animations, leg.skeleton, leg.Plant(), soles));

	REQUIRE(w.size() == 12);

	// The run starts at the clip's own edge, so it is fully planted from frame 0 -- there is no
	// touchdown in this clip to ramp against.
	CHECK(w[0] == Catch::Approx(1.0f).margin(0.01));
	CHECK(w[4] == Catch::Approx(1.0f).margin(0.01));

	// It ends at 6, not 7: frame 7's slide is measured across the frames either side of it, and
	// frame 8 is already carrying the foot forward. That lift is a real transition, so it ramps out
	// -- whole at 5, half at 6, nothing from 7 -- which is the three frames counted from the one
	// the foot is no longer down in.
	CHECK(w[5] == Catch::Approx(1.0f).margin(0.01));
	CHECK(w[6] == Catch::Approx(0.5f).margin(0.01));
	for (size_t i = 7; i < w.size(); ++i) CHECK(w[i] == Catch::Approx(0.0f).margin(0.01));
}

TEST_CASE("Plant weights are addressed frame-major over the whole pool", "[skinning][plant]")
{
	// Two clips, so a weight read through a clip's firstSample has to land in that clip's own run.
	Leg leg;
	leg.AddFoot();
	leg.Finish();

	leg.AddClip();
	for (int i = 0; i < 4; ++i) leg.AddFrame(glm::vec3(0.0f, 0.5f, 0.0f));

	leg.AddClip();
	for (int i = 0; i < 8; ++i) leg.AddFrame(glm::vec3(0.0f));

	const std::vector<SolePlane> soles = solePlanes(leg.Meshes(), leg.skeleton, leg.Chains());
	const std::vector<float>     w =
		WeightsOf(measurePlantWeights(leg.animations, leg.skeleton, leg.Plant(), soles));

	REQUIRE(w.size() == 12);

	// The airborne clip is entirely unplanted, and the standing one is planted from its own first
	// frame -- runs are found per clip, so neither reaches into the other's frames.
	for (size_t i = 0; i < 4; ++i) CHECK(w[i] == Catch::Approx(0.0f).margin(0.01));
	for (size_t i = 4; i < 12; ++i) CHECK(w[i] == Catch::Approx(1.0f).margin(0.01));
}

TEST_CASE("A leg naming a bone the rig does not carry is refused", "[skinning][plant]")
{
	Leg leg;
	leg.AddFoot();
	leg.Finish();

	leg.AddClip();
	leg.AddFrame(glm::vec3(0.0f));

	const std::vector<AvatarLegChain> bad = { AvatarLegChain{ 1, 2, c_Bones, c_Bones } };

	CHECK_THROWS(solePlanes(leg.Meshes(), leg.skeleton, bad));

	// Judged here too, and not left to the pose walk: this indexes the ankle straight into the
	// pose, so a caller that skipped solePlanes would read past the end of it.
	const std::vector<SolePlane> soles = { SolePlane{ glm::vec3(0.0f),
		                                              glm::vec3(0.0f, 1.0f, 0.0f) } };
	CHECK_THROWS(
		measurePlantWeights(leg.animations, leg.skeleton, ResolvedAvatar{ .legs = bad }, soles));
}

TEST_CASE(
	"Baked plant weights are found only for the pairing they were measured on",
	"[skinning][plant]")
{
	Leg leg;
	leg.AddFoot();
	leg.Finish();

	leg.AddClip();
	for (int i = 0; i < 8; ++i) leg.AddFrame(glm::vec3(0.0f));

	const ResolvedAvatar plant = leg.Plant();

	bakePlantWeights(leg.animations, leg.Meshes(), leg.skeleton, plant);

	REQUIRE_FALSE(leg.animations.plantWeights.Empty());
	CHECK(leg.animations.plantWeights.legCount == 1);

	const std::optional<std::vector<uint8_t>> found =
		findPlantWeights(leg.animations, leg.Meshes(), leg.skeleton, plant);
	REQUIRE(found.has_value());
	CHECK(*found == leg.animations.plantWeights.weights);

	SECTION("a chain edited in the avatar is measured afresh")
	{
		const std::vector<AvatarLegChain> other = { AvatarLegChain{ 0, 1, 2, 3 } };
		CHECK_FALSE(findPlantWeights(
						leg.animations,
						leg.Meshes(),
						leg.skeleton,
						ResolvedAvatar{ .legs = other })
		                .has_value());
	}

	SECTION("a mesh whose vertices moved is measured afresh")
	{
		Leg moved;
		moved.AddFoot(0.05f);
		moved.Finish();

		CHECK_FALSE(
			findPlantWeights(leg.animations, moved.Meshes(), leg.skeleton, plant).has_value());
	}

	SECTION("a clip taken out of the plant is measured afresh, and at zero")
	{
		// The weights are the avatar's, so they key the bytes like a leg does. Nothing for an
		// empty list, so a file baked before the key existed is still current.
		const ResolvedAvatar none = leg.Plant({ { "clip", 0.0f } });
		CHECK_FALSE(findPlantWeights(leg.animations, leg.Meshes(), leg.skeleton, none).has_value());

		bakePlantWeights(leg.animations, leg.Meshes(), leg.skeleton, none);
		for (const uint8_t w : leg.animations.plantWeights.weights) CHECK(w == 0);
	}

	SECTION("a clip weighted down is measured afresh, and scaled")
	{
		// A weight keys the bytes as a name does: the same clip at half is a different file.
		const ResolvedAvatar half = leg.Plant({ { "clip", 0.5f } });
		CHECK_FALSE(findPlantWeights(leg.animations, leg.Meshes(), leg.skeleton, half).has_value());

		bakePlantWeights(leg.animations, leg.Meshes(), leg.skeleton, half);
		for (const uint8_t w : leg.animations.plantWeights.weights) CHECK(w == 128);
	}

	SECTION("a rig with no avatar bakes nothing, and nothing is found for it")
	{
		bakePlantWeights(leg.animations, leg.Meshes(), leg.skeleton, ResolvedAvatar());

		CHECK(leg.animations.plantWeights.Empty());
		CHECK_FALSE(findPlantWeights(leg.animations, leg.Meshes(), leg.skeleton, ResolvedAvatar())
		                .has_value());
	}

	SECTION("they round-trip through the .banim")
	{
		leg.animations.skeleton = "Derived/Skeletons/leg.bskel";

		const AnimationSet read = AssetCodec<AnimationSet>::Deserialize(
			AssetCodec<AnimationSet>::Serialize(leg.animations));

		CHECK(read.plantWeights == leg.animations.plantWeights);
	}
}
