#pragma once
#include "pipeline/GraphicsPipeline.h"

namespace bgl
{
	class GraphicsPipelineImpl
	{
	public:
		GraphicsPipelineImpl() = default;
		GraphicsPipelineImpl(ID3D12Device* device, const GraphicsPipelineDesc& desc);

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

		[[nodiscard]]
		D3D12_PRIMITIVE_TOPOLOGY
		GetPrimitiveTopology() const
		{
			return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		}

	private:
		GraphicsPipelineDesc             m_Desc;
		wrl::ComPtr<ID3D12PipelineState> m_PipelineState;
		wrl::ComPtr<ID3D12RootSignature> m_RootSignature;

		friend class DeviceImpl;
	};
}
