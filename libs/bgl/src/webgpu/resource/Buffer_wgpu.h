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

		Buffer(const wgpu::Device& device, const BufferDesc& desc);

		Buffer(const Buffer&)     = delete;
		Buffer(Buffer&&) noexcept = default;
		Buffer&
		operator=(const Buffer&) = delete;
		Buffer&
		operator=(Buffer&&) noexcept = default;

		[[nodiscard]] const wgpu::Buffer&
		GetHandle() const noexcept
		{
			return m_Buffer;
		}

		[[nodiscard]] uint64_t
		GetByteSize() const noexcept
		{
			return m_Desc.byteSize;
		}

		[[nodiscard]] const BufferDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return m_Buffer == nullptr;
		}

	private:
		wgpu::Buffer m_Buffer = nullptr;
		BufferDesc   m_Desc;
	};
}
