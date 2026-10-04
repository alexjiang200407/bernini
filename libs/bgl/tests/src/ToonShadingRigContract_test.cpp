#include "gfx/GraphicsBase.h"
#include "scene/toon_shading_rig_record.h"
#include "util/DispatchReport.h"
#include "util/GpuValidation.h"
#include "util/SkinnedSynth.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/idl/ToonShadingRig.h>
#include <bgl/idl/ToonShadingRigBlock.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/InstanceDesc.h>
#include <bgl/types/MeshInstanceBlockDesc.h>
#include <bgl/types/MeshInstanceBlockHandle.h>
#include <bgl/types/MeshInstanceHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/SkinnedMeshInstanceDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/ToonShadingRigDesc.h>
#include <bgl/types/ToonShadingRigHandle.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The toon-shading-rig contract as bgl owns it: what AddToonShadingRig refuses, which placements may take a
// rig, the lifetime a held rig has, and the layout the evaluation pass and the pixel stage read.

namespace
{
	bgl::test::GraphicsSetup
	HeadlessOptions()
	{
		auto opts                        = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.gpuContext.enableDebugLayer = false;
		return opts;
	}

	/** Two edits, one mirrored: three slots of the eight. */
	bgl::ToonShadingRigDesc
	ValidRig()
	{
		const auto side  = bgl::ToonShadingRigKeyDesc()
		                       .SetLight(glm::vec3(1.0f, 0.0f, 0.5f))
		                       .SetPosition(glm::vec3(0.02f, 0.0f, 0.1f))
		                       .SetGain(-0.8f);
		const auto front = bgl::ToonShadingRigKeyDesc()
		                       .SetLight(glm::vec3(0.0f, 0.0f, 1.0f))
		                       .SetPosition(glm::vec3(0.02f, 0.0f, 0.1f));

		return bgl::ToonShadingRigDesc().SetEdits(
			{ bgl::ToonShadingRigEditDesc().SetKeys({ side, front }).SetMirrored(true),
		      bgl::ToonShadingRigEditDesc().SetKeys({ front }) });
	}

	bgl::MeshInstanceHandle
	AddStaticPlacement(bgl::IScene& scene, bgl::ISceneView& view)
	{
		const bgl::GeomHandle geom =
			scene.AddSphereGeom(8, 8, 0.1f, scene.CreatePbrMaterial(bgl::PbrMaterialDesc()));
		return view.CreateStaticMeshInstance(bgl::StaticMeshInstanceDesc().SetGeom(geom));
	}

	/** A placement of the one-bone sliding quad. */
	bgl::MeshInstanceHandle
	AddSkinnedPlacement(bgl::IScene& scene, bgl::ISceneView& view)
	{
		const bgl::GeomHandle geom = bgl::test::skinned_synth::AddSlidingQuadGeom(
			scene,
			scene.CreatePbrMaterial(bgl::PbrMaterialDesc()));
		return view.CreateSkinnedMeshInstance(
			bgl::SkinnedMeshInstanceDesc().SetGeom(geom).SetPlayback(
				bgl::SkinnedPlaybackDesc::FromClip(bgl::test::skinned_synth::c_LoopClip)));
	}

}

