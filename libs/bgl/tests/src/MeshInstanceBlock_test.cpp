#include "gfx/GraphicsBase.h"
#include "scene/SceneView.h"
#include "util/TestEnvironment.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/VelocityReadback.h"
#include <bgl/IExternalBuffer.h>
#include <bgl/IGraphics.h>
#include <bgl/IMeshInstanceWriter.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/MeshInstanceBlockDesc.h>
#include <bgl/types/MeshInstanceBlockHandle.h>
#include <bgl/types/MeshInstanceWriterDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/Viewport.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/cmd/QueuePoint.h>
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/NativeObject.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Instance blocks end to end: what the block, writer, import and frame-wait calls refuse, the epoch a
// block moves, the run it claims in the view's mesh buffer, and what Write Instance Blocks draws from it --
// the same pixels and motion as the same placements made on the CPU, from parameters or from a
// buffer another owner wrote.

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

	bgl::SceneRef
	MakeScene(bgl::IGraphics& gfx)
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 4;
		desc.initialMeshlets             = 16;
		desc.initialSubmeshes            = 4;
		desc.initialVertexBufferByteSize = 10000;
		desc.initialIndices              = 400;
		desc.initialPbrMaterials         = 4;
		return gfx.CreateScene(desc);
	}

	const auto c_ProbeWriter = bgl::MeshInstanceWriterDesc()
	                               .SetSlangModuleName("MeshInstanceWriterProbe")
	                               .SetSlangTypeName("MeshInstanceWriterProbe");

	// Whether the view's epoch moved since the last call, as the next frame would see it.
	bool
	EpochMoved(bgl::ISceneView& view)
	{
		return view.As<bgl::SceneView>()->AdvanceTemporalEpoch();
	}
}

TEST_CASE("An instance block refuses what it cannot place", "[instance_block]")
{
	auto gfx   = bgl::test::CreateGraphics(HeadlessOptions());
	auto scene = MakeScene(*gfx);
	auto view  = gfx->CreateSceneView(scene, 8);

	const auto cube = scene->AddCubeGeom(scene->CreatePbrMaterial(bgl::PbrMaterialDesc()));

	SECTION("a null geom")
	{
		CHECK_THROWS_AS(
			view->CreateMeshInstanceBlock(
				bgl::MeshInstanceBlockDesc().SetGeom(bgl::GeomHandle()).SetCapacity(4)),
			bgl::SceneError);
	}

	SECTION("a deleted geom")
	{
		scene->DeleteGeom(cube);
		CHECK_THROWS_AS(
			view->CreateMeshInstanceBlock(
				bgl::MeshInstanceBlockDesc().SetGeom(cube).SetCapacity(4)),
			bgl::SceneError);
	}

	SECTION("no placements, or more than one dispatch reaches")
	{
		CHECK_THROWS_AS(
			view->CreateMeshInstanceBlock(
				bgl::MeshInstanceBlockDesc().SetGeom(cube).SetCapacity(0)),
			bgl::SceneError);
		CHECK_THROWS_AS(
			view->CreateMeshInstanceBlock(
				bgl::MeshInstanceBlockDesc().SetGeom(cube).SetCapacity(
					bgl::c_MaxMeshInstanceBlockCapacity + 1)),
			bgl::SceneError);
	}

	SECTION("a block already deleted, or one never made")
	{
		const auto block = view->CreateMeshInstanceBlock(
			bgl::MeshInstanceBlockDesc().SetGeom(cube).SetCapacity(4));
		view->DeleteMeshInstanceBlock(block);

		CHECK_THROWS_AS(view->DeleteMeshInstanceBlock(block), bgl::SceneError);
		CHECK_THROWS_AS(view->SetBlockWriter(block, nullptr), bgl::SceneError);
		CHECK_THROWS_AS((void)view->GetBlockParams(block), bgl::SceneError);
		CHECK_THROWS_AS(
			view->DeleteMeshInstanceBlock(bgl::MeshInstanceBlockHandle()),
			bgl::SceneError);
	}
}

