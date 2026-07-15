#include "pipeline/PipelineLayout_d3d12.h"
#include "resource/Shader.h"
#include "shadercache/ShaderCache_d3d12.h"
#include "uniforms/SlangReflection.h"

namespace bgl::pipeline_util
{
	namespace
	{
		std::vector<IShader*>
		OrderedShaders(std::initializer_list<IShader*> shaders)
		{
			std::vector<IShader*> ordered;
			for (IShader* shader : shaders)
			{
				if (shader != nullptr)
					ordered.push_back(shader);
			}
			return ordered;
		}

		// Links the composition and pulls DXIL + reflection out of the one linked
		// program, so the bytecode and the root signature always agree. This is the
		// slow path: GetSlangModule() front-end-compiles the source here.
		CachedProgram
		CompileWithSlang(slang::ISession* session, const std::vector<IShader*>& shaders)
		{
			SlangErrorChecker                              errChecker;
			std::vector<slang::IComponentType*>            components;
			std::unordered_set<slang::IModule*>            uniqueModules;
			std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPoints;

			for (IShader* shader : shaders)
			{
				slang::IModule* module = shader->GetSlangModule();
				gassert(module != nullptr, "Shader module cannot be null");

				if (uniqueModules.insert(module).second)
					components.emplace_back(module);

				Slang::ComPtr<slang::IEntryPoint> entryPoint;
				module->findEntryPointByName(
					shader->GetDesc().entryPointName.c_str(),
					entryPoint.writeRef());
				gassert(entryPoint != nullptr, "Failed to find entry point in module");

				components.emplace_back(entryPoint.get());
				entryPoints.emplace_back(std::move(entryPoint));
			}

			Slang::ComPtr<slang::IComponentType> program;
			session->createCompositeComponentType(
				components.data(),
				static_cast<SlangInt>(components.size()),
				program.writeRef(),
				errChecker.WriteDiagnosticBlob()) >>
				errChecker;
			gassert(program != nullptr, "Failed to compose shader modules");

			Slang::ComPtr<slang::IComponentType> linkedProgram;
			program->link(linkedProgram.writeRef(), errChecker.WriteDiagnosticBlob()) >> errChecker;

			slang::ProgramLayout* layout = linkedProgram->getLayout();

			CachedProgram result;

			// Entry-point names are unique within one PSO's linked program.
			std::unordered_map<std::string_view, SlangInt> entryPointIndexByName;
			for (SlangUInt i = 0; i < layout->getEntryPointCount(); ++i)
			{
				entryPointIndexByName.emplace(
					layout->getEntryPointByIndex(i)->getName(),
					static_cast<SlangInt>(i));
			}

			for (IShader* shader : shaders)
			{
				const std::string& entryName = shader->GetDesc().entryPointName;

				auto found = entryPointIndexByName.find(entryName);
				gassert(
					found != entryPointIndexByName.end(),
					"Entry point missing from linked program");

				Slang::ComPtr<slang::IBlob> code;
				linkedProgram->getEntryPointCode(
					found->second,
					0,
					code.writeRef(),
					errChecker.WriteDiagnosticBlob()) >>
					errChecker;
				gassert(code != nullptr, "Failed to generate entry point bytecode");

				const auto* bytes = static_cast<const std::byte*>(code->getBufferPointer());
				result.entryPointDxil.emplace_back(
					entryName,
					std::vector<std::byte>(bytes, bytes + code->getBufferSize()));
			}

			for (uint32_t i = 0; i < layout->getParameterCount(); ++i)
			{
				slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);

				if (param->getCategory() != slang::ParameterCategory::ConstantBuffer)
					continue;

				slang::TypeLayoutReflection* typeLayout    = param->getTypeLayout();
				slang::TypeLayoutReflection* elementLayout = typeLayout->getElementTypeLayout();

				CachedCbuffer cbuffer;
				cbuffer.name           = param->getName();
				cbuffer.size           = static_cast<uint32_t>(elementLayout->getSize());
				cbuffer.rootParamIndex = static_cast<uint32_t>(result.cbuffers.size());
				cbuffer.shaderRegister = static_cast<uint32_t>(param->getBindingIndex());
				cbuffer.registerSpace  = static_cast<uint32_t>(param->getBindingSpace());
				cbuffer.layout         = ReflectLayoutFromSlang(elementLayout);
				result.cbuffers.push_back(std::move(cbuffer));
			}

