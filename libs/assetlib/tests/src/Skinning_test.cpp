#include <algorithm>
#include <array>
#include <assetlib/avatar.h>
#include <assetlib/bmesh.h>
#include <assetlib/codecs.h>
#include <assetlib/skinning.h>
#include <assetlib/vertex_layout.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/VertexLayout.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <core/hash.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace assetlib;

namespace
{
	constexpr uint16_t c_Unorm16Max = std::numeric_limits<uint16_t>::max();

	/** A quantized weight, written exactly as the importer writes one. */
	uint16_t
	Quantize(float weight) noexcept
	{
		return static_cast<uint16_t>(std::lround(std::clamp(weight, 0.0f, 1.0f) * c_Unorm16Max));
	}

	/**
	 * A submesh whose vertices carry position, normal, joints and weights, interleaved in that
	 * order -- the layout the importer produces for a skinned primitive.
	 */
	struct SkinnedMesh
	{
		BMesh   mesh;
		Submesh submesh{};  // POD: value-initialised, or vertexCount is whatever the stack held

		SkinnedMesh()
		{
			submesh.layout.attributeCount = 4;
			submesh.layout.attributes[0]  = { VertexSemantic::kPosition,
				                              VertexFormat::kFloat32x3,
				                              0 };
			submesh.layout.attributes[1]  = { VertexSemantic::kNormal,
				                              VertexFormat::kFloat32x3,
				                              12 };
			submesh.layout.attributes[2]  = { VertexSemantic::kJoints0,
				                              VertexFormat::kUint16x4,
				                              24 };
			submesh.layout.attributes[3]  = { VertexSemantic::kWeights0,
				                              VertexFormat::kUnorm16x4,
				                              32 };
			submesh.layout.stride         = 40;
		}

		void
		Add(const glm::vec3&               position,
		    const glm::vec3&               normal,
		    const std::array<uint16_t, 4>& joints,
		    const std::array<uint16_t, 4>& weights)
		{
			const size_t base = mesh.vertexData.size();
			mesh.vertexData.resize(base + submesh.layout.stride);

			std::byte* at = mesh.vertexData.data() + base;
			std::memcpy(at, &position, sizeof(position));
			std::memcpy(at + 12, &normal, sizeof(normal));
			std::memcpy(at + 24, joints.data(), joints.size() * sizeof(uint16_t));
			std::memcpy(at + 32, weights.data(), weights.size() * sizeof(uint16_t));

			++submesh.vertexCount;
		}
	};

}

// The gate the bake depends on: skinned by identity, a vertex is exactly where it was authored. Any
// drift here is baked into every frame of every clip and is invisible afterwards.
TEST_CASE("A bind-pose skin reproduces the source vertices exactly", "[skinning]")
{
	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(1.0f, 2.0f, 3.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		{ { 0, 0, 0, 0 } },
		{ { c_Unorm16Max, 0, 0, 0 } });
	fixture.Add(
		glm::vec3(-4.0f, 0.5f, 7.25f),
		glm::vec3(1.0f, 0.0f, 0.0f),
		{ { 1, 0, 0, 0 } },
		{ { c_Unorm16Max, 0, 0, 0 } });

	const std::vector<glm::mat4> identity(2, glm::mat4(1.0f));
	const auto                   skinned = skinSubmesh(fixture.mesh, fixture.submesh, identity);

	REQUIRE(skinned.size() == 2);
	CHECK(skinned[0].position.x == Catch::Approx(1.0f));
	CHECK(skinned[0].position.y == Catch::Approx(2.0f));
	CHECK(skinned[0].position.z == Catch::Approx(3.0f));
	CHECK(skinned[1].position.x == Catch::Approx(-4.0f));
	CHECK(skinned[1].position.z == Catch::Approx(7.25f));
	CHECK(skinned[0].blendedNormal.y == Catch::Approx(1.0f));
}

// End to end through the real entry points, on a rig whose rest pose is not the identity -- the
// inverse bind is what has to cancel, and an identity bind pose would hide a missing one.
TEST_CASE("A rest-pose frame of a clip skins a mesh to itself", "[skinning][pose]")
{
	Skeleton skeleton;

	Bone root{};
	root.bindPose   = { glm::vec3(0.0f, 3.0f, 0.0f),
		                glm::angleAxis(glm::radians(25.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
		                glm::vec3(1.5f) };
	root.parent     = c_InvalidIndex;
	root.nameOffset = skeleton.stringPool.add("root");
	skeleton.bones.push_back(root);

	skeleton.bones[0].inverseBind = glm::inverse(bindPoseModelTransforms(skeleton)[0]);

	AnimationSet animations;
	animations.boneCount         = 1;
	animations.skeletonSignature = skeletonSignature(skeleton);
	animations.samples           = { skeleton.bones[0].bindPose };

	AnimationClip clip{};
	clip.nameOffset = animations.stringPool.add("rest");
	clip.frameCount = 1;
	clip.sampleRate = 30.0f;
	animations.clips.push_back(clip);

	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(2.0f, -1.0f, 0.5f),
		glm::vec3(0.0f, 0.0f, 1.0f),
		{ { 0, 0, 0, 0 } },
		{ { c_Unorm16Max, 0, 0, 0 } });

	const auto skinning =
		skinningMatrices(skeleton, poseModelTransforms(skeleton, animations, 0, 0));
	const auto skinned = skinSubmesh(fixture.mesh, fixture.submesh, skinning);

	REQUIRE(skinned.size() == 1);
	CHECK(skinned[0].position.x == Catch::Approx(2.0f).margin(1e-5));
	CHECK(skinned[0].position.y == Catch::Approx(-1.0f).margin(1e-5));
	CHECK(skinned[0].position.z == Catch::Approx(0.5f).margin(1e-5));
}

// Weights are renormalized to sum 1 before quantizing, and a unorm16 round trip must not lose that:
// a vertex whose shares no longer sum to 1 shrinks toward the origin under blending, which reads as
// a mesh that deflates where two bones meet.
TEST_CASE("Quantized weights still sum to one through the decode", "[skinning]")
{
	SkinnedMesh fixture;

	// Deliberately not thirds: 65535 = 3 x 5 x 17 x 257, so a third quantizes exactly and three of
	// them sum to exactly 65535 -- the one split that cannot fail. Tenths do not divide it, so each
	// rounds and the sum is only 1 to within the residue four roundings can leave.
	fixture.Add(
		glm::vec3(0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		{ { 1, 1, 1, 1 } },
		{ { Quantize(0.1f), Quantize(0.2f), Quantize(0.3f), Quantize(0.4f) } });

	// Every influence is the same bone, which moves the vertex 12 along X. The result is therefore
	// 12 times the decoded weights' sum, so the coordinate *is* that sum, scaled -- anything short
	// of 12 is a vertex that would drift toward the origin under blending.
	std::vector<glm::mat4> skinning(2, glm::mat4(1.0f));
	skinning[1] = glm::translate(glm::mat4(1.0f), glm::vec3(12.0f, 0.0f, 0.0f));

	const auto skinned = skinSubmesh(fixture.mesh, fixture.submesh, skinning);

	// Within four roundings of unorm16, and no looser -- 1e-3 would also pass a decode that divided
	// by 65536 instead of 65535.
	REQUIRE(skinned.size() == 1);
	CHECK(skinned[0].position.x == Catch::Approx(12.0f).margin(12.0f * 2.0f / 65535.0f));

	SECTION("and a whole-weight vertex lands exactly on its bone")
	{
		SkinnedMesh single;
		single.Add(
			glm::vec3(0.0f),
			glm::vec3(0.0f),
			{ { 1, 0, 0, 0 } },
			{ { c_Unorm16Max, 0, 0, 0 } });

		const auto only = skinSubmesh(single.mesh, single.submesh, skinning);
		CHECK(only[0].position.x == Catch::Approx(12.0f));
	}
}

TEST_CASE("Two bones blend a vertex between them", "[skinning]")
{
	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		{ { 0, 1, 0, 0 } },
		{ { Quantize(0.25f), Quantize(0.75f), 0, 0 } });

	std::vector<glm::mat4> skinning(2, glm::mat4(1.0f));
	skinning[1] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 4.0f, 0.0f));

	const auto skinned = skinSubmesh(fixture.mesh, fixture.submesh, skinning);
	CHECK(skinned[0].position.y == Catch::Approx(3.0f).margin(1e-3));
}

