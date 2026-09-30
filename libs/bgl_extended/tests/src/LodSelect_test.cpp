#include "gfx/GraphicsBase.h"
#include "gfx/viewport.h"
#include "scene/CullState.h"
#include "scene/SceneView.h"
#include "util/LodMesh.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/util.h"
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LodLevel.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/LodSelectionDesc.h>
#include <bgl/types/MeshInstanceHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/Viewport.h>
#include <bgl_common/idl/InstanceLod.h>
#include <bgl_common/idl/InstanceVisibility.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/types/Viewport.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gamelib/lod_select.h>
#include <optional>
#include <vector>

// The cull choosing each placement's level, driven by real frames: a three-level mesh placed at
// known distances before a known camera, and each placement's word and visibility read back after
// the frame. The level a size earns, the hysteresis that holds one in the gap above a threshold, a
// forced level, the draw-nothing tier, and a change that dissolves -- two entries, then one.

namespace
{
	constexpr uint32_t c_Size = 64;

	// Level 0's box spans -1..1 on every axis, so its sphere's radius is sqrt(3). A 90-degree field
	// of view on a 64-pixel grid spans 32 pixels per unit at distance one, so a placement at
	// distance d spans 2 * sqrt(3) * 32 / d = 110.85 / d pixels.
	constexpr float c_PixelsAtOne = 110.851f;

	// Levels 0, 1 and 2 from 30, 10 and 3 pixels; nothing below 3.
	const std::vector<float> c_Thresholds = { 30.0f, 10.0f, 3.0f };

	float
	DistanceFor(float pixels)
	{
		return c_PixelsAtOne / pixels;
	}

	/** Copies a whole compute buffer back after the frame that wrote it. */
	std::vector<std::byte>
	ReadBack(bgl::GraphicsBase* gfxBase, const bgpu::ComputeBuffer& buffer)
	{
		auto resourceManager = gfxBase->GetResourceManagerCpy();
		auto device          = gfxBase->GetDevice();
		gfxBase->WaitIdle();

		auto listDesc  = bgpu::CommandListDesc();
		listDesc.type  = bgpu::QueueType::kGraphics;
		auto allocator = device->CreateCommandAllocator();
		auto list      = device->CreateCommandList(listDesc, allocator, resourceManager);
		auto queue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

		auto rbDesc      = bgpu::ReadbackBufferDesc();
		rbDesc.byteSize  = buffer.ByteSize();
		rbDesc.debugName = "LOD Readback";
		auto rb          = resourceManager->CreateReadbackBuffer(rbDesc);

		list->Open(queue, allocator);
		list->Barrier(
			buffer.GetBufferHandle(),
			bgpu::BufferBarrierDesc()
				.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
				.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
				.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
				.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));
		list->CopyBufferToReadback(rb, buffer.GetBufferHandle());
		list->Close();
		queue->WaitForFenceCPUBlocking(queue->ExecuteCommandList(list));

