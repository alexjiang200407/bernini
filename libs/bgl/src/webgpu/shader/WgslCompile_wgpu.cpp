#include "shader/WgslCompile_wgpu.h"

#include "slang/SlangErrorChecker.h"

#include <bgl/IGraphics.h>

namespace bgl
{
	std::string
	CompileEntryPointToWgsl(
		slang::ISession* session,
		slang::IModule*  module,
		const char*      entryPointName)
	{
		gassert(session != nullptr, "CompileEntryPointToWgsl: null session");
		gassert(module != nullptr, "CompileEntryPointToWgsl: null module");

		SlangErrorChecker errChecker;

		auto entryPoint = Slang::ComPtr<slang::IEntryPoint>();
		module->findEntryPointByName(entryPointName, entryPoint.writeRef());
		if (entryPoint == nullptr)
			throw GraphicsError(std::string("wgsl: entry point not found: ") + entryPointName);

		slang::IComponentType* components[] = { module, entryPoint.get() };

		auto program = Slang::ComPtr<slang::IComponentType>();
		session->createCompositeComponentType(
			components,
			std::size(components),
			program.writeRef(),
			errChecker.WriteDiagnosticBlob()) >>
			errChecker;

		auto linked = Slang::ComPtr<slang::IComponentType>();
		program->link(linked.writeRef(), errChecker.WriteDiagnosticBlob()) >> errChecker;

		auto code = Slang::ComPtr<slang::IBlob>();
		linked->getEntryPointCode(0, 0, code.writeRef(), errChecker.WriteDiagnosticBlob()) >>
			errChecker;

		return std::string(
			static_cast<const char*>(code->getBufferPointer()),
			code->getBufferSize());
	}
}
