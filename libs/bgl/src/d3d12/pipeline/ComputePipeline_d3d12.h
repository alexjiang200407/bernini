#pragma once
#include "pipeline/ComputePipeline.h"
#include "uniforms/Uniforms.h"
#include <core/str/str.h>

namespace bgl
{
	class ComputePipeline : public core::RefCounter<IComputePipeline>
	{
	public:
		ComputePipeline(
			ID3D12Device*              device,
			slang::ISession*           session,
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

			gfatal("Uniform layout entry not found: {}", name);
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
		ComputePipelineDesc                  m_Desc;
		wrl::ComPtr<ID3D12PipelineState>     m_PipelineState;
		wrl::ComPtr<ID3D12RootSignature>     m_RootSignature;
		Slang::ComPtr<slang::IComponentType> m_LinkedProgram;
		UniformLayoutMap                     m_UniformLayoutEntries;
	};
}