// A posed tangent is reconstructed from the posed normal plus a twist, and the
// twist is measured against this: a tangent that did not follow its bone would bake as a twist that
// is not there.
TEST_CASE("A tangent rides its bone's rotation, and a mesh without one skins to zero", "[skinning]")
{
	SECTION("with a tangent attribute")
	{
		BMesh   mesh;
		Submesh submesh{};
		submesh.layout.attributeCount = 5;
		submesh.layout.attributes[0]  = { VertexSemantic::kPosition, VertexFormat::kFloat32x3, 0 };
		submesh.layout.attributes[1]  = { VertexSemantic::kNormal, VertexFormat::kFloat32x3, 12 };
		submesh.layout.attributes[2]  = { VertexSemantic::kJoints0, VertexFormat::kUint16x4, 24 };
		submesh.layout.attributes[3]  = { VertexSemantic::kWeights0, VertexFormat::kUnorm16x4, 32 };
		submesh.layout.attributes[4]  = { VertexSemantic::kTangent, VertexFormat::kFloat32x4, 40 };
		submesh.layout.stride         = 56;
		submesh.vertexCount           = 1;

		const glm::vec3               position(0.0f);
		const glm::vec3               normal(0.0f, 0.0f, 1.0f);
		const std::array<uint16_t, 4> joints  = { { 0, 0, 0, 0 } };
		const std::array<uint16_t, 4> weights = { { c_Unorm16Max, 0, 0, 0 } };
		const glm::vec4               tangent(1.0f, 0.0f, 0.0f, -1.0f);

		mesh.vertexData.resize(56);
		std::byte* at = mesh.vertexData.data();
		std::memcpy(at, &position, sizeof(position));
		std::memcpy(at + 12, &normal, sizeof(normal));
		std::memcpy(at + 24, joints.data(), sizeof(joints));
		std::memcpy(at + 32, weights.data(), sizeof(weights));
		std::memcpy(at + 40, &tangent, sizeof(tangent));

		// A quarter turn about the normal: the normal stays put and only the tangent can show it.
		const std::vector<glm::mat4> skinning(
			1,
			glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f)));

		const auto skinned = skinSubmesh(mesh, submesh, skinning);
		REQUIRE(skinned.size() == 1);
		CHECK(skinned[0].blendedNormal.z == Catch::Approx(1.0f));
		CHECK(skinned[0].blendedTangent.x == Catch::Approx(0.0f).margin(1e-6));
		CHECK(skinned[0].blendedTangent.y == Catch::Approx(1.0f));
		CHECK(skinned[0].blendedTangent.z == Catch::Approx(0.0f).margin(1e-6));
	}

	SECTION("without one")
	{
		SkinnedMesh fixture;
		fixture.Add(
			glm::vec3(0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			{ { 0, 0, 0, 0 } },
			{ { c_Unorm16Max, 0, 0, 0 } });

		const std::vector<glm::mat4> skinning(
			2,
			glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f)));

		const auto skinned = skinSubmesh(fixture.mesh, fixture.submesh, skinning);
		REQUIRE(skinned.size() == 1);
		CHECK(skinned[0].blendedTangent == glm::vec3(0.0f));
	}
}

TEST_CASE("A submesh with no joints comes through unskinned", "[skinning]")
{
	BMesh   mesh;
	Submesh submesh{};
	submesh.layout.attributeCount = 1;
	submesh.layout.attributes[0]  = { VertexSemantic::kPosition, VertexFormat::kFloat32x3, 0 };
	submesh.layout.stride         = 12;
	submesh.vertexCount           = 1;

	const glm::vec3 position(9.0f, 8.0f, 7.0f);
	mesh.vertexData.resize(12);
	std::memcpy(mesh.vertexData.data(), &position, sizeof(position));

	// A static attachment on a rigged mesh reaches here; skinning it by the pose would move it away
	// from where it was authored, which the identity matrices below would hide.
	const std::vector<glm::mat4> skinning(2, glm::translate(glm::mat4(1.0f), glm::vec3(100.0f)));
	const auto                   skinned = skinSubmesh(mesh, submesh, skinning);

	REQUIRE(skinned.size() == 1);
	CHECK(skinned[0].position.x == Catch::Approx(9.0f));
	CHECK(skinned[0].blendedNormal == glm::vec3(0.0f));
}

// An exporter writes (0,0,0,0) for a vertex it never assigned, and the importer renormalizes that
// to four zeros rather than refusing the mesh. Blending them would land the vertex on the origin --
// and since the bake fits one AABB around every clip of a rig, one such vertex drags that box out
// and costs precision on every other vertex of every frame.
TEST_CASE("A vertex with no influences stays where it was authored", "[skinning]")
{
	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(3.0f, 4.0f, 5.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		{ { 0, 0, 0, 0 } },
		{ { 0, 0, 0, 0 } });

	const std::vector<glm::mat4> skinning(
		2,
		glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, 0.0f)));

	const auto skinned = skinSubmesh(fixture.mesh, fixture.submesh, skinning);

	REQUIRE(skinned.size() == 1);
	CHECK(skinned[0].position.x == Catch::Approx(3.0f));
	CHECK(skinned[0].position.y == Catch::Approx(4.0f));
	CHECK(skinned[0].position.z == Catch::Approx(5.0f));
	CHECK(skinned[0].blendedNormal.y == Catch::Approx(1.0f));
}

