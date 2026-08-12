#pragma once
#include "fg/PassDesc.h"
#include "uniforms/Uniforms.h"
#include "util/util.h"

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

	// The geometry tables the Forward_StaticMesh expansion and vertex decode read. Every pass
	// built on those shaders declares and binds all seven, so the set lives here rather than in
	// any one pass.
	constexpr std::array<SceneBuffer, 7> c_ForwardDataBuffers = {
		{ { "scene.instanceBuffer",
		    "instanceBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { "scene.meshInstanceBuffer",
		    "meshBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { "scene.submeshBuffer",
		    "submeshBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { "scene.meshletBuffer",
		    "meshletBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { "scene.vertexMapBuffer",
		    "vertexMapBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { "scene.vertexDataBuffer",
		    "vertexDataBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader },
		  { "scene.indexBuffer",
		    "indexBuffer",
		    BarrierAccessFlag::kShaderResource,
		    BarrierSyncFlag::kVertexShader } }
	};

	constexpr std::array<SceneBuffer, 2> c_ExpansionBuffers = {
		{ { "scene.compactedInstances",
		    "compactedInstances",
		    BarrierAccessFlag::kUnorderedAccess,
		    BarrierSyncFlag::kVertexShader },
		  { "compactedInstances.psoPrefixSumBuffer",
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
