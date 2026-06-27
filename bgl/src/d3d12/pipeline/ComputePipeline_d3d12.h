#pragma once
#include "pipeline/ComputePipeline.h"
#include "uniforms/Uniforms.h"

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
		GetUniformLayoutEntry(const std::string& name) const noexcept override
		{
			return m_UniformLayoutEntries.at(name);
		}

	private:
		ComputePipelineDesc                                 m_Desc;
		wrl::ComPtr<ID3D12PipelineState>                    m_PipelineState;
		wrl::ComPtr<ID3D12RootSignature>                    m_RootSignature;
		Slang::ComPtr<slang::IComponentType>                m_LinkedProgram;
		std::unordered_map<std::string, UniformLayoutEntry> m_UniformLayoutEntries;
	};
}