TEST_CASE("An instance block moves the epoch once to exist and once to go", "[instance_block]")
{
	auto gfx   = bgl::test::CreateGraphics(HeadlessOptions());
	auto scene = MakeScene(*gfx);
	auto view  = gfx->CreateSceneView(scene, 8);

	const auto cube = scene->AddCubeGeom(scene->CreatePbrMaterial(bgl::PbrMaterialDesc()));
	(void)EpochMoved(*view);

	// Many blocks in one frame move it once, as many placements do; a slot vector's growth is
	// crossed on the way.
	std::vector<bgl::MeshInstanceBlockHandle> blocks;
	for (uint32_t i = 0; i < 9; ++i)
	{
		blocks.emplace_back(view->CreateMeshInstanceBlock(
			bgl::MeshInstanceBlockDesc().SetGeom(cube).SetCapacity(64)));
	}
	CHECK(EpochMoved(*view));
	CHECK_FALSE(EpochMoved(*view));

	auto writer = gfx->CreateMeshInstanceWriter(c_ProbeWriter);
	view->SetBlockWriter(blocks[0], writer);
	view->GetBlockParams(blocks[0])["shown"] = 3u;
	CHECK_FALSE(EpochMoved(*view));

	view->DeleteMeshInstanceBlock(blocks[4]);
	CHECK(EpochMoved(*view));

	// The other handles survive a neighbour's deletion.
	CHECK_NOTHROW((void)view->GetBlockParams(blocks[0]));
	CHECK_NOTHROW(view->DeleteMeshInstanceBlock(blocks[8]));
}

TEST_CASE("An instance writer compiles against the contract or is refused", "[instance_block]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());

	SECTION("a conforming type compiles")
	{
		auto writer = gfx->CreateMeshInstanceWriter(c_ProbeWriter);
		REQUIRE(writer != nullptr);
		CHECK(writer->GetDesc().slangTypeName == "MeshInstanceWriterProbe");
	}

	SECTION("a type that is not a writer is refused with the compiler's reason")
	{
		CHECK_THROWS_WITH(
			gfx->CreateMeshInstanceWriter(
				bgl::MeshInstanceWriterDesc()
					.SetSlangModuleName("NotAMeshInstanceWriter")
					.SetSlangTypeName("NotAMeshInstanceWriter")),
			Catch::Matchers::ContainsSubstring("is not an instance writer"));
	}

	SECTION("a module that does not exist is refused")
	{
		CHECK_THROWS_AS(
			gfx->CreateMeshInstanceWriter(
				bgl::MeshInstanceWriterDesc()
					.SetSlangModuleName("NoSuchMeshInstanceWriterModule")
					.SetSlangTypeName("Writer")),
			bgl::GraphicsError);
	}

	SECTION("a name that is not one is refused before it reaches the compiler")
	{
		CHECK_THROWS_AS(
			gfx->CreateMeshInstanceWriter(
				bgl::MeshInstanceWriterDesc().SetSlangModuleName("").SetSlangTypeName("T")),
			bgl::GraphicsError);
		CHECK_THROWS_AS(
			gfx->CreateMeshInstanceWriter(
				bgl::MeshInstanceWriterDesc()
					.SetSlangModuleName("MeshInstanceWriterProbe")
					.SetSlangTypeName("A; B")),
			bgl::GraphicsError);
		CHECK_THROWS_AS(
			gfx->CreateMeshInstanceWriter(
				bgl::MeshInstanceWriterDesc().SetSlangModuleName("a..b").SetSlangTypeName(
					"MeshInstanceWriterProbe")),
			bgl::GraphicsError);
	}
}