		const auto* mapped = static_cast<const std::byte*>(resourceManager->MapReadback(rb));
		REQUIRE(mapped != nullptr);
		auto bytes = std::vector<std::byte>(mapped, mapped + rbDesc.byteSize);
		resourceManager->UnmapReadback(rb);
		resourceManager->DestroyReadbackBuffer(rb, false);
		return bytes;
	}

	/** A scene of one three-level mesh, a camera at the origin looking down -Z, and a clock. */
	struct LodScene
	{
		bgl::GraphicsRef                     gfx;
		bgl::SceneRef                        scene;
		bgl::SceneViewRef                    view;
		bgl::RenderTargetRef                 target;
		bgl::GeomHandle                      geom;
		std::vector<bgl::MeshInstanceHandle> placements;
		float                                time = 0.0f;

		LodScene()
		{
			auto opts                      = bgl::test::GraphicsSetup();
			opts.gpuContext.shaderCacheDir = bgl::test::ShaderCacheDir();
			gfx                            = bgl::test::CreateGraphics(opts);
			REQUIRE(gfx != nullptr);

			auto desc                        = bgl::SceneDesc();
			desc.initialGeom                 = 4;
			desc.initialSubmeshes            = 8;
			desc.initialMeshlets             = 32;
			desc.initialVertexBufferByteSize = 8000;
			desc.initialIndices              = 500;
			scene                            = gfx->CreateScene(desc);
			view                             = gfx->CreateSceneView(scene, 16);

			const std::array<bgl::test::EntrySpec, 3> levels = { {
				{ 4, glm::vec3(-1.0f), glm::vec3(1.0f) },
				{ 2, glm::vec3(-1.0f), glm::vec3(1.0f) },
				{ 1, glm::vec3(-1.0f), glm::vec3(1.0f) },
			} };
			const auto material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
			geom                = scene->AddStaticMeshGeom(
				bgl::test::MakeLodMesh(levels, 1, c_Thresholds),
				0,
				std::array{ material });
			REQUIRE(geom.IsValid());

			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = c_Size;
			targetDesc.height   = c_Size;
			targetDesc.headless = true;
			target              = gfx->CreateRenderTarget(targetDesc);
		}

		/** A placement at `distance` straight ahead; the placements' slots follow their order. */
		bgl::MeshInstanceHandle
		Place(float distance)
		{
			placements.push_back(view->CreateStaticMeshInstance(geom, At(distance)));
			return placements.back();
		}

		static glm::mat4
		At(float distance)
		{
			return glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -distance));
		}

		/** One frame, 30 ms after the last. */
		void
		Frame()
		{
			auto job     = bgl::RenderJob();
			job.view     = view;
			job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));
			job.camera   = bgl::Camera()
			                   .LookAt(
								   glm::vec3(0.0f),
								   glm::vec3(0.0f, 0.0f, -1.0f),
								   glm::vec3(0.0f, 1.0f, 0.0f))
			                   .Perspective(glm::radians(90.0f), 1.0f, 0.1f, 200.0f);
			job.time     = time;
			gfx->DrawFrame(target, job);
			time += 0.03f;
		}

		/** Each placement's word and visibility bits, as the last frame's cull left them. */
		struct Read
		{
			bgl::InstanceLodState lod;
			uint32_t              visible = 0;
		};

		std::vector<Read>
		ReadAll()
		{
			auto* gfxBase = gfx->As<bgl::GraphicsBase>();
			auto* raw     = view->As<bgl::SceneView>();
			auto& cull    = raw->GetCullState(0);

			const auto words      = ReadBack(gfxBase, cull.GetInstanceLod());
			const auto visibility = ReadBack(gfxBase, cull.GetInstanceVisibility());

			auto reads = std::vector<Read>();
			for (size_t i = 0; i < placements.size(); ++i)
			{
				auto word = bgl::idl::InstanceLod();
				std::memcpy(
					&word,
					words.data() + placements[i].handle.index * sizeof(word),
					sizeof(word));
				auto bits = bgl::idl::InstanceVisibility();
				std::memcpy(&bits, visibility.data() + i * sizeof(bits), sizeof(bits));
				reads.push_back({ bgl::UnpackInstanceLod(word), bits.visible });
			}
			return reads;
		}
	};

	constexpr uint32_t c_Current = bgl::idl::cVisibleCurrentBit;
	// Both levels, in the bucket's dissolve lane.
	constexpr uint32_t c_Both = bgl::idl::cVisibleCurrentBit | bgl::idl::cVisibleOutgoingBit |
	                            bgl::idl::cVisibleDissolvingBit;

	// The draw-nothing tier of a three-level mesh reads back as level 3.
	constexpr auto c_Nothing = static_cast<bgl::LodLevel>(3);
}

