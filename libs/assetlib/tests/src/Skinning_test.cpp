#include <assetlib/skeleton.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>

#include <catch2/catch_approx.hpp>

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
		{ 0, 0, 0, 0 },
		{ c_Unorm16Max, 0, 0, 0 });
	fixture.Add(
		glm::vec3(-4.0f, 0.5f, 7.25f),
		glm::vec3(1.0f, 0.0f, 0.0f),
		{ 1, 0, 0, 0 },
		{ c_Unorm16Max, 0, 0, 0 });

	const std::vector<glm::mat4> identity(2, glm::mat4(1.0f));
	const auto                   skinned = skinSubmesh(fixture.mesh, fixture.submesh, identity);

	REQUIRE(skinned.size() == 2);
	CHECK(skinned[0].position.x == Catch::Approx(1.0f));
	CHECK(skinned[0].position.y == Catch::Approx(2.0f));
	CHECK(skinned[0].position.z == Catch::Approx(3.0f));
	CHECK(skinned[1].position.x == Catch::Approx(-4.0f));
	CHECK(skinned[1].position.z == Catch::Approx(7.25f));
	CHECK(skinned[0].normal.y == Catch::Approx(1.0f));
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
		{ 0, 0, 0, 0 },
		{ c_Unorm16Max, 0, 0, 0 });

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
		{ 1, 1, 1, 1 },
		{ Quantize(0.1f), Quantize(0.2f), Quantize(0.3f), Quantize(0.4f) });

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
		single.Add(glm::vec3(0.0f), glm::vec3(0.0f), { 1, 0, 0, 0 }, { c_Unorm16Max, 0, 0, 0 });

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
		{ 0, 1, 0, 0 },
		{ Quantize(0.25f), Quantize(0.75f), 0, 0 });

	std::vector<glm::mat4> skinning(2, glm::mat4(1.0f));
	skinning[1] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 4.0f, 0.0f));

	const auto skinned = skinSubmesh(fixture.mesh, fixture.submesh, skinning);
	CHECK(skinned[0].position.y == Catch::Approx(3.0f).margin(1e-3));
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
	CHECK(skinned[0].normal == glm::vec3(0.0f));
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
		{ 0, 0, 0, 0 },
		{ 0, 0, 0, 0 });

	const std::vector<glm::mat4> skinning(
		2,
		glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, 0.0f)));

	const auto skinned = skinSubmesh(fixture.mesh, fixture.submesh, skinning);

	REQUIRE(skinned.size() == 1);
	CHECK(skinned[0].position.x == Catch::Approx(3.0f));
	CHECK(skinned[0].position.y == Catch::Approx(4.0f));
	CHECK(skinned[0].position.z == Catch::Approx(5.0f));
	CHECK(skinned[0].normal.y == Catch::Approx(1.0f));
}

TEST_CASE("Skinning refuses a submesh it cannot read", "[skinning]")
{
	SkinnedMesh fixture;
	fixture.Add(glm::vec3(0.0f), glm::vec3(0.0f), { 0, 0, 0, 0 }, { c_Unorm16Max, 0, 0, 0 });

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
