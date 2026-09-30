#include "passes/TransparentForwardPhase.h"
#include "fg/PassDesc.h"
#include "passes/ForwardPhases.h"
#include "passes/SceneBindings.h"
#include "scene/scene_buffer_names.h"
#include <bgl_common/idl/BaseTable.h>
#include <bgl_common/idl/LodDrawMode.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/MeshletState.h>
#include <core/err/util.h>
#include <string>

namespace bgl
{
	void
	TransparentForwardPhase::Declare(PassDesc& desc) const
	{
		desc.AddBufferReadWrite(c_SortedTransparentInstancesName, BarrierSyncFlag::kVertexShader)
			.AddIndirectArgs(c_TransparentDispatchArgsName);

		// The sorted list holds every stage, the skinned one included.
		for (const auto& binding : c_SkinnedBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}
	}

	void
	TransparentForwardPhase::Record(
		ForwardPhases&     kernels,
		MeshletState&      state,
		const DrawData&    draw,
		const PassContext& resources) const
	{
		ICommandList* cmd = resources.GetCommandList();
		core::ensure(cmd != nullptr, "Pass commandlist must be initialized");

		// Built whenever any transparent bucket is demanded; absent, the sorted list is empty too.
		MeshletKernel* kernel = kernels.BindTransparentKernel(state, draw, resources);
		if (kernel == nullptr)
		{
			return;
		}

		if (auto expansionData = kernel->FindUniforms("expansionData"))
		{
			(*expansionData)["compactedInstances"] =
				resources.GetBuffer(c_SortedTransparentInstancesName);
			(*expansionData)["baseTable"] = idl::BaseTable::kDepthSorted;
			// A blended surface has no depth to dither against, so it swaps level at once.
			(*expansionData)["lodDrawMode"]   = idl::LodDrawMode::kCurrent;
			(*expansionData)["cullBackfaces"] = 1u;
		}

		// The sort leaves the whole list farthest-first, so the depth-sorted draw is one dispatch
		// whose count lives entirely on the GPU: the single grid at index 0 of its own arguments.
		state.indirectArgs = resources.GetBuffer(c_TransparentDispatchArgsName);
		cmd->SetMeshletState(state);
		cmd->DispatchMeshIndirect(0);
	}
}
