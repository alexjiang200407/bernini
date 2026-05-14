#include "pipeline/GraphicsPipeline_d3d12.h"
#include "resource/Rtv_d3d12.h"

namespace bgl
{

	GraphicsPipelineImpl::GraphicsPipelineImpl(
		ID3D12Device*               device,
		const GraphicsPipelineDesc& desc) : m_Desc(desc)
	{
		gassert(device != nullptr, "Device pointer must not be null.");

		// 1. Create Root Signature
		// TODO: Reflect this info from the shader
		CD3DX12_ROOT_PARAMETER rootParams[1]  = {};
		const UINT             shaderRegister = 0;  // Maps to register(b0)
		const UINT             registerSpace  = 0;  // Maps to space0
		const UINT             dwordCount     = (desc.rootConstantsSize + 3) / 4;

		rootParams[0].InitAsConstants(dwordCount, shaderRegister, registerSpace);

		D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
		rsDesc.NumParameters             = std::size(rootParams);
		rsDesc.pParameters               = rootParams;
		rsDesc.NumStaticSamplers         = 0;
		rsDesc.pStaticSamplers           = nullptr;
		rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		               D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

		wrl::ComPtr<ID3DBlob> sigBlob, errBlob;
		D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob) >>
			d3d12ErrChecker;

		device->CreateRootSignature(
			0,
			sigBlob->GetBufferPointer(),
			sigBlob->GetBufferSize(),
			IID_PPV_ARGS(&m_RootSignature)) >>
			d3d12ErrChecker;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

		psoDesc.pRootSignature = m_RootSignature.Get();

		psoDesc.VS = D3D12_SHADER_BYTECODE{ desc.vertexShader.GetBytecode(),
			                                desc.vertexShader.GetBytecodeSize() };

		psoDesc.PS = D3D12_SHADER_BYTECODE{ desc.pixelShader.GetBytecode(),
			                                desc.pixelShader.GetBytecodeSize() };

		// Bind converted states
		psoDesc.BlendState        = ConvertBlendState(desc.renderState.blendState);
		psoDesc.DepthStencilState = ConvertDepthStencilState(desc.renderState.depthStencilState);
		psoDesc.RasterizerState   = ConvertRasterState(desc.renderState.rasterState);

		// Topology & Input Configuration
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.InputLayout           = { nullptr,
			                              0 };  // Null because you use bindless / GPU-driven rendering
		psoDesc.IBStripCutValue       = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;

		// Framebuffer Attachment Formats
		{
			psoDesc.NumRenderTargets = static_cast<UINT>(desc.rtvFormats.size());
			for (size_t i = 0; i < 8; ++i)
			{
				if (i < desc.rtvFormats.size())
				{
					psoDesc.RTVFormats[i] = ConvertFormat(desc.rtvFormats[i]);
				}
				else
				{
					psoDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
				}
			}

			psoDesc.DSVFormat = ConvertFormat(desc.dsvFormat);
		}

		psoDesc.SampleDesc.Count   = 1;
		psoDesc.SampleDesc.Quality = 0;
		psoDesc.SampleMask         = UINT_MAX;

		device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PipelineState)) >>
			d3d12ErrChecker;
	}

	GraphicsPipelineDesc&
	bgl::GraphicsPipelineDesc::AddRtvFormat(const Rtv& rtv)
	{
		auto& desc = rtv.GetDesc();
		rtvFormats.push_back(desc.format);
		return *this;
	}
}
