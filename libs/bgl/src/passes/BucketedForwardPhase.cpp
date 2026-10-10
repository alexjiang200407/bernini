#include "passes/BucketedForwardPhase.h"
#include "fg/PassDesc.h"
#include "gfx/DrawBucketTable.h"
#include "gfx/frame_constants.h"
#include "passes/DrawData.h"
#include "passes/ForwardPhases.h"
#include "passes/SceneBindings.h"
#include "passes/draw_bucket_config.h"
#include "scene/scene_buffer_names.h"
#include <bgl/ISceneView.h>
#include <bgl/idl/BaseTable.h>
#include <bgl/idl/DrawBucket.h>
#include <bgl/idl/LodDrawMode.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/constants/constants.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/MeshletState.h>
#include <core/err/util.h>
#include <cstdint>
#include <string>
#include <string_view>

namespace bgl
{
	BucketedForwardPhase::BucketedForwardPhase(
		const GeometryStage    stage,
		const std::string_view name) noexcept : m_Stage(stage), m_Name(name)
	{}

	bool
	BucketedForwardPhase::HasWork(const DrawData& draw) const
	{
		return draw.view->GetInstanceCount() > 0;
	}

	void
	BucketedForwardPhase::Declare(PassDesc& desc, const DrawData& /*draw*/) const
	{
		desc.AddRenderTarget(c_MotionVectorsName).AddIndirectArgs(c_CompactDispatchArgsName);

		// The static stage reads no skinned tables, and only the impostor stage its arena.
		if (m_Stage == GeometryStage::kSkinnedMesh)
		{
			for (const auto& binding : c_SkinnedBuffers)
			{
				desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
			}
		}
		if (m_Stage == GeometryStage::kImpostor)
		{
			for (const auto& binding : c_ImpostorBuffers)
			{
				desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
			}
		}
	}

	void
	RecordDrawBucket(
		ForwardPhases&      kernels,
		bgpu::MeshletState& state,
		const DrawData&     draw,
		const PassContext&  resources,
		const uint32_t      bucket)
	{
		bgpu::ICommandList* cmd = resources.GetCommandList();
		core::ensure(cmd != nullptr, "Pass commandlist must be initialized");

		const auto             dispatchArgs = resources.GetBuffer(c_CompactDispatchArgsName);
		const DrawBucketTable& table        = kernels.DrawBuckets();

		// Placements at rest, then those dissolving, each lane its own dispatch through its own
		// pipeline; a lane the cull left empty dispatches nothing (DrawBucketCountIndex).
		for (const DrawLane lane : { DrawLane::kAtRest, DrawLane::kDissolve })
		{
			bgpu::MeshletKernel* kernel =
				kernels.BindDrawBucketKernel(bucket, lane, state, draw, resources);
			if (kernel == nullptr)
			{
				continue;
			}

			const uint32_t drawLane =
				lane == DrawLane::kDissolve ? bucket + idl::cDissolveLane : bucket;
			if (auto expansionData = kernel->FindUniforms("expansionData"))
			{
				(*expansionData)["drawLane"]    = drawLane;
				(*expansionData)["baseTable"]   = idl::BaseTable::kDrawBucketed;
				(*expansionData)["lodDrawMode"] = lane == DrawLane::kDissolve ?
				                                      idl::LodDrawMode::kDissolve :
				                                      idl::LodDrawMode::kCurrent;
				(*expansionData)["cullBackfaces"] =
					DrawBucketMeshStageCullsBackfaces(table.Desc(bucket));
			}

			state.indirectArgs  = dispatchArgs;
			state.commandCounts = dispatchArgs;
			cmd->SetMeshletState(state);
			cmd->DispatchMeshIndirectCount(drawLane, DrawBucketCountIndex(drawLane));
		}
	}

	void
	BucketedForwardPhase::Record(
		ForwardPhases&      kernels,
		bgpu::MeshletState& state,
		const DrawData&     draw,
		const PassContext&  resources) const
	{
		const DrawBucketTable& table = kernels.DrawBuckets();

		// The transparent buckets are depth-ordered, so they draw in the transparent phase instead;
		// the water buckets draw over the depth this phase writes.
		for (uint32_t bucket = 0, count = table.Count(); bucket < count; ++bucket)
		{
			if (table.Transparent(bucket) || table.Desc(bucket).geom != m_Stage ||
			    kernels.IsWaterBucket(bucket))
			{
				continue;
			}
			RecordDrawBucket(kernels, state, draw, resources, bucket);
		}
	}
}
