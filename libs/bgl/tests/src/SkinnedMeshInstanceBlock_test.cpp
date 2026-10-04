#include "gfx/GraphicsBase.h"
#include "scene/SceneView.h"
#include "util/PaletteReadback.h"
#include "util/SkinnedSynth.h"
#include "util/TestEnvironment.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/VelocityReadback.h"
#include <bgl/GeomType.h>
#include <bgl/IGraphics.h>
#include <bgl/IMeshInstanceWriter.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/idl/PosePool.h>
#include <bgl/types/Camera.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/InstanceDesc.h>
#include <bgl/types/LodSelectionDesc.h>
#include <bgl/types/MeshInstanceBlockDesc.h>
#include <bgl/types/MeshInstanceWriterDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/SkinnedMeshInstanceDesc.h>
#include <bgl/types/Viewport.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// A skinned geom's instance block end to end: its placements are automatic ones, drawn per instance
// or from the rig's table as the view chooses, each sharing the block's one record. What the same
// placements spawned one by one on the CPU draw is the reference.

namespace
{
	namespace synth = bgl::test::skinned_synth;

	constexpr uint32_t c_Size     = 64;
	constexpr uint32_t c_Capacity = 8;
	constexpr uint32_t c_Shown    = 3;
	constexpr float    c_Spacing  = 3.0f;

	const auto c_Origin = glm::vec3(-3.0f, 0.0f, 0.0f);

	// Sliding, so a pose the clock did not reach shows as a quad somewhere else.
	const auto c_Playback = bgl::SkinnedPlaybackDesc::FromClip(synth::c_LoopClip, 0.0f, 1.0f);

	struct SkinnedBlockScene
	{
		bgl::GraphicsRef           gfx;
		bgl::SceneRef              scene;
		bgl::GeomHandle            quad;
		bgl::MeshInstanceWriterRef mover;

		explicit SkinnedBlockScene(bgl::GraphicsRef graphics) : gfx(std::move(graphics))
		{
			auto sceneDesc                        = bgl::SceneDesc();
			sceneDesc.initialGeom                 = 4;
			sceneDesc.initialMeshlets             = 16;
			sceneDesc.initialSubmeshes            = 4;
			sceneDesc.initialVertexBufferByteSize = 10000;
			sceneDesc.initialIndices              = 400;
			sceneDesc.initialPbrMaterials         = 4;
			scene                                 = gfx->CreateScene(sceneDesc);

			auto white            = bgl::PbrMaterialDesc();
			white.baseColorFactor = glm::vec4(1.0f);
			white.metallicFactor  = 0.0f;
			quad  = synth::AddSlidingQuadGeom(*scene, scene->CreatePbrMaterial(white));
			mover = gfx->CreateMeshInstanceWriter(
				bgl::MeshInstanceWriterDesc()
					.SetSlangModuleName("SkinnedMeshInstanceWriterMover")
					.SetSlangTypeName("SkinnedMeshInstanceWriterMover")
					.SetGeomType(bgl::GeomType::kSkinnedMesh));
		}

		struct Drawn
		{
			bgl::SceneViewRef    view;
			bgl::RenderTargetRef target;
			bgl::RenderJob       job;
		};

		/** A view whose automatic placements all draw from `source`, within a budget of `budget`. */
		Drawn
		MakeView(bgl::PoseSource source, uint32_t budget = c_Capacity)
		{
			auto drawn = Drawn();
			drawn.view = gfx->CreateSceneView(scene, 8);
			bgl::test::ApplyEnvironment(scene.Get(), drawn.view.Get());

			auto selection            = bgl::LodSelectionDesc();
			selection.fadeSeconds     = 0.0f;
			selection.poseBudget      = budget;
			selection.forcePoseSource = source;
			drawn.view->SetLodSelection(selection);

			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = c_Size;
			targetDesc.height   = c_Size;
			targetDesc.headless = true;
			drawn.target        = gfx->CreateRenderTarget(targetDesc);

			drawn.job.view     = drawn.view;
			drawn.job.camera   = bgl::Camera()
			                         .LookAt(
										 glm::vec3(0.0f, 0.0f, 12.0f),
										 glm::vec3(0.0f),
										 glm::vec3(0.0f, 1.0f, 0.0f))
			                         .Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
			drawn.job.viewport = bgl::Viewport(float(c_Size), float(c_Size));
			drawn.job.time     = 0.0f;
			return drawn;
		}

