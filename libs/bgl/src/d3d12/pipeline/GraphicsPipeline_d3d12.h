#pragma once
#include "pipeline/GraphicsPipeline.h"
#include "uniforms/Uniforms.h"

namespace bgl
{
	class ShaderCache;

	class GraphicsPipeline : public core::RefCounter<IGraphicsPipeline>
	{
	public:
		GraphicsPipeline(
			ID3D12Device*               device,
			ShaderCache*                cache,
			const GraphicsPipelineDesc& desc);

		~GraphicsPipeline() noexcept override;

		GraphicsPipeline(const GraphicsPipeline&) = delete;
		GraphicsPipeline(GraphicsPipeline&&)      = delete;

		GraphicsPipeline&
		operator=(const GraphicsPipeline&) = delete;

		GraphicsPipeline&
		operator=(GraphicsPipeline&&) = delete;

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

		const GraphicsPipelineDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		UniformLayoutEntry
		GetUniformLayoutEntry(std::string_view name) const noexcept override
		{
			if (auto found = m_UniformLayoutEntries.find(name);
			    found != m_UniformLayoutEntries.end())
			{
				return found->second;
			}

			gfatal("Uniform buffer with name '{}' not found in pipeline.", name);
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
		GraphicsPipelineDesc             m_Desc;
		wrl::ComPtr<ID3D12PipelineState> m_PipelineState;
		wrl::ComPtr<ID3D12RootSignature> m_RootSignature;
		UniformLayoutMap                 m_UniformLayoutEntries;
	};
}