TEST_CASE("AddToonShadingRig refuses a rig no pass could evaluate", "[toonshadingrig][contract]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());

	CHECK(scene->AddToonShadingRig(ValidRig()).IsValid());
	CHECK(scene->AddToonShadingRig(bgl::ToonShadingRigDesc()).IsValid());

	const float     nan  = std::numeric_limits<float>::quiet_NaN();
	constexpr float c_Pi = std::numbers::pi_v<float>;

	const auto key = [](bgl::ToonShadingRigDesc& d) -> bgl::ToonShadingRigKeyDesc& {
		return d.edits[0].keys[0];
	};

	using Break = std::function<void(bgl::ToonShadingRigDesc&)>;
	const std::vector<std::pair<std::string, Break>> breaks = {
		{ "more slots than a rig holds",
		  [](bgl::ToonShadingRigDesc& d) {
			  const bgl::ToonShadingRigEditDesc single = d.edits[1];
			  d.edits.resize(bgl::cMaxToonShadingRigSlots, single);
		  } },
		{ "an edit with no keys", [](bgl::ToonShadingRigDesc& d) { d.edits[1].keys.clear(); } },
		{ "more keys than an edit may hold",
		  [](bgl::ToonShadingRigDesc& d) {
			  const bgl::ToonShadingRigKeyDesc front = d.edits[1].keys[0];
			  d.edits[1].keys.assign(bgl::cMaxToonShadingRigKeysPerEdit + 1, front);
		  } },
		{ "a zero key sharpness",
		  [](bgl::ToonShadingRigDesc& d) { d.edits[0].keySharpness         = 0.0f; } },
		{ "a NaN key sharpness",
		  [nan](bgl::ToonShadingRigDesc& d) { d.edits[0].keySharpness      = nan; } },
		{ "a zero light", [key](bgl::ToonShadingRigDesc& d) { key(d).light = glm::vec3(0.0f); } },
		{ "a NaN light", [key, nan](bgl::ToonShadingRigDesc& d) { key(d).light.y = nan; } },
		{ "a light too long to square",
		  [key](bgl::ToonShadingRigDesc& d) { key(d).light = glm::vec3(1e30f, 0.0f, 0.0f); } },
		{ "a NaN position", [key, nan](bgl::ToonShadingRigDesc& d) { key(d).position.z = nan; } },
		{ "a NaN gain", [key, nan](bgl::ToonShadingRigDesc& d) { key(d).gain           = nan; } },
		{ "an infinite rotation",
		  [key](bgl::ToonShadingRigDesc& d) {
			  key(d).rotation = std::numeric_limits<float>::infinity();
		  } },
		{ "a zero size", [key](bgl::ToonShadingRigDesc& d) { key(d).size         = 0.0f; } },
		{ "a negative radius", [key](bgl::ToonShadingRigDesc& d) { key(d).radius = -0.1f; } },
		{ "an anisotropy of one", [key](bgl::ToonShadingRigDesc& d) { key(d).anisotropy = 1.0f; } },
		{ "a negative anisotropy",
		  [key](bgl::ToonShadingRigDesc& d) { key(d).anisotropy = -0.1f; } },
		{ "a sharpness above one", [key](bgl::ToonShadingRigDesc& d) { key(d).sharpness = 1.5f; } },
		{ "a negative normal smoothing",
		  [key](bgl::ToonShadingRigDesc& d) { key(d).normalSmoothing          = -0.5f; } },
		{ "a zero head radius", [](bgl::ToonShadingRigDesc& d) { d.headRadius = 0.0f; } },
		{ "a fade that ends where it starts",
		  [](bgl::ToonShadingRigDesc& d) { d.fadeEndPixels = d.fadeStartPixels; } },
		{ "a negative fade end", [](bgl::ToonShadingRigDesc& d) { d.fadeEndPixels = -1.0f; } },
		{ "a minimum elevation below straight down",
		  [](bgl::ToonShadingRigDesc& d) { d.faceLight.minElevation               = -c_Pi; } },
		{ "elevations the wrong way round",
		  [](bgl::ToonShadingRigDesc& d) {
			  d.faceLight.minElevation = 0.5f;
			  d.faceLight.maxElevation = 0.2f;
		  } },
		{ "a maximum azimuth past pi",
		  [](bgl::ToonShadingRigDesc& d) { d.faceLight.maxAzimuth = 4.0f; } },
		{ "a negative maximum azimuth",
		  [](bgl::ToonShadingRigDesc& d) { d.faceLight.maxAzimuth = -0.1f; } },
		{ "an azimuth fade that ends before it starts",
		  [](bgl::ToonShadingRigDesc& d) {
			  d.faceLight.azimuthFadeEnd = d.faceLight.azimuthFadeStart;
		  } },
		{ "an azimuth fade amount above one",
		  [](bgl::ToonShadingRigDesc& d) { d.faceLight.azimuthFadeAmount = 1.5f; } },
		{ "a projective head transform",
		  [](bgl::ToonShadingRigDesc& d) { d.headToBone[0][3]            = 0.5f; } },
		{ "a singular head transform",
		  [](bgl::ToonShadingRigDesc& d) { d.headToBone[1]               = glm::vec4(0.0f); } },
		{ "the no-bone sentinel as a head bone",
		  [](bgl::ToonShadingRigDesc& d) { d.headBoneIndex = bgl::idl::cNoHeadBone; } },
		{ "a NaN head transform", [nan](bgl::ToonShadingRigDesc& d) { d.headToBone[3][0] = nan; } },
	};

	for (const auto& [name, broken] : breaks)
	{
		INFO(name);
		auto desc = ValidRig();
		broken(desc);
		CHECK_THROWS_AS(scene->AddToonShadingRig(desc), bgl::SceneError);
	}

	auto                             longest = ValidRig();
	const bgl::ToonShadingRigKeyDesc front   = longest.edits[1].keys[0];
	longest.edits[1].keys.assign(bgl::cMaxToonShadingRigKeysPerEdit, front);
	CHECK_NOTHROW(scene->AddToonShadingRig(longest));

	// Exactly the eight slots a rig holds is not refused: four mirrored edits.
	auto                              full     = ValidRig();
	const bgl::ToonShadingRigEditDesc mirrored = full.edits[0];
	full.edits.assign(bgl::cMaxToonShadingRigSlots / 2, mirrored);
	CHECK_NOTHROW(scene->AddToonShadingRig(full));
}