TEST_CASE("Skinning refuses a submesh it cannot read", "[skinning]")
{
	SkinnedMesh fixture;
	fixture
		.Add(glm::vec3(0.0f), glm::vec3(0.0f), { { 0, 0, 0, 0 } }, { { c_Unorm16Max, 0, 0, 0 } });

	const std::vector<glm::mat4> skinning(2, glm::mat4(1.0f));

	SECTION("vertices past the end of the pool")
	{
		Submesh beyond     = fixture.submesh;
		beyond.vertexCount = 4;
		CHECK_THROWS_AS(skinSubmesh(fixture.mesh, beyond, skinning), std::runtime_error);
	}

	SECTION("a joint index the pose does not hold")
	{
		const std::vector<glm::mat4> tooFew(0);
		CHECK_THROWS_AS(skinSubmesh(fixture.mesh, fixture.submesh, tooFew), std::runtime_error);
	}

	// Indices with no shares, or shares naming no bone: either half alone is unusable, and silently
	// treating it as static would bake a limb that never moves.
	SECTION("half a skin")
	{
		Submesh halved               = fixture.submesh;
		halved.layout.attributeCount = 3;
		CHECK_THROWS_AS(skinSubmesh(fixture.mesh, halved, skinning), std::runtime_error);
	}

	// The span check bounds whole vertices, not the attributes inside one, so a layout claiming an
	// attribute past its own stride would read off the end of the last vertex.
	SECTION("an attribute that extends past the stride")
	{
		Submesh overhanging                     = fixture.submesh;
		overhanging.layout.attributes[3].offset = 36;  // unorm16x4 needs 8, stride is 40
		CHECK_THROWS_AS(skinSubmesh(fixture.mesh, overhanging, skinning), std::runtime_error);
	}

	SECTION("an attribute encoded in a format this does not decode")
	{
		Submesh repacked                     = fixture.submesh;
		repacked.layout.attributes[0].format = VertexFormat::kFloat32x2;
		CHECK_THROWS_AS(skinSubmesh(fixture.mesh, repacked, skinning), std::runtime_error);
	}

	SECTION("no position at all")
	{
		Submesh positionless              = fixture.submesh;
		positionless.layout.attributes[0] = { VertexSemantic::kNormal,
			                                  VertexFormat::kFloat32x3,
			                                  0 };
		CHECK_THROWS_AS(skinSubmesh(fixture.mesh, positionless, skinning), std::runtime_error);
	}
}

TEST_CASE("posedBounds measures the pose, not the bind pose", "[skinning][bounds]")
{
	// One bone, two vertices at opposite corners of a unit box, both welded to it.
	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(-1.0f),
		glm::vec3(0.0f, 0.0f, 1.0f),
		{ { 0, 0, 0, 0 } },
		{ { 65535, 0, 0, 0 } });
	fixture.Add(
		glm::vec3(1.0f),
		glm::vec3(0.0f, 0.0f, 1.0f),
		{ { 0, 0, 0, 0 } },
		{ { 65535, 0, 0, 0 } });

	fixture.submesh.aabbMin = glm::vec3(-1.0f);
	fixture.submesh.aabbMax = glm::vec3(1.0f);
	fixture.mesh.submeshes.push_back(fixture.submesh);
	fixture.mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });

	auto skeleton = assetlib::Skeleton();

	auto bone        = assetlib::Bone();
	bone.bindPose    = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
	bone.inverseBind = glm::mat4(1.0f);
	bone.parent      = assetlib::c_InvalidIndex;
	bone.nameOffset  = 0;
	skeleton.bones.push_back(bone);

	auto animations              = assetlib::AnimationSet();
	animations.boneCount         = 1;
	animations.skeletonSignature = assetlib::skeletonSignature(skeleton);

	auto clip        = assetlib::AnimationClip();
	clip.firstSample = 0;
	clip.frameCount  = 2;
	clip.sampleRate  = 30.0f;
	animations.clips.push_back(clip);

	const auto still =
		assetlib::Transform{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };

	SECTION("a clip that scales the root far past its bind pose")
	{
		// A bind-pose box is no use for framing a camera or sizing a culling volume once a clip
		// leaves it, and a root scale is the bluntest way to leave it.
		animations.samples.push_back(still);
		animations.samples.push_back(
			{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(100.0f) });

		const assetlib::Bounds bounds =
			assetlib::posedBounds(fixture.mesh, 0, skeleton, animations);

		CHECK(bounds.min.x == Catch::Approx(-100.0f));
		CHECK(bounds.max.y == Catch::Approx(100.0f));
	}

	SECTION("a vertex with no influences is bounded where it was authored")
	{
		// The bind position, not the origin. A vertex whose weights are all zero -- what an exporter
		// writes for one it never assigned -- belongs to no bone's box, so anything that blended it
		// would drag the box to (0,0,0) and cost every other vertex precision. skinSubmesh's own
		// path has this pinned; this is the same rule through the bounds walk.
		SkinnedMesh loose;
		loose.Add(
			glm::vec3(7.0f, 8.0f, 9.0f),
			glm::vec3(0.0f, 0.0f, 1.0f),
			{ { 0, 0, 0, 0 } },
			{ { 0, 0, 0, 0 } });  // never assigned
		loose.submesh.aabbMin = glm::vec3(7.0f, 8.0f, 9.0f);
		loose.submesh.aabbMax = glm::vec3(7.0f, 8.0f, 9.0f);
		loose.mesh.submeshes.push_back(loose.submesh);
		loose.mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });

		// A pose that would move it a long way if it were influenced at all.
		animations.samples.push_back(still);
		animations.samples.push_back(
			{ glm::vec3(500.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });

		const assetlib::Bounds bounds = assetlib::posedBounds(loose.mesh, 0, skeleton, animations);

		CHECK(bounds.min.x == Catch::Approx(7.0f));
		CHECK(bounds.max.x == Catch::Approx(7.0f));
		CHECK(bounds.max.z == Catch::Approx(9.0f));
	}

	SECTION("a clip that travels is bounded where it travels to")
	{
		animations.samples.push_back(still);
		animations.samples.push_back(
			{ glm::vec3(50.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });

		const assetlib::Bounds bounds =
			assetlib::posedBounds(fixture.mesh, 0, skeleton, animations);

		// Frame 0 puts the vertices on [-1, 1]; frame 1 slides them to [49, 51].
		CHECK(bounds.min.x == Catch::Approx(-1.0f));
		CHECK(bounds.max.x == Catch::Approx(51.0f));
	}

	SECTION("a rig whose clips never move it is bounded exactly by its vertices")
	{
		animations.samples.push_back(still);
		animations.samples.push_back(still);

		const assetlib::Bounds bounds =
			assetlib::posedBounds(fixture.mesh, 0, skeleton, animations);

		// A bone whose pose is the identity sweeps its own box back onto the vertices it was built
		// from, so nothing is lost to the axis-aligned round trip here.
		CHECK(bounds.min.x == Catch::Approx(-1.0f));
		CHECK(bounds.max.x == Catch::Approx(1.0f));
	}
}