TEST_CASE("A block's parameters exist while it has a writer, and are its own", "[instance_block]")
{
	auto gfx   = bgl::test::CreateGraphics(HeadlessOptions());
	auto scene = MakeScene(*gfx);
	auto view  = gfx->CreateSceneView(scene, 8);

	const auto cube = scene->AddCubeGeom(scene->CreatePbrMaterial(bgl::PbrMaterialDesc()));
	const auto first =
		view->CreateMeshInstanceBlock(bgl::MeshInstanceBlockDesc().SetGeom(cube).SetCapacity(8));
	const auto other =
		view->CreateMeshInstanceBlock(bgl::MeshInstanceBlockDesc().SetGeom(cube).SetCapacity(8));

	CHECK_THROWS_AS((void)view->GetBlockParams(first), bgl::SceneError);

	auto writer = gfx->CreateMeshInstanceWriter(c_ProbeWriter);
	view->SetBlockWriter(first, writer);
	view->SetBlockWriter(other, writer);

	view->GetBlockParams(first)["shown"] = 5u;
	view->GetBlockParams(other)["shown"] = 2u;
	CHECK(view->GetBlockParams(first)["shown"] == 5u);
	CHECK(view->GetBlockParams(other)["shown"] == 2u);

	SECTION("a name the writer's Params does not declare is not written")
	{
		CHECK_THROWS(view->GetBlockParams(first)["noSuchField"] = 1u);
	}

	SECTION("rebinding starts from zero")
	{
		view->SetBlockWriter(first, writer);
		CHECK(view->GetBlockParams(first)["shown"] == 0u);
	}

	SECTION("unbinding takes the parameters away")
	{
		view->SetBlockWriter(first, nullptr);
		CHECK_THROWS_AS((void)view->GetBlockParams(first), bgl::SceneError);
		CHECK(view->GetBlockParams(other)["shown"] == 2u);
	}

	SECTION("a writer another renderer compiled is refused")
	{
		auto second       = bgl::test::CreateGraphics(HeadlessOptions());
		auto secondWriter = second->CreateMeshInstanceWriter(c_ProbeWriter);
		CHECK_THROWS_AS(view->SetBlockWriter(first, secondWriter), bgl::SceneError);
	}
}

TEST_CASE("A writer places a block through the contract alone", "[instance_block][compute]")
{
	// The probe runs against a fake IMeshInstanceBlock that records each slot, so what is checked is
	// the contract's meaning -- which slots a writer places, where, and which it hides -- and not
	// the renderer's block, which only a drawn frame can show.
	auto  gfx    = bgl::test::CreateGraphics(HeadlessOptions());
	auto  base   = gfx->As<bgl::GraphicsBase>();
	auto  rm     = base->GetResourceManagerCpy();
	auto* device = base->GetDevice();

	constexpr uint32_t c_Capacity = 8;
	constexpr uint32_t c_Shown    = 5;

	auto slots = rm->CreateComputeBuffer(
		bgpu::ComputeBufferDesc()
			.SetElement<glm::vec4>()
			.SetInitialCount(c_Capacity)
			.SetDebugName("Recorded Slots"));

	auto kernel = device->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSRecordingInstanceBlock"))
			.SetDebugName("CSRecordingInstanceBlock"));

	kernel["gUniforms"]["block"]["slots"]    = slots;
	kernel["gUniforms"]["block"]["capacity"] = c_Capacity;
	kernel["gUniforms"]["params"]["origin"]  = glm::vec3(10.0f, 2.0f, -3.0f);
	kernel["gUniforms"]["params"]["shown"]   = c_Shown;

	auto readbackDesc     = bgpu::ReadbackBufferDesc();
	readbackDesc.byteSize = c_Capacity * sizeof(glm::vec4);
	auto readback         = rm->CreateReadbackBuffer(readbackDesc);

	auto listDesc = bgpu::CommandListDesc();
	listDesc.type = bgpu::QueueType::kCompute;

	auto allocator = device->CreateCommandAllocator();
	auto list      = device->CreateCommandList(listDesc, allocator, rm);
	auto queue     = device->CreateCommandQueue(bgpu::QueueType::kCompute);

	auto state   = bgpu::ComputeState();
	state.kernel = &kernel;

	list->Open(queue, allocator);
	list->SetComputeState(state);
	list->Dispatch(1, 1, 1);
	list->Barrier(
		slots,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));
	list->CopyBufferToReadback(readback, slots);
	list->Close();
	queue->WaitForFenceCPUBlocking(queue->ExecuteCommandList(list));

	const auto* recorded = static_cast<const glm::vec4*>(rm->MapReadback(readback));
	REQUIRE(recorded != nullptr);
	for (uint32_t slot = 0; slot < c_Capacity; ++slot)
	{
		INFO("slot " << slot);
		if (slot < c_Shown)
		{
			CHECK(recorded[slot] == glm::vec4(10.0f + static_cast<float>(slot), 2.0f, -3.0f, 1.0f));
		}
		else
		{
			CHECK(recorded[slot].w == -1.0f);
		}
	}
	rm->UnmapReadback(readback);

	rm->DestroyReadbackBuffer(readback, false);
	rm->DestroyBuffer(slots, false);
}

