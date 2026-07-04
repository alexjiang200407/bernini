#include "pipeline/MeshletPipeline_d3d12.h"
#include "pipeline/util.h"
#include "resource/Rtv_d3d12.h"
#include "resource/Shader.h"
#include <core/math.h>

// clang-format off
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#pragma warning(disable: 5029) // Allow __declspec(align) on non-class types
        struct PSO_STREAM
        {
            typedef __declspec(align(sizeof(void*))) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ALIGNED_TYPE;

            ALIGNED_TYPE RootSignature_Type;        ID3D12RootSignature* RootSignature;
            ALIGNED_TYPE PrimitiveTopology_Type;    D3D12_PRIMITIVE_TOPOLOGY_TYPE PrimitiveTopologyType;
            ALIGNED_TYPE AmplificationShader_Type;  D3D12_SHADER_BYTECODE AmplificationShader;
            ALIGNED_TYPE MeshShader_Type;           D3D12_SHADER_BYTECODE MeshShader;
            ALIGNED_TYPE PixelShader_Type;          D3D12_SHADER_BYTECODE PixelShader;
            ALIGNED_TYPE RasterizerState_Type;      D3D12_RASTERIZER_DESC RasterizerState;
            ALIGNED_TYPE DepthStencilState_Type;    D3D12_DEPTH_STENCIL_DESC DepthStencilState;
            ALIGNED_TYPE BlendState_Type;           D3D12_BLEND_DESC BlendState;
            ALIGNED_TYPE SampleDesc_Type;           DXGI_SAMPLE_DESC SampleDesc;
            ALIGNED_TYPE SampleMask_Type;           UINT SampleMask;
            ALIGNED_TYPE RenderTargets_Type;        D3D12_RT_FORMAT_ARRAY RenderTargets;
            ALIGNED_TYPE DSVFormat_Type;            DXGI_FORMAT DSVFormat;
        };
#pragma warning(pop)
// clang-format on

namespace bgl
{
	MeshletPipeline::MeshletPipeline(
		ID3D12Device*              device,
		slang::ISession*           session,
		const MeshletPipelineDesc& desc) : m_Desc(desc)
	{
		gassert(session != nullptr, "Session cannot be null");
		gassert(device != nullptr, "Device pointer must not be null.");

		wrl::ComPtr<ID3D12Device2> device2;
		device->QueryInterface(IID_PPV_ARGS(&device2)) >> d3d12ErrChecker;

		gassert(desc.meshShader != nullptr, "Mesh shader cannot be null");

		pipeline_util::PipelineLayout pipelineLayout = pipeline_util::BuildPipelineLayout(
			device,
			session,
			{ desc.meshShader, desc.pixelShader, desc.ampShader });

		m_LinkedProgram        = std::move(pipelineLayout.linkedProgram);
		m_RootSignature        = std::move(pipelineLayout.rootSignature);
		m_UniformLayoutEntries = std::move(pipelineLayout.uniformLayoutEntries);

		PSO_STREAM psoDesc = {};

		psoDesc.RootSignature_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
		psoDesc.RootSignature      = m_RootSignature.Get();

		psoDesc.PrimitiveTopology_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY;
		psoDesc.PrimitiveTopologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		psoDesc.AmplificationShader_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS;
		if (desc.ampShader)
		{
			psoDesc.AmplificationShader =
				D3D12_SHADER_BYTECODE{ desc.ampShader->GetBytecode(),
				                       desc.ampShader->GetBytecodeSize() };
		}

		psoDesc.MeshShader_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
		psoDesc.MeshShader      = D3D12_SHADER_BYTECODE{ desc.meshShader->GetBytecode(),
			                                             desc.meshShader->GetBytecodeSize() };

		psoDesc.PixelShader_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
		if (desc.pixelShader)
		{
			psoDesc.PixelShader = D3D12_SHADER_BYTECODE{ desc.pixelShader->GetBytecode(),
				                                         desc.pixelShader->GetBytecodeSize() };
		}

		psoDesc.RasterizerState_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER;
		psoDesc.RasterizerState      = ConvertRasterState(desc.renderState.rasterState);

		psoDesc.DepthStencilState_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL;
		psoDesc.DepthStencilState = ConvertDepthStencilState(desc.renderState.depthStencilState);

		psoDesc.BlendState_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND;
		psoDesc.BlendState      = ConvertBlendState(desc.renderState.blendState);

		psoDesc.SampleDesc_Type    = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC;
		psoDesc.SampleDesc.Count   = 1;
		psoDesc.SampleDesc.Quality = 0;

		psoDesc.SampleMask_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK;
		psoDesc.SampleMask      = UINT_MAX;

		psoDesc.RenderTargets_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
		psoDesc.RenderTargets.NumRenderTargets = static_cast<UINT>(desc.rtvFormats.size());
		for (size_t i = 0; i < 8; ++i)
		{
			if (i < desc.rtvFormats.size())
			{
				psoDesc.RenderTargets.RTFormats[i] = ConvertFormat(desc.rtvFormats[i]);
			}
			else
			{
				psoDesc.RenderTargets.RTFormats[i] = DXGI_FORMAT_UNKNOWN;
			}
		}

		psoDesc.DSVFormat_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT;
		psoDesc.DSVFormat      = ConvertFormat(desc.dsvFormat);

		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{};
		streamDesc.SizeInBytes                   = sizeof(PSO_STREAM);
		streamDesc.pPipelineStateSubobjectStream = &psoDesc;

		device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_PipelineState)) >>
			d3d12ErrChecker;
	}

	MeshletPipeline::~MeshletPipeline() noexcept
	{
		logger::trace("~MeshletPipeline");
		m_PipelineState.Reset();
		m_RootSignature.Reset();
	}

	MeshletPipelineDesc&
	bgl::MeshletPipelineDesc::AddRtvFormat(const Rtv& rtv)
	{
		auto& desc = rtv.GetDesc();
		rtvFormats.push_back(desc.format);
		return *this;
	}
}
