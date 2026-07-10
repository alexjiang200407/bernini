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
		std::vector<slang::IComponentType*> components;
		std::unordered_set<slang::IModule*> uniqueModules;

		// Keep the entry points alive for the duration of composition/linking.
		std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPoints;
		std::vector<IShader*>                          orderedShaders;

		auto addShaderToComposition = [&](IShader* shader) {
			if (!shader)
				return;

			slang::IModule* module = shader->GetSlangModule();
			gassert(module != nullptr, "Shader module cannot be null");

			// ONLY add the module if it hasn't been added yet
			if (uniqueModules.insert(module).second)
			{
				components.emplace_back(module);
			}

			const auto&                       shaderDesc = shader->GetDesc();
			Slang::ComPtr<slang::IEntryPoint> entryPoint;

			// Find the entry point by name from the module
			module->findEntryPointByName(shaderDesc.entryPointName.c_str(), entryPoint.writeRef());
			gassert(entryPoint != nullptr, "Failed to find entry point in module");

			components.emplace_back(entryPoint.get());
			entryPoints.emplace_back(std::move(entryPoint));
			orderedShaders.emplace_back(shader);
		};

		for (IShader* shader : shaders)
		{
			addShaderToComposition(shader);
		}

		Slang::ComPtr<slang::IComponentType> program;
		session->createCompositeComponentType(
			components.data(),
			static_cast<SlangInt>(components.size()),
			program.writeRef(),
			errChecker.WriteDiagnosticBlob()) >>
			errChecker;

		gassert(program != nullptr, "Failed to compose shader modules");

		program->link(result.linkedProgram.writeRef(), errChecker.WriteDiagnosticBlob()) >>
			errChecker;

		slang::ProgramLayout* layout = result.linkedProgram->getLayout();

		// Generate the target bytecode for each entry point from the SAME linked program used
		// for reflection below. Because both the code and the layout come from this one linked
		// program, the bindings baked into the bytecode always match the root signature we
		// build from the reflection. Look the entry point up by name: entry-point names are
		// unique within a single PSO's linked program.
		std::unordered_map<std::string_view, SlangInt> entryPointIndexByName;
		for (SlangUInt i = 0; i < layout->getEntryPointCount(); ++i)
		{
			entryPointIndexByName.emplace(
				layout->getEntryPointByIndex(i)->getName(),
				static_cast<SlangInt>(i));
		}

		for (IShader* shader : orderedShaders)
		{
			const std::string& entryName = shader->GetDesc().entryPointName;

			auto found = entryPointIndexByName.find(entryName);
			gassert(
				found != entryPointIndexByName.end(),
				"Entry point missing from linked program");

			Slang::ComPtr<slang::IBlob> code;
			result.linkedProgram->getEntryPointCode(
				found->second,
				0,
				code.writeRef(),
				errChecker.WriteDiagnosticBlob()) >>
				errChecker;

			gassert(code != nullptr, "Failed to generate entry point bytecode");
			result.entryPointCode[shader] = std::move(code);
		}

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
		rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
		               D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

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