			return result;
		}

		PipelineLayout
		BuildLayoutFromProgram(
			ID3D12Device*                device,
			const std::vector<IShader*>& shaders,
			const CachedProgram&         program)
		{
			PipelineLayout result;

			std::vector<CD3DX12_ROOT_PARAMETER> rootParams;
			rootParams.reserve(program.cbuffers.size());

			for (const CachedCbuffer& cbuffer : program.cbuffers)
			{
				gassert(
					cbuffer.rootParamIndex == rootParams.size(),
					"Cached cbuffer root parameter order is inconsistent");

				auto cbvParam = CD3DX12_ROOT_PARAMETER();
				cbvParam.InitAsConstantBufferView(cbuffer.shaderRegister, cbuffer.registerSpace);
				rootParams.push_back(cbvParam);

				UniformLayoutEntry entry{};
				entry.size           = cbuffer.size;
				entry.layout         = std::make_shared<const ReflectedLayout>(cbuffer.layout);
				entry.rootParamIndex = cbuffer.rootParamIndex;
				result.uniformLayoutEntries[cbuffer.name] = entry;
			}

			D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
			rsDesc.NumParameters             = static_cast<UINT>(rootParams.size());
			rsDesc.pParameters               = rootParams.empty() ? nullptr : rootParams.data();
			rsDesc.NumStaticSamplers         = 0;
			rsDesc.pStaticSamplers           = nullptr;
			rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
			               D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

			wrl::ComPtr<ID3DBlob> sigBlob, errBlob;
			D3D12SerializeRootSignature(
				&rsDesc,
				D3D_ROOT_SIGNATURE_VERSION_1,
				&sigBlob,
				&errBlob) >>
				d3d12ErrChecker;

			device->CreateRootSignature(
				0,
				sigBlob->GetBufferPointer(),
				sigBlob->GetBufferSize(),
				IID_PPV_ARGS(&result.rootSignature)) >>
				d3d12ErrChecker;

			for (IShader* shader : shaders)
			{
				const std::string& entryName = shader->GetDesc().entryPointName;

				auto found = std::find_if(
					program.entryPointDxil.begin(),
					program.entryPointDxil.end(),
					[&](const auto& e) { return e.first == entryName; });
				gassert(
					found != program.entryPointDxil.end(),
					"Cached program is missing bytecode for a shader");

				result.entryPointCode[entryName] = found->second;
			}

			return result;
		}
	}

	PipelineLayout
	BuildPipelineLayout(
		ID3D12Device*                   device,
		slang::ISession*                session,
		const ShaderCache*              cache,
		std::initializer_list<IShader*> shaders)
	{
		gassert(device != nullptr, "Device pointer must not be null.");
		gassert(session != nullptr, "Session cannot be null");

		const std::vector<IShader*> ordered = OrderedShaders(shaders);

		CachedProgram program;
		bool          haveProgram = false;
		uint64_t      key         = 0;

		if (cache != nullptr)
		{
			std::vector<std::pair<std::string, std::string>> moduleEntries;
			moduleEntries.reserve(ordered.size());
			for (IShader* shader : ordered)
			{
				moduleEntries.emplace_back(
					shader->GetDesc().slangModuleName,
					shader->GetDesc().entryPointName);
			}

			key         = cache->ComputeKey(std::move(moduleEntries));
			haveProgram = cache->TryLoad(key, program);
		}

		if (!haveProgram)
		{
			program = CompileWithSlang(session, ordered);
			if (cache != nullptr)
				cache->Store(key, program);
		}

		return BuildLayoutFromProgram(device, ordered, program);
	}
}