// The property the whole bake rests on: a box that misses a vertex culls a limb that is on screen.
// posedBounds never measures one, so containment is asserted against exactPosedBounds, which does.
TEST_CASE("The posed box holds every vertex the exact walk finds", "[skinning][bounds]")
{
	// Two bones along X, and a strip of vertices spanning both -- the ends welded to one bone each
	// and the middle split, which is where a per-bone box is loosest.
	Skeleton skeleton;

	Bone root{};
	root.bindPose   = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
	root.parent     = c_InvalidIndex;
	root.nameOffset = skeleton.stringPool.add("root");
	skeleton.bones.push_back(root);

	Bone elbow{};
	elbow.bindPose   = { glm::vec3(2.0f, 0.0f, 0.0f),
		                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		                 glm::vec3(1.0f) };
	elbow.parent     = 0;
	elbow.nameOffset = skeleton.stringPool.add("elbow");
	skeleton.bones.push_back(elbow);

	const std::vector<glm::mat4> bind = bindPoseModelTransforms(skeleton);
	for (size_t i = 0; i < skeleton.bones.size(); ++i)
		skeleton.bones[i].inverseBind = glm::inverse(bind[i]);

	// Thirds, not halves: 65535 is odd, so two quantized halves sum to 1.0000305 and the exact walk
	// blends fractionally past the hull the box is built to hold. Thirds divide 65535 exactly, which
	// leaves containment a property of the algorithm rather than of the residue.
	const auto nearWeight = Quantize(1.0f / 3.0f);
	const auto farWeight  = Quantize(2.0f / 3.0f);

	SkinnedMesh fixture;
	for (const float y : { -0.5f, 0.5f })
	{
		fixture.Add(
			glm::vec3(0.0f, y, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			{ { 0, 0, 0, 0 } },
			{ { c_Unorm16Max, 0, 0, 0 } });
		fixture.Add(
			glm::vec3(2.0f, y, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			{ { 0, 1, 0, 0 } },
			{ { nearWeight, farWeight, 0, 0 } });
		fixture.Add(
			glm::vec3(4.0f, y, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			{ { 1, 0, 0, 0 } },
			{ { c_Unorm16Max, 0, 0, 0 } });
	}

	fixture.submesh.aabbMin = glm::vec3(0.0f, -0.5f, 0.0f);
	fixture.submesh.aabbMax = glm::vec3(4.0f, 0.5f, 0.0f);
	fixture.mesh.submeshes.push_back(fixture.submesh);
	fixture.mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });

	AnimationSet animations;
	animations.boneCount         = 2;
	animations.skeletonSignature = skeletonSignature(skeleton);

	AnimationClip clip{};
	clip.nameOffset = animations.stringPool.add("swing");
	clip.sampleRate = 30.0f;

	const auto holds = [](const Bounds& box, const Bounds& inner) {
		return glm::all(glm::lessThanEqual(box.min, inner.min)) &&
		       glm::all(glm::greaterThanEqual(box.max, inner.max));
	};

	SECTION("a limb swung through a right angle")
	{
		constexpr uint32_t c_Frames = 7;
		for (uint32_t frame = 0; frame < c_Frames; ++frame)
		{
			const float angle =
				glm::radians(90.0f * static_cast<float>(frame) / static_cast<float>(c_Frames - 1));
			animations.samples.push_back(skeleton.bones[0].bindPose);
			animations.samples.push_back(
				{ glm::vec3(2.0f, 0.0f, 0.0f),
			      glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f)),
			      glm::vec3(1.0f) });
		}
		clip.frameCount = c_Frames;
		animations.clips.push_back(clip);

		const Bounds exact = exactPosedBounds(fixture.mesh, 0, skeleton, animations);
		const Bounds box   = posedBounds(fixture.mesh, 0, skeleton, animations);

		CHECK(holds(box, exact));

		// Loose, but on the order of the limb's own thickness rather than its length: the slack a
		// rotation costs is the bone's box re-axis-aligned, not the whole rig's.
		CHECK(box.max.y - box.min.y < 2.0f * (exact.max.y - exact.min.y));
	}

	SECTION("a limb that only travels is bounded exactly")
	{
		// No rotation anywhere, so the axis-aligned round trip loses nothing and the conservative
		// box is the tight one -- which is what separates a bone-local box from sweeping the whole
		// bind-pose box by every bone.
		for (const float x : { 0.0f, 10.0f })
		{
			animations.samples.push_back(
				{ glm::vec3(x, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
			animations.samples.push_back(skeleton.bones[1].bindPose);
		}
		clip.frameCount = 2;
		animations.clips.push_back(clip);

		const Bounds exact = exactPosedBounds(fixture.mesh, 0, skeleton, animations);
		const Bounds box   = posedBounds(fixture.mesh, 0, skeleton, animations);

		CHECK(box.min.x == Catch::Approx(exact.min.x).margin(1e-5));
		CHECK(box.max.x == Catch::Approx(exact.max.x).margin(1e-5));
		CHECK(box.min.y == Catch::Approx(exact.min.y).margin(1e-5));
		CHECK(box.max.y == Catch::Approx(exact.max.y).margin(1e-5));
	}

	SECTION("a vertex no bone moves is held where it was authored")
	{
		fixture.Add(
			glm::vec3(0.0f, 0.0f, 9.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			{ { 0, 0, 0, 0 } },
			{ { 0, 0, 0, 0 } });
		fixture.mesh.submeshes[0].vertexCount = fixture.submesh.vertexCount;

		animations.samples.push_back(
			{ glm::vec3(100.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
		animations.samples.push_back(skeleton.bones[1].bindPose);
		clip.frameCount = 1;
		animations.clips.push_back(clip);

		const Bounds box = posedBounds(fixture.mesh, 0, skeleton, animations);

		CHECK(box.max.z == Catch::Approx(9.0f));
		CHECK(holds(box, exactPosedBounds(fixture.mesh, 0, skeleton, animations)));
	}
}

// One walk of the clip set answers for every mesh entry at once, so the boxes come back in a
// parallel array that has to be re-paired with the entries that produced them. An entry the decode
// refuses leaves a gap in that pairing, which is the way it goes wrong.
TEST_CASE(
	"A bake over several mesh entries pairs each box with its own entry",
	"[skinning][bounds]")
{
	// Four entries over one vertex pool: two that decode, one whose submesh overruns the pool, and
	// one with no vertices at all -- which has only its submesh box to be bounded by.
	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(1.0f),
		glm::vec3(0.0f, 0.0f, 1.0f),
		{ { 0, 0, 0, 0 } },
		{ { c_Unorm16Max, 0, 0, 0 } });
	fixture.Add(
		glm::vec3(2.0f),
		glm::vec3(0.0f, 0.0f, 1.0f),
		{ { 0, 0, 0, 0 } },
		{ { c_Unorm16Max, 0, 0, 0 } });
	fixture.Add(
		glm::vec3(5.0f),
		glm::vec3(0.0f, 0.0f, 1.0f),
		{ { 0, 0, 0, 0 } },
		{ { c_Unorm16Max, 0, 0, 0 } });

	const auto sliceOf = [&](uint32_t first, uint32_t count) {
		Submesh out          = fixture.submesh;
		out.vertexByteOffset = first * out.layout.stride;
		out.vertexCount      = count;
		out.aabbMin          = glm::vec3(0.0f);
		out.aabbMax          = glm::vec3(0.0f);
		return out;
	};

	fixture.mesh.submeshes.push_back(sliceOf(0, 1));
	fixture.mesh.submeshes.push_back(sliceOf(1, 99));  // past the end of the pool
	fixture.mesh.submeshes.push_back(sliceOf(2, 0));
	fixture.mesh.submeshes.back().aabbMin = glm::vec3(-3.0f);
	fixture.mesh.submeshes.back().aabbMax = glm::vec3(-2.0f);
	fixture.mesh.submeshes.push_back(sliceOf(2, 1));

	for (uint32_t i = 0; i < 4; ++i)
		fixture.mesh.meshes.push_back({ .firstSubmesh = i, .submeshCount = 1, .nameOffset = 0 });

	auto skeleton    = assetlib::Skeleton();
	auto bone        = assetlib::Bone();
	bone.bindPose    = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
	bone.inverseBind = glm::mat4(1.0f);
	bone.parent      = assetlib::c_InvalidIndex;
	bone.nameOffset  = 0;
	skeleton.bones.push_back(bone);

	auto animations              = assetlib::AnimationSet();
	animations.boneCount         = 1;
	animations.skeletonSignature = assetlib::skeletonSignature(skeleton);

	auto clip       = assetlib::AnimationClip();
	clip.frameCount = 2;
	clip.sampleRate = 30.0f;
	animations.clips.push_back(clip);

	animations.samples.push_back(
		{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
	animations.samples.push_back(
		{ glm::vec3(10.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });

	REQUIRE(assetlib::isSkinned(fixture.mesh, 1));
	assetlib::bakePosedBounds(animations, fixture.mesh, skeleton);

	SECTION("the entry whose decode fails leaves the others' boxes where they belong")
	{
		REQUIRE(animations.posedBoxes.size() == 3);

		const auto boxFor = [&](uint32_t meshIndex) {
			return assetlib::findPosedBounds(animations, fixture.mesh, skeleton)[meshIndex];
		};

		CHECK_FALSE(boxFor(1).has_value());

		// Entry 0 holds the vertex at 1, entry 3 the one at 5; a mispaired array would swap them.
		REQUIRE(boxFor(0).has_value());
		CHECK(boxFor(0)->min.x == Catch::Approx(1.0f));
		CHECK(boxFor(0)->max.x == Catch::Approx(11.0f));

		REQUIRE(boxFor(3).has_value());
		CHECK(boxFor(3)->min.x == Catch::Approx(5.0f));
		CHECK(boxFor(3)->max.x == Catch::Approx(15.0f));

		// Entry 2 has no vertex to sweep, so it falls back to the box its submesh already carries.
		REQUIRE(boxFor(2).has_value());
		CHECK(boxFor(2)->min.x == Catch::Approx(-3.0f));
		CHECK(boxFor(2)->max.x == Catch::Approx(-2.0f));
	}

	SECTION("each entry gets the box measuring it alone would have given it")
	{
		for (const uint32_t meshIndex : { 0u, 3u })
		{
			const assetlib::Bounds alone =
				assetlib::posedBounds(fixture.mesh, meshIndex, skeleton, animations);
			const std::optional<assetlib::Bounds> baked =
				assetlib::findPosedBounds(animations, fixture.mesh, skeleton)[meshIndex];

			REQUIRE(baked.has_value());
			CHECK(baked->min.x == Catch::Approx(alone.min.x));
			CHECK(baked->max.x == Catch::Approx(alone.max.x));
			CHECK(baked->max.z == Catch::Approx(alone.max.z));
		}
	}
}

// PosedBox is 36 bytes of members in a 40-byte struct, and the whole 40 go into the .banim
// verbatim. Aggregate-initialising one leaves that tail indeterminate, so two runs write files
// that differ by junk no reader looks at -- which breaks byte-comparison, and byte-comparison is
// how `migrate` decides a file is current and how a produced project is checked against an
// imported one. The poison below is what a real allocation supplies for free.
TEST_CASE("A baked posed box carries no indeterminate padding", "[skinning][bounds]")
{
	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(-1.0f),
		glm::vec3(0.0f, 0.0f, 1.0f),
		{ { 0, 0, 0, 0 } },
		{ { 65535, 0, 0, 0 } });

	fixture.submesh.aabbMin = glm::vec3(-1.0f);
	fixture.submesh.aabbMax = glm::vec3(1.0f);
	fixture.mesh.submeshes.push_back(fixture.submesh);
	fixture.mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });

	auto skeleton    = assetlib::Skeleton();
	auto bone        = assetlib::Bone();
	bone.bindPose    = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
	bone.inverseBind = glm::mat4(1.0f);
	bone.parent      = assetlib::c_InvalidIndex;
	bone.nameOffset  = 0;
	skeleton.bones.push_back(bone);

	auto animations              = assetlib::AnimationSet();
	animations.boneCount         = 1;
	animations.skeletonSignature = assetlib::skeletonSignature(skeleton);

	auto clip       = assetlib::AnimationClip();
	clip.frameCount = 1;
	clip.sampleRate = 30.0f;
	animations.clips.push_back(clip);
	animations.samples.push_back(
		{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });

	// Reserved and poisoned, so the bake's push_back lands on storage that is anything but zero.
	animations.posedBoxes.reserve(4);
	std::memset(animations.posedBoxes.data(), 0xAB, 4 * sizeof(assetlib::PosedBox));

	assetlib::bakePosedBounds(animations, fixture.mesh, skeleton);
	REQUIRE(animations.posedBoxes.size() == 1);

	constexpr size_t c_MembersEnd =
		offsetof(assetlib::PosedBox, meshIndex) + sizeof(assetlib::PosedBox::meshIndex);
	STATIC_REQUIRE(c_MembersEnd < sizeof(assetlib::PosedBox));

	const auto* bytes = reinterpret_cast<const unsigned char*>(animations.posedBoxes.data());
	for (size_t i = c_MembersEnd; i < sizeof(assetlib::PosedBox); ++i)
	{
		INFO("padding byte " << i);
		CHECK(bytes[i] == 0);
	}
}

TEST_CASE("A baked posed box answers only for the pairing it measured", "[skinning][bounds]")
{
	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(-1.0f),
		glm::vec3(0.0f, 0.0f, 1.0f),
		{ { 0, 0, 0, 0 } },
		{ { 65535, 0, 0, 0 } });
	fixture.Add(
		glm::vec3(1.0f),
		glm::vec3(0.0f, 0.0f, 1.0f),
		{ { 0, 0, 0, 0 } },
		{ { 65535, 0, 0, 0 } });

	fixture.submesh.aabbMin = glm::vec3(-1.0f);
	fixture.submesh.aabbMax = glm::vec3(1.0f);
	fixture.mesh.submeshes.push_back(fixture.submesh);
	fixture.mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });

	auto skeleton = assetlib::Skeleton();

	auto bone        = assetlib::Bone();
	bone.bindPose    = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
	bone.inverseBind = glm::mat4(1.0f);
	bone.parent      = assetlib::c_InvalidIndex;
	bone.nameOffset  = 0;
	skeleton.bones.push_back(bone);

	auto animations              = assetlib::AnimationSet();
	animations.boneCount         = 1;
	animations.skeletonSignature = assetlib::skeletonSignature(skeleton);

	auto clip        = assetlib::AnimationClip();
	clip.firstSample = 0;
	clip.frameCount  = 2;
	clip.sampleRate  = 30.0f;
	animations.clips.push_back(clip);

	animations.samples.push_back(
		{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });
	animations.samples.push_back(
		{ glm::vec3(50.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });

	assetlib::bakePosedBounds(animations, fixture.mesh, skeleton);

	SECTION("the bake stores what the measure returns, through the .banim and back")
	{
		const assetlib::AnimationSet loaded = assetlib::AssetCodec<AnimationSet>::Deserialize(
			assetlib::AssetCodec<AnimationSet>::Serialize(animations));

		const std::optional<assetlib::Bounds> found =
			assetlib::findPosedBounds(loaded, fixture.mesh, skeleton)[0];

		REQUIRE(found.has_value());
		CHECK(found->min.x == Catch::Approx(-1.0f));
		CHECK(found->max.x == Catch::Approx(51.0f));
	}

	SECTION("a mesh that changed since the bake is measured, not matched")
	{
		fixture.mesh.vertexData[0] ^= std::byte{ 0x01 };
		CHECK_FALSE(assetlib::findPosedBounds(animations, fixture.mesh, skeleton)[0].has_value());
	}

	SECTION("a bind re-authored since the bake is measured, not matched")
	{
		// The one skeleton edit skeletonSignature deliberately lets through -- see skeleton.h.
		skeleton.bones[0].inverseBind = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f));
		CHECK_FALSE(assetlib::findPosedBounds(animations, fixture.mesh, skeleton)[0].has_value());
	}

	SECTION("a submesh table regrouped over identical bytes is measured, not matched")
	{
		// The vertex blob alone cannot see this edit, which is why the tables are in the hash.
		fixture.mesh.submeshes[0].vertexCount = 1;
		CHECK_FALSE(assetlib::findPosedBounds(animations, fixture.mesh, skeleton)[0].has_value());
	}

	SECTION("a box naming a mesh entry this mesh does not have is ignored")
	{
		// One optional per entry the mesh actually has, so there is no index 1 to ask about --
		// reading one would be past the end of the vector, not a nullopt.
		REQUIRE(assetlib::findPosedBounds(animations, fixture.mesh, skeleton).size() == 1);

		// The guard that makes a box out of step with its mesh harmless: a re-export that dropped
		// an entry leaves boxes behind naming it.
		auto stray      = animations.posedBoxes.front();
		stray.meshIndex = 7;
		animations.posedBoxes.push_back(stray);

		const auto boxes = assetlib::findPosedBounds(animations, fixture.mesh, skeleton);
		REQUIRE(boxes.size() == 1);
		CHECK(boxes[0].has_value());
	}

	SECTION("rebaking the same source replaces its entries rather than stacking them")
	{
		assetlib::bakePosedBounds(animations, fixture.mesh, skeleton);
		CHECK(animations.posedBoxes.size() == 1);
	}

	SECTION("a mesh with no skin gets no box; its submeshes already carry one")
	{
		SkinnedMesh rigid;
		rigid.Add(
			glm::vec3(2.0f),
			glm::vec3(0.0f, 0.0f, 1.0f),
			{ { 0, 0, 0, 0 } },
			{ { 65535, 0, 0, 0 } });
		rigid.submesh.layout.attributeCount = 2;  // position and normal only
		rigid.mesh.submeshes.push_back(rigid.submesh);
		rigid.mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });

		auto rebaked = assetlib::AnimationSet(animations);
		rebaked.posedBoxes.clear();
		assetlib::bakePosedBounds(rebaked, rigid.mesh, skeleton);
		CHECK(rebaked.posedBoxes.empty());
	}
}

