#pragma once

namespace assetlib
{
	/**
	 * Bytes at explicit offsets, from whatever is behind them, with every range checked against the
	 * real extent first.
	 *
	 * A container names its own offsets and sizes in its own header, so a truncated or crafted file
	 * can ask for a read past the end -- or, worse, for an allocation sized by a field nobody
	 * checked. **Check before you allocate**: a count read out of the file must be measured against
	 * the source before it reaches a vector's constructor, or a corrupt header asks for hundreds of
	 * gigabytes and the failure arrives as `bad_alloc` rather than as the malformed-file error the
	 * caller is handling.
	 *
	 * Two implementations, because a chunked container reads its header, its table and a couple of
	 * its chunks and nothing else, and that has to keep working whether the container is a file on
	 * disk or an entry inside an archive.
	 */
	class IRangeReader
	{
	public:
		virtual ~IRangeReader() = default;

		IRangeReader(const IRangeReader&) = delete;
		IRangeReader(IRangeReader&&)      = delete;
		IRangeReader&
		operator=(const IRangeReader&) = delete;
		IRangeReader&
		operator=(IRangeReader&&) = delete;

		[[nodiscard]] virtual uint64_t
		GetSize() const noexcept = 0;

		/** @throws std::runtime_error if the range is outside the source, or the read fails. */
		virtual void
		ReadAt(void* destination, uint64_t bytes, uint64_t offset) = 0;

		/**
		 * @throws std::runtime_error unless `[offset, offset + bytes)` lies inside the source.
		 *         Compares by subtraction: a crafted offset near UINT64_MAX would wrap past the sum.
		 */
		void
		CheckRange(uint64_t bytes, uint64_t offset) const;

	protected:
		/** @param what Container name the error messages are prefixed with, e.g. "bmesh". */
		explicit IRangeReader(std::string_view what) : m_What(what) {}

		std::string m_What;
	};
}