TEST_CASE("Frame waits and the last frame's point are between frames only", "[instance_block]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 64;
	targetDesc.height   = 64;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto scene = MakeScene(*gfx);
	auto view  = gfx->CreateSceneView(scene, 8);

	auto job = bgl::RenderJob();
	job.view = view;
	job.camera =
		bgl::Camera()
			.LookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
	job.viewport = bgl::Viewport(64.0f, 64.0f);

	CHECK(gfx->GetLastFrameDone().IsNull());
	CHECK_THROWS_AS(gfx->WaitBeforeNextFrame(bgpu::QueuePoint()), bgl::GraphicsError);

	gfx->DrawFrame(target, job);
	const bgpu::QueuePoint done = gfx->GetLastFrameDone();
	REQUIRE_FALSE(done.IsNull());

	SECTION("the point is passed once the frame drains")
	{
		gfx->WaitIdle();
		CHECK(done.queue->IsFenceComplete(done.value));
	}

	SECTION("a frame waits on another queue's point on the GPU, and the CPU does not")
	{
		auto  base      = gfx->As<bgl::GraphicsBase>();
		auto* device    = base->GetDevice();
		auto  rm        = base->GetResourceManagerCpy();
		auto  producer  = device->CreateCommandQueue(bgpu::QueueType::kCompute);
		auto  allocator = device->CreateCommandAllocator();
		auto  listDesc  = bgpu::CommandListDesc();
		listDesc.type   = bgpu::QueueType::kCompute;
		auto list       = device->CreateCommandList(listDesc, allocator, rm);

		// Nothing is submitted on the producer yet, so its next value cannot have passed.
		const auto point =
			bgpu::QueuePoint{ .queue = producer, .value = producer->GetNextFenceValue() };
		gfx->WaitBeforeNextFrame(point);
		gfx->DrawFrame(target, job);

		const bgpu::QueuePoint waiting = gfx->GetLastFrameDone();
		CHECK_FALSE(waiting.queue->IsFenceComplete(waiting.value));

		list->Open(producer, allocator);
		list->Close();
		REQUIRE(producer->ExecuteCommandList(list) == point.value);

		gfx->WaitIdle();
		CHECK(waiting.queue->IsFenceComplete(waiting.value));
	}

	SECTION("inside a frame, both refuse")
	{
		gfx->BeginFrame(target);
		CHECK_THROWS_AS(gfx->WaitBeforeNextFrame(done), bgl::GraphicsError);
		CHECK_THROWS_AS((void)gfx->GetLastFrameDone(), bgl::GraphicsError);
		CHECK_THROWS_AS(gfx->CreateMeshInstanceWriter(c_ProbeWriter), bgl::GraphicsError);
		gfx->Draw(job);
		gfx->EndFrame();
	}
}

