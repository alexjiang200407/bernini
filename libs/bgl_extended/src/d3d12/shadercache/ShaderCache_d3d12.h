#pragma once
#include <bgl_common/ReflectedLayout.h>
#include <bgpu/GpuContext.h>
#include <bgpu/ProgramCache.h>
#include <core/type_traits.h>
#include <cstdint>
#include <string_view>

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

	struct EntryPointDxil
	{
		std::string            entryPoint;
		std::vector<std::byte> dxil;
	};

	// The full result of compiling one PSO's shader composition, in an API-neutral form
	// (DXIL bytes + reflection). The slang compile path and the disk-cache load path
	// both converge on this, so a single builder turns it into D3D12 objects.
	struct CachedProgram
	{
		std::vector<CachedCbuffer>  cbuffers;
		std::vector<EntryPointDxil> entryPointDxil;
	};

	// The renderer's two-layer shader cache on D3D12: its programs (DXIL + reflection) in the
	// context's program cache, and an ID3D12PipelineLibrary of driver-compiled PSOs in the same
	// directory. See docs/shader_cache.md.
	class ShaderCache
	{
	public:
		// device backs the pipeline library. usePipelineLibrary false keeps the programs but drops
		// the driver PSO layer; pass false when GPU-based validation is on. See the note on
		// m_PsoLibrary.
		//
		// @pre context->GetProgramCache() is not null.
		ShaderCache(bgpu::GpuContextRef context, ID3D12Device* device, bool usePipelineLibrary);

		~ShaderCache();

		ShaderCache(const ShaderCache&) = delete;

		ShaderCache&
		operator=(const ShaderCache&) = delete;

		// Stable key for a PSO's shader composition. moduleEntries must be the entry point of
		// every shader in the PSO.
		[[nodiscard]] uint64_t
		ComputeKey(std::vector<bgpu::ProgramEntryPoint> moduleEntries) const;

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
		// caller then creates the PSO normally. Safe from any thread, as is StorePipeline:
		// pipelines are built in parallel.
		[[nodiscard]] bool
		LoadPipeline(
			uint64_t                                identity,
			const D3D12_PIPELINE_STATE_STREAM_DESC& desc,
			ID3D12PipelineState**                   outPipeline);

		void
		StorePipeline(uint64_t identity, ID3D12PipelineState* pipeline);

	private:
		// Held so the program cache below outlives this.
		bgpu::GpuContextRef       m_Context;
		const bgpu::ProgramCache& m_Programs;

		// Null when GPU-based validation is on -- that run exists to instrument every shader, and a
		// PSO replayed out of the library was compiled without the instrumentation -- and null when
		// another writer holds the directory's library.
		wrl::ComPtr<ID3D12PipelineLibrary1> m_PsoLibrary;
		std::vector<std::byte>              m_PsoLibraryBlob;  // backs m_PsoLibrary
		bool                                m_PsoLibraryDirty = false;
		std::mutex                          m_PsoLibraryMutex;

		// Held for as long as this cache may write the library, so the claim below is the OS's to
		// arbitrate and a killed process releases it with nothing to clean up.
		HANDLE m_PsoLibraryLock = nullptr;

		/** Whether this cache is the one writer of the directory's driver library. */
		[[nodiscard]] bool
		ClaimPipelineLibrary();
	};
}
