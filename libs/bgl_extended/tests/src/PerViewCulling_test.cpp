#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "culling/Frustum.h"
#include "fg/FrameGraph.h"
#include "gfx/GraphicsBase.h"
#include "idl/PsoType.h"
#include "idl/idl.h"
#include "passes/CompactInstancesPass.h"
#include "passes/DrawData.h"
#include "pipeline/PipelineBatch.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/CullState.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "scene/scene_buffer_names.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <core/math.h>

// One SceneView culled against two different frustums in a single frame -- a cascade set in
// miniature, and the thing that was impossible before each frustum got its own CullState. Both
// frustums' compaction outputs are read back and checked against a CPU reference cull over the same
// spheres, so the failure this pins is not "the graph threw" but "frustum 1 overwrote frustum 0" and
// its twin, "both frustums resolved to one buffer".

namespace
{
	// Eyes on the -Z axis looking further down it, 90-degree fov, square aspect. The two overlap in
	// the middle of the range but neither contains the other, so a result that is right for one
	// frustum is wrong for the other in both directions.
	glm::mat4
	FrustumLookingDownZ(float eyeZ)
	{
		return bgl::Camera()
		    .LookAt(
				glm::vec3(0.0f, 0.0f, eyeZ),
				glm::vec3(0.0f, 0.0f, eyeZ - 1.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
		    .Perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f)
		    .GetViewProjection();
	}

	// The compacted list holds instance-buffer slots. Only the first `count` are written; the tail
	// keeps whatever the previous frame left.
	std::vector<uint32_t>
	CompactedSet(const uint32_t* compacted, uint32_t count)
	{
		auto out = std::vector<uint32_t>(compacted, compacted + count);
		std::ranges::sort(out);
		return out;
	}
}

TEST_CASE("One view culled against two frustums keeps both results", "[culling][compute][view]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialSubmeshes            = 8;
	sceneDesc.initialMeshlets             = 16;
	sceneDesc.initialVertexBufferByteSize = 32000;
	sceneDesc.initialIndices              = 1000;

	auto sceneHandle = gfx->CreateScene(sceneDesc);
	REQUIRE(sceneHandle != nullptr);

	auto* scene = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	// A real material, so the instances land in a real PSO bucket: the histogram and the compaction
	// both skip an instance carrying pso kInvalid, and every assertion below would read zero.
	auto material         = bgl::MaterialHandle();
	material.materialType = bgl::MaterialType::kPBR;

	const auto geom = scene->AddCubeGeom(material);
	REQUIRE(geom.IsValid());

	// Frustum 0 sees z in [-1, -100] from the origin; frustum 1 sees z in [-51, -150] from z = -50.
	const glm::vec3 positions[] = {
		glm::vec3(0.0f, 0.0f, -10.0f),   // frustum 0 only -- behind frustum 1's eye
		glm::vec3(0.0f, 0.0f, -30.0f),   // frustum 0 only
		glm::vec3(0.0f, 0.0f, -70.0f),   // both
		glm::vec3(0.0f, 0.0f, -90.0f),   // both
		glm::vec3(0.0f, 0.0f, -130.0f),  // frustum 1 only -- past frustum 0's far plane
		glm::vec3(0.0f, 0.0f, -140.0f),  // frustum 1 only
		glm::vec3(60.0f, 0.0f, -10.0f),  // neither -- outside frustum 0's side planes, behind 1
		glm::vec3(0.0f, 0.0f, 50.0f),    // neither -- behind both eyes
	};
	constexpr uint32_t c_InstanceCount = static_cast<uint32_t>(std::size(positions));

	auto viewHandle = gfx->CreateSceneView(sceneHandle, c_InstanceCount);
	REQUIRE(viewHandle != nullptr);

	auto* view = viewHandle->As<bgl::SceneView>();
	REQUIRE(view != nullptr);

	for (const glm::vec3& position : positions)
	{
		REQUIRE(view->CreateStaticMeshInstance(geom, glm::translate(glm::mat4(1.0f), position))
		            .IsValid());
	}

	// One submesh per cube, allocated in order and never deleted, so instance-buffer slot i is
	// positions[i]. The reference cull below is indexed on that.
	REQUIRE(view->GetInstanceCount() == c_InstanceCount);

	view->EnsureCullStateCount(2);
	REQUIRE(view->GetCullStateCount() == 2);

	auto& submeshBuffer = scene->GetSubmeshBuffer();

	// Element 0 of the submesh buffer is the reserved null, so the cube's submesh is found through
	// its geom's range rather than at a fixed index.
	const uint32_t submeshRoot = scene->GetGeomSubmeshes(geom.handle.index).range.offsetStart;
	REQUIRE(submeshBuffer.IsIndexValid(submeshRoot));

	// Whatever radius the scene cooked for the cube, so the reference cull tests the sphere the
	// shader tests rather than one this file assumed.
	const glm::vec4       bounds      = submeshBuffer.AtIndex(submeshRoot).boundingSphere;
	const glm::vec3       localCenter = glm::vec3(bounds);
	const float           localRadius = bounds.w;
	const glm::mat4       viewProj[2] = { FrustumLookingDownZ(0.0f), FrustumLookingDownZ(-50.0f) };
	std::vector<uint32_t> expected[2];

	for (uint32_t cullIdx = 0; cullIdx < 2; ++cullIdx)
	{
		const bgl::FrustumPlanes planes = bgl::ExtractFrustumPlanes(viewProj[cullIdx]);

		for (uint32_t i = 0; i < c_InstanceCount; ++i)
		{
			// Translation only, so the radius carries over unscaled.
			if (bgl::SphereIntersectsFrustum(planes, localCenter + positions[i], localRadius))
			{
				expected[cullIdx].push_back(i);
			}
		}
	}

	// The premise of the test: two frustums that disagree, each seeing something the other does not.
	REQUIRE(expected[0] != expected[1]);
	REQUIRE_FALSE(expected[0].empty());
	REQUIRE_FALSE(expected[1].empty());

	auto cmdListDesc  = bgl::CommandListDesc();
	cmdListDesc.type  = bgl::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	auto compactPass = bgl::CompactInstancesPass();
	{
		auto pipelines = bgl::PipelineBatch(device);
		compactPass.Init(device, pipelines, resourceManager);
		pipelines.Build();
	}

	// Off the instance buffer's capacity, which is what CullState sizes against -- a readback
	// smaller than its source overruns, since the copy is bounded by the source's byte size.
	const uint32_t paddedCount =
		core::round_up(view->GetInstanceBuffer().Capacity(), bgl::idl::cHistogramGroupSize);

	bgl::ReadbackBufferHandle rbCompacted[2];
	bgl::ReadbackBufferHandle rbPrefixSum[2];
	for (uint32_t cullIdx = 0; cullIdx < 2; ++cullIdx)
	{
		auto rbDesc          = bgl::ReadbackBufferDesc();
		rbDesc.byteSize      = static_cast<uint64_t>(paddedCount) * sizeof(uint32_t);
		rbDesc.debugName     = "Compacted Readback";
		rbCompacted[cullIdx] = resourceManager->CreateReadbackBuffer(rbDesc);

		rbDesc.byteSize      = static_cast<uint64_t>(bgl::idl::c_PsoCount) * sizeof(uint32_t);
		rbDesc.debugName     = "Prefix-Sum Readback";
		rbPrefixSum[cullIdx] = resourceManager->CreateReadbackBuffer(rbDesc);
	}

	bgl::FrameGraph fg;
	fg.RegisterQueue("main", cmdQueue, cmdList);

	// Exactly what RenderContext::Draw does: the scene's and the view's buffers under the view's
	// scope, then the cull pipeline once per frustum under that frustum's scope inside it.
	fg.SetResourceNamespace(view->GetResourceNamespace());
	scene->AttachToFrameGraph(fg, 0);
	view->AttachToFrameGraph(fg, 0);

	for (uint32_t cullIdx = 0; cullIdx < 2; ++cullIdx)
	{
		auto draw               = bgl::DrawData();
		draw.drawIdx            = 0;
		draw.cullIdx            = cullIdx;
		draw.view               = viewHandle;
		draw.cullState          = &view->GetCullState(cullIdx);
		draw.viewState.viewProj = viewProj[cullIdx];
		draw.viewState.cullView = bgl::BuildCullView(viewProj[cullIdx]);

		fg.SetResourceNamespace(view->GetCullNamespace(cullIdx));
		compactPass.AttachToFrameGraph(fg, draw);
	}

	// Both readbacks are recorded after both culls, and that is the whole point: passes execute in
	// submission order, so a readback sitting between the two culls would copy the first frustum's
	// result before the second could overwrite it -- and would pass just as happily with one shared
	// buffer behind both scopes. Reading both at the end is what makes the two coexist, or not.
	//
	// They also go through ctx.GetBuffer rather than CullState's members, so the handles under test
	// are the ones name resolution produced for each frustum's scope.
	for (uint32_t cullIdx = 0; cullIdx < 2; ++cullIdx)
	{
		fg.SetResourceNamespace(view->GetCullNamespace(cullIdx));

		fg.AddPass(
			bgl::PassDesc()
				.SetName("Readback {}", cullIdx)
				.AddBufferArg(
					bgl::c_CompactedInstancesName,
					bgl::BarrierSyncFlag::kCopy,
					bgl::BarrierAccessFlag::kCopySource)
				.AddBufferArg(
					bgl::c_PsoPrefixSumName,
					bgl::BarrierSyncFlag::kCopy,
					bgl::BarrierAccessFlag::kCopySource)
				.SetSideEffect()
				.SetExec([&, cullIdx](const bgl::PassContext& ctx) {
					auto* cmd = ctx.GetCommandList();
					cmd->CopyBufferToReadback(
						rbCompacted[cullIdx],
						ctx.GetBuffer(bgl::c_CompactedInstancesName));
					cmd->CopyBufferToReadback(
						rbPrefixSum[cullIdx],
						ctx.GetBuffer(bgl::c_PsoPrefixSumName));
				}));
	}

	fg.Compile(resourceManager.Get());

	cmdList->Open(cmdQueue, cmdAllocator);
	fg.Execute();
	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	for (uint32_t cullIdx = 0; cullIdx < 2; ++cullIdx)
	{
		INFO("frustum " << cullIdx);

		const auto* prefixSum =
			static_cast<const uint32_t*>(resourceManager->MapReadback(rbPrefixSum[cullIdx]));
		REQUIRE(prefixSum != nullptr);

		// Inclusive scan over the PSO buckets, so the last entry is everything that survived.
		const uint32_t visible = prefixSum[bgl::idl::c_PsoCount - 1];
		resourceManager->UnmapReadback(rbPrefixSum[cullIdx]);

		CHECK(visible == expected[cullIdx].size());
		REQUIRE(visible <= paddedCount);

		const auto* compacted =
			static_cast<const uint32_t*>(resourceManager->MapReadback(rbCompacted[cullIdx]));
		REQUIRE(compacted != nullptr);

		CHECK(CompactedSet(compacted, visible) == expected[cullIdx]);
		resourceManager->UnmapReadback(rbCompacted[cullIdx]);
	}

	compactPass.Release(false);
}
