#include "gfx/GraphicsBase.h"
#include "scene/SceneView.h"
#include "util/PaletteReadback.h"
#include "util/SkinnedSynth.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MeshInstanceFlag.h>
#include <bgl/ToonShadingRigLimits.h>
#include <bgl/glm.h>
#include <bgl/idl/MeshInstance.h>
#include <bgl/idl/ToonShadingRigBlock.h>
#include <bgl/idl/ToonShadingRigPool.h>
#include <bgl/types/Camera.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/InstanceDesc.h>
#include <bgl/types/MeshInstanceBlockDesc.h>
#include <bgl/types/MeshInstanceFlags.h>
#include <bgl/types/MeshInstanceHandle.h>
#include <bgl/types/MeshInstanceWriterDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/SkinnedMeshInstanceDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/ToonShadingRigDesc.h>
#include <bgl/types/ToonShadingRigHandle.h>
#include <bgl/types/Viewport.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <set>
#include <vector>

// The toon shading rig's per-draw selection and evaluation, read back off the GPU: every block
// against a CPU reference of the same math, and which placements the pool takes.

namespace
{
	bgl::test::GraphicsSetup
	HeadlessOptions()
	{
		auto opts                        = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.gpuContext.enableDebugLayer = true;
		return opts;
	}

	bgl::Camera
	FrontCamera()
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 10.0f),
				glm::vec3(0.0f, 0.0f, 9.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), 400.0f / 300.0f, 0.5f, 500.0f);
		return camera;
	}

	/** A world with one view and target, drawn on demand under a chosen sun. */
	struct Fixture
	{
		bgl::GraphicsRef     gfx;
		bgl::SceneRef        scene;
		bgl::SceneViewRef    view;
		bgl::RenderTargetRef target;
		bgl::MaterialHandle  material;

		Fixture()
		{
			gfx = bgl::test::CreateGraphics(HeadlessOptions());
			REQUIRE(gfx != nullptr);
			scene    = gfx->CreateScene(bgl::SceneDesc());
			view     = gfx->CreateSceneView(scene, 16);
			material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());

			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = 400;
			targetDesc.height   = 300;
			targetDesc.headless = true;
			target              = gfx->CreateRenderTarget(targetDesc);
			REQUIRE(target != nullptr);
		}

		void
		Draw(const glm::vec3& toLight, float time = 0.0f)
		{
			view->SetDirectionalLight(
				{ .direction = -glm::normalize(toLight),
			      .color     = glm::vec3(1.0f),
			      .intensity = 1.0f });
			auto job     = bgl::RenderJob();
			job.view     = view;
			job.camera   = FrontCamera();
			job.viewport = bgl::Viewport(400.0f, 300.0f);
			job.time     = time;
			gfx->DrawFrame(target, job);
		}

		[[nodiscard]] bgl::SceneView*
		View() const
		{
			return view->As<bgl::SceneView>();
		}

		[[nodiscard]] bgl::GraphicsBase*
		Base() const
		{
			return gfx->As<bgl::GraphicsBase>();
		}

		[[nodiscard]] std::vector<bgl::idl::ToonShadingRigBlock>
		Blocks() const
		{
			const bgl::ToonShadingRigState& rigs = View()->GetToonShadingRigs();
			return bgl::test::ReadBuffer<bgl::idl::ToonShadingRigBlock>(
				Base(),
				rigs.GetBlockBuffer(),
				rigs.GetBlockCapacity());
		}

		[[nodiscard]] uint32_t
		Selected() const
		{
			return bgl::test::ReadBuffer<bgl::idl::ToonShadingRigPool>(
					   Base(),
					   View()->GetToonShadingRigs().GetPoolBuffer(),
					   1)[0]
			    .selected;
		}

		/** Each placement's flags word as the GPU holds it, by MeshInstance entry. */
		[[nodiscard]] std::vector<uint32_t>
		GpuFlags() const
		{
			auto&      meshes  = View()->GetMeshBuffer();
			const auto records = bgl::test::ReadBuffer<bgl::idl::MeshInstance>(
				Base(),
				meshes.GetBufferHandle(),
				meshes.Capacity());
			auto flags = std::vector<uint32_t>();
			for (const bgl::idl::MeshInstance& record : records)
			{
				flags.push_back(record.flags);
			}
			return flags;
		}

		bgl::MeshInstanceHandle
		Place(const glm::mat4& transform)
		{
			const auto geom = scene->AddSphereGeom(8, 8, 0.5f, material);
			return view->CreateStaticMeshInstance(
				bgl::StaticMeshInstanceDesc().SetGeom(geom).SetTransform(transform));
		}
	};

	/** The block index plus one a placement's flags carry, zero for none. */
	uint32_t
	SlotBits(uint32_t flags)
	{
		return (flags >> bgl::idl::cToonShadingRigSlotShift) & bgl::idl::cToonShadingRigSlotMask;
	}

	bgl::ToonShadingRigKeyDesc
	Key(const glm::vec3& light, const glm::vec3& position, float gain, float size)
	{
		return bgl::ToonShadingRigKeyDesc()
		    .SetLight(light)
		    .SetPosition(position)
		    .SetGain(gain)
		    .SetSize(size);
	}

	/** Two edits, the first mirrored and shade-only, the second of either sign: three slots. */
	bgl::ToonShadingRigDesc
	TestRig()
	{
		const glm::vec3 spot  = glm::vec3(0.05f, 0.04f, 0.11f);
		auto            side  = Key(glm::vec3(1.0f, 0.0f, 0.35f), spot, -0.6f, 0.14f)
		                            .SetAnisotropy(0.35f)
		                            .SetSharpness(0.45f)
		                            .SetRotation(0.9f)
		                            .SetRadius(0.12f)
		                            .SetNormalSmoothing(0.5f);
		auto            front = Key(glm::vec3(0.0f, 0.0f, 1.0f), spot, 0.0f, 0.3f);
		auto low = Key(glm::vec3(0.7f, -0.7f, 0.35f), spot * 1.2f, -0.4f, 0.12f).SetBend(0.3f);

		auto up   = Key(glm::vec3(0.0f, 1.0f, 0.2f), glm::vec3(0.0f, 0.05f, 0.1f), 0.4f, 0.2f)
		                .SetBend(0.2f)
		                .SetBulge(-0.1f);
		auto down = Key(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -0.02f, 0.12f), -0.3f, 0.15f);

		auto faceLight = bgl::FaceLightDesc()
		                     .SetMinElevation(glm::radians(-20.0f))
		                     .SetMaxElevation(glm::radians(15.0f))
		                     .SetMaxAzimuth(glm::radians(60.0f))
		                     .SetAzimuthFadeStart(glm::radians(30.0f))
		                     .SetAzimuthFadeEnd(glm::radians(70.0f))
		                     .SetAzimuthFadeAmount(0.6f);

		return bgl::ToonShadingRigDesc()
		    .SetHeadToBone(
				glm::rotate(
					glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)),
					glm::radians(10.0f),
					glm::vec3(0.0f, 1.0f, 0.0f)))
		    .SetFaceLight(faceLight)
		    .SetHeadRadius(0.5f)
		    .SetFadeStartPixels(10.0f)
		    .SetFadeEndPixels(5.0f)
		    .SetEdits(
				{ bgl::ToonShadingRigEditDesc().SetKeys({ side, front, low }).SetMirrored(true),
		          bgl::ToonShadingRigEditDesc().SetKeys({ up, down }).SetKeySharpness(4.0f) });
	}

	// The CPU reference: the evaluation pass's math, from the desc.

	glm::vec3
	RemapFaceLight(const bgl::FaceLightDesc& face, const glm::vec3& eta)
	{
		float azimuth   = std::atan2(eta.x, eta.z);
		float elevation = std::asin(std::clamp(eta.y, -1.0f, 1.0f));

		float t = std::clamp(
			(elevation - face.azimuthFadeStart) / (face.azimuthFadeEnd - face.azimuthFadeStart),
			0.0f,
			1.0f);
		t = t * t * (3.0f - 2.0f * t);
		azimuth *= 1.0f - t * face.azimuthFadeAmount;
		azimuth   = std::clamp(azimuth, -face.maxAzimuth, face.maxAzimuth);
		elevation = std::clamp(elevation, face.minElevation, face.maxElevation);
		return { std::sin(azimuth) * std::cos(elevation),
			     std::sin(elevation),
			     std::cos(azimuth) * std::cos(elevation) };
	}

	std::array<glm::vec4, 6>
	EvaluateEdit(const bgl::ToonShadingRigEditDesc& edit, const glm::vec3& light, bool mirror)
	{
		float     sum  = 0.0f;
		float     gain = 0.0f;
		glm::vec3 position(0.0f);
		float     size = 0.0f;
		glm::vec4 shape(0.0f);
		glm::vec3 rest(0.0f);
		bool      anyShade = false;
		bool      anyLight = false;
		for (const bgl::ToonShadingRigKeyDesc& key : edit.keys)
		{
			const float w =
				std::exp(edit.keySharpness * (glm::dot(glm::normalize(key.light), light) - 1.0f));
			sum += w;
			gain += w * key.gain;
			position += w * key.position;
			size += w * key.size;
			shape += w * glm::vec4(key.anisotropy, key.sharpness, key.bend, key.bulge);
			rest += w * glm::vec3(key.rotation, key.radius, key.normalSmoothing);
			anyShade = anyShade || key.gain < 0.0f;
			anyLight = anyLight || key.gain > 0.0f;
		}
		const float norm = 1.0f / std::max(sum, 1e-12f);
		gain *= norm;
		position *= norm;
		size *= norm;
		shape *= norm;
		rest *= norm;
		if (anyShade && !anyLight)
		{
			gain = std::min(gain, 0.0f);
		}
		else if (anyLight && !anyShade)
		{
			gain = std::max(gain, 0.0f);
		}

		const glm::vec3 lz =
			glm::length(position) > 1e-6f ? glm::normalize(position) : glm::vec3(0, 0, 1);
		const glm::vec3 across = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), lz);
		const glm::vec3 lx =
			glm::length(across) > 1e-6f ? glm::normalize(across) : glm::vec3(1, 0, 0);
		const glm::vec3 ly = glm::cross(lz, lx);

		return { glm::vec4(position, gain),
			     glm::vec4(lx, std::max(size, 1e-4f)),
			     glm::vec4(ly, std::clamp(shape.x, 0.0f, 0.98f)),
			     glm::vec4(lz, std::clamp(shape.y, 0.0f, 1.0f)),
			     glm::vec4(shape.z, shape.w, std::cos(rest.x), std::sin(rest.x)),
			     glm::vec4(
					 std::max(rest.y, 0.0f),
					 std::clamp(rest.z, 0.0f, 1.0f),
					 mirror ? 1.0f : 0.0f,
					 0.0f) };
	}

	void
	CheckNear(const glm::vec4& actual, const glm::vec4& expected, float margin = 2e-3f)
	{
		CHECK(actual.x == Catch::Approx(expected.x).margin(margin));
		CHECK(actual.y == Catch::Approx(expected.y).margin(margin));
		CHECK(actual.z == Catch::Approx(expected.z).margin(margin));
		CHECK(actual.w == Catch::Approx(expected.w).margin(margin));
	}

	/**
	 * Checks `block` against the reference for `rig` on a head at `headWorld` under `toLight`, its
	 * fade whole. Returns the face light in head space, for a caller checking the remap's shape.
	 */
	glm::vec3
	CheckBlock(
		const bgl::idl::ToonShadingRigBlock& block,
		const bgl::ToonShadingRigDesc&       rig,
		const glm::mat4&                     headWorld,
		const glm::vec3&                     toLight)
	{
		const glm::mat3 rotation(
			glm::normalize(glm::vec3(headWorld[0])),
			glm::normalize(glm::vec3(headWorld[1])),
			glm::normalize(glm::vec3(headWorld[2])));
		const glm::vec3 eta  = glm::normalize(glm::transpose(rotation) * glm::normalize(toLight));
		const glm::vec3 face = RemapFaceLight(rig.faceLight, eta);

		const glm::mat4 rows = glm::transpose(glm::inverse(headWorld));
		for (int r = 0; r < 3; ++r)
		{
			INFO("headFromWorld row " << r);
			CheckNear(block.headFromWorld[r], rows[r]);
		}
		CheckNear(block.faceLight, glm::vec4(glm::normalize(rotation * face), 1.0f));

		uint32_t slot = 0;
		for (size_t e = 0; e < rig.edits.size(); ++e)
		{
			const auto check = [&](const glm::vec3& light, bool mirror) {
				INFO("edit " << e << (mirror ? " mirrored" : ""));
				const auto  expected = EvaluateEdit(rig.edits[e], light, mirror);
				const auto& got      = block.slots[slot++];
				CheckNear(got.positionAndGain, expected[0]);
				CheckNear(got.axisXAndSize, expected[1]);
				CheckNear(got.axisYAndAnisotropy, expected[2]);
				CheckNear(got.axisZAndSharpness, expected[3]);
				CheckNear(got.bendBulgeRotation, expected[4]);
				CheckNear(got.radiusSmoothingMirror, expected[5]);
			};
			check(face, false);
			if (rig.edits[e].mirrored)
			{
				check(glm::vec3(-face.x, face.y, face.z), true);
			}
		}
		CHECK(block.slotCount == slot);
		return face;
	}
}

