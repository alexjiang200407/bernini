#pragma once
#include "uniforms/ReflectedLayout.h"

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

	// Persistent, on-disk cache of compiled shaders, keyed by their source composition
	// and compile options. It has two layers, each skipping a different compile stage
	// that is otherwise paid on every launch:
	//   * the program cache (.bsc files) holds DXIL + reflection, skipping the whole
	//     slang pipeline (front-end parse + DXIL codegen);
	//   * the pipeline library (an ID3D12PipelineLibrary blob) holds driver-compiled
	//     PSOs, skipping the driver's DXIL -> GPU-ISA compile.
	//
	// Program-cache invalidation is coarse and automatic: a single hash folds the
	// shader compiler version, the compile options, this cache's format version, and
	// the content of every shader source file. Any change flips every key, so stale
	// entries are missed and recompiled. The pipeline library additionally
	// self-invalidates against the driver and adapter (D3D12 rejects a foreign blob).
	class ShaderCache
	{
	public:
		// searchPaths are the session's shader source roots; every file under them
		// contributes to the invalidation hash. optionsSalt captures the compiler
		// version and compile options that affect codegen. device backs the pipeline
		// library.
		ShaderCache(
			ID3D12Device*                   device,
			std::filesystem::path           cacheDir,
			std::string_view                optionsSalt,
			const std::vector<std::string>& searchPaths);

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

		// Rolling hash of arbitrary bytes, used by pipeline creation to derive a PSO's
		// identity from its bytecode and render state. Seed the first call with 0.
		[[nodiscard]] static uint64_t
		CombineHash(uint64_t seed, const void* data, size_t size);

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
		std::filesystem::path               m_CacheDir;
		uint64_t                            m_SourceSalt = 0;
		wrl::ComPtr<ID3D12PipelineLibrary1> m_PsoLibrary;
		std::vector<std::byte>              m_PsoLibraryBlob;  // backs m_PsoLibrary
		bool                                m_PsoLibraryDirty = false;
	};
}
