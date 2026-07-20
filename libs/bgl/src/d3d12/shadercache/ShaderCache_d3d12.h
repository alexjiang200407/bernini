#pragma once
#include "uniforms/ReflectedLayout.h"
#include <core/type_traits.h>

namespace bgl
{
	// One reflected constant buffer of a linked program: everything needed to rebuild
	// the root parameter and the Uniforms mirror without touching slang.
	struct CachedCbuffer
	{
		std::string     name;
		uint32_t        size           = 0;
		uint32_t        rootParamIndex = 0;
		uint32_t        shaderRegister = 0;
		uint32_t        registerSpace  = 0;
		ReflectedLayout layout;
	};

	// The full result of compiling one PSO's shader composition, in an API-neutral form
	// (DXIL bytes + reflection). The slang compile path and the disk-cache load path
	// both converge on this, so a single builder turns it into D3D12 objects.
	struct CachedProgram
	{
		std::vector<CachedCbuffer>                                  cbuffers;
		std::vector<std::pair<std::string, std::vector<std::byte>>> entryPointDxil;
	};

	// Persistent, two-layer on-disk cache of compiled shaders: a program cache (DXIL +
	// reflection) and an ID3D12PipelineLibrary of driver-compiled PSOs. See
	// docs/shader_cache.md.
	class ShaderCache
	{
	public:
		// searchPaths are the session's shader source roots; every file under them
		// contributes to the invalidation hash. optionsSalt captures the compiler
		// version and compile options that affect codegen. device backs the pipeline
		// library.
		//
		// usePipelineLibrary false keeps the program cache but drops the driver PSO
		// layer; pass false when GPU-based validation is on. See the note on
		// m_PsoLibrary.
		ShaderCache(
			ID3D12Device*                   device,
			std::filesystem::path           cacheDir,
			std::string_view                optionsSalt,
			const std::vector<std::string>& searchPaths,
			bool                            usePipelineLibrary);

		~ShaderCache();

		ShaderCache(const ShaderCache&) = delete;

		ShaderCache&
		operator=(const ShaderCache&) = delete;

		// Stable key for a PSO's shader composition. moduleEntries must be the
		// (module, entry-point) pairs of every shader in the PSO.
		[[nodiscard]] uint64_t
		ComputeKey(std::vector<std::pair<std::string, std::string>> moduleEntries) const;

		// Reads and deserializes the cached program for key. Returns false on a miss
		// or any read/parse error (the caller then recompiles).
		[[nodiscard]] bool
		TryLoad(uint64_t key, CachedProgram& out) const;

		void
		Store(uint64_t key, const CachedProgram& program) const;

		// Rolling hash used by pipeline creation to derive a PSO's identity from its
		// bytecode and render state. Seed the first call with 0.
		[[nodiscard]] static uint64_t
		CombineHash(uint64_t seed, std::span<const std::byte> bytes);

		template <core::type_traits::trivially_copyable T>
			requires(!std::convertible_to<const T&, std::span<const std::byte>>)
		[[nodiscard]] static uint64_t
		CombineHash(uint64_t seed, const T& value)
		{
			return CombineHash(seed, std::as_bytes(std::span<const T, 1>(&value, 1)));
		}

		// Loads a driver-compiled PSO previously stored under an identical identity.
		// Returns false on a miss (including when the library is unavailable); the
		// caller then creates the PSO normally.
		[[nodiscard]] bool
		LoadPipeline(
			uint64_t                                identity,
			const D3D12_PIPELINE_STATE_STREAM_DESC& desc,
			ID3D12PipelineState**                   outPipeline);

		void
		StorePipeline(uint64_t identity, ID3D12PipelineState* pipeline);

	private:
		std::filesystem::path m_CacheDir;
		uint64_t              m_SourceSalt = 0;

		// Null when GPU-based validation is on: that run exists to instrument every shader, and a
		// PSO replayed out of the library was compiled without the instrumentation.
		wrl::ComPtr<ID3D12PipelineLibrary1> m_PsoLibrary;
		std::vector<std::byte>              m_PsoLibraryBlob;  // backs m_PsoLibrary
		bool                                m_PsoLibraryDirty = false;
	};
}
