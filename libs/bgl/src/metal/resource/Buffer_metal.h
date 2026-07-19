#pragma once
#include "metal_cpp.h"

namespace bgl
{
	// The Metal definition of the RHI's forward-declared `Buffer`. A GPU-private structured buffer;
	// its bindless slot index (from the ResourceManager's pool) is what a handle carries.
	class Buffer
	{
	public:
		Buffer() = default;

		Buffer(MTL::Device* device, uint64_t byteSize, std::string_view debugName)
		{
			m_Buffer = NS::TransferPtr(
				device->newBuffer(byteSize, MTL::ResourceStorageModePrivate));
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

	private:
		NS::SharedPtr<MTL::Buffer> m_Buffer;
		uint64_t                   m_ByteSize = 0;
	};
}
