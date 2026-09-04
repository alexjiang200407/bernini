#pragma once
#include "IRangeReader.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace assetlib
{
	/** A file on disk, opened once and seeked -- one open however many ranges are read. */
	class CheckedFileReader final : public IRangeReader
	{
	public:
		/** @throws std::runtime_error if the file cannot be opened or sized. */
		CheckedFileReader(std::filesystem::path path, std::string_view what);

		CheckedFileReader(const CheckedFileReader&) = delete;
		CheckedFileReader(CheckedFileReader&&)      = delete;
		CheckedFileReader&
		operator=(const CheckedFileReader&) = delete;
		CheckedFileReader&
		operator=(CheckedFileReader&&) = delete;

		[[nodiscard]] uint64_t
		GetSize() const noexcept override
		{
			return m_Size;
		}

		void
		ReadAt(void* destination, uint64_t bytes, uint64_t offset) override;

	private:
		std::filesystem::path m_Path;
		std::ifstream         m_In;
		uint64_t              m_Size = 0;
	};
}
