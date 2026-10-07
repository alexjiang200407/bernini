#pragma once
#include <bgpu/reflection/ReflectedLayout.h>

#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>
#include <filesystem>

// The reflection half of a renderer's shader cache entry. The store, the salt and the key are the
// GPU context's (bgpu::ProgramCache); what an entry contains is each backend's, and a constant
// buffer's layout is the part both backends write the same way. See docs/shader_cache.md.
namespace bgpu::shader_cache
{
	void
	WriteLayout(core::io::ByteWriter& writer, const ReflectedLayout& layout);

	/** @throws std::runtime_error on a truncated stream, as every ByteReader read does. */
	ReflectedLayout
	ReadLayout(core::io::ByteReader& reader);

	// A backend's driver pipeline library, beside the program cache in its directory.
	constexpr const char* c_PipelineLibraryFile = "pipelines.psolib";

	/**
	 * This process's claim to write a directory's driver pipeline library. The library is replaced
	 * whole by whoever writes it last, so two writers on one directory -- the suite's shards, or two
	 * renderers in one process -- would each discard the other's: one claims it, and the rest build
	 * their pipelines without a library, which costs pipeline creation and nothing else.
	 *
	 * The OS arbitrates it, through a lock file opened unshared and deleted on close, so a killed
	 * process releases it with nothing to clean up. Windows only; elsewhere nothing is claimed.
	 */
	class PipelineLibraryClaim
	{
	public:
		explicit PipelineLibraryClaim(const std::filesystem::path& directory) noexcept;
		~PipelineLibraryClaim() noexcept;

		PipelineLibraryClaim(const PipelineLibraryClaim&) = delete;
		PipelineLibraryClaim(PipelineLibraryClaim&&)      = delete;
		PipelineLibraryClaim&
		operator=(const PipelineLibraryClaim&) = delete;
		PipelineLibraryClaim&
		operator=(PipelineLibraryClaim&&) = delete;

		[[nodiscard]] bool
		Held() const noexcept
		{
			return m_Lock != nullptr;
		}

	private:
		void* m_Lock = nullptr;  // the lock file's HANDLE
	};
}
