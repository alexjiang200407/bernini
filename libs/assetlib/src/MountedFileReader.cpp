#include "MountedFileReader.h"

#include <core/err/util.h>

namespace assetlib
{
	MountedFileReader::MountedFileReader(
		const core::file::IFileSystem& fileSystem,
		std::string_view               path,
		std::string_view what) : IRangeReader(what), m_FileSystem(&fileSystem), m_Path(path)
	{
		const auto stamp = m_FileSystem->Stat(m_Path);
		core::throw_runtime_error_if(
			!stamp.has_value(),
			"{}: '{}' is not in the mounted filesystem",
			m_What,
			m_Path);

		m_Size = stamp->size;
	}

	void
	MountedFileReader::ReadAt(void* destination, uint64_t bytes, uint64_t offset)
	{
		CheckRange(bytes, offset);

		const std::vector<std::byte> range = m_FileSystem->ReadRange(m_Path, offset, bytes);
		std::copy_n(range.data(), bytes, static_cast<std::byte*>(destination));
	}
}
