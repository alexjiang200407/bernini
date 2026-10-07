#pragma once
#include "shadercache/util.h"
#include "volk_vulkan.h"
#include <bgpu/GpuContext.h>
#include <bgpu/ProgramCache.h>
#include <bgpu/reflection/ReflectedLayout.h>
#include <cstddef>
#include <cstdint>
#include <memory>
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
	 * The Vulkan half of the shader cache: its programs in the context's program cache, and a
	 * VkPipelineCache of driver-compiled pipelines saved beside them. See docs/shader_cache.md.
	 */
	class ShaderCache
	{
	public:
		/**
		 * `usePipelineCache` false keeps the programs but drops the driver layer; pass false when
		 * GPU validation is on, since a pipeline replayed from the cache was compiled without its
		 * instrumentation.
		 *
		 * @pre context->GetProgramCache() is not null.
		 */
		ShaderCache(GpuContextRef context, bool usePipelineCache);

		ShaderCache(const ShaderCache&) = delete;
		ShaderCache(ShaderCache&&)      = delete;
		ShaderCache&
		operator=(const ShaderCache&) = delete;
		ShaderCache&
		operator=(ShaderCache&&) = delete;
		~ShaderCache() noexcept;

		/** Stable key for a pipeline's shader composition: every shader's module and entry point. */
		[[nodiscard]] uint64_t
		ComputeKey(std::vector<ProgramEntryPoint> moduleEntries) const;

		/** False on a miss or an entry that does not decode; the caller then compiles. */
		[[nodiscard]] bool
		TryLoad(uint64_t key, CachedProgram& out) const;

		void
		Store(uint64_t key, const CachedProgram& program) const;

		/**
		 * The driver's cache every pipeline is created through, or null when this device keeps
		 * none. Vulkan synchronizes it internally, so pipelines built in parallel share it.
		 */
		[[nodiscard]] VkPipelineCache
		GetVkPipelineCache() const noexcept
		{
			return m_PipelineCache;
		}

	private:
		// Held so the program cache below outlives this.
		GpuContextRef       m_Context;
		const ProgramCache& m_Programs;

		// Null when GPU validation is on, and when another writer holds the directory's library.
		VkPipelineCache m_PipelineCache = VK_NULL_HANDLE;
		// Released after the cache is written.
		std::unique_ptr<shader_cache::PipelineLibraryClaim> m_Claim;
	};

	/** The driver cache a pipeline is created through: `cache`'s, or none without one. */
	[[nodiscard]] inline VkPipelineCache
	PipelineCacheOf(const ShaderCache* cache) noexcept
	{
		return cache != nullptr ? cache->GetVkPipelineCache() : VK_NULL_HANDLE;
	}
}