namespace
{
	/** A rig from `{name, parent}` pairs, each bone's inverse bind derived from the rest pose. */
	Skeleton
	RigFrom(const std::vector<std::pair<const char*, uint32_t>>& bones)
	{
		Skeleton skeleton;
		for (const auto& [name, parent] : bones)
		{
			Bone bone{};
			bone.bindPose   = { glm::vec3(0.0f, 1.0f, 0.0f),
				                glm::angleAxis(glm::radians(10.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
				                glm::vec3(1.0f) };
			bone.parent     = parent;
			bone.nameOffset = skeleton.stringPool.add(name);
			skeleton.bones.push_back(bone);
		}

		const std::vector<glm::mat4> model = bindPoseModelTransforms(skeleton);
		for (size_t i = 0; i < skeleton.bones.size(); ++i)
			skeleton.bones[i].inverseBind = glm::inverse(model[i]);

		return skeleton;
	}

	/** Whether any vertex of `mesh` carries weight on `bone` -- the premise a narrowing test rests on. */
	bool
	WeightedInMesh(const BMesh& mesh, const uint16_t bone)
	{
		for (const Submesh& submesh : mesh.submeshes)
		{
			const VertexAttribute* joints = findAttribute(submesh.layout, VertexSemantic::kJoints0);
			const VertexAttribute* weights =
				findAttribute(submesh.layout, VertexSemantic::kWeights0);
			if (joints == nullptr || weights == nullptr)
				continue;

			for (uint32_t v = 0; v < submesh.vertexCount; ++v)
			{
				const size_t base =
					submesh.vertexByteOffset + static_cast<size_t>(v) * submesh.layout.stride;
				for (size_t i = 0; i < c_InfluencesPerVertex; ++i)
				{
					uint16_t joint  = 0;
					uint16_t weight = 0;
					std::memcpy(
						&joint,
						mesh.vertexData.data() + base + joints->offset + i * sizeof(uint16_t),
						sizeof(joint));
					std::memcpy(
						&weight,
						mesh.vertexData.data() + base + weights->offset + i * sizeof(uint16_t),
						sizeof(weight));
					if (joint == bone && weight != 0)
						return true;
				}
			}
		}
		return false;
	}

	/** One two-frame clip that swings every bone, so a wrong index shows up as a moved vertex. */
	AnimationSet
	ClipsFor(const Skeleton& skeleton)
	{
		AnimationSet animations;
		animations.boneCount         = static_cast<uint32_t>(skeleton.bones.size());
		animations.skeletonSignature = skeletonSignature(skeleton);
		animations.skeletonBoneNames = skeletonBoneNames(skeleton);
		animations.skeleton          = "Derived/Skeletons/rig.bskel";

		AnimationClip clip{};
		clip.nameOffset  = animations.stringPool.add("swing");
		clip.firstSample = 0;
		clip.frameCount  = 2;
		clip.sampleRate  = 30.0f;
		clip.duration    = 1.0f / 30.0f;

		for (uint32_t f = 0; f < clip.frameCount; ++f)
			for (uint32_t b = 0; b < animations.boneCount; ++b)
			{
				Transform pose = skeleton.bones[b].bindPose;
				pose.rotation  = glm::angleAxis(
					glm::radians(15.0f * static_cast<float>(f + 1) * static_cast<float>(b + 1)),
					glm::vec3(0.0f, 0.0f, 1.0f));
				animations.samples.push_back(pose);
			}

		animations.clips = { clip };
		return animations;
	}
}

// The gate the whole feature rests on: an appended bone must change where nothing lands. If a
// remapped pairing skins one vertex differently from the pairing it was cooked as, the remap has
// silently mis-posed the rig -- which is the failure skeletonSignature exists to prevent.
TEST_CASE("A remapped mesh and clip set skin exactly as the cooked pair did", "[skinning][remap]")
{
	const auto cooked = RigFrom({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "head", 1 } });

	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(1.0f, 2.0f, 3.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		{ { 0, 1, 0, 0 } },
		{ { c_Unorm16Max / 2, c_Unorm16Max / 2, 0, 0 } });
	fixture.Add(
		glm::vec3(-4.0f, 0.5f, 7.25f),
		glm::vec3(1.0f, 0.0f, 0.0f),
		{ { 2, 0, 0, 0 } },
		{ { c_Unorm16Max, 0, 0, 0 } });

	fixture.mesh.submeshes         = { fixture.submesh };
	fixture.mesh.skeleton          = "Derived/Skeletons/rig.bskel";
	fixture.mesh.skeletonSignature = skeletonSignature(cooked);
	fixture.mesh.skeletonBoneNames = skeletonBoneNames(cooked);

	auto clips = ClipsFor(cooked);

	const auto before = skinSubmesh(
		fixture.mesh,
		fixture.mesh.submeshes[0],
		skinningMatrices(cooked, poseModelTransforms(cooked, clips, 0, 1)));

	// A socket on the spine: the depth-first walk places it before head, so head slides 2 -> 3.
	const auto grown =
		RigFrom({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "grip", 1 }, { "head", 1 } });