TEST_CASE("An import refuses a buffer that is not one", "[instance_block]")
{
	// Refused before the resource manager is asked, so no backend needs to adopt anything: a real
	// import is the backends' to prove.
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());

	uint32_t   notABuffer = 0;
	const auto object     = bgpu::NativeObject{ .pointer = &notABuffer };

	CHECK_THROWS_AS(gfx->ImportBuffer(bgpu::NativeBufferDesc()), bgl::GraphicsError);
	CHECK_THROWS_AS(
		gfx->ImportBuffer(
			bgpu::NativeBufferDesc()
				.SetObject(bgpu::NativeObjectType::kMtlBuffer, object)
				.SetBuffer(bgpu::StructBufferDesc().SetElementCount(4))),
		bgl::GraphicsError);
	CHECK_THROWS_AS(
		gfx->ImportBuffer(
			bgpu::NativeBufferDesc()
				.SetObject(bgpu::NativeObjectType::kMtlBuffer, object)
				.SetBuffer(bgpu::StructBufferDesc().SetElement<uint32_t>())),
		bgl::GraphicsError);
	CHECK_THROWS_WITH(
		gfx->ImportBuffer(
			bgpu::NativeBufferDesc()
				.SetObject(bgpu::NativeObjectType::kMtlBuffer, object)
				.SetBuffer(
					bgpu::StructBufferDesc().SetElement<uint32_t>().SetElementCount(4).SetIsUav())),
		Catch::Matchers::ContainsSubstring("read-only"));
}

TEST_CASE(
	"A block claims its own upload blocks, and a neighbour's write leaves them clean",
	"[instance_block]")
{
	auto  gfx       = bgl::test::CreateGraphics(HeadlessOptions());
	auto  scene     = MakeScene(*gfx);
	auto  view      = gfx->CreateSceneView(scene, 8);
	auto* sceneView = view->As<bgl::SceneView>();
	REQUIRE(sceneView != nullptr);

	const auto cube = scene->AddCubeGeom(scene->CreatePbrMaterial(bgl::PbrMaterialDesc()));

	const auto before = view->CreateStaticMeshInstance(
		bgl::StaticMeshInstanceDesc().SetGeom(cube).SetTransform(glm::mat4(1.0f)));
	const uint32_t baseline = view->GetInstanceCount();

	// Past one upload block (256 MeshInstances), so the run spans two.
	constexpr uint32_t c_Capacity = 300;
	const auto         block      = view->CreateMeshInstanceBlock(
		bgl::MeshInstanceBlockDesc().SetGeom(cube).SetCapacity(c_Capacity));
	const auto after = view->CreateStaticMeshInstance(
		bgl::StaticMeshInstanceDesc().SetGeom(cube).SetTransform(glm::mat4(1.0f)));

	const bgpu::EntryRange range = sceneView->GetInstanceBlock(block).range;
	CHECK(range.count == c_Capacity);
	CHECK(range.first % 256 == 0);
	CHECK(view->GetInstanceCount() == baseline + c_Capacity + 1);

	auto& meshes = sceneView->GetMeshBuffer();
	for (uint32_t slot = 0; slot < c_Capacity; ++slot)
	{
		const bgl::idl::MeshInstance& mesh = meshes.AtIndex(range.first + slot);
		CHECK(bgl::MeshInstanceFlags(mesh.flags).any(bgl::MeshInstanceFlag::kHidden));
	}

	// Neither neighbour landed in the block's two upload blocks.
	const uint32_t firstBlock = range.first / 256;
	for (const auto neighbour : { before, after })
	{
		CHECK(neighbour.handle.index / 256 != firstBlock);
		CHECK(neighbour.handle.index / 256 != firstBlock + 1);
	}

	// Draw once to flush the block's one upload, then move both neighbours.
	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 64;
	targetDesc.height   = 64;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	auto job            = bgl::RenderJob();
	job.view            = view;
	job.camera =
		bgl::Camera()
			.LookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
	job.viewport = bgl::Viewport(64.0f, 64.0f);
	gfx->DrawFrame(target, job);
	CHECK_FALSE(meshes.IsBlockDirty(firstBlock));

	view->SetInstanceTransform(before, glm::translate(glm::mat4(1.0f), glm::vec3(1.0f)));
	view->SetInstanceTransform(after, glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f)));
	CHECK_FALSE(meshes.IsBlockDirty(firstBlock));
	CHECK_FALSE(meshes.IsBlockDirty(firstBlock + 1));
	CHECK(meshes.IsBlockDirty(after.handle.index / 256));

	view->DeleteMeshInstanceBlock(block);
	CHECK(view->GetInstanceCount() == baseline + 1);
	CHECK_FALSE(meshes.IsIndexValid(range.first));
	gfx->WaitIdle();
}

