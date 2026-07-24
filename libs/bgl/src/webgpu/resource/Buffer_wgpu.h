#pragma once

#include "resource/Buffer.h"

namespace bgl
{
	struct BufferDesc
	{
		uint64_t    byteSize  = 0;
		bool        isUav     = false;
		std::string debugName = "Unnamed Buffer";
	};

	class Buffer final
	{
	public:
		Buffer() = default;

		Buffer(WGPUDevice device, const BufferDesc& desc);

		~Buffer() noexcept;

		Buffer(const Buffer&) = delete;
		Buffer(Buffer&& other) noexcept;

		Buffer&
		operator=(const Buffer&) = delete;

		Buffer&
		operator=(Buffer&& other) noexcept;

		[[nodiscard]] WGPUBuffer
		GetHandle() const noexcept
		{
			return m_Buffer;
		}

		[[nodiscard]] uint64_t
		GetByteSize() const noexcept
		{
			return m_ByteSize;
		}

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return m_Buffer == nullptr;
		}

	private:
		WGPUBuffer m_Buffer   = nullptr;
		uint64_t   m_ByteSize = 0;
	};
}
