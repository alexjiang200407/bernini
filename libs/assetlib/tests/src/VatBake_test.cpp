#include <assetlib/asset_describe.h>
#include <assetlib/asset_refs.h>
#include <assetlib/banim_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/bvat_io.h>
#include <assetlib/image_io.h>
#include <assetlib/skeleton.h>
#include <assetlib/skinning.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BVat.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/Skeleton.h>

#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "MountAt.h"
#include "VatFixture.h"
#include "mounted_io.h"
#include "vat_tangent.h"
#include <assetlib/AssetStore.h>

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;

	glm::vec3
	TexelPosition(const ImageData& image, const BVat& vat, uint32_t x, uint32_t y)
	{
		const auto* texel = reinterpret_cast<const uint16_t*>(
			image.pixels.data() + image.subresources[0].offset +
			y * image.subresources[0].rowPitch + uint64_t(x) * 8);

		const glm::vec3 unorm(texel[0], texel[1], texel[2]);
		return vat.boundsMin + unorm / 65535.0f * (vat.boundsMax - vat.boundsMin);
	}

	glm::vec3
	TexelNormal(const ImageData& image, uint32_t x, uint32_t y)
	{
		const std::byte* texel = image.pixels.data() + image.subresources[0].offset +
		                         y * image.subresources[0].rowPitch + uint64_t(x) * 4;

		const auto channel = [&](int i) {
			return static_cast<float>(std::to_integer<uint8_t>(texel[i])) / 255.0f * 2.0f - 1.0f;
		};
		return glm::vec3(channel(0), channel(1), channel(2));
	}

	float
	TexelTwist(const ImageData& image, uint32_t x, uint32_t y)
	{
		const std::byte* texel = image.pixels.data() + image.subresources[0].offset +
		                         y * image.subresources[0].rowPitch + uint64_t(x) * 4;
		return unpackTwist(static_cast<float>(std::to_integer<uint8_t>(texel[3])) / 255.0f);
	}

	/**
	 * A one-bone rig and a skinned quad carrying a tangent, with one clip whose frames pose the
	 * bone: rest, a quarter turn about the normal (pure twist -- the shortest arc sees nothing), a
	 * tilt about X (pure arc), the tilt twisted, and the normal turned right round.
	 */
	struct TangentFixture
	{
		Skeleton     skeleton;
		AnimationSet animations;
		BMesh        mesh;

		static constexpr uint32_t c_Frames = 5;

		TangentFixture()
		{
			Bone root{};
			root.bindPose = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
			root.parent   = c_InvalidIndex;
			root.nameOffset = skeleton.stringPool.add("root");
			skeleton.bones.push_back(root);
			skeleton.bones[0].inverseBind = glm::mat4(1.0f);

			animations.skeleton          = "Skeletons/one.bskel";
			animations.skeletonSignature = skeletonSignature(skeleton);
			animations.boneCount         = 1;

			AnimationClip clip{};
			clip.nameOffset  = animations.stringPool.add("poses");
			clip.firstSample = 0;
			clip.frameCount  = c_Frames;
			clip.sampleRate  = 30.0f;
			clip.duration    = (c_Frames - 1) / 30.0f;
			animations.clips.push_back(clip);

			const glm::vec3 x(1.0f, 0.0f, 0.0f);
			const glm::vec3 y(0.0f, 1.0f, 0.0f);
			const glm::vec3 z(0.0f, 0.0f, 1.0f);
			const auto      turn = [](float degrees, const glm::vec3& axis) {
				return glm::angleAxis(glm::radians(degrees), axis);
			};
			const std::array<glm::quat, c_Frames> poses = { {
				glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				turn(90.0f, z),
				turn(60.0f, x),
				turn(60.0f, x) * turn(90.0f, z),
				turn(180.0f, y),
			} };
			for (const glm::quat& pose : poses)
				animations.samples.push_back({ glm::vec3(0.0f), pose, glm::vec3(1.0f) });

			Submesh quad{};
			quad.layout.attributeCount = 5;
			quad.layout.attributes[0]  = { VertexSemantic::kPosition, VertexFormat::kFloat32x3, 0 };
			quad.layout.attributes[1]  = { VertexSemantic::kNormal, VertexFormat::kFloat32x3, 12 };
			quad.layout.attributes[2]  = { VertexSemantic::kJoints0, VertexFormat::kUint16x4, 24 };
			quad.layout.attributes[3] = { VertexSemantic::kWeights0, VertexFormat::kUnorm16x4, 32 };
			quad.layout.attributes[4] = { VertexSemantic::kTangent, VertexFormat::kFloat32x4, 40 };
			quad.layout.stride        = 56;

			// Four corners of a +Z-facing quad, tangent +X.
			AddVertex(quad, glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
			AddVertex(quad, glm::vec3(1.0f, -1.0f, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
			AddVertex(quad, glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
			AddVertex(quad, glm::vec3(1.0f, 1.0f, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, -1.0f));
			mesh.submeshes.push_back(quad);
			mesh.skeleton = "Skeletons/one.bskel";
		}

		void
		AddVertex(Submesh& submesh, const glm::vec3& position, const glm::vec4& tangent)
		{
			const size_t base = mesh.vertexData.size();
			mesh.vertexData.resize(base + submesh.layout.stride);

			const glm::vec3               normal(0.0f, 0.0f, 1.0f);
			const std::array<uint16_t, 4> joints  = { { 0, 0, 0, 0 } };
			const std::array<uint16_t, 4> weights = { { c_Unorm16Max, 0, 0, 0 } };

			std::byte* at = mesh.vertexData.data() + base;
			std::memcpy(at, &position, sizeof(position));
			std::memcpy(at + 12, &normal, sizeof(normal));
			std::memcpy(at + 24, joints.data(), sizeof(joints));
			std::memcpy(at + 32, weights.data(), sizeof(weights));
			std::memcpy(at + 40, &tangent, sizeof(tangent));
			++submesh.vertexCount;
		}
	};

	/** The margin one unorm16 step spans on `axis` of the box, the bake's quantization unit. */
	float
	UnormMargin(const BVat& vat, int axis)
	{
		return (vat.boundsMax[axis] - vat.boundsMin[axis]) / 65535.0f + 1e-6f;
	}
}

TEST_CASE("A baked texel matches the CPU skin within unorm tolerance", "[vat]")
{
	VatFixture fixture;
	const BVat vat = bakeVat(fixture.mesh, fixture.skeleton, fixture.animations);

	REQUIRE(vat.width == 3);
	REQUIRE(vat.height == 6);  // (3 + 1) + (1 + 1)
	CHECK(vat.skeletonSignature == skeletonSignature(fixture.skeleton));

	const ImageData positions = decodeKTX2(vat.positionsKtx2);
	const ImageData normals   = decodeKTX2(vat.normalsKtx2);
	REQUIRE(positions.width == 3);
	REQUIRE(positions.height == 6);
	REQUIRE(positions.vkFormat == VkFormat::R16G16B16A16_UNORM);
	REQUIRE(normals.vkFormat == VkFormat::R8G8B8A8_UNORM);

	for (uint32_t clip = 0; clip < vat.clips.size(); ++clip)
	{
		for (uint32_t frame = 0; frame < vat.clips[clip].frameCount; ++frame)
		{
			const auto palette = skinningMatrices(
				fixture.skeleton,
				poseModelTransforms(fixture.skeleton, fixture.animations, clip, frame));

			const uint32_t row = vat.clips[clip].firstRow + frame;

			for (size_t s = 0; s < fixture.mesh.submeshes.size(); ++s)
			{
				const auto expected = skinSubmesh(fixture.mesh, fixture.mesh.submeshes[s], palette);

				for (uint32_t v = 0; v < expected.size(); ++v)
				{
					const uint32_t  column = vat.columns[s].columnBase + v;
					const glm::vec3 baked  = TexelPosition(positions, vat, column, row);

					for (int axis = 0; axis < 3; ++axis)
						CHECK(
							baked[axis] == Catch::Approx(expected[v].position[axis])
											   .margin(UnormMargin(vat, axis)));
				}
			}
		}
	}

	SECTION("the slide clip's welded vertex lands on its bone, closed-form")
	{
		// Column 1 is the child-bone vertex; at frame f of clip 0 its x is exactly f.
		for (uint32_t frame = 0; frame < 3; ++frame)
		{
			const glm::vec3 baked = TexelPosition(positions, vat, 1, frame);
			CHECK(baked.x == Catch::Approx(float(frame)).margin(UnormMargin(vat, 0)));
			CHECK(baked.y == Catch::Approx(2.0f).margin(UnormMargin(vat, 1)));
			CHECK(baked.z == Catch::Approx(1.0f).margin(UnormMargin(vat, 2)));
		}
	}

	SECTION("a normal survives the unorm8 round trip as a unit vector")
	{
		// The child vertex's normal is +X and the clip only translates, so every frame stores it.
		const glm::vec3 normal = TexelNormal(normals, 1, 1);
		CHECK(normal.x == Catch::Approx(1.0f).margin(2.0f / 255.0f));
		CHECK(normal.y == Catch::Approx(0.0f).margin(2.0f / 255.0f));
		CHECK(normal.z == Catch::Approx(0.0f).margin(2.0f / 255.0f));
	}

	SECTION("the static submesh never moves")
	{
		for (uint32_t row = 0; row < vat.height; ++row)
		{
			const glm::vec3 baked = TexelPosition(positions, vat, 2, row);
			CHECK(baked.x == Catch::Approx(5.0f).margin(UnormMargin(vat, 0)));
		}
	}

	SECTION("the palettes are the side-channel of the same frames")
	{
		REQUIRE(vat.palettes.size() == 8);  // (3 + 1) frames x 2 bones
		const auto expected = skinningMatrices(
			fixture.skeleton,
			poseModelTransforms(fixture.skeleton, fixture.animations, 0, 2));

		const VatClip& slide = vat.clips[0];
		for (int axis = 0; axis < 4; ++axis)
			CHECK(
				vat.palettes[slide.firstPalette + 2 * vat.boneCount + 1][3][axis] ==
				Catch::Approx(expected[1][3][axis]));
	}
}

// What the shader rebuilds from a texel -- the bind tangent carried onto the posed normal by the
// shortest arc, then turned by the alpha -- must be the tangent the CPU skin produced, in every
// pose. The quarter turn about the normal is the frame that matters: the arc alone cannot see it.
TEST_CASE("A baked twist turns the bind tangent onto the skinned one", "[vat]")
{
	TangentFixture fixture;
	const BVat     vat = bakeVat(fixture.mesh, fixture.skeleton, fixture.animations);
	REQUIRE(vat.width == 4);
	REQUIRE(vat.height == TangentFixture::c_Frames + 1);

	const ImageData normals = decodeKTX2(vat.normalsKtx2);

	const glm::vec3 bindNormal(0.0f, 0.0f, 1.0f);
	const glm::vec3 bindTangent(1.0f, 0.0f, 0.0f);

	// One unorm8 step of twist is 2 pi / 255, and the normal's own steps lean the frame by about as
	// much again.
	const float margin = 2.0f * glm::two_pi<float>() / 255.0f;

	for (uint32_t frame = 0; frame < TangentFixture::c_Frames; ++frame)
	{
		const auto palette = skinningMatrices(
			fixture.skeleton,
			poseModelTransforms(fixture.skeleton, fixture.animations, 0, frame));
		const auto skinned = skinSubmesh(fixture.mesh, fixture.mesh.submeshes[0], palette);

		for (uint32_t v = 0; v < 4; ++v)
		{
			const glm::vec3 posedNormal = TexelNormal(normals, v, frame);
			const float     twist       = TexelTwist(normals, v, frame);
			const glm::vec3 rebuilt =
				vatPosedTangent(bindNormal, bindTangent, glm::normalize(posedNormal), twist);

			const glm::vec3 expected = glm::normalize(skinned[v].blendedTangent);
			INFO("frame " << frame << " vertex " << v);
			for (int axis = 0; axis < 3; ++axis)
				CHECK(rebuilt[axis] == Catch::Approx(expected[axis]).margin(margin));
		}
	}

	SECTION("the quarter turn about the normal is a twist the arc alone would miss")
	{
		const glm::vec3 posedNormal = glm::normalize(TexelNormal(normals, 0, 1));
		CHECK(posedNormal.z == Catch::Approx(1.0f).margin(2.0f / 255.0f));

		// Arc only: the bind tangent, unmoved. The skin turned it onto +Y.
		const glm::vec3 arcOnly = rotateByShortestArc(bindTangent, bindNormal, posedNormal);
		CHECK(arcOnly.x == Catch::Approx(1.0f).margin(1e-2f));
		CHECK(TexelTwist(normals, 0, 1) == Catch::Approx(glm::half_pi<float>()).margin(margin));
	}

	SECTION("the rest frame and a pure tilt bake no twist")
	{
		CHECK(TexelTwist(normals, 0, 0) == Catch::Approx(0.0f).margin(margin));
		CHECK(TexelTwist(normals, 0, 2) == Catch::Approx(0.0f).margin(margin));
	}

	SECTION("a mesh without a tangent bakes no twist either")
	{
		VatFixture      plain;
		const BVat      plainVat     = bakeVat(plain.mesh, plain.skeleton, plain.animations);
		const ImageData plainNormals = decodeKTX2(plainVat.normalsKtx2);
		for (uint32_t row = 0; row < plainVat.height; ++row)
			for (uint32_t column = 0; column < plainVat.width; ++column)
				CHECK(TexelTwist(plainNormals, column, row) == Catch::Approx(0.0f).margin(margin));
	}
}

TEST_CASE("Each clip ends on a duplicated padding row", "[vat]")
{
	VatFixture fixture;
	const BVat vat = bakeVat(fixture.mesh, fixture.skeleton, fixture.animations);

	const ImageData positions = decodeKTX2(vat.positionsKtx2);
	const ImageData normals   = decodeKTX2(vat.normalsKtx2);

	const auto rowsMatch = [](const ImageData& image, uint32_t a, uint32_t b) {
		const uint64_t pitch = image.subresources[0].rowPitch;
		return std::memcmp(
				   image.pixels.data() + image.subresources[0].offset + a * pitch,
				   image.pixels.data() + image.subresources[0].offset + b * pitch,
				   pitch) == 0;
	};

	for (const VatClip& clip : vat.clips)
	{
		const uint32_t last = clip.firstRow + clip.frameCount - 1;
		CHECK(rowsMatch(positions, last, last + 1));
		CHECK(rowsMatch(normals, last, last + 1));
	}

	// And the two clips do not share rows: "slide" spans [0, 3], "rest" starts at 4.
	CHECK(vat.clips[0].firstRow == 0);
	CHECK(vat.clips[1].firstRow == 4);
	CHECK(vat.clips[1].firstPalette == 6);
}

TEST_CASE("A .bvat round-trips, and its tables read without the pixels", "[vat]")
{
	VatFixture fixture;
	BVat       vat = bakeVat(fixture.mesh, fixture.skeleton, fixture.animations);

	vat.mesh            = "Meshes/rig.bmesh";
	vat.skeleton        = "Skeletons/rig.bskel";
	vat.animations      = "Animations/rig.banim";
	vat.meshStamp       = { 123, 456 };
	vat.skeletonStamp   = { 7, 8 };
	vat.animationsStamp = { 9, 10 };

	const BVat read = deserializeVat(serializeVat(vat));

	CHECK(read.width == vat.width);
	CHECK(read.height == vat.height);
	CHECK(read.boneCount == vat.boneCount);
	CHECK(read.boundsMin == vat.boundsMin);
	CHECK(read.boundsMax == vat.boundsMax);
	CHECK(read.clips.size() == vat.clips.size());
	CHECK(read.columns.size() == vat.columns.size());
	CHECK(read.palettes == vat.palettes);
	CHECK(read.mesh == vat.mesh);
	CHECK(read.skeleton == vat.skeleton);
	CHECK(read.animations == vat.animations);
	CHECK(read.skeletonSignature == vat.skeletonSignature);
	CHECK(read.meshStamp == vat.meshStamp);
	CHECK(read.animationsStamp == vat.animationsStamp);
	CHECK(read.positionsKtx2 == vat.positionsKtx2);
	CHECK(read.normalsKtx2 == vat.normalsKtx2);
	CHECK(read.stringPool.at(read.clips[0].nameOffset) == "slide");
	CHECK(read.stringPool.at(read.clips[1].nameOffset) == "rest");

	SECTION("tables-only leaves the payloads behind")
	{
		const fs::path path = fs::temp_directory_path() / "bernini_vat_roundtrip.bvat";
		saveVat(vat, path);

		const BVat tables = loadVatTables(path);
		CHECK(tables.positionsKtx2.empty());
		CHECK(tables.normalsKtx2.empty());
		CHECK(tables.width == vat.width);
		CHECK(tables.palettes == vat.palettes);
		CHECK(tables.mesh == vat.mesh);
		CHECK(tables.stringPool.at(tables.clips[0].nameOffset) == "slide");

		// And what a tables-only read holds cannot be written back as though it were the bake.
		CHECK_THROWS_WITH(serializeVat(tables), Catch::Matchers::ContainsSubstring("tables-only"));

		const VatRefs refs = loadVatRefs(path);
		CHECK(refs.mesh == "Meshes/rig.bmesh");
		CHECK(refs.skeleton == "Skeletons/rig.bskel");
		CHECK(refs.animations == "Animations/rig.banim");

		fs::remove(path);
	}

	SECTION("a bake from before the cache format does not read")
	{
		// The twist-era files and every other chunk-era bake are refused whole: derived, so the
		// refusal is what sends them to a re-bake rather than to a reader.
		auto bytes = serializeVat(vat);

		// A chunk-era header carried a version pair where the cache header's version field sits.
		bytes[4] = std::byte{ 4 };
		bytes[5] = std::byte{ 0 };
		bytes[6] = std::byte{ 0 };
		bytes[7] = std::byte{ 0 };

		CHECK_THROWS_WITH(
			deserializeVat(bytes),
			Catch::Matchers::ContainsSubstring("before the cache format"));
	}

	SECTION("a clip with no frames is a malformed file, not a caller's problem")
	{
		// The bake can never emit one (validateAnimationSet refuses a zero-frame clip), so this
		// guards a crafted or corrupted stream -- which consumers index frames from.
		vat.clips[1].frameCount = 0;
		vat.height -= 1;
		CHECK_THROWS_WITH(serializeVat(vat), Catch::Matchers::ContainsSubstring("no frames"));
	}
}

TEST_CASE("A bake from files stamps its inputs and the refs scan reports them", "[vat]")
{
	VatFixture fixture;

	const fs::path root = fs::temp_directory_path() / "bernini_vat_bake_root";
	fs::remove_all(root);
	fs::create_directories(root / "Meshes");
	fs::create_directories(root / "Skeletons");
	fs::create_directories(root / "Animations");

	save(fixture.mesh, root / "Meshes/rig.bmesh");
	saveSkeleton(fixture.skeleton, root / "Skeletons/rig.bskel");
	saveAnimations(fixture.animations, root / "Animations/rig.banim");

	auto desc       = VatBakeDesc();
	desc.mesh       = "Meshes/rig.bmesh";
	desc.animations = "Animations/rig.banim";

	const BVat vat = bakeVat(AssetStore(root), desc);
	CHECK(vat.mesh == "Meshes/rig.bmesh");
	CHECK(vat.skeleton == "Skeletons/rig.bskel");
	CHECK(vat.animations == "Animations/rig.banim");
	CHECK(vat.meshStamp == stampOf(root / "Meshes/rig.bmesh"));
	CHECK(vat.skeletonStamp == stampOf(root / "Skeletons/rig.bskel"));
	CHECK(vat.animationsStamp == stampOf(root / "Animations/rig.banim"));
	CHECK_FALSE(vatIsStale(vat, MountAt(root)));

	// The bug the content stamp exists for, on the VAT path: a checkout rewrites the inputs' mtimes
	// without changing a byte, and a `.bvat` that noticed would be re-baked on every acquire.
	SECTION("an input whose mtime moved but whose bytes did not is not stale")
	{
		for (const char* input :
		     { "Meshes/rig.bmesh", "Skeletons/rig.bskel", "Animations/rig.banim" })
		{
			const fs::path path = root / input;
			fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(5));
		}

		CHECK(vat.meshStamp == stampOf(root / "Meshes/rig.bmesh"));
		CHECK_FALSE(vatIsStale(vat, MountAt(root)));
	}

	SECTION("a changed input reads as stale")
	{
		fixture.animations.stringPool.add("padding-so-the-size-moves");
		saveAnimations(fixture.animations, root / "Animations/rig.banim");
		CHECK(vatIsStale(vat, MountAt(root)));
	}

	SECTION("a deleted input reads as stale, not as unchanged")
	{
		fs::remove(root / "Animations/rig.banim");
		CHECK(vatIsStale(vat, MountAt(root)));
	}

	SECTION("the refs scan reports the three edges")
	{
		saveVat(vat, root / "Meshes/rig.bvat");

		const auto graph = AssetRefGraph::Scan(AssetStore(root));
		CHECK(graph.vatsScanned == 1);

		const auto edges = graph.ReferencesOf("Meshes/rig.bvat");
		REQUIRE(edges.size() == 3);
		for (const AssetRef& edge : edges) CHECK(edge.kind == RefKind::kVatSource);

		CHECK(graph.IsReferenced("Meshes/rig.bmesh"));
		CHECK(graph.IsReferenced("Animations/rig.banim"));
		CHECK(graph.broken.empty());
	}

	SECTION("a bake sweeps with its inputs rather than blocking them")
	{
		saveVat(vat, root / "Meshes/rig.bvat");

		const auto graph = AssetRefGraph::Scan(AssetStore(root));

		// The clip set: referenced only by the bake, so deletable, and the bake goes with it.
		const DeletionPlan plan = planDeletion(graph, "Animations/rig.banim");
		CHECK(plan.Allowed());
		REQUIRE(plan.derived.size() == 1);
		CHECK(plan.derived[0] == "Meshes/rig.bvat");

		const DeletionResult result = deleteAsset(plan, AssetStore(root));
		CHECK(result.status == DeletionStatus::kDeleted);
		CHECK_FALSE(fs::exists(root / "Animations/rig.banim"));
		CHECK_FALSE(fs::exists(root / "Meshes/rig.bvat"));

		// A real referrer still blocks: the mesh is held by nothing now (the bake is gone), but
		// the skeleton is held by the mesh, whose kMeshSkeleton edge is not a bake's.
		const auto after = AssetRefGraph::Scan(AssetStore(root));
		CHECK_FALSE(planDeletion(after, "Skeletons/rig.bskel").Allowed());
	}

	SECTION("a directory blocked only by a bake outside it is deletable")
	{
		saveVat(vat, root / "Meshes/rig.bvat");

		const auto graph = AssetRefGraph::Scan(AssetStore(root));

		const DeletionPlan plan = planDeletion(graph, "Animations");
		CHECK(plan.Allowed());
		REQUIRE(plan.derived.size() == 1);
		CHECK(plan.derived[0] == "Meshes/rig.bvat");
	}

	SECTION("describe reads the tables alone and reports a stale input")
	{
		saveVat(vat, root / "Meshes/rig.bvat");
		fixture.animations.stringPool.add("padding-so-the-size-moves");
		saveAnimations(fixture.animations, root / "Animations/rig.banim");

		const core::file::LooseFileSystem files(root);
		const std::string text = describe(loadVatTables(root / "Meshes/rig.bvat"), &files);
		CHECK(text.find("bvat") != std::string::npos);
		CHECK(text.find("slide") != std::string::npos);
		CHECK(text.find("(not read)") != std::string::npos);
		CHECK(text.find("STALE") != std::string::npos);
	}

	fs::remove_all(root);
}

TEST_CASE("The bake refuses what it cannot represent, naming the count", "[vat]")
{
	VatFixture fixture;

	SECTION("more padded frame rows than a texture can hold")
	{
		AnimationClip& slide = fixture.animations.clips[0];
		slide.frameCount     = 20000;
		fixture.animations.samples.clear();
		fixture.animations.clips.resize(1);
		for (uint32_t frame = 0; frame < 20000; ++frame)
		{
			fixture.animations.samples.push_back(fixture.skeleton.bones[0].bindPose);
			fixture.animations.samples.push_back(fixture.skeleton.bones[1].bindPose);
		}

		CHECK_THROWS_WITH(
			bakeVat(fixture.mesh, fixture.skeleton, fixture.animations),
			Catch::Matchers::ContainsSubstring("20001") &&
				Catch::Matchers::ContainsSubstring("16384"));
	}

	SECTION("more vertex columns than a texture can hold")
	{
		Submesh wide{};
		wide.layout.attributeCount = 2;
		wide.layout.attributes[0]  = { VertexSemantic::kPosition, VertexFormat::kFloat32x3, 0 };
		wide.layout.attributes[1]  = { VertexSemantic::kJoints0, VertexFormat::kUint16x4, 12 };
		// Joints without weights would be refused by the skinner; this refusal fires first, on the
		// counts alone, so the layout only has to look skinned.
		wide.vertexByteOffset = static_cast<uint32_t>(fixture.mesh.vertexData.size());
		wide.vertexCount      = 20000;
		wide.layout.stride    = 20;
		fixture.mesh.vertexData.resize(fixture.mesh.vertexData.size() + 20000 * 20);
		fixture.mesh.submeshes.push_back(wide);

		CHECK_THROWS_WITH(
			bakeVat(fixture.mesh, fixture.skeleton, fixture.animations),
			Catch::Matchers::ContainsSubstring("20003") &&
				Catch::Matchers::ContainsSubstring("16384"));
	}

	SECTION("a mesh with no joints has nothing to animate")
	{
		BMesh flat;
		flat.submeshes.push_back(fixture.mesh.submeshes[1]);
		flat.vertexData = fixture.mesh.vertexData;

		CHECK_THROWS_WITH(
			bakeVat(flat, fixture.skeleton, fixture.animations),
			Catch::Matchers::ContainsSubstring("nothing to animate"));
	}

	SECTION("clips cooked against another rig are refused by signature")
	{
		fixture.animations.skeletonSignature ^= 1;
		CHECK_THROWS_WITH(
			bakeVat(fixture.mesh, fixture.skeleton, fixture.animations),
			Catch::Matchers::ContainsSubstring("different rig"));
	}

	SECTION("an empty clip set has nothing to bake")
	{
		fixture.animations.clips.clear();
		fixture.animations.samples.clear();
		CHECK_THROWS_AS(
			bakeVat(fixture.mesh, fixture.skeleton, fixture.animations),
			std::runtime_error);
	}
}
