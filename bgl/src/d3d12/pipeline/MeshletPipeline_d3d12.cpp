#include "pipeline/MeshletPipeline_d3d12.h"
#include "resource/Rtv_d3d12.h"
#include <core/math.h>

// clang-format off
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
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
        } psoDesc = { };
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

		SlangErrorChecker                   errChecker;
		std::vector<slang::IComponentType*> slangModules;

		gassert(desc.meshShader != nullptr, "Mesh shader cannot be null");
		slangModules.emplace_back(desc.meshShader->GetSlangModule());

		if (desc.pixelShader != nullptr)
		{
			slangModules.emplace_back(desc.pixelShader->GetSlangModule());
		}
		if (desc.ampShader != nullptr)
		{
			slangModules.emplace_back(desc.ampShader->GetSlangModule());
		}

		Slang::ComPtr<slang::IComponentType> program;
		{
			session->createCompositeComponentType(
				slangModules.data(),
				slangModules.size(),
				program.writeRef(),
				errChecker.WriteDiagnosticBlob()) >>
				errChecker;

			gassert(program != nullptr, "Failed to compose shader modules");
		}

		program->link(m_LinkedProgram.writeRef(), errChecker.WriteDiagnosticBlob()) >> errChecker;

		slang::ProgramLayout*            layout         = m_LinkedProgram->getLayout();
		slang::VariableLayoutReflection* constantBuffer = nullptr;
		UINT                             shaderRegister = 0;
		UINT                             registerSpace  = 0;

		for (uint32_t i = 0; i < layout->getParameterCount(); ++i)
		{
			slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);
			if (param->getCategory() == slang::ParameterCategory::ConstantBuffer)
			{
				if (!constantBuffer)
				{
					constantBuffer = param;
					shaderRegister = static_cast<UINT>(param->getBindingIndex());
					registerSpace  = static_cast<UINT>(param->getBindingSpace());
				}
				else
				{
					gfatal("Multiple root constant buffers not supported");
				}
			}
		}

		if (constantBuffer != nullptr)
		{
			slang::TypeLayoutReflection* bufferLayout = constantBuffer->getTypeLayout();
			m_UniformLayout                           = bufferLayout->getElementTypeLayout();
			m_UniformSize = static_cast<uint32_t>(m_UniformLayout->getSize());

			gassert(m_UniformSize != 0, "Uniform buffer size cannot be zero");
		}

		CD3DX12_ROOT_PARAMETER rootParams[1]  = {};
		UINT                   rootParamCount = 0;

		const UINT dwordCount = core::align(m_UniformSize, 4) / 4;

		if (dwordCount > 0)
		{
			rootParams[0].InitAsConstants(dwordCount, shaderRegister, registerSpace);
			rootParamCount = 1;
		}

		D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
		rsDesc.NumParameters             = rootParamCount;
		rsDesc.pParameters               = rootParamCount > 0 ? rootParams : nullptr;
		rsDesc.NumStaticSamplers         = 0;
		rsDesc.pStaticSamplers           = nullptr;
		rsDesc.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;  // Bypasses Input Assembler flags

		wrl::ComPtr<ID3DBlob> sigBlob, errBlob;
		D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob) >>
			d3d12ErrChecker;

		device->CreateRootSignature(
			0,
			sigBlob->GetBufferPointer(),
			sigBlob->GetBufferSize(),
			IID_PPV_ARGS(&m_RootSignature)) >>
			d3d12ErrChecker;

		// --- POPULATING THE CUSTOM PSO_STREAM ---
		PSO_STREAM psoDesc = {};

		// 1. Root Signature
		psoDesc.RootSignature_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
		psoDesc.RootSignature      = m_RootSignature.Get();

		// 2. Topology
		psoDesc.PrimitiveTopology_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY;
		psoDesc.PrimitiveTopologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		// 3. Shaders (Amplification, Mesh, and Pixel)
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

		// 4. Fixed Function States
		psoDesc.RasterizerState_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER;
		psoDesc.RasterizerState      = ConvertRasterState(desc.renderState.rasterState);

		psoDesc.DepthStencilState_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL;
		psoDesc.DepthStencilState = ConvertDepthStencilState(desc.renderState.depthStencilState);

		psoDesc.BlendState_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND;
		psoDesc.BlendState      = ConvertBlendState(desc.renderState.blendState);

		// 5. MSAA Configurations
		psoDesc.SampleDesc_Type    = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC;
		psoDesc.SampleDesc.Count   = 1;
		psoDesc.SampleDesc.Quality = 0;

		psoDesc.SampleMask_Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK;
		psoDesc.SampleMask      = UINT_MAX;

		// 6. Output Target Formats
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

		// --- COMPILE PIPELINE ---
		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{};
		streamDesc.SizeInBytes                   = sizeof(PSO_STREAM);
		streamDesc.pPipelineStateSubobjectStream = &psoDesc;

		device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_PipelineState)) >>
			d3d12ErrChecker;
	}

	MeshletPipeline::~MeshletPipeline() noexcept
	{
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
