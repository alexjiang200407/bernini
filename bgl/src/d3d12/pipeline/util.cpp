#include "pipeline/util.h"
#include "resource/Shader.h"

namespace bgl::pipeline_util
{
	PipelineLayout
	BuildPipelineLayout(
		ID3D12Device*                   device,
		slang::ISession*                session,
		std::initializer_list<IShader*> shaders)
	{
		gassert(device != nullptr, "Device pointer must not be null.");
		gassert(session != nullptr, "Session cannot be null");

		PipelineLayout result;

		SlangErrorChecker                   errChecker;
		std::vector<slang::IComponentType*> slangModules;
		std::unordered_set<slang::IModule*> uniqueModules;

		auto addShaderToComposition = [&](IShader* shader) {
			if (!shader)
				return;

			slang::IModule* module = shader->GetSlangModule();
			gassert(module != nullptr, "Shader module cannot be null");

			// ONLY add the module if it hasn't been added yet
			if (uniqueModules.insert(module).second)
			{
				slangModules.emplace_back(module);
			}

			const auto&         shaderDesc = shader->GetDesc();
			slang::IEntryPoint* entryPoint = nullptr;

			// Find the entry point by name from the module
			module->findEntryPointByName(shaderDesc.entryPointName.c_str(), &entryPoint);
			gassert(entryPoint != nullptr, "Failed to find entry point in module");

			slangModules.emplace_back(entryPoint);
		};

		for (IShader* shader : shaders)
		{
			addShaderToComposition(shader);
		}

		Slang::ComPtr<slang::IComponentType> program;
		session->createCompositeComponentType(
			slangModules.data(),
			static_cast<SlangInt>(slangModules.size()),
			program.writeRef(),
			errChecker.WriteDiagnosticBlob()) >>
			errChecker;

		gassert(program != nullptr, "Failed to compose shader modules");

		program->link(result.linkedProgram.writeRef(), errChecker.WriteDiagnosticBlob()) >>
			errChecker;

		slang::ProgramLayout* layout = result.linkedProgram->getLayout();

		std::vector<CD3DX12_ROOT_PARAMETER> rootParams;

		for (uint32_t i = 0; i < layout->getParameterCount(); ++i)
		{
			slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);

			if (param->getCategory() == slang::ParameterCategory::ConstantBuffer)
			{
				slang::TypeLayoutReflection* typeLayout    = param->getTypeLayout();
				slang::TypeLayoutReflection* elementLayout = typeLayout->getElementTypeLayout();

				uint32_t    bufferSize = static_cast<uint32_t>(elementLayout->getSize());
				std::string bufferName = param->getName();

				UINT shaderRegister = static_cast<UINT>(param->getBindingIndex());
				UINT registerSpace  = static_cast<UINT>(param->getBindingSpace());

				UniformLayoutEntry entry{};
				entry.size                              = bufferSize;
				entry.layout                            = elementLayout;
				entry.rootParamIndex                    = static_cast<uint32_t>(rootParams.size());
				result.uniformLayoutEntries[bufferName] = entry;

				auto cbvParam = CD3DX12_ROOT_PARAMETER();
				cbvParam.InitAsConstantBufferView(shaderRegister, registerSpace);
				rootParams.push_back(cbvParam);
			}
		}

		D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
		rsDesc.NumParameters             = static_cast<UINT>(rootParams.size());
		rsDesc.pParameters               = rootParams.empty() ? nullptr : rootParams.data();
		rsDesc.NumStaticSamplers         = 0;
		rsDesc.pStaticSamplers           = nullptr;
		rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

		wrl::ComPtr<ID3DBlob> sigBlob, errBlob;
		D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob) >>
			d3d12ErrChecker;

		device->CreateRootSignature(
			0,
			sigBlob->GetBufferPointer(),
			sigBlob->GetBufferSize(),
			IID_PPV_ARGS(&result.rootSignature)) >>
			d3d12ErrChecker;

		return result;
	}
}