	REQUIRE_FALSE(meshMatchesSkeleton(fixture.mesh, grown));
	REQUIRE(remapAnimations(clips, grown));
	REQUIRE(remapMesh(fixture.mesh, grown));
	CHECK(meshMatchesSkeleton(fixture.mesh, grown));

	const auto after = skinSubmesh(
		fixture.mesh,
		fixture.mesh.submeshes[0],
		skinningMatrices(grown, poseModelTransforms(grown, clips, 0, 1)));

	REQUIRE(after.size() == before.size());
	for (size_t v = 0; v < before.size(); ++v)
	{
		INFO("vertex " << v);
		CHECK(after[v].position.x == Catch::Approx(before[v].position.x));
		CHECK(after[v].position.y == Catch::Approx(before[v].position.y));
		CHECK(after[v].position.z == Catch::Approx(before[v].position.z));
		CHECK(after[v].blendedNormal.x == Catch::Approx(before[v].blendedNormal.x));
		CHECK(after[v].blendedNormal.y == Catch::Approx(before[v].blendedNormal.y));
	}
}

TEST_CASE(
	"remapMesh refuses a weighted vertex naming a bone the rig never had",
	"[skinning][remap]")
{
	const auto cooked = RigFrom({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "head", 1 } });

	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(1.0f, 2.0f, 3.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		{ { 7, 0, 0, 0 } },  // no such bone, and it carries all the weight
		{ { c_Unorm16Max, 0, 0, 0 } });

	fixture.mesh.submeshes         = { fixture.submesh };
	fixture.mesh.skeleton          = "Derived/Skeletons/rig.bskel";
	fixture.mesh.skeletonSignature = skeletonSignature(cooked);
	fixture.mesh.skeletonBoneNames = skeletonBoneNames(cooked);

	const auto grown =
		RigFrom({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "grip", 1 }, { "head", 1 } });

	const std::vector<std::byte> untouched = fixture.mesh.vertexData;

	CHECK_FALSE(remapMesh(fixture.mesh, grown));
	CHECK(fixture.mesh.vertexData == untouched);
	CHECK(fixture.mesh.skeletonSignature == skeletonSignature(cooked));
}

