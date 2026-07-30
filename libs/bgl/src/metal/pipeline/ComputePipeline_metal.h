#pragma once
#include "metal_cpp.h"

#include "pipeline/ComputePipeline.h"
#include "uniforms/UniformLayoutEntry.h"

#include <core/ref/RefCounter.h>

namespace bgl
{
	/**
	 * The Metal compute pipeline. Compiles its shader's entry point to MSL via the Slang session,
	 * builds an MTL::ComputePipelineState, and reflects the constant buffers into the API-agnostic
	 * UniformLayoutEntry map (shared with the D3D12 backend). The reflected `rootParamIndex` is the
	 * Metal `[[buffer(N)]]` slot the kernel binds its uniforms at.
	 */
	class ComputePipeline final : public core::RefCounter<IComputePipeline>
	{
	public:
		ComputePipeline(
			MTL::Device*               device,
			slang::ISession*           session,
			const ComputePipelineDesc& desc);

		const ComputePipelineDesc&
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

		[[nodiscard]] MTL::ComputePipelineState*
		GetMTLPipelineState() const noexcept
		{
			return m_PipelineState.get();
		}

		[[nodiscard]] MTL::Size
		GetThreadsPerThreadgroup() const noexcept
		{
			return m_ThreadsPerThreadgroup;
		}

		// Byte offsets of the cbuffer's bindless handle fields, for the dispatch-time gpuAddress
		// translation. Precomputed at build so Dispatch never re-walks the layout.
		[[nodiscard]] const std::vector<uint32_t>&
		GetHandleOffsets(std::string_view name) const noexcept
		{
			auto it = m_HandleOffsets.find(name);
			gassert(it != m_HandleOffsets.end(), "Unknown uniform buffer name");
			return it->second;
		}

	private:
		ComputePipelineDesc                                 m_Desc;
		NS::SharedPtr<MTL::ComputePipelineState>            m_PipelineState;
		UniformLayoutMap                                    m_UniformLayoutEntries;
		core::str::unordered_str_map<std::vector<uint32_t>> m_HandleOffsets;
		MTL::Size m_ThreadsPerThreadgroup = MTL::Size(1, 1, 1);
	};
}
