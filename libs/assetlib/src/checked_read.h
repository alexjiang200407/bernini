#pragma once

namespace assetlib
{
	/**
	 * A container file opened for reading, with every range checked against its real size.
	 *
	 * A container names its own offsets and sizes in its own header, so a truncated or crafted file
	 * can ask for a read past the end -- or, worse, for an allocation sized by a field nobody
	 * checked. **Check before you allocate**: a count read out of the file must be measured against
	 * the file before it reaches a vector's constructor, or a corrupt header asks for hundreds of
	 * gigabytes and the failure arrives as `bad_alloc` rather than as the malformed-file error the
	 * caller is handling.
	 */
	class CheckedFileReader
	{
	public:
		/**
		 * @param what Container name the error messages are prefixed with, e.g. "bmesh".
		 * @throws std::runtime_error if the file cannot be opened or sized.
		 */
		CheckedFileReader(std::filesystem::path path, std::string_view what);

		// Function-scoped and single-use. Declared rather than left implicit because the ifstream
		// member deletes them anyway, and MSVC treats each one it has to delete for you as an error.
		CheckedFileReader(const CheckedFileReader&) = delete;
		CheckedFileReader(CheckedFileReader&&)      = delete;
		CheckedFileReader&
		operator=(const CheckedFileReader&) = delete;
		CheckedFileReader&
		operator=(CheckedFileReader&&) = delete;

		[[nodiscard]] uint64_t
		GetSize() const noexcept
		{
			return m_Size;
		}

		/**
		 * @throws std::runtime_error unless `[offset, offset + bytes)` lies inside the file.
		 *         Compares by subtraction: a crafted offset near UINT64_MAX would wrap past the sum.
		 */
		void
		CheckRange(uint64_t bytes, uint64_t offset) const;

		/** @throws std::runtime_error if the range is outside the file, or the read fails. */
		void
		ReadAt(void* destination, uint64_t bytes, uint64_t offset);

	private:
		std::filesystem::path m_Path;
		std::string           m_What;
		std::ifstream         m_In;
		uint64_t              m_Size = 0;
	};
}
