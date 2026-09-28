#include "KernelCode.h"
#include <bgpu/GpuContext.h>
#include <bgpu/SlangErrorChecker.h>
#include <core/err/util.h>
#include <cstdint>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>
#include <string_view>

namespace crowd
{
	namespace
	{
		// By the parameter's own category: a structured buffer is a UAV on D3D12 and a buffer on
		// Metal, where Slang counts cbuffers and buffers in one [[buffer(N)]] space. A cbuffer is
		// Mixed on Metal, and its buffer index is the ConstantBuffer offset there -- never
		// getBindingIndex(), which answers for whichever category comes first.
		KernelBinding
		BindingOf(slang::ProgramLayout* layout, std::string_view name)
		{
			for (uint32_t i = 0; i < layout->getParameterCount(); ++i)
			{
				slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);
				if (name != param->getName())
					continue;

				auto category = param->getCategory();
				if (category == slang::ParameterCategory::Mixed)
					category = slang::ParameterCategory::ConstantBuffer;
				return KernelBinding{
					.index = static_cast<uint32_t>(param->getOffset(category)),
					.space = static_cast<uint32_t>(param->getBindingSpace(category)),
				};
			}

			core::throw_runtime_error("Kernel declares no parameter '{}'", name);
		}
	}

	KernelCode
	CompileKernel(bgpu::GpuContext& context, std::string_view moduleName)
	{
		bgpu::SlangErrorChecker errChecker;

		slang::IModule* module = context.LoadModule(moduleName);
		core::ensure(module != nullptr, "Failed to load Slang module '{}'", moduleName);

		slang::ISession* session = module->getSession();

		Slang::ComPtr<slang::IEntryPoint> entryPoint;
		module->findEntryPointByName("main", entryPoint.writeRef());
		core::ensure(entryPoint != nullptr, "'{}' has no entry point 'main'", moduleName);

		slang::IComponentType* components[] = { module, entryPoint.get() };

		Slang::ComPtr<slang::IComponentType> program;
		session->createCompositeComponentType(
			components,
			2,
			program.writeRef(),
			errChecker.WriteDiagnosticBlob()) >>
			errChecker;

		Slang::ComPtr<slang::IComponentType> linked;
		program->link(linked.writeRef(), errChecker.WriteDiagnosticBlob()) >> errChecker;

		Slang::ComPtr<slang::IBlob> code;
		linked->getEntryPointCode(0, 0, code.writeRef(), errChecker.WriteDiagnosticBlob()) >>
			errChecker;
		core::ensure(code != nullptr, "'{}' generated no code", moduleName);

		slang::ProgramLayout* layout = linked->getLayout();

		SlangUInt threadGroup[3] = { 1, 1, 1 };
		layout->getEntryPointByIndex(0)->getComputeThreadGroupSize(3, threadGroup);
		core::ensure(
			threadGroup[1] == 1 && threadGroup[2] == 1,
			"'{}' must be a one-dimensional kernel",
			moduleName);

		return KernelCode{
			.code = std::string(
				static_cast<const char*>(code->getBufferPointer()),
				code->getBufferSize()),
			.params          = BindingOf(layout, "gParams"),
			.output          = BindingOf(layout, "gOutput"),
			.threadsPerGroup = static_cast<uint32_t>(threadGroup[0]),
		};
	}
}