TEST_CASE("remapMesh zeroes an unweighted influence it cannot place", "[skinning][remap]")
{
	// decodeInfluences accepts an out-of-range joint whose weight is zero, so a mesh may carry one
	// -- but SkinMatrix fetches all four palette matrices whatever their weights and
	// AssertBoneIndices checks all four, so leaving it would read off the end of the palette and
	// fail a GPU_DEBUG build.
	const auto cooked = RigFrom({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "head", 1 } });

	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(1.0f, 2.0f, 3.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		{ { 2, 9, 0, 0 } },  // bone 2 carries everything; 9 does not exist and carries nothing
		{ { c_Unorm16Max, 0, 0, 0 } });

	fixture.mesh.submeshes         = { fixture.submesh };
	fixture.mesh.skeleton          = "Derived/Skeletons/rig.bskel";
	fixture.mesh.skeletonSignature = skeletonSignature(cooked);
	fixture.mesh.skeletonBoneNames = skeletonBoneNames(cooked);

	const auto grown =
		RigFrom({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "grip", 1 }, { "head", 1 } });

	REQUIRE(remapMesh(fixture.mesh, grown));

	// head moved 2 -> 3; the index that named nothing is now a bone that exists and weighs nothing.
	uint16_t first  = 0;
	uint16_t second = 0;
	std::memcpy(&first, fixture.mesh.vertexData.data() + 24, sizeof(first));
	std::memcpy(&second, fixture.mesh.vertexData.data() + 26, sizeof(second));
	CHECK(first == 3);
	CHECK(second == 0);

	// And every index the mesh now carries is one the grown rig holds -- what the GPU asserts.
	CHECK(first < grown.bones.size());
	CHECK(second < grown.bones.size());
}

// ADR-5's gate. Both derived bakes are keyed on something an append used to move, so the load
// succeeded and then re-measured -- 131 ms of posed bounds on the reference rig, plus plant
// weights, on every load. Neither measurement changes: a bone with no weight sweeps no box and
// moves no sole. These pin that the keys now say so, and that they still move for an edit that
// does change what they key.
TEST_CASE("An added bone does not move the posed-bounds key", "[skinning][perf][remap]")
{
	const auto cooked = RigFrom({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "head", 1 } });

	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(1.0f, 2.0f, 3.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		{ { 0, 1, 0, 0 } },
		{ { c_Unorm16Max / 2, c_Unorm16Max / 2, 0, 0 } });
	fixture.Add(
		glm::vec3(-4.0f, 0.5f, 7.25f),
		glm::vec3(1.0f, 0.0f, 0.0f),
		{ { 2, 0, 0, 0 } },
		{ { c_Unorm16Max, 0, 0, 0 } });

	fixture.mesh.submeshes         = { fixture.submesh };
	fixture.mesh.skeleton          = "Derived/Skeletons/rig.bskel";
	fixture.mesh.skeletonSignature = skeletonSignature(cooked);
	fixture.mesh.skeletonBoneNames = skeletonBoneNames(cooked);

	const uint64_t before = posedBoundsSignature(fixture.mesh, cooked);

	SECTION("a socket added to the rig leaves it alone")
	{
		// The case the feature exists for: a clips-only group stranded against a rig that grew.
		// Appended last and hung off `head`, so every index the mesh names still means the bone it
		// was cooked against, and the socket sits at a depth no other bone has -- its inverse bind
		// is distinct, so an equal key is evidence it was skipped rather than a collision.
		const auto grown =
			RigFrom({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "head", 1 }, { "grip", 2 } });

		REQUIRE(grown.bones[3].inverseBind != grown.bones[2].inverseBind);
		CHECK(posedBoundsSignature(fixture.mesh, grown) == before);
	}

	SECTION("remapping the mesh itself moves it, and narrowing cannot reach that")
	{
		// remapMesh rewrites kJoints0 *inside* vertexData, which this hashes wholesale, so the
		// bytes genuinely differ. Such a pairing re-measures once per load until migrate bakes the
		// remap down and the box with it.
		const auto grown =
			RigFrom({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "grip", 1 }, { "head", 1 } });

		auto remapped = fixture.mesh;
		REQUIRE(remapMesh(remapped, grown));

		CHECK(posedBoundsSignature(remapped, grown) != before);
	}

	SECTION("but re-authoring a weighted bone's rest pose moves it")
	{
		auto edited = cooked;
		edited.bones[1].inverseBind =
			glm::translate(edited.bones[1].inverseBind, glm::vec3(0.0f, 0.5f, 0.0f));

		CHECK(posedBoundsSignature(fixture.mesh, edited) != before);
	}

	SECTION("and so does moving a vertex")
	{
		auto moved = fixture.mesh;
		moved.vertexData[0] ^= std::byte{ 0x01 };

		CHECK(posedBoundsSignature(moved, cooked) != before);
	}
}

