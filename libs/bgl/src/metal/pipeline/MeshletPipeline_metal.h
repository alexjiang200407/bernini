#pragma once
#include "metal_cpp.h"

#include "pipeline/MeshletPipeline.h"
#include "pipeline/MetalPipelineReflection.h"
#include "uniforms/UniformLayoutEntry.h"

#include <core/ref/RefCounter.h>

namespace bgl
{
	/**
	 * The Metal meshlet (mesh-shader) render pipeline. Links the amplification/mesh/pixel entry
	 * points into one MSL library and builds an MTL::RenderPipelineState from a
	 * MeshRenderPipelineDescriptor. The object/mesh threadgroup sizes come from the shaders'
	 * [numthreads]; the render targets are described per dispatch, not baked into the PSO.
	 */
	class MeshletPipeline final : public core::RefCounter<IMeshletPipeline>
	{
	public:
		MeshletPipeline(
			MTL::Device*               device,
			slang::ISession*           session,
			const MeshletPipelineDesc& desc);

		const MeshletPipelineDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		UniformLayoutEntry
		GetUniformLayoutEntry(std::string_view name) const noexcept override
		{
			auto it = m_UniformLayoutEntries.find(name);
			gassert(it != m_UniformLayoutEntries.end(), "Unknown uniform buffer name");
			return it->second;
		}

		std::vector<std::string>
		GetUniformBufferNames() const noexcept override
		{
			std::vector<std::string> names;
			names.reserve(m_UniformLayoutEntries.size());
			for (const auto& [name, entry] : m_UniformLayoutEntries) names.push_back(name);
			return names;
		}

		[[nodiscard]] MTL::RenderPipelineState*
		GetMTLPipelineState() const noexcept
		{
			return m_PipelineState.get();
		}

		[[nodiscard]] MTL::Size
		GetThreadsPerObjectThreadgroup() const noexcept
		{
			return m_ThreadsPerObject;
		}

		[[nodiscard]] MTL::Size
		GetThreadsPerMeshThreadgroup() const noexcept
		{
			return m_ThreadsPerMesh;
		}

		[[nodiscard]] const std::vector<uint32_t>&
		GetHandleOffsets(std::string_view name) const noexcept
		{
			auto it = m_HandleOffsets.find(name);
			gassert(it != m_HandleOffsets.end(), "Unknown uniform buffer name");
			return it->second;
		}

	private:
		MeshletPipelineDesc                     m_Desc;
		NS::SharedPtr<MTL::RenderPipelineState> m_PipelineState;
		UniformLayoutMap                        m_UniformLayoutEntries;
		MetalHandleOffsetMap                    m_HandleOffsets;
		MTL::Size                               m_ThreadsPerObject = MTL::Size(1, 1, 1);
		MTL::Size                               m_ThreadsPerMesh   = MTL::Size(1, 1, 1);
	};
}