		/**
		 * `c_Shown` automatic placements where the mover puts its shown slots, one by one, each
		 * spawned `offsetStep` seconds further into the clip than the one before -- what a slot
		 * the mover gives that offset should draw.
		 */
		void
		AddFromCpu(bgl::ISceneView& view, float offsetStep = 0.0f) const
		{
			for (uint32_t slot = 0; slot < c_Shown; ++slot)
			{
				const float ahead = float(slot) * offsetStep * synth::c_SampleRate;
				view.CreateSkinnedMeshInstance(
					bgl::SkinnedMeshInstanceDesc()
						.SetGeom(quad)
						.SetTransform(
							glm::translate(
								glm::mat4(1.0f),
								c_Origin + glm::vec3(c_Spacing * float(slot), 0.0f, 0.0f)))
						.SetPlayback(
							bgl::SkinnedPlaybackDesc::FromClip(synth::c_LoopClip, ahead, 1.0f))
						.SetSource(bgl::PoseSource::kAuto));
			}
		}

		bgl::MeshInstanceBlockHandle
		AddBlock(bgl::ISceneView& view, float offsetStep = 0.0f) const
		{
			const auto block = view.CreateMeshInstanceBlock(
				bgl::MeshInstanceBlockDesc()
					.SetGeom(quad)
					.SetCapacity(c_Capacity)
					.SetPlayback(c_Playback));
			view.SetBlockWriter(block, mover);
			auto params          = view.GetBlockParams(block);
			params["origin"]     = c_Origin;
			params["spacing"]    = c_Spacing;
			params["motion"]     = glm::vec3(0.0f);
			params["shown"]      = c_Shown;
			params["offsetStep"] = offsetStep;
			return block;
		}

		/** Draws `frames` frames, the clock advancing a 60th of a second each, and the last one's pixels. */
		std::vector<std::byte>
		Draw(Drawn& drawn, int frames)
		{
			for (int i = 0; i < frames; ++i)
			{
				drawn.job.time += 1.0f / 60.0f;
				gfx->DrawFrame(drawn.target, drawn.job);
			}
			const auto image = gfx->ScreenshotToMemory(drawn.target);
			return std::vector<std::byte>(image.pixels.begin(), image.pixels.end());
		}

		bgl::idl::PosePool
		ReadPool(const Drawn& drawn) const
		{
			return bgl::test::ReadBuffer<bgl::idl::PosePool>(
				gfx->As<bgl::GraphicsBase>(),
				drawn.view->As<bgl::SceneView>()->GetAutoPose().GetPoolBuffer(),
				1)[0];
		}
	};

	bgl::test::GraphicsSetup
	HeadlessOptions()
	{
		auto opts                        = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.gpuContext.enableDebugLayer = true;
		return opts;
	}
}

TEST_CASE(
	"A written skinned block draws what the same automatic placements draw from the CPU",
	"[instance_block][skinned][auto][render]")
{
	auto s = SkinnedBlockScene(bgl::test::CreateGraphics(HeadlessOptions()));

	const auto source = GENERATE(bgl::PoseSource::kPerInstance, bgl::PoseSource::kBoneAnimTable);
	INFO((source == bgl::PoseSource::kPerInstance ? "per instance" : "from the table"));

	auto fromCpu = s.MakeView(source);
	s.AddFromCpu(*fromCpu.view);

	// Eight slots, three shown: the five hidden ones must neither draw nor be posed.
	auto fromBlock = s.MakeView(source);
	s.AddBlock(*fromBlock.view);

	const auto cpu   = s.Draw(fromCpu, 6);
	const auto block = s.Draw(fromBlock, 6);
	CHECK(block == cpu);

	auto empty = s.MakeView(source);
	CHECK(s.Draw(empty, 6) != cpu);
}

