#include <assetlib/banim_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/skeleton.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
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

// The VAT bake reconstructs a posed tangent from the posed normal plus a baked twist, and the
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
	const auto near = Quantize(1.0f / 3.0f);
	const auto far  = Quantize(2.0f / 3.0f);

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
			{ { near, far, 0, 0 } });
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
			return assetlib::findPosedBounds(animations, fixture.mesh, meshIndex, skeleton);
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
				assetlib::findPosedBounds(animations, fixture.mesh, meshIndex, skeleton);

			REQUIRE(baked.has_value());
			CHECK(baked->min.x == Catch::Approx(alone.min.x));
			CHECK(baked->max.x == Catch::Approx(alone.max.x));
			CHECK(baked->max.z == Catch::Approx(alone.max.z));
		}
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
		const assetlib::AnimationSet loaded =
			assetlib::deserializeAnimations(assetlib::serializeAnimations(animations));

		const std::optional<assetlib::Bounds> found =
			assetlib::findPosedBounds(loaded, fixture.mesh, 0, skeleton);

		REQUIRE(found.has_value());
		CHECK(found->min.x == Catch::Approx(-1.0f));
		CHECK(found->max.x == Catch::Approx(51.0f));
	}

	SECTION("a mesh that changed since the bake is measured, not matched")
	{
		fixture.mesh.vertexData[0] ^= std::byte{ 0x01 };
		CHECK_FALSE(assetlib::findPosedBounds(animations, fixture.mesh, 0, skeleton).has_value());
	}

	SECTION("a bind re-authored since the bake is measured, not matched")
	{
		// The one skeleton edit skeletonSignature deliberately lets through -- see skeleton.h.
		skeleton.bones[0].inverseBind = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f));
		CHECK_FALSE(assetlib::findPosedBounds(animations, fixture.mesh, 0, skeleton).has_value());
	}

	SECTION("a submesh table regrouped over identical bytes is measured, not matched")
	{
		// The vertex blob alone cannot see this edit, which is why the tables are in the hash.
		fixture.mesh.submeshes[0].vertexCount = 1;
		CHECK_FALSE(assetlib::findPosedBounds(animations, fixture.mesh, 0, skeleton).has_value());
	}

	SECTION("a mesh entry the bake never saw finds nothing")
	{
		CHECK_FALSE(assetlib::findPosedBounds(animations, fixture.mesh, 1, skeleton).has_value());
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