TEST_CASE("a placement draws the level its size on screen earns", "[lod][culling][render]")
{
	auto lods = LodScene();
	lods.Place(DistanceFor(55.0f));
	lods.Place(DistanceFor(22.0f));
	lods.Place(DistanceFor(5.5f));
	lods.Place(DistanceFor(2.2f));
	lods.Frame();

	const auto reads = lods.ReadAll();
	CHECK(reads[0].lod.level == bgl::LodLevel::kLod0);
	CHECK(reads[1].lod.level == bgl::LodLevel::kLod1);
	CHECK(reads[2].lod.level == bgl::LodLevel::kLod2);
	CHECK(reads[3].lod.level == c_Nothing);

	// A first choice has nothing to dissolve from, and the tier below the last draws nothing.
	for (size_t i = 0; i < 3; ++i)
	{
		INFO("placement " << i);
		CHECK_FALSE(reads[i].lod.outgoing.has_value());
		CHECK(reads[i].visible == c_Current);
	}
	CHECK(reads[3].visible == 0u);
}

TEST_CASE("the size test a tool reads chooses the level the cull chose", "[lod][culling][render]")
{
	auto lods = LodScene();
	lods.Place(DistanceFor(55.0f));
	lods.Place(DistanceFor(22.0f));
	lods.Place(DistanceFor(5.5f));
	lods.Place(DistanceFor(2.2f));
	// Off-axis and scaled, so the distance is not the depth and the radius is not the box's.
	const glm::mat4 aside = glm::scale(
		glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 1.0f, -9.0f)),
		glm::vec3(0.5f, 1.5f, 1.0f));
	lods.placements.push_back(lods.view->CreateStaticMeshInstance(lods.geom, aside));
	lods.Frame();

	// Frame()'s camera stands at the origin looking down -Z, so its view is the identity.
	const glm::mat4 viewProj =
		bgl::Camera().Perspective(glm::radians(90.0f), 1.0f, 0.1f, 200.0f).GetProjection();
	const float pixelsPerUnit = game::PixelsPerUnit(
		bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size)),
		viewProj);
	const glm::vec4 sphere = core::bounding_sphere_of(glm::vec3(-1.0f), glm::vec3(1.0f));

	const std::array<glm::mat4, 5> worlds = {
		LodScene::At(DistanceFor(55.0f)),
		LodScene::At(DistanceFor(22.0f)),
		LodScene::At(DistanceFor(5.5f)),
		LodScene::At(DistanceFor(2.2f)),
		aside,
	};
	const auto reads = lods.ReadAll();
	for (size_t i = 0; i < worlds.size(); ++i)
	{
		INFO("placement " << i);
		REQUIRE(reads[i].lod.level.has_value());
		const float size = game::ProjectedDiameter(
			game::TransformSphere(worlds[i], sphere),
			glm::vec3(0.0f),
			pixelsPerUnit);
		CHECK(
			game::ChooseLevel(c_Thresholds, size, 1.0f, std::nullopt) ==
			static_cast<uint32_t>(*reads[i].lod.level));
	}
}

TEST_CASE("a placement resting just past a threshold holds its level", "[lod][culling][render]")
{
	auto desc        = bgl::LodSelectionDesc();
	desc.fadeSeconds = 0.0f;

	auto lods = LodScene();
	lods.view->SetLodSelection(desc);
	const auto placement = lods.Place(DistanceFor(29.0f));
	lods.Frame();
	REQUIRE(lods.ReadAll()[0].lod.level == bgl::LodLevel::kLod1);

	SECTION("into the gap above level 0's floor, it stays at level 1")
	{
		lods.view->SetInstanceTransform(placement, LodScene::At(DistanceFor(32.0f)));
		lods.Frame();
		CHECK(lods.ReadAll()[0].lod.level == bgl::LodLevel::kLod1);

		SECTION("and clearing the gap takes it finer")
		{
			lods.view->SetInstanceTransform(placement, LodScene::At(DistanceFor(35.0f)));
			lods.Frame();
			CHECK(lods.ReadAll()[0].lod.level == bgl::LodLevel::kLod0);

			SECTION("while going coarser happens at the threshold itself")
			{
				lods.view->SetInstanceTransform(placement, LodScene::At(DistanceFor(29.0f)));
				lods.Frame();
				CHECK(lods.ReadAll()[0].lod.level == bgl::LodLevel::kLod1);
			}
		}
	}
}

