#include "passes/WriteInstanceBlocksPass.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "instance_block/MeshInstanceBlock.h"
#include "instance_block/MeshInstanceWriter.h"
#include "passes/DrawData.h"
#include "scene/SceneView.h"
#include <bgpu/cmd/CommandList.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/err/util.h>

namespace bgl
{
	void
	WriteInstanceBlocksPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		auto* view = draw.view->As<SceneView>();
		core::ensure(view != nullptr, "WriteInstanceBlocksPass requires a bgl::SceneView");

		bool written = false;
		view->ForEachWrittenBlock([&written](const MeshInstanceBlock&) { written = true; });
		if (!written)
		{
			return;
		}

		fg.AddPass(
			PassDesc()
				.SetName("Write Instance Blocks {}", draw.drawIdx)
				.AddBufferReadWrite(
					"scene.meshInstanceBuffer",
					bgpu::BarrierSyncFlag::kComputeShader)
				.SetExec([draw](const PassContext& ctx) { Execute(ctx, draw); }));
	}

	void
	WriteInstanceBlocksPass::Execute(const PassContext& ctx, const DrawData& draw)
	{
		auto* view = draw.view->As<SceneView>();
		core::ensure(view != nullptr, "WriteInstanceBlocksPass requires a bgl::SceneView");

		// The buffer's writable view, re-read now: a growth since the last frame replaced it.
		const bgpu::BufferUavHandle meshes = view->GetMeshBuffer().GetWritableView();
		core::ensure(!meshes.IsNull(), "A view's mesh buffer always has a writable view");

		auto cmdList = ctx.GetCommandList();
		view->ForEachWrittenBlock([&](MeshInstanceBlock& block) {
			bgpu::Uniforms& uniforms      = block.kernel["gUniforms"];
			uniforms["block"]["meshes"]   = meshes;
			uniforms["block"]["first"]    = block.range.first;
			uniforms["block"]["capacity"] = block.capacity;

			auto state   = bgpu::ComputeState();
			state.kernel = &block.kernel;
			cmdList->SetComputeState(state);
			cmdList->Dispatch(MeshInstanceWriter::DispatchGroups(block.capacity), 1, 1);
		});
	}
}
