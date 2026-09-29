#pragma once
#include "metal_cpp.h"
#include "types/ShaderStage.h"

#include "pipeline/MetalPipelineReflection.h"
#include <array>
#include <bgl_common/ReflectedLayout.h>
#include <bgpu/GpuContext.h>
#include <bgpu/ProgramCache.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgl
{
	// One reflected constant buffer of a linked program: the Uniforms mirror plus the bindless
	// handle fields the dispatch path rewrites, so both can be rebuilt without touching slang.
	struct CachedCbuffer
	{
		std::string             name;
		uint32_t                size = 0;
		ReflectedLayout         layout;
		std::vector<HandleSlot> handles;
	};

	// One compiled stage. Metal compiles each meshlet stage as its own program, so the MSL and the
	// [[buffer(N)]] indices both belong to the stage rather than the pipeline -- see
	// MeshletPipeline_metal.
	struct CachedStage
	{
		ShaderStage                                   stage = ShaderStage::kCompute;
		std::string                                   entryPoint;
		std::string                                   msl;
		std::vector<std::pair<std::string, uint32_t>> bindings;
		std::array<uint32_t, 3>                       threadsPerThreadgroup{ 1, 1, 1 };
	};

	// The whole result of compiling one PSO's shader composition. The slang path and the disk-cache
	// path both converge here, so pipeline construction has one way to build itself.
	struct CachedProgram
	{
		std::vector<CachedCbuffer> cbuffers;
		std::vector<CachedStage>   stages;
	};

	/**
	 * The renderer's two-layer shader cache on Metal: its programs (MSL + reflection) in the
	 * context's program cache, skipping the slang front-end, and an MTL::BinaryArchive of
	 * driver-compiled pipelines in the same directory, skipping the MSL->GPU compile. See
	 * docs/shader_cache.md.
	 */
	class ShaderCache
	{
	public:
		// usePipelineLibrary false keeps the programs but drops the binary archive; pass false when
		// GPU validation is on. An archive is written by an uninstrumented run, and Metal crashes
		// inside newBinaryArchive loading one into a validating device.
		//
		// @pre context->GetProgramCache() is not null.
		ShaderCache(bgpu::GpuContextRef context, MTL::Device* device, bool usePipelineLibrary);

		~ShaderCache();

		ShaderCache(const ShaderCache&) = delete;

		ShaderCache&
		operator=(const ShaderCache&) = delete;

		// Stable key for a PSO's shader composition, from the (module, entry-point) pairs of every
		// shader in it.
		[[nodiscard]] uint64_t
		ComputeKey(std::vector<std::pair<std::string, std::string>> moduleEntries) const;

		// False on a miss or any read/parse error, and then the caller recompiles.
		[[nodiscard]] bool
		TryLoad(uint64_t key, CachedProgram& out) const;

		void
		Store(uint64_t key, const CachedProgram& program) const;

		/**
		 * Runs `build` with the binary archive held for the calling thread alone, or with null when
		 * no archive could be opened. A descriptor handed the archive reads it inside the driver's
		 * pipeline creation and the built pipeline is added back afterwards, so both belong inside
		 * `build`: Metal documents no thread-safety for MTL::BinaryArchive, and pipelines are built
		 * in parallel. The MSL compile, which is the cost, happens before this and outside it.
		 */
		void
		WithArchive(const std::function<void(MTL::BinaryArchive*)>& build);

		// Records that a pipeline was added to the archive, so the destructor writes it out. An
		// archive is serialized whole, so this is deferred to one write per run. @pre called from
		// inside WithArchive's `build`.
		void
		MarkArchiveDirty() noexcept
		{
			m_ArchiveDirty = true;
		}

	private:
		// Held so the program cache below outlives this.
		bgpu::GpuContextRef       m_Context;
		const bgpu::ProgramCache& m_Programs;

		NS::SharedPtr<MTL::BinaryArchive> m_Archive;
		std::mutex                        m_ArchiveMutex;
		bool                              m_ArchiveDirty = false;
	};
}
