#include "pipeline/ComputePipeline_d3d12.h"
#include "pipeline/util.h"
#include "resource/Shader.h"
#include "shadercache/ShaderCache_d3d12.h"

// clang-format off
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#pragma warning(disable: 5029) // Allow __declspec(align) on non-class types
namespace
{
        struct ComputePsoStream
        {
            typedef __declspec(align(sizeof(void*))) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ALIGNED_TYPE;

            ALIGNED_TYPE RootSignature_Type;        ID3D12RootSignature* RootSignature;
            ALIGNED_TYPE ComputeShader_Type;        D3D12_SHADER_BYTECODE ComputeShader;
        };
}
#pragma warning(pop)
// clang-format on

namespace bgl
{
	ComputePipeline::ComputePipeline(
		ID3D12Device*              device,
		slang::ISession*           session,
		ShaderCache*               cache,
		const ComputePipelineDesc& desc) : m_Desc(desc)
	{
		gassert(session != nullptr, "Session cannot be null");
		gassert(device != nullptr, "Device pointer must not be null.");
		gassert(desc.shader != nullptr, "Compute shader cannot be null");

		wrl::ComPtr<ID3D12Device2> device2;
		device->QueryInterface(IID_PPV_ARGS(&device2)) >> d3d12ErrChecker;

		pipeline_util::PipelineLayout pipelineLayout =
			pipeline_util::BuildPipelineLayout(device, session, cache, { desc.shader });

		m_RootSignature        = std::move(pipelineLayout.rootSignature);
		m_UniformLayoutEntries = std::move(pipelineLayout.uniformLayoutEntries);

		auto codeIt = pipelineLayout.entryPointCode.find(desc.shader->GetDesc().entryPointName);
		gassert(
			codeIt != pipelineLayout.entryPointCode.end(),
			"Missing compiled bytecode for compute shader");

		ComputePsoStream psoDesc = {};

		psoDesc.RootSignature_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
		psoDesc.RootSignature      = m_RootSignature.Get();

		psoDesc.ComputeShader_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS;
		psoDesc.ComputeShader =
			D3D12_SHADER_BYTECODE{ codeIt->second.data(), codeIt->second.size() };

		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{};
		streamDesc.SizeInBytes                   = sizeof(ComputePsoStream);
		streamDesc.pPipelineStateSubobjectStream = &psoDesc;

		uint64_t identity = 0;
		if (cache != nullptr)
			identity = ShaderCache::CombineHash(0, codeIt->second);

		if (cache == nullptr || !cache->LoadPipeline(identity, streamDesc, &m_PipelineState))
		{
			device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_PipelineState)) >>
				d3d12ErrChecker;

			if (cache != nullptr)
				cache->StorePipeline(identity, m_PipelineState.Get());
		}
	}

	ComputePipeline::~ComputePipeline() noexcept
	{
		logger::trace("~ComputePipeline");
		m_PipelineState.Reset();
		m_RootSignature.Reset();
	}
}
