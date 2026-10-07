#pragma once
#include <bgpu/GpuContext.h>
#include <bgpu/ProgramCache.h>
#include <bgpu/reflection/ReflectedLayout.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bgpu
{
	// One reflected constant buffer of a linked program: what rebuilds its descriptor binding and
	// its Uniforms mirror without touching Slang.
	struct CachedCbuffer
	{
		std::string     name;
		uint32_t        size           = 0;
		uint32_t        rootParamIndex = 0;
		uint32_t        binding        = 0;
		uint32_t        set            = 0;
		ReflectedLayout layout;
	};

	struct EntryPointSpirv
	{
		std::string            entryPoint;
		std::vector<std::byte> spirv;
	};

	// One pipeline's shader composition compiled: SPIR-V per entry point and the reflection. The
	// Slang path and the cache's load path both arrive here, so one builder makes the Vulkan
	// objects from it.
	struct CachedProgram
	{
		std::vector<CachedCbuffer>   cbuffers;
		std::vector<EntryPointSpirv> entryPointSpirv;
	};

	/**
	 * The Vulkan half of the shader cache: its programs in the context's program cache. No driver
	 * pipeline cache is kept beside them yet. See docs/shader_cache.md.
	 */
	class ShaderCache
	{
	public:
		/** @pre context->GetProgramCache() is not null. */
		explicit ShaderCache(GpuContextRef context);

		ShaderCache(const ShaderCache&) = delete;
		ShaderCache(ShaderCache&&)      = delete;
		ShaderCache&
		operator=(const ShaderCache&) = delete;
		ShaderCache&
		operator=(ShaderCache&&) = delete;
		~ShaderCache()           = default;

		/** Stable key for a pipeline's shader composition: every shader's module and entry point. */
		[[nodiscard]] uint64_t
		ComputeKey(std::vector<ProgramEntryPoint> moduleEntries) const;

		/** False on a miss or an entry that does not decode; the caller then compiles. */
		[[nodiscard]] bool
		TryLoad(uint64_t key, CachedProgram& out) const;

		void
		Store(uint64_t key, const CachedProgram& program) const;

	private:
		// Held so the program cache below outlives this.
		GpuContextRef       m_Context;
		const ProgramCache& m_Programs;
	};
}