TEST_CASE(
	"A held toon shading rig outlives DeleteToonShadingRig until its placements let go",
	"[toonshadingrig][contract]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 4);

	const bgl::ToonShadingRigHandle rig   = scene->AddToonShadingRig(ValidRig());
	const bgl::ToonShadingRigHandle other = scene->AddToonShadingRig(ValidRig());
	const auto                      a     = AddStaticPlacement(*scene, *view);
	const auto                      b     = AddStaticPlacement(*scene, *view);

	CHECK_FALSE(view->GetToonShadingRig(a).IsValid());

	view->SetToonShadingRig(a, rig);
	view->SetToonShadingRig(b, rig);
	CHECK(view->GetToonShadingRig(a) == rig);
	CHECK_THROWS_AS(scene->DeleteToonShadingRig(rig), bgl::SceneError);

	SECTION("cleared")
	{
		view->ClearToonShadingRig(a);
		CHECK_FALSE(view->GetToonShadingRig(a).IsValid());
		CHECK_NOTHROW(view->ClearToonShadingRig(a));
		CHECK_THROWS_AS(scene->DeleteToonShadingRig(rig), bgl::SceneError);
		view->ClearToonShadingRig(b);
		CHECK_NOTHROW(scene->DeleteToonShadingRig(rig));
	}

	SECTION("replaced")
	{
		view->SetToonShadingRig(a, other);
		view->SetToonShadingRig(b, other);
		CHECK(view->GetToonShadingRig(b) == other);
		CHECK_NOTHROW(scene->DeleteToonShadingRig(rig));
		CHECK_THROWS_AS(scene->DeleteToonShadingRig(other), bgl::SceneError);
	}

	SECTION("the same rig twice holds one use")
	{
		view->SetToonShadingRig(a, rig);
		view->ClearToonShadingRig(a);
		view->ClearToonShadingRig(b);
		CHECK_NOTHROW(scene->DeleteToonShadingRig(rig));
	}

	SECTION("deleted placements")
	{
		view->DeleteMeshInstance(a);
		view->DeleteMeshInstance(b);
		CHECK_NOTHROW(scene->DeleteToonShadingRig(rig));
	}

	SECTION("a destroyed view")
	{
		view.Reset();
		CHECK_NOTHROW(scene->DeleteToonShadingRig(rig));
	}

	SECTION("deleted twice")
	{
		view->ClearToonShadingRig(a);
		view->ClearToonShadingRig(b);
		scene->DeleteToonShadingRig(rig);
		CHECK_THROWS_AS(scene->DeleteToonShadingRig(rig), bgl::SceneError);
		CHECK_THROWS_AS(view->SetToonShadingRig(a, rig), bgl::SceneError);
	}

	CHECK_THROWS_AS(scene->DeleteToonShadingRig(bgl::ToonShadingRigHandle()), bgl::SceneError);
}

