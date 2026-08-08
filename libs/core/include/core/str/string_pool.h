#pragma once

namespace core
{
	/**
	 * A table of names held as one blob of NUL-terminated bytes and addressed by byte offset, which
	 * is the form a file stores them in -- a pool is written and read as its bytes, with no fixups.
	 *
	 * Offset 0 always reads as the empty string, so a zero-initialised reference means "unnamed"
	 * without a sentinel. Nothing is stored for an empty string, and the leading NUL that reserves
	 * offset 0 is written only once there is another name to distinguish it from, so a pool nothing
	 * has been added to stays empty and costs no bytes in the file.
	 */
	class string_pool
	{
	public:
		string_pool() = default;

		/** Adopts a pool's bytes as they were read from a file. */
		explicit string_pool(std::vector<char> bytes) noexcept : m_Bytes(std::move(bytes)) {}

		/** The offset `str` reads back at. An empty string is not stored; it is offset 0. */
		uint32_t
		add(std::string_view str)
		{
			if (str.empty())
				return 0;

			if (m_Bytes.empty())
				m_Bytes.push_back('\0');

			const auto offset = static_cast<uint32_t>(m_Bytes.size());
			m_Bytes.insert(m_Bytes.end(), str.begin(), str.end());
			m_Bytes.push_back('\0');
			return offset;
		}

		/**
		 * The name at `offset` -- empty for offset 0, and for an offset past the end.
		 *
		 * The scan for the terminator is bounded by the pool rather than trusting one to be there:
		 * these bytes come off disk, and a truncated pool would otherwise be read past.
		 */
		[[nodiscard]] std::string_view
		at(uint32_t offset) const noexcept
		{
			if (offset == 0 || offset >= m_Bytes.size())
				return {};

			const std::string_view tail(m_Bytes.data() + offset, m_Bytes.size() - offset);
			const size_t           end = tail.find('\0');
			return end == std::string_view::npos ? tail : tail.substr(0, end);
		}

		[[nodiscard]] const std::vector<char>&
		bytes() const noexcept
		{
			return m_Bytes;
		}

		[[nodiscard]] bool
		empty() const noexcept
		{
			return m_Bytes.empty();
		}

		void
		clear() noexcept
		{
			m_Bytes.clear();
		}

		friend bool
		operator==(const string_pool&, const string_pool&) = default;

	private:
		std::vector<char> m_Bytes;
	};
}
