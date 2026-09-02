#pragma once
#include "metal_cpp.h"
#include "types/ShaderStage.h"

#include "pipeline/MetalPipelineReflection.h"
#include <bgl_common/ReflectedLayout.h>

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
	 * Persistent, two-layer on-disk cache of compiled shaders: a program cache (MSL + reflection)
	 * skipping the slang front-end, and an MTL::BinaryArchive of driver-compiled pipelines skipping
	 * the MSL->GPU compile. See docs/shader_cache.md.
	 */
	class ShaderCache
	{
	public:
		// searchPaths are the session's shader source roots; every file under them contributes to
		// the invalidation hash. optionsSalt captures the compiler version and the compile options
		// that affect codegen.
		//
		// usePipelineLibrary false keeps the program cache but drops the binary archive; pass false
		// when GPU validation is on. An archive is written by an uninstrumented run, and Metal
		// crashes inside newBinaryArchive loading one into a validating device.
		ShaderCache(
			MTL::Device*                    device,
			std::filesystem::path           cacheDir,
			std::string_view                optionsSalt,
			const std::vector<std::string>& searchPaths,
			bool                            usePipelineLibrary);

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
		std::filesystem::path m_CacheDir;
		uint64_t              m_SourceSalt = 0;

		NS::SharedPtr<MTL::BinaryArchive> m_Archive;
		std::mutex                        m_ArchiveMutex;
		bool                              m_ArchiveDirty = false;
	};
}
