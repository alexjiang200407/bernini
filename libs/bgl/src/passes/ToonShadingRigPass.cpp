#include "passes/ToonShadingRigPass.h"
#include "fg/FrameGraph.h"
#include "passes/DrawData.h"
#include "scene/SceneView.h"
#include "scene/scene_buffer_names.h"
#include <bgpu/cmd/CommandList.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/pipeline/PipelineBatch.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/err/util.h>
#include <core/math.h>
#include <cstdint>

namespace bgl
{
	namespace
	{
		constexpr uint32_t c_GroupSize = 64;
	}

	ToonShadingRigPass::ToonShadingRigPass(const PassInitContext& ctx)
	{
		core::ensure(ctx.device != nullptr, "Device must be initialized");

		ctx.pipelines->Add(
			m_Evaluate,
			bgpu::ComputePipelineDesc()
				.SetShader(ctx.device->CreateShader("programs.toon.EvaluateToonShadingRigs"))
				.SetDebugName("Evaluate Toon Shading Rigs"));
	}

	void
	ToonShadingRigPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		const auto* view = draw.view->As<SceneView>();
		core::ensure(view != nullptr, "ToonShadingRigPass requires a bgl::SceneView");

		if (view->GetToonShadingRigs().GetPlacementCount() == 0)
		{
			return;
		}

		fg.AddPass(
			PassDesc()
				.SetName("Toon Shading Rigs {}", draw.drawIdx)
				.AddBufferRead(c_ToonShadingRigRangesName, bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferReadWrite(c_MeshInstanceBufferName, bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.playbackBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.rigBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.skinnedBoneBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.clipBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead(c_BonePaletteName, bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead(c_BoneAnimTableName, bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead(c_InstancePoseName, bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead(c_DominantFramesName, bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead(c_ToonShadingRigBufferName, bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead(c_ToonShadingRigKeyBufferName, bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferReadWrite(c_ToonShadingRigPoolName, bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferReadWrite(
					c_ToonShadingRigBlocksName,
					bgpu::BarrierSyncFlag::kComputeShader)
				.SetExec([draw, this](const PassContext& ctx) { Execute(ctx, draw); }));
	}

	void
	ToonShadingRigPass::Execute(const PassContext& ctx, const DrawData& draw)
	{
		auto* view = draw.view->As<SceneView>();
		core::ensure(view != nullptr, "ToonShadingRigPass requires a bgl::SceneView");

		const ToonShadingRigState& rigs    = view->GetToonShadingRigs();
		const uint32_t             threads = rigs.GetPlacementCount();

		const bgpu::BufferUavHandle meshes = view->GetMeshBuffer().GetWritableView();
		core::ensure(!meshes.IsNull(), "A view's mesh buffer always has a writable view");

		bgpu::Uniforms& uniforms   = m_Evaluate["gUniforms"];
		uniforms["ranges"]         = ctx.GetBuffer(c_ToonShadingRigRangesName);
		uniforms["rangeCount"]     = rigs.GetRangeCount();
		uniforms["threadCount"]    = threads;
		uniforms["meshes"]         = meshes;
		uniforms["playbackBuffer"] = ctx.GetBuffer("scene.playbackBuffer");
		uniforms["rigs"]           = ctx.GetBuffer("scene.rigBuffer");
		uniforms["boneBuffer"]     = ctx.GetBuffer("scene.skinnedBoneBuffer");
		uniforms["clipBuffer"]     = ctx.GetBuffer("scene.clipBuffer");
		uniforms["bonePalettes"]   = ctx.GetBuffer(c_BonePaletteName);
		uniforms["boneAnimTables"] = ctx.GetBuffer(c_BoneAnimTableName);
		uniforms["instancePose"]   = ctx.GetBuffer(c_InstancePoseName);
		uniforms["dominantFrames"] = ctx.GetBuffer(c_DominantFramesName);
		uniforms["toonRigs"]       = ctx.GetBuffer(c_ToonShadingRigBufferName);
		uniforms["toonKeys"]       = ctx.GetBuffer(c_ToonShadingRigKeyBufferName);
		uniforms["pool"]           = ctx.GetBuffer(c_ToonShadingRigPoolName);
		uniforms["blocks"]         = ctx.GetBuffer(c_ToonShadingRigBlocksName);

		const idl::CullView& cull = draw.viewState.cullView;
		for (uint32_t plane = 0; plane < 6; ++plane)
		{
			uniforms["frustumPlanes"][plane] = cull.frustumPlanes[plane];
		}
		uniforms["cameraPos"]     = draw.viewState.cameraPos;
		uniforms["pixelsPerUnit"] = draw.viewState.pixelsPerUnit;
		uniforms["toLight"]       = -draw.lighting.sunDirection;
		uniforms["time"]          = draw.clock.time;

		auto computeState   = bgpu::ComputeState();
		computeState.kernel = &m_Evaluate;

		auto cmdList = ctx.GetCommandList();
		cmdList->SetComputeState(computeState);
		cmdList->Dispatch(core::div_ceil(threads, c_GroupSize), 1, 1);
	}
}
