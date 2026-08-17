#pragma once
#include <core/file/IFileSystem.h>

#include "IRangeReader.h"

namespace assetlib
{
	/**
	 * One file inside a mounted filesystem, read through its ranged read.
	 *
	 * Costs an open per range where CheckedFileReader costs one for the whole read, so a plain
	 * directory still goes through that one; this is for what a directory cannot serve.
	 *
	 * @throws std::runtime_error at construction if `path` is not in `fileSystem`.
	 */
	class MountedFileReader final : public IRangeReader
	{
	public:
		MountedFileReader(
			const core::file::IFileSystem& fileSystem,
			std::string_view               path,
			std::string_view               what);

		MountedFileReader(const MountedFileReader&) = delete;
		MountedFileReader(MountedFileReader&&)      = delete;
		MountedFileReader&
		operator=(const MountedFileReader&) = delete;
		MountedFileReader&
		operator=(MountedFileReader&&) = delete;

		[[nodiscard]] uint64_t
		GetSize() const noexcept override
		{
			return m_Size;
		}

		void
		ReadAt(void* destination, uint64_t bytes, uint64_t offset) override;

	private:
		const core::file::IFileSystem* m_FileSystem;
		std::string                    m_Path;
		uint64_t                       m_Size = 0;
	};
}