TEST_CASE("A block with no writer draws nothing", "[instance_block][render]")
{
	auto gfx   = bgl::test::CreateGraphics(HeadlessOptions());
	auto scene = MakeScene(*gfx);
	auto view  = gfx->CreateSceneView(scene, 8);
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto white            = bgl::PbrMaterialDesc();
	white.baseColorFactor = glm::vec4(1.0f);
	white.metallicFactor  = 0.0f;
	const auto cube       = scene->AddCubeGeom(scene->CreatePbrMaterial(white));

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 64;
	targetDesc.height   = 64;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	auto job            = bgl::RenderJob();
	job.view            = view;
	job.camera =
		bgl::Camera()
			.LookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
	job.viewport = bgl::Viewport(64.0f, 64.0f);

	const auto draw = [&] {
		// A few frames, so TAA's history settles on what this scene draws.
		for (int i = 0; i < 4; ++i)
		{
			gfx->DrawFrame(target, job);
		}
		const auto image = gfx->ScreenshotToMemory(target);
		return std::vector<std::byte>(image.pixels.begin(), image.pixels.end());
	};

	const auto empty = draw();

	// Its slots sit at the origin the camera looks at, hidden: drawn, they would show a cube.
	const auto block =
		view->CreateMeshInstanceBlock(bgl::MeshInstanceBlockDesc().SetGeom(cube).SetCapacity(16));
	CHECK(draw() == empty);

	// The control: one cube there is visible.
	view->DeleteMeshInstanceBlock(block);
	view->CreateStaticMeshInstance(
		bgl::StaticMeshInstanceDesc().SetGeom(cube).SetTransform(glm::mat4(1.0f)));
	CHECK(draw() != empty);
}

namespace
{
	// One lit cube geom, a view, a target and a camera on the row a mover writes: what the
	// written-block cases compare a block against.
	struct BlockScene
	{
		static constexpr uint32_t c_Size = 64;

		bgl::GraphicsRef           gfx;
		bgl::SceneRef              scene;
		bgl::GeomHandle            cube;
		bgl::MeshInstanceWriterRef mover;

		explicit BlockScene(bgl::GraphicsRef graphics) :
			gfx(std::move(graphics)), scene(MakeScene(*gfx))
		{
			auto white            = bgl::PbrMaterialDesc();
			white.baseColorFactor = glm::vec4(1.0f);
			white.metallicFactor  = 0.0f;
			cube                  = scene->AddCubeGeom(scene->CreatePbrMaterial(white));
			mover                 = gfx->CreateMeshInstanceWriter(
				bgl::MeshInstanceWriterDesc()
					.SetSlangModuleName("MeshInstanceWriterMover")
					.SetSlangTypeName("MeshInstanceWriterMover"));
		}

		struct Drawn
		{
			bgl::SceneViewRef    view;
			bgl::RenderTargetRef target;
			bgl::RenderJob       job;
		};

		Drawn
		MakeView()
		{
			auto drawn = Drawn();
			drawn.view = gfx->CreateSceneView(scene, 8);
			bgl::test::ApplyEnvironment(scene.Get(), drawn.view.Get());

			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = c_Size;
			targetDesc.height   = c_Size;
			targetDesc.headless = true;
			drawn.target        = gfx->CreateRenderTarget(targetDesc);

			drawn.job.view     = drawn.view;
			drawn.job.camera   = bgl::Camera()
			                         .LookAt(
										 glm::vec3(0.0f, 2.0f, 8.0f),
										 glm::vec3(0.0f),
										 glm::vec3(0.0f, 1.0f, 0.0f))
			                         .Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
			drawn.job.viewport = bgl::Viewport(float(c_Size), float(c_Size));
			return drawn;
		}

