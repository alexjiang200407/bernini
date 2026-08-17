#include "CheckedFileReader.h"

#include <core/err/util.h>

#include "fs_util.h"

namespace assetlib
{
	CheckedFileReader::CheckedFileReader(std::filesystem::path path, std::string_view what) :
		IRangeReader(what), m_Path(std::move(path))
	{
		// Cleared so fileErrorMessage cannot blame a stale errno from an unrelated call.
		errno = 0;
		m_In.open(m_Path, std::ios::binary);
		core::throw_runtime_error_if(
			!m_In,
			"{}",
			fileErrorMessage(m_What + ": cannot open file for reading", m_Path));

		std::error_code ec;
		m_Size = std::filesystem::file_size(m_Path, ec);
		core::throw_runtime_error_if(
			static_cast<bool>(ec),
			"{}",
			fileErrorMessage(m_What + ": cannot size file", m_Path));
	}

	void
	CheckedFileReader::ReadAt(void* destination, uint64_t bytes, uint64_t offset)
	{
		CheckRange(bytes, offset);

		m_In.seekg(static_cast<std::streamoff>(offset));
		m_In.read(static_cast<char*>(destination), static_cast<std::streamsize>(bytes));
		core::throw_runtime_error_if(
			!m_In,
			"{}",
			fileErrorMessage(m_What + ": failed to read file", m_Path));
	}
}