TEST_CASE(
	"SetToonShadingRig refuses a head bone the placement has no pose for",
	"[toonshadingrig][contract]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 4);

	const auto placed  = AddStaticPlacement(*scene, *view);
	const auto skinned = AddSkinnedPlacement(*scene, *view);

	const bgl::ToonShadingRigHandle ownFrame = scene->AddToonShadingRig(ValidRig());
	const bgl::ToonShadingRigHandle boneZero =
		scene->AddToonShadingRig(ValidRig().SetHeadBoneIndex(0u));
	const bgl::ToonShadingRigHandle pastRig =
		scene->AddToonShadingRig(ValidRig().SetHeadBoneIndex(1u));

	CHECK_NOTHROW(view->SetToonShadingRig(placed, ownFrame));
	CHECK_NOTHROW(view->SetToonShadingRig(skinned, ownFrame));
	CHECK_NOTHROW(view->SetToonShadingRig(skinned, boneZero));

	CHECK_THROWS_AS(view->SetToonShadingRig(placed, boneZero), bgl::SceneError);
	CHECK_THROWS_AS(view->SetToonShadingRig(skinned, pastRig), bgl::SceneError);

	// A refused call leaves the placement holding what it held.
	CHECK(view->GetToonShadingRig(placed) == ownFrame);
	CHECK(view->GetToonShadingRig(skinned) == boneZero);
	CHECK_NOTHROW(scene->DeleteToonShadingRig(pastRig));

	CHECK_THROWS_AS(view->SetToonShadingRig(placed, bgl::ToonShadingRigHandle()), bgl::SceneError);
	CHECK_THROWS_AS(view->SetToonShadingRig(bgl::MeshInstanceHandle(), ownFrame), bgl::SceneError);
	CHECK_THROWS_AS(view->ClearToonShadingRig(bgl::MeshInstanceHandle()), bgl::SceneError);
	CHECK_THROWS_AS(view->GetToonShadingRig(bgl::MeshInstanceHandle()), bgl::SceneError);
}

TEST_CASE(
	"An instance block holds one use of the toon shading rig it shares",
	"[toonshadingrig][contract][instance_block]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 4);

	const bgl::GeomHandle sphere =
		scene->AddSphereGeom(8, 8, 0.1f, scene->CreatePbrMaterial(bgl::PbrMaterialDesc()));
	const bgl::ToonShadingRigHandle rig = scene->AddToonShadingRig(ValidRig());

	const auto blockOf = [&](const bgl::ToonShadingRigHandle shared) {
		return view->CreateMeshInstanceBlock(
			bgl::MeshInstanceBlockDesc().SetGeom(sphere).SetCapacity(16).SetToonShadingRig(shared));
	};

	const bgl::MeshInstanceBlockHandle first  = blockOf(rig);
	const bgl::MeshInstanceBlockHandle second = blockOf(rig);
	CHECK(blockOf(bgl::ToonShadingRigHandle()).IsValid());
	CHECK_THROWS_AS(scene->DeleteToonShadingRig(rig), bgl::SceneError);

	SECTION("until every block holding it is deleted")
	{
		view->DeleteMeshInstanceBlock(first);
		CHECK_THROWS_AS(scene->DeleteToonShadingRig(rig), bgl::SceneError);
		view->DeleteMeshInstanceBlock(second);
		CHECK_NOTHROW(scene->DeleteToonShadingRig(rig));
	}

	SECTION("until its view is destroyed")
	{
		view.Reset();
		CHECK_NOTHROW(scene->DeleteToonShadingRig(rig));
	}
}

TEST_CASE(
	"An instance block is refused a toon shading rig it could not carry",
	"[toonshadingrig][contract][instance_block]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 4);

	const auto            material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
	const bgl::GeomHandle sphere   = scene->AddSphereGeom(8, 8, 0.1f, material);
	const bgl::GeomHandle quad     = bgl::test::skinned_synth::AddSlidingQuadGeom(*scene, material);

	const bgl::ToonShadingRigHandle dead = scene->AddToonShadingRig(ValidRig());
	scene->DeleteToonShadingRig(dead);
	const bgl::ToonShadingRigHandle boneZero =
		scene->AddToonShadingRig(ValidRig().SetHeadBoneIndex(0u));
	const bgl::ToonShadingRigHandle pastRig =
		scene->AddToonShadingRig(ValidRig().SetHeadBoneIndex(1u));

	const auto block = [](const bgl::GeomHandle geom, const bgl::ToonShadingRigHandle rig) {
		return bgl::MeshInstanceBlockDesc()
		    .SetGeom(geom)
		    .SetCapacity(16)
		    .SetPlayback(bgl::SkinnedPlaybackDesc::FromClip(bgl::test::skinned_synth::c_LoopClip))
		    .SetToonShadingRig(rig);
	};

	CHECK_THROWS_WITH(
		view->CreateMeshInstanceBlock(block(sphere, dead)),
		Catch::Matchers::ContainsSubstring("null, or already deleted"));
	CHECK_THROWS_WITH(
		view->CreateMeshInstanceBlock(block(sphere, boneZero)),
		Catch::Matchers::ContainsSubstring("needs a skinned geom"));

	// The rig is checked ahead of the refusal every skinned block still meets, so the message
	// names the bone.
	CHECK_THROWS_WITH(
		view->CreateMeshInstanceBlock(block(quad, pastRig)),
		Catch::Matchers::ContainsSubstring("headBoneIndex 1"));

	// A refused block holds nothing.
	CHECK_NOTHROW(scene->DeleteToonShadingRig(boneZero));
	CHECK_NOTHROW(scene->DeleteToonShadingRig(pastRig));
}

