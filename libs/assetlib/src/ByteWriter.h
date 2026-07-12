#pragma once
#include <core/math.h>
#include <core/type_traits.h>

namespace assetlib
{
	/** Append-only little-endian byte buffer used to build the container stream. */
	class ByteWriter
	{
	public:
		[[nodiscard]] size_t
		size() const noexcept
		{
			return m_Buffer.size();
		}

		void
		writeBytes(std::span<const std::byte> bytes)
		{
			m_Buffer.insert(m_Buffer.end(), bytes.begin(), bytes.end());
		}

		template <core::type_traits::trivially_copyable T>
		void
		writePod(const T& value)
		{
			const auto* first = reinterpret_cast<const std::byte*>(&value);
			m_Buffer.insert(m_Buffer.end(), first, first + sizeof(T));
		}

		template <core::type_traits::trivially_copyable T>
		void
		writePodArray(std::span<const T> values)
		{
			const auto* first = reinterpret_cast<const std::byte*>(values.data());
			m_Buffer.insert(m_Buffer.end(), first, first + values.size_bytes());
		}

		void
		alignTo(size_t alignment)
		{
			m_Buffer.resize(core::align(m_Buffer.size(), alignment), std::byte{ 0 });
		}

		template <core::type_traits::trivially_copyable T>
		void
		patchPod(size_t offset, const T& value)
		{
			assert(offset + sizeof(T) <= m_Buffer.size());
			const auto* first = reinterpret_cast<const std::byte*>(&value);
			std::copy_n(first, sizeof(T), m_Buffer.begin() + static_cast<ptrdiff_t>(offset));
		}

		[[nodiscard]] std::vector<std::byte>
		take() noexcept
		{
			return std::move(m_Buffer);
		}

	private:
		std::vector<std::byte> m_Buffer;
	};
}