TEST_CASE(
	"A static placement's toon shading rig is evaluated in its head space",
	"[toonshadingrig][render]")
{
	Fixture world;

	const glm::mat4 placement = glm::scale(
		glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
		glm::vec3(2.0f));
	const auto instance = world.Place(placement);

	const bgl::ToonShadingRigDesc   desc = TestRig();
	const bgl::ToonShadingRigHandle rig  = world.scene->AddToonShadingRig(desc);
	world.view->SetToonShadingRig(instance, rig);

	const glm::mat4 headWorld = placement * desc.headToBone;
	const auto      head      = [&](const glm::vec3& face) {
		return std::pair(std::atan2(face.x, face.z), std::asin(face.y));
	};

	{
		INFO("a sun above and to the right of the face");
		const glm::vec3 sun(0.3f, 0.5f, 1.0f);
		world.Draw(sun);
		const auto blocks = world.Blocks();
		CHECK(world.Selected() == 1u);
		CHECK(blocks[0].placement == instance.handle.index);
		CheckBlock(blocks[0], desc, headWorld, sun);
		CHECK(SlotBits(world.GpuFlags()[instance.handle.index]) == 1u);
	}

	{
		INFO("overhead: the light swings to the front and is held below the elevation cap");
		const glm::vec3 sun(0.6f, 1.0f, 0.05f);
		world.Draw(sun);
		const auto [azimuth, elevation] = head(CheckBlock(world.Blocks()[0], desc, headWorld, sun));
		CHECK(elevation <= desc.faceLight.maxElevation + 1e-4f);
		CHECK(std::abs(azimuth) < desc.faceLight.maxAzimuth);
	}

	{
		INFO("behind the head: the azimuth is held at its clamp");
		const glm::vec3 sun(0.2f, 0.1f, -1.0f);
		world.Draw(sun);
		const glm::vec3 eta = glm::transpose(
								  glm::mat3(
									  glm::normalize(glm::vec3(headWorld[0])),
									  glm::normalize(glm::vec3(headWorld[1])),
									  glm::normalize(glm::vec3(headWorld[2])))) *
		                      glm::normalize(sun);
		REQUIRE(std::abs(std::atan2(eta.x, eta.z)) > glm::radians(90.0f));
		const auto [azimuth, elevation] = head(CheckBlock(world.Blocks()[0], desc, headWorld, sun));
		CHECK(std::abs(azimuth) == Catch::Approx(desc.faceLight.maxAzimuth).margin(1e-4));
	}

	{
		INFO("from the side");
		const glm::vec3 sun(1.0f, 0.0f, 0.0f);
		world.Draw(sun);
		CheckBlock(world.Blocks()[0], desc, headWorld, sun);
	}
}

