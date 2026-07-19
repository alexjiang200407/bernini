#pragma once
#include "metal_cpp.h"

namespace bgl
{
	// The Metal definition of the RHI's forward-declared `ReadbackBuffer`. Shared storage so the CPU
	// reads it directly -- on Apple Silicon's unified memory `contents()` is the map, no copy.
	class ReadbackBuffer
	{
	public:
		ReadbackBuffer() = default;

		ReadbackBuffer(MTL::Device* device, uint64_t byteSize, std::string_view debugName)
		{
			m_Buffer =
				NS::TransferPtr(device->newBuffer(byteSize, MTL::ResourceStorageModeShared));
			if (!debugName.empty())
			{
				m_Buffer->setLabel(
					NS::String::string(std::string(debugName).c_str(), NS::UTF8StringEncoding));
			}
			m_ByteSize = byteSize;
		}

		[[nodiscard]] MTL::Buffer*
		Handle() const noexcept
		{
			return m_Buffer.get();
		}

		[[nodiscard]] uint64_t
		ByteSize() const noexcept
		{
			return m_ByteSize;
		}

		[[nodiscard]] const void*
		Contents() const noexcept
		{
			return m_Buffer->contents();
		}

	private:
		NS::SharedPtr<MTL::Buffer> m_Buffer;
		uint64_t                   m_ByteSize = 0;
	};
}
