#pragma once
#include "pipeline/ComputePipeline.h"
#include "uniforms/Uniforms.h"
#include <core/err/util.h>
#include <core/str/str.h>

namespace bgpu
{
	class GpuContext;
}

namespace bgl
{
	class ShaderCache;

	class ComputePipeline : public core::RefCounter<IComputePipeline>
	{
	public:
		ComputePipeline(
			const bgpu::GpuContext&    context,
			ShaderCache*               cache,
			const ComputePipelineDesc& desc);

		~ComputePipeline() noexcept override;

		ComputePipeline(const ComputePipeline&) = delete;
		ComputePipeline(ComputePipeline&&)      = delete;

		ComputePipeline&
		operator=(const ComputePipeline&) = delete;

		ComputePipeline&
		operator=(ComputePipeline&&) = delete;

		[[nodiscard]]
		ID3D12RootSignature*
		GetRootSignature() const noexcept
		{
			return m_RootSignature.Get();
		}

		[[nodiscard]]
		ID3D12PipelineState*
		GetPipelineState() const noexcept
		{
			return m_PipelineState.Get();
		}

		const ComputePipelineDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		UniformLayoutEntry
		GetUniformLayoutEntry(std::string_view name) const noexcept override
		{
			auto it = m_UniformLayoutEntries.find(name);
			if (it != m_UniformLayoutEntries.end())
			{
				return it->second;
			}

			core::fatal("Uniform layout entry not found: {}", name);
		}

		std::vector<std::string>
		GetUniformBufferNames() const noexcept override
		{
			std::vector<std::string> names;
			names.reserve(m_UniformLayoutEntries.size());
			for (const auto& [name, entry] : m_UniformLayoutEntries)
			{
				names.push_back(name);
			}
			return names;
		}

	private:
		ComputePipelineDesc              m_Desc;
		wrl::ComPtr<ID3D12PipelineState> m_PipelineState;
		wrl::ComPtr<ID3D12RootSignature> m_RootSignature;
		UniformLayoutMap                 m_UniformLayoutEntries;
	};
}
