#include "passes/SkinnedPosePass.h"
#include "cmd/CommandList.h"
#include "fg/FrameGraph.h"
#include "passes/DrawData.h"
#include "pipeline/PipelineBatch.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "types/Barrier.h"
#include "uniforms/Uniforms.h"
#include <bgl/types/GroundPlaneDesc.h>
#include <bgl_common/gassert.h>
#include <cstdint>
#include <spdlog/spdlog.h>

namespace bgl
{
	void
	SkinnedPosePass::Init(IDevice* device, PipelineBatch& pipelines)
	{
		gassert(device != nullptr, "Device must be initialized");

		pipelines.Add(
			m_PoseSkinned,
			ComputePipelineDesc()
				.SetShader(device->CreateShader("programs.anim.PoseSkinned"))
				.SetDebugName("Pose Skinned"));
	}

	void
	SkinnedPosePass::Release()
	{
		logger::trace("SkinnedPosePass::Release");
		m_PoseSkinned.Reset();
	}

	void
	SkinnedPosePass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		fg.AddPass(
			PassDesc()
				.SetName("Pose Skinned {}", draw.drawIdx)
				.AddBufferArg(
					"scene.posedInstances",
					BarrierSyncFlag::kComputeShader,
					BarrierAccessFlag::kShaderResource)
				.AddBufferArg(
					"scene.meshInstanceBuffer",
					BarrierSyncFlag::kComputeShader,
					BarrierAccessFlag::kShaderResource)
				.AddBufferArg(
					"scene.playbackBuffer",
					BarrierSyncFlag::kComputeShader,
					BarrierAccessFlag::kShaderResource)
				.AddBufferArg(
					"scene.rigBuffer",
					BarrierSyncFlag::kComputeShader,
					BarrierAccessFlag::kShaderResource)
				.AddBufferArg(
					"scene.skinnedBoneBuffer",
					BarrierSyncFlag::kComputeShader,
					BarrierAccessFlag::kShaderResource)
				.AddBufferArg(
					"scene.clipBuffer",
					BarrierSyncFlag::kComputeShader,
					BarrierAccessFlag::kShaderResource)
				.AddBufferArg(
					"scene.boneSampleBuffer",
					BarrierSyncFlag::kComputeShader,
					BarrierAccessFlag::kShaderResource)
				.AddBufferArg(
					"scene.skinnedLegBuffer",
					BarrierSyncFlag::kComputeShader,
					BarrierAccessFlag::kShaderResource)
				.AddBufferArg(
					"scene.plantWeightBuffer",
					BarrierSyncFlag::kComputeShader,
					BarrierAccessFlag::kShaderResource)
				.AddBufferArg(
					"scene.bonePalettes",
					BarrierSyncFlag::kComputeShader,
					BarrierAccessFlag::kUnorderedAccess)
				.SetExec([draw, this](const PassContext& ctx) { Execute(ctx, draw); }));
	}

	void
	SkinnedPosePass::Execute(const PassContext& ctx, const DrawData& draw)
	{
		const auto* view = draw.view->As<SceneView>();
		gassert(view != nullptr, "SkinnedPosePass requires a bgl::SceneView");

		const uint32_t posed = view->GetPosedInstanceCount();
		if (posed == 0)
		{
			return;
		}

		Uniforms& uniforms            = m_PoseSkinned["gUniforms"];
		uniforms["posedInstances"]    = ctx.GetBuffer("scene.posedInstances");
		uniforms["meshBuffer"]        = ctx.GetBuffer("scene.meshInstanceBuffer");
		uniforms["playbackBuffer"]    = ctx.GetBuffer("scene.playbackBuffer");
		uniforms["rigs"]              = ctx.GetBuffer("scene.rigBuffer");
		uniforms["boneBuffer"]        = ctx.GetBuffer("scene.skinnedBoneBuffer");
		uniforms["clipBuffer"]        = ctx.GetBuffer("scene.clipBuffer");
		uniforms["sampleBuffer"]      = ctx.GetBuffer("scene.boneSampleBuffer");
		uniforms["legBuffer"]         = ctx.GetBuffer("scene.skinnedLegBuffer");
		uniforms["plantWeightBuffer"] = ctx.GetBuffer("scene.plantWeightBuffer");
		uniforms["bonePalettes"]      = ctx.GetBuffer("scene.bonePalettes");
		uniforms["time"]              = draw.clock.time;
		uniforms["prevTime"]          = draw.clock.prevTime;
		uniforms["posedCount"]        = posed;

		const Scene*           scene  = view->GetScene()->As<Scene>();
		const GroundPlaneDesc& ground = scene->GetGround();
		uniforms["groundPoint"]       = ground.point;
		uniforms["groundNormal"]      = ground.normal;
		uniforms["plantFeet"]         = scene->GetFootPlanting() ? 1u : 0u;

		auto computeState   = ComputeState();
		computeState.kernel = &m_PoseSkinned;

		auto cmdList = ctx.GetCommandList();
		cmdList->SetComputeState(computeState);

		// One group per instance, not per bone: the hierarchy walk barriers within a group, so a rig
		// cannot be split across two.
		cmdList->Dispatch(posed, 1, 1);
	}
}