TEST_CASE(
	"A skinned block's slots are automatic placements, and a hidden one takes no pose",
	"[instance_block][skinned][auto][render]")
{
	auto  s     = SkinnedBlockScene(bgl::test::CreateGraphics(HeadlessOptions()));
	auto  drawn = s.MakeView(bgl::PoseSource::kPerInstance);
	auto* view  = drawn.view->As<bgl::SceneView>();

	const auto block = s.AddBlock(*drawn.view);
	s.Draw(drawn, 2);

	// Every slot is on the list the cull walks, and only the shown ones were posed.
	CHECK(view->GetAutoPose().GetPlacementCount() == c_Capacity);
	CHECK(s.ReadPool(drawn).posed == c_Shown);

	// The slots share one record, which the block, not a slot, owns.
	const bgpu::EntryRange range  = view->GetInstanceBlock(block).range;
	const uint32_t         record = view->GetMeshBuffer().AtIndex(range.first).playback.byteOffset;
	REQUIRE(record != 0);
	for (uint32_t slot = 1; slot < c_Capacity; ++slot)
		CHECK(view->GetMeshBuffer().AtIndex(range.first + slot).playback.byteOffset == record);
	CHECK(view->GetPlaybackArena().IsOffsetValid(record));

	drawn.view->DeleteMeshInstanceBlock(block);
	s.Draw(drawn, 1);
	CHECK(view->GetAutoPose().GetPlacementCount() == 0);
	CHECK_FALSE(view->GetPlaybackArena().IsOffsetValid(record));
}

TEST_CASE(
	"A skinned block's slot plays its record its offset ahead of the clock",
	"[instance_block][skinned][auto][render]")
{
	auto s = SkinnedBlockScene(bgl::test::CreateGraphics(HeadlessOptions()));

	const auto source = GENERATE(bgl::PoseSource::kPerInstance, bgl::PoseSource::kBoneAnimTable);
	INFO((source == bgl::PoseSource::kPerInstance ? "per instance" : "from the table"));

	// A fifth of a loop cycle apart, so each of the three slots stands somewhere else.
	constexpr float c_OffsetStep = 0.4f / synth::c_SampleRate;

	auto fromCpu = s.MakeView(source);
	s.AddFromCpu(*fromCpu.view, c_OffsetStep);

	auto fromBlock = s.MakeView(source);
	s.AddBlock(*fromBlock.view, c_OffsetStep);

	const auto cpu   = s.Draw(fromCpu, 6);
	const auto block = s.Draw(fromBlock, 6);
	CHECK(block == cpu);

	// The offsets are what moved them: the same block played in step draws something else.
	auto inStep = s.MakeView(source);
	s.AddBlock(*inStep.view);
	CHECK(s.Draw(inStep, 6) != cpu);

	// And the pose the frame before was evaluated at the same offset, or the motion would differ.
	const auto cpuMotion =
		bgl::test::ReadVelocityTexels(s.gfx.Get(), fromCpu.target.Get(), c_Size, c_Size);
	const auto blockMotion =
		bgl::test::ReadVelocityTexels(s.gfx.Get(), fromBlock.target.Get(), c_Size, c_Size);
	REQUIRE(cpuMotion.size() == blockMotion.size());
	uint32_t moving = 0;
	for (size_t i = 0; i < cpuMotion.size(); ++i)
	{
		CHECK(glm::length(glm::vec2(blockMotion[i]) - glm::vec2(cpuMotion[i])) < 1e-3f);
		moving += glm::length(glm::vec2(cpuMotion[i])) > 1e-3f ? 1u : 0u;
	}
	CHECK(moving > 0);
}