TEST_CASE("AddToonShadingRig packs what the evaluation pass reads", "[toonshadingrig][contract]")
{
	const auto key = [](const float gain) {
		return bgl::ToonShadingRigKeyDesc()
		    .SetLight(glm::vec3(0.0f, 3.0f, 4.0f))
		    .SetPosition(glm::vec3(0.1f, 0.2f, 0.3f))
		    .SetGain(gain)
		    .SetSize(0.25f)
		    .SetAnisotropy(0.5f)
		    .SetSharpness(0.75f)
		    .SetBend(-1.0f)
		    .SetBulge(2.0f)
		    .SetRotation(1.5f)
		    .SetRadius(0.07f)
		    .SetNormalSmoothing(0.125f);
	};

	auto headToBone  = glm::mat4(1.0f);
	headToBone[3]    = glm::vec4(1.0f, 2.0f, 3.0f, 1.0f);
	headToBone[0][1] = 0.5f;

	const auto desc =
		bgl::ToonShadingRigDesc()
			.SetHeadBoneIndex(4u)
			.SetHeadToBone(headToBone)
			.SetEdits(
				{ bgl::ToonShadingRigEditDesc()
	                  .SetKeys({ key(-0.5f), key(0.0f) })
	                  .SetMirrored(true),
	              bgl::ToonShadingRigEditDesc().SetKeys({ key(0.25f), key(0.0f), key(0.5f) }),
	              bgl::ToonShadingRigEditDesc()
	                  .SetKeys({ key(-0.5f), key(0.5f) })
	                  .SetKeySharpness(24.0f),
	              bgl::ToonShadingRigEditDesc().SetKeys({ key(0.0f) }) });

	const bgl::PackedToonShadingRig packed = bgl::PackToonShadingRig(desc);
	const bgl::idl::ToonShadingRig& record = packed.record;

	CHECK(record.editCount == 4);
	CHECK(record.slotCount == 5);
	CHECK(record.headBoneIndex == 4);
	CHECK(record.keys.Null());
	REQUIRE(packed.keys.size() == 8);

	const std::array<uint32_t, 4> firstKeys = { 0, 2, 5, 7 };
	const std::array<uint32_t, 4> keyCounts = { 2, 3, 2, 1 };
	const std::array<uint32_t, 4> flags     = {
		std::to_underlying(bgl::idl::ToonShadingRigEditFlag::kShadeOnly) |
			std::to_underlying(bgl::idl::ToonShadingRigEditFlag::kMirrored),
		std::to_underlying(bgl::idl::ToonShadingRigEditFlag::kLightOnly),
		0u,
		0u,
	};
	for (uint32_t e = 0; e < 4; ++e)
	{
		INFO("edit " << e);
		CHECK(record.edits[e].firstKey == firstKeys[e]);
		CHECK(record.edits[e].keyCount == keyCounts[e]);
		CHECK(record.edits[e].flags == flags[e]);
	}
	CHECK(record.edits[2].keySharpness == 24.0f);
	CHECK(record.edits[0].keySharpness == 10.0f);

	const bgl::idl::ToonShadingRigKey& first = packed.keys[0];
	CHECK(first.lightAndGain.x == Catch::Approx(0.0f));
	CHECK(first.lightAndGain.y == Catch::Approx(0.6f));
	CHECK(first.lightAndGain.z == Catch::Approx(0.8f));
	CHECK(first.lightAndGain.w == -0.5f);
	CHECK(first.positionAndSize == glm::vec4(0.1f, 0.2f, 0.3f, 0.25f));
	CHECK(first.shape == glm::vec4(0.5f, 0.75f, -1.0f, 2.0f));
	CHECK(first.rotationRadiusSmoothing == glm::vec4(1.5f, 0.07f, 0.125f, 0.0f));
	CHECK(packed.keys[3].lightAndGain.w == 0.0f);

	// Rows, as a placement's transform is stored: the translation is each row's w.
	CHECK(record.headToBone[0] == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
	CHECK(record.headToBone[1] == glm::vec4(0.5f, 1.0f, 0.0f, 2.0f));
	CHECK(record.headToBone[2] == glm::vec4(0.0f, 0.0f, 1.0f, 3.0f));

	CHECK(
		bgl::PackToonShadingRig(bgl::ToonShadingRigDesc()).record.headBoneIndex ==
		bgl::idl::cNoHeadBone);
}

/**
 * A raw buffer loads a toon shading rig, a key and an evaluated block back as the CPU wrote them: the
 * layout bgpu_idlgen's C++ mirror asserts is the one Load<T> reads, at the ends of each struct and
 * of each fixed array, where a target's own packing would show first.
 */
TEST_CASE(
	"A raw buffer loads toon-shading-rig records as written",
	"[toonshadingrig][raw][compute]")
{
	constexpr uint32_t c_RigOffset   = 16;
	constexpr uint32_t c_KeyOffset   = c_RigOffset + 256;
	constexpr uint32_t c_BlockOffset = c_KeyOffset + 64;
	constexpr uint32_t c_BufferBytes = c_BlockOffset + 896;
	constexpr uint32_t c_OutValues   = 12;

	static_assert(c_RigOffset + sizeof(bgl::idl::ToonShadingRig) <= c_KeyOffset);
	static_assert(c_KeyOffset + sizeof(bgl::idl::ToonShadingRigKey) <= c_BlockOffset);
	static_assert(c_BlockOffset + sizeof(bgl::idl::ToonShadingRigBlock) <= c_BufferBytes);
	static_assert(sizeof(bgl::idl::ToonShadingRig) == 240);
	static_assert(sizeof(bgl::idl::ToonShadingRigKey) == 64);
	static_assert(sizeof(bgl::idl::ToonShadingRigSlot) == 96);
	static_assert(
		sizeof(bgl::idl::ToonShadingRigBlock) == 80 + bgl::cMaxToonShadingRigSlots * 96,
		"a block is its header and its slots, unpadded");

	constexpr uint32_t c_Last = bgl::cMaxToonShadingRigSlots - 1;

	// Every field distinct, so a read at a neighbour's offset is a wrong value, not a coincidence.
	auto rig                       = bgl::idl::ToonShadingRig();
	rig.headToBone[2]              = glm::vec4(1.0f, 2.0f, 3.0f, 4.0f);
	rig.headBoneIndex              = 5;
	rig.editCount                  = 6;
	rig.slotCount                  = 7;
	rig.headRadius                 = 0.125f;
	rig.fadeStartPixels            = 96.0f;
	rig.fadeEndPixels              = 48.0f;
	rig.minElevation               = -0.25f;
	rig.maxElevation               = 0.75f;
	rig.maxAzimuth                 = 1.25f;
	rig.azimuthFadeStart           = 0.5f;
	rig.azimuthFadeEnd             = 1.5f;
	rig.azimuthFadeAmount          = 0.625f;
	rig.keys.offsetStart           = 9;
	rig.edits[c_Last].firstKey     = 10;
	rig.edits[c_Last].keyCount     = 11;
	rig.edits[c_Last].keySharpness = 12.5f;
	rig.edits[c_Last].flags = std::to_underlying(bgl::idl::ToonShadingRigEditFlag::kMirrored) |
	                          std::to_underlying(bgl::idl::ToonShadingRigEditFlag::kShadeOnly);

	auto key                    = bgl::idl::ToonShadingRigKey();
	key.lightAndGain            = glm::vec4(0.0f, 0.6f, 0.8f, -0.5f);
	key.rotationRadiusSmoothing = glm::vec4(1.4f, 0.07f, 0.5f, 0.0f);

	auto block                                = bgl::idl::ToonShadingRigBlock();
	block.headFromWorld[1]                    = glm::vec4(21.0f, 22.0f, 23.0f, 24.0f);
	block.faceLight                           = glm::vec4(0.0f, 0.0f, 1.0f, 0.375f);
	block.slotCount                           = 8;
	block.slots[c_Last].bendBulgeRotation     = glm::vec4(0.0f, 0.0f, 0.0f, 0.875f);
	block.slots[c_Last].axisZAndSharpness     = glm::vec4(0.0f, 0.0f, 0.0f, 0.3125f);
	block.slots[c_Last].radiusSmoothingMirror = glm::vec4(0.25f, 0.5f, 1.0f, 0.0f);

	std::array<std::byte, c_BufferBytes> bytes{};
	std::memcpy(bytes.data() + c_RigOffset, &rig, sizeof(rig));
	std::memcpy(bytes.data() + c_KeyOffset, &key, sizeof(key));
	std::memcpy(bytes.data() + c_BlockOffset, &block, sizeof(block));

	const std::vector<glm::vec4> got = bgl::test::DispatchReport(
		"CSToonShadingRigLoad",
		bytes,
		c_OutValues,
		[&](bgpu::ComputeKernel& kernel) {
			kernel["gUniforms"]["rigOffset"]   = c_RigOffset;
			kernel["gUniforms"]["keyOffset"]   = c_KeyOffset;
			kernel["gUniforms"]["blockOffset"] = c_BlockOffset;
		});
	REQUIRE(got.size() == c_OutValues);

	const auto same = [](const glm::vec4& a, const glm::vec4& b) {
		CHECK(a.x == Catch::Approx(b.x));
		CHECK(a.y == Catch::Approx(b.y));
		CHECK(a.z == Catch::Approx(b.z));
		CHECK(a.w == Catch::Approx(b.w));
	};

	same(got[0], rig.headToBone[2]);
	same(got[1], glm::vec4(5.0f, 6.0f, 7.0f, rig.headRadius));
	same(
		got[2],
		glm::vec4(rig.fadeStartPixels, rig.fadeEndPixels, rig.minElevation, rig.maxElevation));
	same(
		got[3],
		glm::vec4(rig.maxAzimuth, rig.azimuthFadeStart, rig.azimuthFadeEnd, rig.azimuthFadeAmount));
	same(got[4], glm::vec4(9.0f, 10.0f, 11.0f, 12.5f));
	CHECK(got[5].x == Catch::Approx(static_cast<float>(rig.edits[c_Last].flags)));
	CHECK(got[5].y == 1.0f);
	CHECK(got[5].z == 1.0f);
	CHECK(got[5].w == 0.0f);
	same(got[6], key.lightAndGain);
	same(got[7], key.rotationRadiusSmoothing);
	same(got[8], block.headFromWorld[1]);
	same(got[9], block.faceLight);
	same(got[10], glm::vec4(8.0f, 0.875f, 0.3125f, 0.0f));
	same(got[11], block.slots[c_Last].radiusSmoothingMirror);
}

/**
 * What a toon character surface that sets nothing means, as the shader contract defaults it: a
 * game's Evaluate starts from these.
 */
TEST_CASE(
	"A default toon character surface means a plain three-tone cel",
	"[toonshadingrig][toon][compute]")
{
	const std::vector<glm::vec4> got =
		bgl::test::DispatchReport("CSToonCharacterDefaults", {}, 4, [](bgpu::ComputeKernel&) {});
	REQUIRE(got.size() == 4);

	CHECK(got[0] == glm::vec4(1.0f));
	CHECK(got[1].x == Catch::Approx(0.75f));
	CHECK(got[1].y == Catch::Approx(0.75f));
	CHECK(got[1].z == Catch::Approx(0.75f));
	CHECK(got[1].w == Catch::Approx(0.5f));
	CHECK(got[2].x == Catch::Approx(0.55f));
	CHECK(got[2].y == Catch::Approx(0.55f));
	CHECK(got[2].z == Catch::Approx(0.55f));
	CHECK(got[2].w == Catch::Approx(0.3f));
	CHECK(got[3].x == Catch::Approx(0.0001f));
	CHECK(got[3].y == Catch::Approx(0.0001f));
	CHECK(got[3].z == 0.0f);
	CHECK(got[3].w == 0.0f);
}