// This number is written into every baked PosedBox and compared byte-for-byte on the way back in,
// so moving it strands every box already on disk -- silently, since a mismatch is a fallback and
// not an error, and the .banim's own cache key knows nothing about it. Cooking the geometry half
// did move it once, by hashing the cooked value instead of chaining on it. So the shape is pinned:
// the geometry signature *is* the running hash, and the weighted bones chain onto it.
TEST_CASE("The posed-bounds key chains onto the geometry signature", "[skinning][remap]")
{
	const auto cooked = RigFrom({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "head", 1 } });

	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(1.0f, 2.0f, 3.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		{ { 0, 2, 0, 0 } },
		{ { c_Unorm16Max / 2, c_Unorm16Max / 2, 0, 0 } });
	fixture.mesh.submeshes = { fixture.submesh };

	// Bones 0 and 2 carry weight, bone 1 does not -- so this also states which bones are chained.
	uint64_t expected = geometrySignature(fixture.mesh);
	expected          = core::hash_pod(cooked.bones[0].inverseBind, expected);
	expected          = core::hash_pod(cooked.bones[2].inverseBind, expected);

	CHECK(posedBoundsSignature(fixture.mesh, cooked) == expected);
}

// The cooked hash is an optimisation, so the one thing it must never do is answer differently
// from the walk it replaces -- a mesh read off disk and the same mesh built in memory key the same.
TEST_CASE("The cooked geometry hash and the walk agree", "[skinning][perf][remap]")
{
	const auto cooked = RigFrom({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "head", 1 } });

	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(1.0f, 2.0f, 3.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		{ { 0, 1, 0, 0 } },
		{ { c_Unorm16Max / 2, c_Unorm16Max / 2, 0, 0 } });

	// Weighted to the last bone, so an insert ahead of it actually renumbers something -- a remap
	// that moved no index would leave the blob alone and prove nothing below.
	fixture.Add(
		glm::vec3(-4.0f, 0.5f, 7.25f),
		glm::vec3(1.0f, 0.0f, 0.0f),
		{ { 2, 0, 0, 0 } },
		{ { c_Unorm16Max, 0, 0, 0 } });
	fixture.mesh.submeshes = { fixture.submesh };

	REQUIRE(fixture.mesh.geometrySignature == 0);
	const uint64_t walked = posedBoundsSignature(fixture.mesh, cooked);

	SECTION("a mesh carrying the cook's answer keys identically")
	{
		auto carried              = fixture.mesh;
		carried.geometrySignature = geometrySignature(carried);

		CHECK(posedBoundsSignature(carried, cooked) == walked);
	}

	SECTION("remapMesh clears it, because the blob it described has been rewritten")
	{
		const auto grown =
			RigFrom({ { "hips", c_InvalidIndex }, { "spine", 0 }, { "grip", 1 }, { "head", 1 } });

		auto remapped              = fixture.mesh;
		remapped.skeletonSignature = skeletonSignature(cooked);
		remapped.skeletonBoneNames = skeletonBoneNames(cooked);
		remapped.geometrySignature = geometrySignature(remapped);

		REQUIRE(remapMesh(remapped, grown));
		CHECK(remapped.geometrySignature == 0);

		// And the cleared field is not merely tidy: the rewritten blob hashes to something else,
		// so keeping the old one would have kept a box measured on the indices it replaced.
		CHECK(geometrySignature(remapped) != geometrySignature(fixture.mesh));
	}
}

TEST_CASE("An added bone does not move the plant-weights key", "[skinning][perf][remap]")
{
	const auto cooked =
		RigFrom({ { "hips", c_InvalidIndex }, { "knee", 0 }, { "ankle", 1 }, { "toe", 2 } });

	SkinnedMesh fixture;
	fixture.Add(
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		{ { 3, 0, 0, 0 } },
		{ { c_Unorm16Max, 0, 0, 0 } });

	fixture.mesh.submeshes         = { fixture.submesh };
	fixture.mesh.skeletonSignature = skeletonSignature(cooked);
	fixture.mesh.skeletonBoneNames = skeletonBoneNames(cooked);

	auto avatar = ResolvedAvatar();
	avatar.legs = { AvatarLegChain{ 0, 1, 2, 3 } };

	const auto     meshes = std::span<const BMesh>(&fixture.mesh, 1);
	const uint64_t before = plantWeightsSignature(meshes, cooked, avatar);

	SECTION("a socket added beside the leg leaves it alone")
	{
		// Appended last, so the leg keeps its indices and only the rig's bone count moves --
		// exactly what skeletonSignature used to catch and should not have.
		const auto grown = RigFrom(
			{ { "hips", c_InvalidIndex },
		      { "knee", 0 },
		      { "ankle", 1 },
		      { "toe", 2 },
		      { "grip", 0 } });

		auto remapped = fixture.mesh;
		REQUIRE(remapMesh(remapped, grown));

		const auto grownMeshes = std::span<const BMesh>(&remapped, 1);
		CHECK(plantWeightsSignature(grownMeshes, grown, avatar) == before);
	}

	SECTION("but re-authoring a leg bone's rest pose moves it")
	{
		auto edited = cooked;
		edited.bones[2].bindPose.translation.y += 0.25f;

		CHECK(plantWeightsSignature(meshes, edited, avatar) != before);
	}

	SECTION("and so does re-authoring the inverse bind of a leg bone nothing is weighted to")
	{
		// The ankle carries no mesh weight here -- only the toe does -- so posedBoundsSignature
		// narrows it away and this key is the only thing left covering it. solePlanes carries
		// every sole through `inverseBind[ankle]`, and glTF authors that separately from the
		// local rest pose, so a rig that re-exported one and not the other must not match.
		REQUIRE_FALSE(WeightedInMesh(fixture.mesh, 2));

		auto edited = cooked;
		edited.bones[2].inverseBind =
			glm::translate(edited.bones[2].inverseBind, glm::vec3(0.0f, 0.0f, 0.125f));

		CHECK(plantWeightsSignature(meshes, edited, avatar) != before);
	}

	SECTION("and renaming a leg bone moves it")
	{
		auto renamed                = cooked;
		renamed.bones[2].nameOffset = renamed.stringPool.add("shin");

		CHECK(plantWeightsSignature(meshes, renamed, avatar) != before);
	}
}