TEST_CASE("a forced level draws on every placement that has it", "[lod][culling][render]")
{
	auto lods = LodScene();
	lods.Place(DistanceFor(55.0f));
	lods.Place(DistanceFor(2.2f));

	auto desc       = bgl::LodSelectionDesc();
	desc.forceLevel = bgl::LodLevel::kLod2;
	lods.view->SetLodSelection(desc);
	lods.Frame();

	for (const auto& read : lods.ReadAll())
	{
		CHECK(read.lod.level == bgl::LodLevel::kLod2);
		CHECK(read.visible == c_Current);
	}

	SECTION("a level past the mesh's draws its coarsest, never nothing")
	{
		desc.forceLevel = bgl::LodLevel::kLod7;
		lods.view->SetLodSelection(desc);
		lods.Frame();
		for (const auto& read : lods.ReadAll())
		{
			CHECK(read.lod.level == bgl::LodLevel::kLod2);
			CHECK_FALSE(read.lod.outgoing.has_value());
		}
	}
}

TEST_CASE("a change of level dissolves: both levels draw until it ends", "[lod][culling][render]")
{
	auto       lods      = LodScene();
	const auto placement = lods.Place(DistanceFor(22.0f));
	lods.Frame();
	REQUIRE(lods.ReadAll()[0].lod.level == bgl::LodLevel::kLod1);

	// The default dissolve is 0.15 s and a frame here 30 ms, so it takes five frames.
	lods.view->SetInstanceTransform(placement, LodScene::At(DistanceFor(55.0f)));
	lods.Frame();

	auto read = lods.ReadAll()[0];
	CHECK(read.lod.level == bgl::LodLevel::kLod0);
	REQUIRE(read.lod.outgoing.has_value());
	CHECK(*read.lod.outgoing == bgl::LodLevel::kLod1);
	CHECK(read.lod.fade == Catch::Approx(0.2f).margin(0.01f));
	CHECK(read.visible == c_Both);

	for (int frame = 0; frame < 3; ++frame) lods.Frame();
	read = lods.ReadAll()[0];
	CHECK(read.lod.fade == Catch::Approx(0.8f).margin(0.01f));
	CHECK(read.visible == c_Both);

	lods.Frame();
	read = lods.ReadAll()[0];
	CHECK(read.lod.level == bgl::LodLevel::kLod0);
	CHECK_FALSE(read.lod.outgoing.has_value());
	CHECK(read.visible == c_Current);

	SECTION("a placement out of view snaps rather than dissolving")
	{
		lods.view->SetInstanceTransform(
			placement,
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, DistanceFor(22.0f))));
		lods.Frame();
		read = lods.ReadAll()[0];
		CHECK(read.lod.level == bgl::LodLevel::kLod1);
		CHECK_FALSE(read.lod.outgoing.has_value());
		CHECK(read.visible == 0u);
	}
}

// The renderer sizes the cull's pixels-per-unit itself (gfx/viewport.h), and the editor's LOD view
// asks gamelib; neither links the other, so this is what keeps the two on one answer.
TEST_CASE("The renderer and gamelib measure pixels per unit alike", "[lod]")
{
	const glm::mat4 viewProj =
		bgl::Camera().Perspective(glm::radians(70.0f), 1.5f, 0.1f, 500.0f).GetProjection();
	CHECK(
		bgl::PixelsPerUnit(bgpu::Viewport(0.0f, 960.0f, 0.0f, 640.0f, 0.0f, 1.0f), viewProj) ==
		game::PixelsPerUnit(bgl::Viewport(0.0f, 960.0f, 0.0f, 640.0f, 0.0f, 1.0f), viewProj));
}
