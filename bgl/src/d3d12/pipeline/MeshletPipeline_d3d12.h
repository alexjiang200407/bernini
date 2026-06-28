#pragma once
#include "pipeline/MeshletPipeline.h"
#include "uniforms/Uniforms.h"

namespace bgl
{
	class MeshletPipeline : public core::RefCounter<IMeshletPipeline>
	{
	public:
		MeshletPipeline(
			ID3D12Device*              device,
			slang::ISession*           session,
			const MeshletPipelineDesc& desc);

		~MeshletPipeline() noexcept override;

		MeshletPipeline(const MeshletPipeline&) = delete;
		MeshletPipeline(MeshletPipeline&&)      = delete;

		MeshletPipeline&
		operator=(const MeshletPipeline&) = delete;

		MeshletPipeline&
		operator=(MeshletPipeline&&) = delete;

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

		const MeshletPipelineDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		UniformLayoutEntry
		GetUniformLayoutEntry(const std::string& name) const noexcept override
		{
			return m_UniformLayoutEntries.at(name);
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
		MeshletPipelineDesc                                 m_Desc;
		wrl::ComPtr<ID3D12PipelineState>                    m_PipelineState;
		wrl::ComPtr<ID3D12RootSignature>                    m_RootSignature;
		Slang::ComPtr<slang::IComponentType>                m_LinkedProgram;
		std::unordered_map<std::string, UniformLayoutEntry> m_UniformLayoutEntries;
	};
}
