#include "checked_read.h"

#include "fs_util.h"
#include <core/err/util.h>

namespace assetlib
{
	void
	IRangeReader::CheckRange(uint64_t bytes, uint64_t offset) const
	{
		const uint64_t size = GetSize();
		if (bytes > size || offset > size - bytes)
			core::throw_runtime_error("{}: a range extends past the end of the source", m_What);
	}

	CheckedFileReader::CheckedFileReader(std::filesystem::path path, std::string_view what) :
		IRangeReader(what), m_Path(std::move(path))
	{
		// Cleared so fileErrorMessage cannot blame a stale errno from an unrelated call.
		errno = 0;
		m_In.open(m_Path, std::ios::binary);
		if (!m_In)
			throw std::runtime_error(
				fileErrorMessage(m_What + ": cannot open file for reading", m_Path));

		std::error_code ec;
		m_Size = std::filesystem::file_size(m_Path, ec);
		if (ec)
			throw std::runtime_error(fileErrorMessage(m_What + ": cannot size file", m_Path));
	}

	void
	CheckedFileReader::ReadAt(void* destination, uint64_t bytes, uint64_t offset)
	{
		CheckRange(bytes, offset);

		m_In.seekg(static_cast<std::streamoff>(offset));
		m_In.read(static_cast<char*>(destination), static_cast<std::streamsize>(bytes));
		if (!m_In)
			throw std::runtime_error(fileErrorMessage(m_What + ": failed to read file", m_Path));
	}

	MountedFileReader::MountedFileReader(
		const core::file::IFileSystem& fileSystem,
		std::string_view               path,
		std::string_view what) : IRangeReader(what), m_FileSystem(&fileSystem), m_Path(path)
	{
		const std::optional<core::file::FileStamp> stamp = m_FileSystem->Stat(m_Path);
		if (!stamp.has_value())
			core::throw_runtime_error("{}: '{}' is not in the mounted filesystem", m_What, m_Path);

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
