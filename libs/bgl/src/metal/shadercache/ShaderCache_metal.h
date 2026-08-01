#pragma once
#include "metal_cpp.h"

#include "pipeline/MetalPipelineReflection.h"
#include "uniforms/ReflectedLayout.h"

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

	enum class CachedStageKind : uint32_t
	{
		kCompute,
		kObject,
		kMesh,
		kFragment,
	};

	// One compiled stage. Metal compiles each meshlet stage as its own program, so the MSL and the
	// [[buffer(N)]] indices both belong to the stage rather than the pipeline -- see
	// MeshletPipeline_metal.
	struct CachedStage
	{
		CachedStageKind                               kind = CachedStageKind::kCompute;
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
		ShaderCache(
			MTL::Device*                    device,
			std::filesystem::path           cacheDir,
			std::string_view                optionsSalt,
			const std::vector<std::string>& searchPaths);

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

		// The archive to hand a pipeline descriptor, so the driver looks the pipeline up instead of
		// compiling it. Null when no archive could be opened.
		[[nodiscard]] MTL::BinaryArchive*
		GetBinaryArchive() const noexcept
		{
			return m_Archive.get();
		}

		// Records that a pipeline was added to the archive, so the destructor writes it out. An
		// archive is serialized whole, so this is deferred to one write per run.
		void
		MarkArchiveDirty() noexcept
		{
			m_ArchiveDirty = true;
		}

	private:
		std::filesystem::path m_CacheDir;
		uint64_t              m_SourceSalt = 0;

		NS::SharedPtr<MTL::BinaryArchive> m_Archive;
		bool                              m_ArchiveDirty = false;
	};
}
