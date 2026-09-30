#include "passes/SkinnedPosePass.h"
#include "fg/FrameGraph.h"
#include "passes/DrawData.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include <bgl/types/GroundPlaneDesc.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/pipeline/PipelineBatch.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/err/util.h>
#include <cstdint>
#include <spdlog/spdlog.h>

namespace bgl
{
	void
	SkinnedPosePass::Init(const PassInitContext& ctx)
	{
		core::ensure(ctx.device != nullptr, "Device must be initialized");

		ctx.pipelines->Add(
			m_PoseSkinned,
			bgpu::ComputePipelineDesc()
				.SetShader(ctx.device->CreateShader("programs.anim.PoseSkinned"))
				.SetDebugName("Pose Skinned"));
	}

	void
	SkinnedPosePass::Release()
	{
		spdlog::trace("SkinnedPosePass::Release");
		m_PoseSkinned.Reset();
	}

	void
	SkinnedPosePass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		fg.AddPass(
			PassDesc()
				.SetName("Pose Skinned {}", draw.drawIdx)
				.AddBufferRead("scene.posedInstances", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.meshInstanceBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.playbackBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.rigBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.skinnedBoneBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.clipBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.boneSampleBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.skinnedLegBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.plantWeightBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.blendNodeBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.blendSampleBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferRead("scene.footIKBuffer", bgpu::BarrierSyncFlag::kComputeShader)
				.AddBufferReadWrite("scene.bonePalettes", bgpu::BarrierSyncFlag::kComputeShader)
				.SetExec([draw, this](const PassContext& ctx) { Execute(ctx, draw); }));
	}

	void
	SkinnedPosePass::Execute(const PassContext& ctx, const DrawData& draw)
	{
		const auto* view = draw.view->As<SceneView>();
		core::ensure(view != nullptr, "SkinnedPosePass requires a bgl::SceneView");

		const uint32_t posed = view->GetPosedInstanceCount();
		if (posed == 0)
		{
			return;
		}

		bgpu::Uniforms& uniforms      = m_PoseSkinned["gUniforms"];
		uniforms["posedInstances"]    = ctx.GetBuffer("scene.posedInstances");
		uniforms["meshBuffer"]        = ctx.GetBuffer("scene.meshInstanceBuffer");
		uniforms["playbackBuffer"]    = ctx.GetBuffer("scene.playbackBuffer");
		uniforms["rigs"]              = ctx.GetBuffer("scene.rigBuffer");
		uniforms["boneBuffer"]        = ctx.GetBuffer("scene.skinnedBoneBuffer");
		uniforms["clipBuffer"]        = ctx.GetBuffer("scene.clipBuffer");
		uniforms["sampleBuffer"]      = ctx.GetBuffer("scene.boneSampleBuffer");
		uniforms["legBuffer"]         = ctx.GetBuffer("scene.skinnedLegBuffer");
		uniforms["plantWeightBuffer"] = ctx.GetBuffer("scene.plantWeightBuffer");
		uniforms["blendNodeBuffer"]   = ctx.GetBuffer("scene.blendNodeBuffer");
		uniforms["blendSampleBuffer"] = ctx.GetBuffer("scene.blendSampleBuffer");
		uniforms["footIKBuffer"]      = ctx.GetBuffer("scene.footIKBuffer");
		uniforms["bonePalettes"]      = ctx.GetBuffer("scene.bonePalettes");
		uniforms["time"]              = draw.clock.time;
		uniforms["prevTime"]          = draw.clock.prevTime;
		uniforms["posedCount"]        = posed;

		const Scene*           scene  = view->GetScene()->As<Scene>();
		const GroundPlaneDesc& ground = scene->GetGround();
		uniforms["groundPoint"]       = ground.point;
		uniforms["groundNormal"]      = ground.normal;
		uniforms["plantFeet"]         = scene->GetFootPlanting() ? 1u : 0u;

		auto computeState   = bgpu::ComputeState();
		computeState.kernel = &m_PoseSkinned;

		auto cmdList = ctx.GetCommandList();
		cmdList->SetComputeState(computeState);

		// One group per instance, not per bone: the hierarchy walk barriers within a group, so a rig
		// cannot be split across two.
		cmdList->Dispatch(posed, 1, 1);
	}
}
