#pragma once
#include "fg/PassDesc.h"
#include "scene/scene_buffer_names.h"
#include "types/Barrier.h"
#include "uniforms/Uniforms.h"
#include "util/util.h"
#include <array>
#include <bgl_common/gassert.h>
#include <span>
#include <string_view>

namespace bgl
{
	/** One scene buffer a pass both declares to the graph and binds into a kernel's uniforms. */
	struct SceneBuffer
	{
		std::string_view graphName;
		std::string_view uniformKey;
		BarrierAccess    access;
		BarrierSync      sync;
	};

	// The geometry tables every forward expansion and vertex decode reads. Every pass built on
	// those shaders declares and binds all eight, so the set lives here rather than in any one
	// pass.
	constexpr std::array<SceneBuffer, 8> c_ForwardDataBuffers = {
		{ { c_InstanceBufferName,
		    "instanceBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { c_MeshInstanceBufferName,
		    "meshBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { c_SubmeshBufferName,
		    "submeshBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { c_MeshletBufferName,
		    "meshletBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { c_VertexMapBufferName,
		    "vertexMapBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { c_VertexDataBufferName,
		    "vertexDataBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { c_IndexBufferName,
		    "indexBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { c_PlaybackArenaBufferName,
		    "playbackBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader } }
	};

	// The rig tables the skinned vertex evaluation reads, whichever pose source a placement draws
	// from. They live here beside the geometry tables because every pass built on the tier-branching
	// geometry stage declares and binds both sets.
	constexpr std::array<SceneBuffer, 4> c_SkinnedBuffers = {
		{ { c_RigBufferName,
		    "rigBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { c_BonePaletteName,
		    "bonePaletteBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { c_BoneAnimTableName,
		    "boneAnimTables",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { c_ClipBufferName,
		    "clipBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader } }
	};

	constexpr std::array<SceneBuffer, 2> c_ExpansionBuffers = {
		{ { c_CompactedInstancesName,
		    "compactedInstances",
		    BarrierAccessFlag::kUnorderedAccess,
		    BarrierSyncFlag::kVertexShader },
		  { c_PsoPrefixSumName,
		    "psoPrefixSum",
		    BarrierAccessFlag::kUnorderedAccess,
		    BarrierSyncFlag::kVertexShader } }
	};

	inline void
	BindSceneBuffers(
		Uniforms&                    uniforms,
		std::span<const SceneBuffer> bindings,
		const PassContext&           resources)
	{
		for (const SceneBuffer& binding : bindings)
		{
			auto uniform = uniforms[binding.uniformKey];
			if (!uniform.IsValid())
			{
				gfatal(
					"{} key doesn't exist in uniforms. Most likely an error",
					binding.uniformKey);
			}

			uniform = resources.GetBuffer(binding.graphName);
		}
	}
}