TEST_CASE(
	"A head bone's toon shading rig follows the pose the frame draws",
	"[toonshadingrig][render]")
{
	Fixture world;

	const auto geom = bgl::test::skinned_synth::AddSlidingQuadGeom(*world.scene, world.material);
	const glm::mat4 placement = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
	const auto      instance  = world.view->CreateSkinnedMeshInstance(
		bgl::SkinnedMeshInstanceDesc().SetGeom(geom).SetTransform(placement).SetPlayback(
			bgl::SkinnedPlaybackDesc::FromClip(bgl::test::skinned_synth::c_LoopClip)));

	const bgl::ToonShadingRigDesc desc = TestRig().SetHeadBoneIndex(0u);
	world.view->SetToonShadingRig(instance, world.scene->AddToonShadingRig(desc));

	const glm::vec3 sun(0.3f, 0.5f, 1.0f);
	const float     frameSeconds = 1.0f / bgl::test::skinned_synth::c_SampleRate;

	for (const auto& [time, slide] :
	     { std::pair(0.0f, 0.0f), std::pair(frameSeconds, bgl::test::skinned_synth::c_Step) })
	{
		INFO("at frame " << time / frameSeconds);
		world.Draw(sun, time);
		const glm::mat4 bone = glm::translate(glm::mat4(1.0f), glm::vec3(slide, 0.0f, 0.0f));
		CHECK(world.Selected() == 1u);
		CheckBlock(world.Blocks()[0], desc, placement * bone * desc.headToBone, sun);
	}
}

