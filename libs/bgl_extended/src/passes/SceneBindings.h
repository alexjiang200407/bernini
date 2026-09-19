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
	// those shaders declares and binds all nine, so the set lives here rather than in any one
	// pass.
	constexpr std::array<SceneBuffer, 9> c_ForwardDataBuffers = {
		{ { c_InstanceBufferName,
		    "instanceBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { c_MeshInstanceBufferName,
		    "meshBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { c_GeomBufferName,
		    "geomBuffer",
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

	// The material arena, for every pass whose pixel stages read records and textures out of it:
	// Forward's shading, and the static-depth receiver's coverage stages. The typed view is bound
	// off the draw rather than through the graph -- a view is not a resource -- but the arena is
	// still declared so its barriers are placed.
	constexpr std::array<SceneBuffer, 1> c_MaterialBuffers = {
		{ { c_MaterialArenaBufferName,
		    "materials",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kPixelShader } }
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
		  { c_DrawBucketPrefixSumName,
		    "drawBucketPrefixSum",
		    BarrierAccessFlag::kUnorderedAccess,
		    BarrierSyncFlag::kVertexShader } }
	};

	/**
	 * Declares what the static tier's amplification stage culls meshlets with: the frustum the draw's
	 * instances were culled against, and the cull counters.
	 * @pre recorded under the cull scope whose compaction the pass draws, where `cull.view` resolves.
	 */
	inline void
	DeclareMeshletCullBuffers(PassDesc& desc)
	{
		desc.AddBufferArg(
			c_CullViewName,
			BarrierSyncFlag::kVertexShader,
			BarrierAccessFlag::kShaderResource);
		desc.AddBufferArg(
			c_CullStatsName,
			BarrierSyncFlag::kVertexShader,
			BarrierAccessFlag::kUnorderedAccess);
	}

	/**
	 * Binds DeclareMeshletCullBuffers' buffers into an `expansionData` that reads them. Only the
	 * static tier's programs do, and the counters only under BERNINI_GPU_DEBUG, so either may be
	 * absent from a kernel's reflection.
	 */
	inline void
	BindMeshletCullBuffers(Uniforms& expansion, const PassContext& resources)
	{
		expansion["cullView"].SetIfValid(resources.GetBuffer(c_CullViewName));
		expansion["stats"].SetIfValid(resources.GetBuffer(c_CullStatsName));
	}

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
