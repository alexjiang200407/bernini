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

		~MeshletPipeline() noexcept;

		[[nodiscard]]
		ID3D12RootSignature*
		GetRootSignature() const
		{
			return m_RootSignature.Get();
		}

		[[nodiscard]]
		ID3D12PipelineState*
		GetPipelineState() const
		{
			return m_PipelineState.Get();
		}

		const MeshletPipelineDesc&
		GetDesc() const override
		{
			return m_Desc;
		}

		slang::TypeLayoutReflection*
		GetUniformLayout() const override
		{
			return m_UniformLayout;
		}

		virtual uint32_t
		GetUniformSize() const override
		{
			return m_UniformSize;
		}

	private:
		MeshletPipelineDesc                  m_Desc;
		wrl::ComPtr<ID3D12PipelineState>     m_PipelineState;
		wrl::ComPtr<ID3D12RootSignature>     m_RootSignature;
		Slang::ComPtr<slang::IComponentType> m_LinkedProgram;
		slang::TypeLayoutReflection*         m_UniformLayout = nullptr;
		uint32_t                             m_UniformSize   = 0;
	};
}