TEST_CASE(
	"Every placed slot of a rigged instance block is evaluated",
	"[toonshadingrig][render][instance_block]")
{
	Fixture world;

	const auto rigDesc = TestRig();
	const auto rig     = world.scene->AddToonShadingRig(rigDesc);
	const auto block   = world.view->CreateMeshInstanceBlock(
		bgl::MeshInstanceBlockDesc()
			.SetGeom(world.scene->AddSphereGeom(8, 8, 0.5f, world.material))
			.SetCapacity(8)
			.SetToonShadingRig(rig));

	auto writer = world.gfx->CreateMeshInstanceWriter(
		bgl::MeshInstanceWriterDesc()
			.SetSlangModuleName("MeshInstanceWriterProbe")
			.SetSlangTypeName("MeshInstanceWriterProbe"));
	world.view->SetBlockWriter(block, writer);
	world.view->GetBlockParams(block)["origin"] = glm::vec3(-2.0f, 0.0f, 0.0f);
	world.view->GetBlockParams(block)["shown"]  = 5u;

	const glm::vec3 sun(0.3f, 0.5f, 1.0f);
	world.Draw(sun);

	const uint32_t first = world.View()->GetInstanceBlock(block).range.first;
	CHECK(world.Selected() == 5u);

	const auto blocks = world.Blocks();
	const auto flags  = world.GpuFlags();
	auto       placed = std::set<uint32_t>();
	for (uint32_t i = 0; i < 5; ++i)
	{
		const uint32_t placement = blocks[i].placement;
		REQUIRE(placement >= first);
		REQUIRE(placement < first + 5);
		placed.insert(placement);
		CHECK(SlotBits(flags[placement]) == i + 1);

		const glm::mat4 slot = glm::translate(
			glm::mat4(1.0f),
			glm::vec3(-2.0f + float(placement - first), 0.0f, 0.0f));
		CheckBlock(blocks[i], rigDesc, slot * rigDesc.headToBone, sun);
	}
	CHECK(placed.size() == 5u);

	for (uint32_t hidden = first + 5; hidden < first + 8; ++hidden)
	{
		CHECK(SlotBits(flags[hidden]) == 0u);
	}
}

