#include "pipeline/GraphicsPipeline_d3d12.h"
#include "constants/constants.h"
#include "convert_d3d12.h"
#include "pipeline/PipelineLayout_d3d12.h"
#include "resource/Shader.h"
#include "shadercache/ShaderCache_d3d12.h"

// clang-format off
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#pragma warning(disable: 5029) // Allow __declspec(align) on non-class types
namespace
{
        struct GraphicsPsoStream
        {
            typedef __declspec(align(sizeof(void*))) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ALIGNED_TYPE;

            ALIGNED_TYPE RootSignature_Type;        ID3D12RootSignature* RootSignature;
            ALIGNED_TYPE PrimitiveTopology_Type;    D3D12_PRIMITIVE_TOPOLOGY_TYPE PrimitiveTopologyType;
            ALIGNED_TYPE VertexShader_Type;         D3D12_SHADER_BYTECODE VertexShader;
            ALIGNED_TYPE PixelShader_Type;          D3D12_SHADER_BYTECODE PixelShader;
            ALIGNED_TYPE RasterizerState_Type;      D3D12_RASTERIZER_DESC RasterizerState;
            ALIGNED_TYPE DepthStencilState_Type;    D3D12_DEPTH_STENCIL_DESC DepthStencilState;
            ALIGNED_TYPE BlendState_Type;           D3D12_BLEND_DESC BlendState;
            ALIGNED_TYPE SampleDesc_Type;           DXGI_SAMPLE_DESC SampleDesc;
            ALIGNED_TYPE SampleMask_Type;           UINT SampleMask;
            ALIGNED_TYPE RenderTargets_Type;        D3D12_RT_FORMAT_ARRAY RenderTargets;
            ALIGNED_TYPE DSVFormat_Type;            DXGI_FORMAT DSVFormat;
        };
}
#pragma warning(pop)
// clang-format on

namespace bgl
{
	GraphicsPipeline::GraphicsPipeline(
		ID3D12Device*               device,
		ShaderCache*                cache,
		const GraphicsPipelineDesc& desc) : m_Desc(desc)
	{
		gassert(device != nullptr, "Device pointer must not be null.");
		gassert(desc.vertexShader != nullptr, "Vertex shader cannot be null");
		gassert(desc.pixelShader != nullptr, "Pixel shader cannot be null");

		wrl::ComPtr<ID3D12Device2> device2;
		device->QueryInterface(IID_PPV_ARGS(&device2)) >> d3d12ErrChecker;

		pipeline_util::PipelineLayout pipelineLayout = pipeline_util::BuildPipelineLayout(
			device,
			cache,
			{ desc.vertexShader, desc.pixelShader });

		m_RootSignature        = std::move(pipelineLayout.rootSignature);
		m_UniformLayoutEntries = std::move(pipelineLayout.uniformLayoutEntries);

		auto bytecodeOf = [&](const core::SharedRef<IShader>& shader) -> D3D12_SHADER_BYTECODE {
			auto found = pipelineLayout.entryPointCode.find(shader->GetDesc().entryPointName);
			gassert(
				found != pipelineLayout.entryPointCode.end(),
				"Missing compiled bytecode for shader");

			return D3D12_SHADER_BYTECODE{ found->second.data(), found->second.size() };
		};

		GraphicsPsoStream psoDesc = {};

		psoDesc.RootSignature_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
		psoDesc.RootSignature      = m_RootSignature.Get();

		psoDesc.PrimitiveTopology_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY;
		psoDesc.PrimitiveTopologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		psoDesc.VertexShader_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS;
		psoDesc.VertexShader      = bytecodeOf(desc.vertexShader);

		psoDesc.PixelShader_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
		psoDesc.PixelShader      = bytecodeOf(desc.pixelShader);

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
		psoDesc.SampleMask      = std::numeric_limits<UINT>::max();

		psoDesc.RenderTargets_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
		psoDesc.RenderTargets.NumRenderTargets = static_cast<UINT>(desc.rtvFormats.size());
		for (size_t i = 0; i < c_MaxRenderTargets; ++i)
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
		streamDesc.SizeInBytes                   = sizeof(GraphicsPsoStream);
		streamDesc.pPipelineStateSubobjectStream = &psoDesc;

		uint64_t identity = static_cast<uint64_t>(ShaderCache::PsoKind::kGraphics);
		if (cache != nullptr)
		{
			for (const core::SharedRef<IShader>& shader : { desc.vertexShader, desc.pixelShader })
			{
				identity = ShaderCache::CombineHash(
					identity,
					pipelineLayout.entryPointCode.at(shader->GetDesc().entryPointName));
			}

			// The render state is part of the graphics PSO but not the bytecode, so it
			// must contribute to the identity. These structs are zero-initialized before
			// conversion, so their padding is deterministic across runs.
			identity = ShaderCache::CombineHash(identity, psoDesc.RasterizerState);
			identity = ShaderCache::CombineHash(identity, psoDesc.DepthStencilState);
			identity = ShaderCache::CombineHash(identity, psoDesc.BlendState);
			identity = ShaderCache::CombineHash(identity, psoDesc.RenderTargets);
			identity = ShaderCache::CombineHash(identity, psoDesc.DSVFormat);
			identity = ShaderCache::CombineHash(identity, psoDesc.PrimitiveTopologyType);
		}

		if (cache == nullptr || !cache->LoadPipeline(identity, streamDesc, &m_PipelineState))
		{
			device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_PipelineState)) >>
				d3d12ErrChecker;

			if (cache != nullptr)
				cache->StorePipeline(identity, m_PipelineState.Get());
		}
	}

	GraphicsPipeline::~GraphicsPipeline() noexcept
	{
		logger::trace("~GraphicsPipeline");
		m_PipelineState.Reset();
		m_RootSignature.Reset();
	}
}
