#include "KernelCode.h"
#include <bgpu/GpuContext.h>
#include <bgpu/ProgramCache.h>
#include <bgpu/SlangErrorChecker.h>
#include <core/err/util.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <slang-com-ptr.h>
#include <slang.h>
#include <span>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <vector>

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

	namespace
	{
		// Bump formatVersion when the encoding below changes: it is in every key, so an entry in the
		// old layout is missed rather than misread.
		constexpr auto c_Owner = bgpu::ProgramCacheOwner{ .tag = "crowd", .formatVersion = 1 };

		void
		WriteBinding(core::io::ByteWriter& writer, const KernelBinding& binding)
		{
			writer.WritePod<uint32_t>(binding.index);
			writer.WritePod<uint32_t>(binding.space);
		}

		KernelBinding
		ReadBinding(core::io::ByteReader& reader)
		{
			auto binding  = KernelBinding();
			binding.index = reader.ReadPod<uint32_t>();
			binding.space = reader.ReadPod<uint32_t>();
			return binding;
		}

		std::vector<std::byte>
		Encode(const KernelCode& kernel)
		{
			auto writer = core::io::ByteWriter();
			writer.WriteString(kernel.code);
			WriteBinding(writer, kernel.params);
			WriteBinding(writer, kernel.output);
			writer.WritePod<uint32_t>(kernel.threadsPerGroup);
			return writer.Take();
		}

		/** @throws std::runtime_error on an entry shorter or longer than one kernel. */
		KernelCode
		Decode(std::span<const std::byte> bytes)
		{
			auto reader = core::io::ByteReader(bytes);
			auto kernel = KernelCode();

			kernel.code            = reader.ReadString();
			kernel.params          = ReadBinding(reader);
			kernel.output          = ReadBinding(reader);
			kernel.threadsPerGroup = reader.ReadPod<uint32_t>();
			if (reader.Remaining() != 0)
				core::throw_runtime_error("{} bytes after the kernel", reader.Remaining());
			return kernel;
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

	KernelCode
	LoadKernel(bgpu::GpuContext& context, std::string_view moduleName)
	{
		const bgpu::ProgramCache* cache = context.GetProgramCache();
		if (cache == nullptr)
			return CompileKernel(context, moduleName);

		const uint64_t key = cache->ComputeKey(c_Owner, { { std::string(moduleName), "main" } });

		std::vector<std::byte> bytes;
		if (cache->TryLoadProgram(key, bytes))
		{
			try
			{
				return Decode(bytes);
			}
			catch (const std::exception& e)
			{
				spdlog::warn(
					"Recompiling '{}': its cache entry does not decode: {}",
					moduleName,
					e.what());
			}
		}

		KernelCode kernel = CompileKernel(context, moduleName);
		cache->StoreProgram(key, Encode(kernel));
		return kernel;
	}
}
