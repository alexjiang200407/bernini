#include "passes/BucketedForwardPhase.h"
#include "cmd/CommandList.h"
#include "constants/constants.h"
#include "fg/PassDesc.h"
#include "gfx/DrawBucketTable.h"
#include "passes/DrawData.h"
#include "passes/ForwardPhases.h"
#include "passes/SceneBindings.h"
#include "passes/draw_bucket_config.h"
#include "pipeline/MeshletKernel.h"
#include "scene/scene_buffer_names.h"
#include "types/Barrier.h"
#include "types/MeshletState.h"
#include <bgl_common/gassert.h>
#include <bgl_common/idl/BaseTable.h>
#include <cstdint>
#include <string>
#include <string_view>

namespace bgl
{
	BucketedForwardPhase::BucketedForwardPhase(
		const GeometryStage    stage,
		const std::string_view name) noexcept : m_Stage(stage), m_Name(name)
	{}

	void
	BucketedForwardPhase::Declare(PassDesc& desc) const
	{
		desc.AddRenderTarget(c_MotionVectorsName).AddIndirectArgs(c_CompactDispatchArgsName);

		// The static stage reads no skinned tables.
		if (m_Stage == GeometryStage::kSkinnedMesh)
		{
			for (const auto& binding : c_SkinnedBuffers)
			{
				desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
			}
		}
	}

	void
	BucketedForwardPhase::Record(
		ForwardPhases&     kernels,
		MeshletState&      state,
		const DrawData&    draw,
		const PassContext& resources) const
	{
		ICommandList* cmd = resources.GetCommandList();
		gassert(cmd != nullptr, "Pass commandlist must be initialized");

		const auto             dispatchArgs = resources.GetBuffer(c_CompactDispatchArgsName);
		const DrawBucketTable& table        = kernels.DrawBuckets();

		// The transparent buckets are depth-ordered, so they draw in the transparent phase instead.
		for (uint32_t bucket = 0, count = table.Count(); bucket < count; ++bucket)
		{
			if (table.Transparent(bucket) || table.Desc(bucket).geom != m_Stage)
			{
				continue;
			}

			// A bucket never demanded has no kernel -- and, by the same fact, no instances to draw.
			MeshletKernel* kernel = kernels.BindDrawBucketKernel(bucket, state, draw, resources);
			if (kernel == nullptr)
			{
				continue;
			}

			if (auto expansionData = kernel->FindUniforms("expansionData"))
			{
				(*expansionData)["drawBucketIndex"] = bucket;
				(*expansionData)["baseTable"]       = idl::BaseTable::kDrawBucketed;
				(*expansionData)["cullBackfaces"] =
					DrawBucketMeshStageCullsBackfaces(table.Desc(bucket));
			}

			state.indirectArgs  = dispatchArgs;
			state.commandCounts = dispatchArgs;
			cmd->SetMeshletState(state);
			cmd->DispatchMeshIndirectCount(bucket, DrawBucketCountIndex(bucket));
		}
	}
}
