#pragma once
#include <algorithm>
#include <core/err/util.h>
#include <core/math.h>
#include <core/type_traits.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace core::io
{
	/** Append-only little-endian byte buffer. */
	class ByteWriter
	{
	public:
		[[nodiscard]] size_t
		Size() const noexcept
		{
			return m_Buffer.size();
		}

		void
		WriteBytes(std::span<const std::byte> bytes)
		{
			m_Buffer.insert(m_Buffer.end(), bytes.begin(), bytes.end());
		}

		template <core::type_traits::trivially_copyable T>
		void
		WritePod(const T& value)
		{
			const auto* first = reinterpret_cast<const std::byte*>(&value);
			m_Buffer.insert(m_Buffer.end(), first, first + sizeof(T));
		}

		template <core::type_traits::trivially_copyable T>
		void
		WritePodArray(std::span<const T> values)
		{
			const auto* first = reinterpret_cast<const std::byte*>(values.data());
			m_Buffer.insert(m_Buffer.end(), first, first + values.size_bytes());
		}

		/** A uint32 length, then the characters: what ByteReader::ReadString reads back. */
		void
		WriteString(std::string_view value)
		{
			WritePod<uint32_t>(static_cast<uint32_t>(value.size()));
			WriteBytes(std::as_bytes(std::span<const char>(value.data(), value.size())));
		}

		/** A uint32 length, then the bytes: what ByteReader::ReadBlob reads back. */
		void
		WriteBlob(std::span<const std::byte> value)
		{
			WritePod<uint32_t>(static_cast<uint32_t>(value.size()));
			WriteBytes(value);
		}

		void
		AlignTo(size_t alignment)
		{
			m_Buffer.resize(core::align(m_Buffer.size(), alignment), std::byte{ 0 });
		}

		template <core::type_traits::trivially_copyable T>
		void
		PatchPod(size_t offset, const T& value)
		{
			core::ensure(
				offset + sizeof(T) <= m_Buffer.size(),
				"offset + sizeof(T) <= m_Buffer.size()");
			const auto* first = reinterpret_cast<const std::byte*>(&value);
			std::copy_n(first, sizeof(T), m_Buffer.begin() + static_cast<ptrdiff_t>(offset));
		}

		[[nodiscard]] std::vector<std::byte>
		Take() noexcept
		{
			return std::move(m_Buffer);
		}

	private:
		std::vector<std::byte> m_Buffer;
	};
}