TEST_CASE("The pool takes no more rigged placements than its capacity", "[toonshadingrig][render]")
{
	Fixture world;

	auto desc = TestRig();
	desc.SetFadeStartPixels(1.0f).SetFadeEndPixels(0.0f);
	const auto rig  = world.scene->AddToonShadingRig(desc);
	const auto geom = world.scene->AddSphereGeom(4, 4, 0.5f, world.material);

	const uint32_t count      = bgl::cToonShadingRigPoolCapacity + 76;
	auto           placements = std::vector<bgl::MeshInstanceHandle>();
	for (uint32_t i = 0; i < count; ++i)
	{
		placements.push_back(
			world.view->CreateStaticMeshInstance(bgl::StaticMeshInstanceDesc().SetGeom(geom)));
		world.view->SetToonShadingRig(placements.back(), rig);
	}

	world.Draw(glm::vec3(0.3f, 0.5f, 1.0f));

	// The counter counts every placement that asked; the bits, the ones that got a block.
	CHECK(world.Selected() == count);

	const auto flags = world.GpuFlags();
	auto       slots = std::set<uint32_t>();
	for (const bgl::MeshInstanceHandle placement : placements)
	{
		if (const uint32_t bits = SlotBits(flags[placement.handle.index]); bits != 0u)
		{
			slots.insert(bits);
		}
	}
	CHECK(slots.size() == bgl::cToonShadingRigPoolCapacity);
	CHECK(*slots.rbegin() == bgl::cToonShadingRigPoolCapacity);
}

