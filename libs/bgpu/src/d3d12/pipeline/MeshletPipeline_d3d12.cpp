#include "pipeline/MeshletPipeline_d3d12.h"
#include "convert_d3d12.h"
#include "native_device_d3d12.h"
#include "pipeline/PipelineLayout_d3d12.h"
#include "shadercache/ShaderCache_d3d12.h"
#include <bgpu/GpuContext.h>
#include <bgpu/resource/Shader.h>
#include <core/err/util.h>
#include <core/math.h>
#include <cstdint>
#include <directx/d3d12.h>
#include <spdlog/spdlog.h>

// clang-format off
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#pragma warning(disable: 5029) // Allow __declspec(align) on non-class types
namespace
{
        struct MeshletPsoStream
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
}
#pragma warning(pop)
// clang-format on

namespace bgpu
{
	MeshletPipeline::MeshletPipeline(
		const bgpu::GpuContext&    context,
		ShaderCache*               cache,
		const MeshletPipelineDesc& desc) : m_Desc(desc)
	{
		ID3D12Device* device = bgpu::GetD3d12Device(context);
		core::ensure(device != nullptr, "Device pointer must not be null.");

		wrl::ComPtr<ID3D12Device2> device2;
		device->QueryInterface(IID_PPV_ARGS(&device2)) >> d3d12ErrChecker;

		core::ensure(desc.meshShader != nullptr, "Mesh shader cannot be null");

		pipeline_util::PipelineLayout pipelineLayout = pipeline_util::BuildPipelineLayout(
			device,
			cache,
			{ desc.meshShader, desc.pixelShader, desc.ampShader });

		m_RootSignature        = std::move(pipelineLayout.rootSignature);
		m_UniformLayoutEntries = std::move(pipelineLayout.uniformLayoutEntries);

		auto bytecodeOf = [&](const core::SharedRef<IShader>& shader) -> D3D12_SHADER_BYTECODE {
			if (shader == nullptr)
			{
				return D3D12_SHADER_BYTECODE{ nullptr, 0 };
			}

			auto found = pipelineLayout.entryPointCode.find(shader->GetDesc().entryPointName);
			core::ensure(
				found != pipelineLayout.entryPointCode.end(),
				"Missing compiled bytecode for shader");

			return D3D12_SHADER_BYTECODE{ found->second.data(), found->second.size() };
		};

		MeshletPsoStream psoDesc = {};

		psoDesc.RootSignature_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
		psoDesc.RootSignature      = m_RootSignature.Get();

		psoDesc.PrimitiveTopology_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY;
		psoDesc.PrimitiveTopologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		psoDesc.AmplificationShader_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS;
		psoDesc.AmplificationShader      = bytecodeOf(desc.ampShader);

		psoDesc.MeshShader_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
		psoDesc.MeshShader      = bytecodeOf(desc.meshShader);

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
		streamDesc.SizeInBytes                   = sizeof(MeshletPsoStream);
		streamDesc.pPipelineStateSubobjectStream = &psoDesc;

		uint64_t identity = 0;
		for (const core::SharedRef<IShader>& shader :
		     { desc.meshShader, desc.pixelShader, desc.ampShader })
		{
			if (shader == nullptr)
				continue;

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

		m_PipelineState.Attach(bgpu::FindPipelineState(context, m_RootSignature.Get(), identity));
		if (m_PipelineState != nullptr)
			return;

		if (cache == nullptr || !cache->LoadPipeline(identity, streamDesc, &m_PipelineState))
		{
			device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_PipelineState)) >>
				d3d12ErrChecker;

			if (cache != nullptr)
				cache->StorePipeline(identity, m_PipelineState.Get());
		}

		bgpu::SharePipelineState(context, m_RootSignature.Get(), identity, m_PipelineState.Get());
	}

	MeshletPipeline::~MeshletPipeline() noexcept
	{
		spdlog::trace("~MeshletPipeline");
		m_PipelineState.Reset();
		m_RootSignature.Reset();
	}
}
