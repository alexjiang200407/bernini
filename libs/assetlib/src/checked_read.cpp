#include "checked_read.h"

#include "fs_util.h"
#include <core/err/util.h>

namespace assetlib
{
	CheckedFileReader::CheckedFileReader(std::filesystem::path path, std::string_view what) :
		m_Path(std::move(path)), m_What(what)
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
	CheckedFileReader::CheckRange(uint64_t bytes, uint64_t offset) const
	{
		if (bytes > m_Size || offset > m_Size - bytes)
			core::throw_runtime_error("{}: a range extends past the end of file", m_What);
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
}
