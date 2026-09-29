#pragma once
#include <algorithm>
#include <core/err/util.h>
#include <core/type_traits.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace core::io
{
	/** Bounds-checked forward cursor over a byte stream. Throws std::runtime_error on overrun. */
	class ByteReader
	{
	public:
		explicit ByteReader(std::span<const std::byte> bytes) noexcept : m_Bytes(bytes) {}

		[[nodiscard]] size_t
		Remaining() const noexcept
		{
			return m_Bytes.size() - m_Cursor;
		}

		[[nodiscard]] std::span<const std::byte>
		ReadBytes(size_t count)
		{
			if (count > Remaining())
				core::throw_runtime_error("byte stream: unexpected end of stream");
			const auto out = m_Bytes.subspan(m_Cursor, count);
			m_Cursor += count;
			return out;
		}

		template <core::type_traits::trivially_copyable T>
		[[nodiscard]] T
		ReadPod()
		{
			const auto raw = ReadBytes(sizeof(T));
			T          value;
			std::copy_n(raw.data(), sizeof(T), reinterpret_cast<std::byte*>(&value));
			return value;
		}

		/** What ByteWriter::WriteString wrote: a uint32 length, then the characters. */
		[[nodiscard]] std::string
		ReadString()
		{
			const auto size = ReadPod<uint32_t>();
			const auto raw  = ReadBytes(size);
			return std::string(reinterpret_cast<const char*>(raw.data()), raw.size());
		}

		/** What ByteWriter::WriteBlob wrote: a uint32 length, then the bytes. */
		[[nodiscard]] std::vector<std::byte>
		ReadBlob()
		{
			const auto size = ReadPod<uint32_t>();
			const auto raw  = ReadBytes(size);
			return std::vector<std::byte>(raw.begin(), raw.end());
		}

		void
		Seek(size_t offset)
		{
			if (offset > m_Bytes.size())
				core::throw_runtime_error("byte stream: seek out of range");
			m_Cursor = offset;
		}

	private:
		std::span<const std::byte> m_Bytes;
		size_t                     m_Cursor = 0;
	};
}