TEST_CASE(
	"A placement that cannot show its face takes no block, and one without a rig is untouched",
	"[toonshadingrig][render]")
{
	Fixture world;

	auto desc = TestRig();
	// The head at the origin spans 2 * 0.5 * 150 / tan(30 degrees) / 10.0125 = 25.95 pixels from the
	// camera, so it is part way through this fade; one 300 units off spans under one.
	desc.SetFadeStartPixels(30.0f).SetFadeEndPixels(20.0f);
	const auto rig = world.scene->AddToonShadingRig(desc);

	const auto seen   = world.Place(glm::mat4(1.0f));
	const auto hidden = world.Place(glm::mat4(1.0f));
	const auto behind = world.Place(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 30.0f)));
	const auto far = world.Place(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -300.0f)));
	const auto unrigged = world.Place(glm::mat4(1.0f));

	for (const auto placement : { seen, hidden, behind, far })
	{
		world.view->SetToonShadingRig(placement, rig);
	}
	world.view->SetMeshInstanceFlags(
		hidden,
		bgl::MeshInstanceFlags(bgl::MeshInstanceFlag::kHidden));

	world.Draw(glm::vec3(0.3f, 0.5f, 1.0f));

	CHECK(world.Selected() == 1u);
	auto flags = world.GpuFlags();
	CHECK(SlotBits(flags[seen.handle.index]) == 1u);
	CHECK(SlotBits(flags[hidden.handle.index]) == 0u);
	CHECK(flags[hidden.handle.index] == uint32_t(bgl::MeshInstanceFlag::kHidden));
	CHECK(SlotBits(flags[behind.handle.index]) == 0u);
	CHECK(SlotBits(flags[far.handle.index]) == 0u);
	CHECK(flags[unrigged.handle.index] == 0u);
	CHECK(world.Blocks()[0].placement == seen.handle.index);
	CHECK(world.Blocks()[0].faceLight.w == Catch::Approx((25.95f - 20.0f) / 10.0f).margin(0.01));

	// Cleared, a placement leaves the pass's reach, and its word goes back to what the CPU wrote.
	world.view->ClearToonShadingRig(seen);
	world.Draw(glm::vec3(0.3f, 0.5f, 1.0f));
	flags = world.GpuFlags();
	CHECK(flags[seen.handle.index] == 0u);
	CHECK(world.Selected() == 0u);
}