		bgl::MeshInstanceBlockHandle
		AddMover(bgl::ISceneView& view, uint32_t capacity, uint32_t shown, glm::vec3 motion)
		{
			const auto block = view.CreateMeshInstanceBlock(
				bgl::MeshInstanceBlockDesc().SetGeom(cube).SetCapacity(capacity));
			view.SetBlockWriter(block, mover);
			auto params       = view.GetBlockParams(block);
			params["origin"]  = glm::vec3(-2.0f, 0.0f, 0.0f);
			params["spacing"] = 2.0f;
			params["motion"]  = motion;
			params["shown"]   = shown;
			return block;
		}

		std::vector<std::byte>
		Draw(Drawn& drawn, int frames)
		{
			for (int i = 0; i < frames; ++i)
			{
				gfx->DrawFrame(drawn.target, drawn.job);
			}
			const auto image = gfx->ScreenshotToMemory(drawn.target);
			return std::vector<std::byte>(image.pixels.begin(), image.pixels.end());
		}
	};
}

TEST_CASE(
	"A written block draws what the same placements draw from the CPU",
	"[instance_block][render]")
{
	auto s = BlockScene(bgl::test::CreateGraphics(HeadlessOptions()));

	auto fromCpu = s.MakeView();
	for (uint32_t i = 0; i < 3; ++i)
	{
		fromCpu.view->CreateStaticMeshInstance(
			bgl::StaticMeshInstanceDesc().SetGeom(s.cube).SetTransform(
				glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f + 2.0f * float(i), 0.0f, 0.0f))));
	}

	// Eight slots, three shown: the five hidden ones sit at the origin of their run and must not draw.
	auto fromBlock = s.MakeView();
	s.AddMover(*fromBlock.view, 8, 3, glm::vec3(0.0f));

	const auto cpu   = s.Draw(fromCpu, 4);
	const auto block = s.Draw(fromBlock, 4);
	CHECK(block == cpu);

	auto empty = s.MakeView();
	CHECK(s.Draw(empty, 4) != cpu);
}

TEST_CASE("A written block's motion is the pair its writer placed", "[instance_block][render]")
{
	const auto motion = glm::vec3(0.25f, 0.0f, 0.0f);

	auto s = BlockScene(bgl::test::CreateGraphics(HeadlessOptions()));

	// The CPU's way to the same pair: placed behind, drawn, then moved to where the block shows it.
	auto                                 fromCpu = s.MakeView();
	std::vector<bgl::MeshInstanceHandle> instances;
	for (uint32_t i = 0; i < 3; ++i)
	{
		instances.push_back(fromCpu.view->CreateStaticMeshInstance(
			bgl::StaticMeshInstanceDesc().SetGeom(s.cube).SetTransform(
				glm::translate(
					glm::mat4(1.0f),
					glm::vec3(-2.0f + 2.0f * float(i), 0.0f, 0.0f) - motion))));
	}
	(void)s.Draw(fromCpu, 3);
	for (uint32_t i = 0; i < 3; ++i)
	{
		fromCpu.view->SetInstanceTransform(
			instances[i],
			glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f + 2.0f * float(i), 0.0f, 0.0f)));
	}
	(void)s.Draw(fromCpu, 1);

	auto fromBlock = s.MakeView();
	s.AddMover(*fromBlock.view, 4, 3, motion);
	(void)s.Draw(fromBlock, 4);

	const auto cpu = bgl::test::ReadVelocityTexels(
		s.gfx.Get(),
		fromCpu.target.Get(),
		BlockScene::c_Size,
		BlockScene::c_Size);
	const auto block = bgl::test::ReadVelocityTexels(
		s.gfx.Get(),
		fromBlock.target.Get(),
		BlockScene::c_Size,
		BlockScene::c_Size);
	REQUIRE(cpu.size() == block.size());

	uint32_t moving = 0;
	for (size_t i = 0; i < cpu.size(); ++i)
	{
		CHECK(glm::length(glm::vec2(block[i]) - glm::vec2(cpu[i])) < 1e-3f);
		moving += glm::length(glm::vec2(cpu[i])) > 1e-3f ? 1u : 0u;
	}
	// Not vacuous: the cubes cover pixels, and those pixels move.
	CHECK(moving > 0);
}

