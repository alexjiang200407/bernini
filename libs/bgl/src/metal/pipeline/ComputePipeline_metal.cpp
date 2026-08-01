#include "pipeline/ComputePipeline_metal.h"
#include "MetalErrorChecker.h"

#include "pipeline/MetalPipelineReflection.h"
#include "shadercache/ShaderCache_metal.h"
#include "slang/SlangErrorChecker.h"
#include "util_metal.h"

#include <core/err/util.h>

namespace bgl
{
	namespace
	{
		// Compiles the kernel through slang into the cacheable form: MSL, reflection, the cbuffers'
		// handle offsets and this stage's buffer indices.
		CachedProgram
		CompileProgram(slang::ISession* session, IShader* shader, const std::string& entryName)
		{
			SlangErrorChecker errChecker;

			slang::IModule* module = shader->GetSlangModule();
			gassert(module != nullptr, "Shader module cannot be null");

			Slang::ComPtr<slang::IEntryPoint> entryPoint;
			module->findEntryPointByName(entryName.c_str(), entryPoint.writeRef());
			gassert(entryPoint != nullptr, "Failed to find entry point in module");

			slang::IComponentType* components[] = { module, entryPoint.get() };

			Slang::ComPtr<slang::IComponentType> program;
			session->createCompositeComponentType(
				components,
				2,
				program.writeRef(),
				errChecker.WriteDiagnosticBlob()) >>
				errChecker;

			Slang::ComPtr<slang::IComponentType> linkedProgram;
			program->link(linkedProgram.writeRef(), errChecker.WriteDiagnosticBlob()) >> errChecker;

			Slang::ComPtr<slang::IBlob> code;
			linkedProgram
					->getEntryPointCode(0, 0, code.writeRef(), errChecker.WriteDiagnosticBlob()) >>
				errChecker;
			gassert(code != nullptr, "Failed to generate MSL");

			slang::ProgramLayout* layout = linkedProgram->getLayout();

			UniformLayoutMap     entries;
			MetalHandleOffsetMap handleOffsets;
			ReflectCbuffers(layout, entries, handleOffsets);

			MetalStageBindingMap bindings;
			ReflectStageBindings(layout, bindings);

			CachedStage stage;
			stage.entryPoint = entryName;
			stage.msl        = std::string(
				static_cast<const char*>(code->getBufferPointer()),
				code->getBufferSize());
			for (const auto& [name, index] : bindings) stage.bindings.emplace_back(name, index);

			SlangUInt threadGroup[3] = { 1, 1, 1 };
			layout->getEntryPointByIndex(0)->getComputeThreadGroupSize(3, threadGroup);
			for (size_t i = 0; i < stage.threadsPerThreadgroup.size(); ++i)
				stage.threadsPerThreadgroup[i] = static_cast<uint32_t>(threadGroup[i]);

			CachedProgram cached;
			cached.stages.push_back(std::move(stage));
			for (const auto& [name, entry] : entries)
			{
				CachedCbuffer cbuffer;
				cbuffer.name    = name;
				cbuffer.size    = entry.size;
				cbuffer.layout  = *entry.layout;
				cbuffer.handles = handleOffsets[name];
				cached.cbuffers.push_back(std::move(cbuffer));
			}
			return cached;
		}
	}

	ComputePipeline::ComputePipeline(
		MTL::Device*               device,
		slang::ISession*           session,
		ShaderCache*               shaderCache,
		const ComputePipelineDesc& desc) : m_Desc(desc)
	{
		gassert(m_Desc.shader != nullptr, "Compute pipeline requires a shader");

		IShader*           shader    = m_Desc.shader.Get();
		const std::string& entryName = shader->GetDesc().entryPointName;

		uint64_t      key = 0;
		CachedProgram cached;
		bool          hit = false;
		if (shaderCache != nullptr)
		{
			key = shaderCache->ComputeKey({ { shader->GetDesc().slangModuleName, entryName } });
			hit = shaderCache->TryLoad(key, cached);
		}

		if (!hit)
		{
			cached = CompileProgram(session, shader, entryName);
			if (shaderCache != nullptr)
				shaderCache->Store(key, cached);
		}

		gassert(cached.stages.size() == 1, "A compute program has exactly one stage");
		const CachedStage& stage = cached.stages.front();

		// One entry point, so this stage's [[buffer(N)]] indices are the whole pipeline's and the
		// shared rootParamIndex can hold them.
		for (const CachedCbuffer& cbuffer : cached.cbuffers)
		{
			UniformLayoutEntry entry{};
			entry.size   = cbuffer.size;
			entry.layout = std::make_shared<const ReflectedLayout>(cbuffer.layout);
			for (const auto& [name, index] : stage.bindings)
			{
				if (name == cbuffer.name)
					entry.rootParamIndex = index;
			}
			m_UniformLayoutEntries[cbuffer.name] = std::move(entry);
			m_HandleOffsets[cbuffer.name]        = cbuffer.handles;
		}

		m_ThreadsPerThreadgroup = MTL::Size(
			stage.threadsPerThreadgroup[0],
			stage.threadsPerThreadgroup[1],
			stage.threadsPerThreadgroup[2]);

		NS::SharedPtr<NS::AutoreleasePool> pool =
			NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		MetalErrorChecker           errChecker;
		NS::SharedPtr<MTL::Library> library =
			NS::TransferPtr(device->newLibrary(Str(stage.msl), nullptr, errChecker.WriteError()));
		library.get() >> errChecker;

		// Slang mangles the entry name in MSL (main -> main_0); a single-entry compute library exposes
		// exactly one kernel function, so take it by name rather than guessing the mangled form.
		NS::Array* names = library->functionNames();
		gassert(names->count() == 1, "Compute library must expose exactly one kernel function");
		NS::SharedPtr<MTL::Function> fn =
			NS::TransferPtr(library->newFunction(static_cast<NS::String*>(names->object(0))));
		gassert(fn.get() != nullptr, "Compute library is missing its kernel function");

		NS::SharedPtr<MTL::ComputePipelineDescriptor> pd =
			NS::TransferPtr(MTL::ComputePipelineDescriptor::alloc()->init());
		pd->setComputeFunction(fn.get());

		MTL::BinaryArchive* archive =
			shaderCache != nullptr ? shaderCache->GetBinaryArchive() : nullptr;
		if (archive != nullptr)
		{
			const MTL::BinaryArchive* archives[] = { archive };
			pd->setBinaryArchives(
				NS::Array::array(
					reinterpret_cast<const NS::Object* const*>(archives),
					std::size(archives)));
		}

		m_PipelineState = NS::TransferPtr(device->newComputePipelineState(
			pd.get(),
			MTL::PipelineOptionNone,
			nullptr,
			errChecker.WriteError()));
		m_PipelineState.get() >> errChecker;

		// Adding after creation: the descriptor only reads from an archive, and a pipeline the
		// archive already holds is added again as a no-op rather than an error.
		if (archive != nullptr && archive->addComputePipelineFunctions(pd.get(), nullptr))
			shaderCache->MarkArchiveDirty();
	}
}
