#pragma once
#include "pipeline/MeshletPipeline.h"
#include "uniforms/Uniforms.h"

namespace bgl
{
	class ShaderCache;

	class MeshletPipeline : public core::RefCounter<IMeshletPipeline>
	{
	public:
		MeshletPipeline(
			ID3D12Device*              device,
			slang::ISession*           session,
			ShaderCache*               cache,
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
		MeshletPipelineDesc              m_Desc;
		wrl::ComPtr<ID3D12PipelineState> m_PipelineState;
		wrl::ComPtr<ID3D12RootSignature> m_RootSignature;
		UniformLayoutMap                 m_UniformLayoutEntries;
	};
}