TEST_CASE(
	"A writer reads another owner's buffer once the frame waits on it",
	"[instance_block][render][import]")
{
	auto s = BlockScene(bgl::test::CreateGraphics(HeadlessOptions()));

	const auto positions = std::vector<glm::vec4>{
		glm::vec4(-2.0f, 0.0f, 0.0f, 1.0f),
		glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
		glm::vec4(2.0f, 0.0f, 0.0f, 1.0f),
	};

	auto fromCpu = s.MakeView();
	for (const glm::vec4& p : positions)
	{
		fromCpu.view->CreateStaticMeshInstance(
			bgl::StaticMeshInstanceDesc().SetGeom(s.cube).SetTransform(
				glm::translate(glm::mat4(1.0f), glm::vec3(p))));
	}
	const auto cpu = s.Draw(fromCpu, 4);

	// A second owner on the renderer's device, as a crowd is: its own manager, its own queue.
	auto  base   = s.gfx->As<bgl::GraphicsBase>();
	auto* device = base->GetDevice();
	auto  rm     = device->CreateResourceManager(bgpu::ResourceManagerDesc::ComputeOnly());
	auto  queue  = device->CreateCommandQueue(bgpu::QueueType::kCompute);
	rm->RegisterQueue(queue.Get());
	const bgpu::BufferHandle produced = rm->CreateStructBuffer(
		bgpu::StructBufferDesc()
			.SetElement<glm::vec4>()
			.SetElementCount(static_cast<uint32_t>(positions.size()))
			.SetDebugName("Producer positions"));

	auto exportedType = bgpu::NativeObjectType::kMtlBuffer;
	auto exported     = rm->GetNativeBuffer(produced, exportedType);
	if (!exported)
	{
		exportedType = bgpu::NativeObjectType::kD3D12Resource;
		exported     = rm->GetNativeBuffer(produced, exportedType);
	}
	REQUIRE(exported);
	const bgl::ExternalBufferRef imported = s.gfx->ImportBuffer(
		bgpu::NativeBufferDesc()
			.SetObject(exportedType, exported)
			.SetBuffer(
				bgpu::StructBufferDesc().SetElement<glm::vec4>().SetElementCount(
					static_cast<uint32_t>(positions.size()))));

	const auto writer = s.gfx->CreateMeshInstanceWriter(
		bgl::MeshInstanceWriterDesc()
			.SetSlangModuleName("MeshInstanceWriterFromBuffer")
			.SetSlangTypeName("MeshInstanceWriterFromBuffer"));

	auto       fromBlock = s.MakeView();
	const auto block     = fromBlock.view->CreateMeshInstanceBlock(
		bgl::MeshInstanceBlockDesc().SetGeom(s.cube).SetCapacity(8));
	fromBlock.view->SetBlockWriter(block, writer);
	auto params         = fromBlock.view->GetBlockParams(block);
	params["positions"] = imported->GetHandle();
	params["shown"]     = static_cast<uint32_t>(positions.size());

	// The frame waits on the producer's next point, and the producer submits after that: only the
	// GPU-side wait orders the upload before the frame's read.
	const auto point = bgpu::QueuePoint{ queue, queue->GetNextFenceValue() };
	s.gfx->WaitBeforeNextFrame(point);

	auto listDesc = bgpu::CommandListDesc();
	listDesc.type = bgpu::QueueType::kCompute;
	auto alloc    = device->CreateCommandAllocator(bgpu::QueueType::kCompute);
	auto list     = device->CreateCommandList(listDesc, alloc, rm);
	list->Open(queue.Get(), alloc.Get());
	list->WriteBuffer(produced, positions.data(), 0, positions.size() * sizeof(glm::vec4));
	list->Close();
	CHECK(queue->ExecuteCommandList(list) == point.value);

	CHECK(s.Draw(fromBlock, 4) == cpu);

	s.gfx->WaitIdle();
	queue->Flush();
	rm->DestroyBuffer(produced, false);
	rm->UnregisterQueue(queue.Get());
}
