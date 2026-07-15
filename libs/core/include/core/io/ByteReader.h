#pragma once
#include <core/type_traits.h>

namespace core::io
{
	/** Bounds-checked forward cursor over a byte stream. Throws std::runtime_error on overrun. */
	class ByteReader
	{
	public:
		explicit ByteReader(std::span<const std::byte> bytes) noexcept : m_Bytes(bytes) {}

		[[nodiscard]] size_t
		remaining() const noexcept
		{
			return m_Bytes.size() - m_Cursor;
		}

		[[nodiscard]] std::span<const std::byte>
		readBytes(size_t count)
		{
			if (count > remaining())
				throw std::runtime_error("byte stream: unexpected end of stream");
			const auto out = m_Bytes.subspan(m_Cursor, count);
			m_Cursor += count;
			return out;
		}

		template <core::type_traits::trivially_copyable T>
		[[nodiscard]] T
		readPod()
		{
			const auto raw = readBytes(sizeof(T));
			T          value;
			std::copy_n(raw.data(), sizeof(T), reinterpret_cast<std::byte*>(&value));
			return value;
		}

		void
		seek(size_t offset)
		{
			if (offset > m_Bytes.size())
				throw std::runtime_error("byte stream: seek out of range");
			m_Cursor = offset;
		}

	private:
		std::span<const std::byte> m_Bytes;
		size_t                     m_Cursor = 0;
	};
}
