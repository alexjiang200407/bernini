#pragma once
#include "metal_cpp.h"

namespace bgl
{
	struct BufferDesc
	{
		uint64_t    byteSize  = 0;
		bool        isUav     = false;
		std::string debugName = "Unnamed Buffer";
	};

	// The Metal definition of the RHI's forward-declared `Buffer`. A GPU-private structured buffer;
	// its bindless slot index (from the ResourceManager's pool) is what a handle carries.
	class Buffer
	{
	public:
		Buffer() = default;

		Buffer(MTL::Device* device, const BufferDesc& desc) : m_Desc(desc)
		{
			m_Buffer =
				NS::TransferPtr(device->newBuffer(desc.byteSize, MTL::ResourceStorageModePrivate));
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

		[[nodiscard]] const BufferDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

	private:
		BufferDesc                 m_Desc;
		NS::SharedPtr<MTL::Buffer> m_Buffer;
	};
}
