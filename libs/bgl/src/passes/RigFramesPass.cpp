#include "passes/RigFramesPass.h"
#include "cmd/CommandList.h"
#include "fg/FrameGraph.h"
#include "passes/DrawData.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "uniforms/Uniforms.h"

namespace bgl
{
	void
	RigFramesPass::Init(IDevice* device)
	{
		gassert(device != nullptr, "Device must be initialized");

		m_PoseRigFrames = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(device->CreateShader("programs.anim.PoseRigFrames"))
				.SetDebugName("Pose Rig Frames"));
	}

	void
	RigFramesPass::Release()
	{
		logger::trace("RigFramesPass::Release");
		m_PoseRigFrames.Reset();
	}

	void
	RigFramesPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		const auto* view = draw.view->As<SceneView>();
		gassert(view != nullptr, "RigFramesPass requires a bgl::SceneView");

		auto* scene = view->GetScene()->As<Scene>();
		gassert(scene != nullptr, "RigFramesPass requires a bgl::Scene");

		// Attached only on a frame that has a table to fill, which is almost none of them. The pass
		// writes `scene.boneAnimTables`, which the scene imports, so the frame graph would keep it as
		// a root however little it did -- and a scene drawing no crowd instance at all would pay a
		// pass node and a UAV transition every frame for a buffer nothing reads.
		if (scene->PendingRigFills().empty())
		{
			return;
		}

		fg.AddPass(
			PassDesc()
				.SetName("Pose Rig Frames {}", draw.drawIdx)
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
					"scene.boneAnimTables",
					BarrierSyncFlag::kComputeShader,
					BarrierAccessFlag::kUnorderedAccess)
				.SetExec([draw, this](const PassContext& ctx) { Execute(ctx, draw); }));
	}

	void
	RigFramesPass::Execute(const PassContext& ctx, const DrawData& draw)
	{
		const auto* view = draw.view->As<SceneView>();
		gassert(view != nullptr, "RigFramesPass requires a bgl::SceneView");

		auto* scene = view->GetScene()->As<Scene>();
		gassert(scene != nullptr, "RigFramesPass requires a bgl::Scene");

		// AttachToFrameGraph asked the same question and did not add this pass on an empty answer;
		// nothing between the two can spawn an instance.
		const std::span<const Scene::RigFill> fills = scene->PendingRigFills();
		gassert(!fills.empty(), "Pose Rig Frames was attached with no rig to fill");

		Uniforms& uniforms         = m_PoseRigFrames["gUniforms"];
		uniforms["rigs"]           = ctx.GetBuffer("scene.rigBuffer");
		uniforms["boneBuffer"]     = ctx.GetBuffer("scene.skinnedBoneBuffer");
		uniforms["clipBuffer"]     = ctx.GetBuffer("scene.clipBuffer");
		uniforms["sampleBuffer"]   = ctx.GetBuffer("scene.boneSampleBuffer");
		uniforms["boneAnimTables"] = ctx.GetBuffer("scene.boneAnimTables");

		auto computeState   = ComputeState();
		computeState.kernel = &m_PoseRigFrames;

		auto cmdList = ctx.GetCommandList();
		cmdList->SetComputeState(computeState);

		// One dispatch per rig rather than one over all of them: the group count is the rig's own
		// frame count, and there is no list to index because a rig wanting a table is rare. Dispatch
		// re-reads the uniforms through the state's kernel, so the two set here reach each one.
		for (const Scene::RigFill& fill : fills)
		{
			uniforms["rigIndex"]   = fill.rigIndex;
			uniforms["frameCount"] = fill.frameCount;

			// One group per frame, not per bone: the hierarchy walk barriers within a group, so a
			// rig's frame cannot be split across two.
			cmdList->Dispatch(fill.frameCount, 1, 1);
		}

		scene->MarkRigFillsRecorded();
	}
}
