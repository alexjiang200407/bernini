#pragma once
#include "metal_cpp.h"

#include "resource/Readback.h"

namespace bgl
{
	// The Metal definition of the RHI's forward-declared `ReadbackBuffer`. Shared storage so the CPU
	// reads it directly: on Apple Silicon's unified memory `contents()` is the mapping, valid for the
	// buffer's whole lifetime -- there is no Map/Unmap round trip and so no mapped pointer to cache,
	// unlike the D3D12 readback heap.
	class ReadbackBuffer
	{
	public:
		ReadbackBuffer() = default;

		ReadbackBuffer(MTL::Device* device, const ReadbackBufferDesc& desc) : m_Desc(desc)
		{
			m_Buffer =
				NS::TransferPtr(device->newBuffer(desc.byteSize, MTL::ResourceStorageModeShared));
			if (!desc.debugName.empty())
			{
				m_Buffer->setLabel(
					NS::String::string(desc.debugName.c_str(), NS::UTF8StringEncoding));
			}
		}

		[[nodiscard]] MTL::Buffer*
		GetMetalBuffer() const noexcept
		{
			return m_Buffer.get();
		}

		[[nodiscard]] const ReadbackBufferDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]] const void*
		GetData() const noexcept
		{
			return m_Buffer->contents();
		}

	private:
		ReadbackBufferDesc         m_Desc;
		NS::SharedPtr<MTL::Buffer> m_Buffer;
	};
}
