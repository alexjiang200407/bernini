#pragma once

namespace bgl
{
	/**
	 * Links one entry point of a Slang module and returns its WGSL source.
	 *
	 * The session must target WGSL. Throws GraphicsError on a Slang compile/link failure.
	 */
	std::string
	CompileEntryPointToWgsl(
		slang::ISession* session,
		slang::IModule*  module,
		const char*      entryPointName);
}
