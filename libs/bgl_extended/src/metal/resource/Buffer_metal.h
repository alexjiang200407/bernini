#pragma once
#include "metal_cpp.h"
#include "resource/Buffer.h"

#include <core/profiling/TaggedBytes.h>

namespace bgl
{
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
			gassert(m_Buffer.get() != nullptr, "Metal buffer allocation failed");

			m_Tracked = core::profiling::TaggedBytes(
				core::profiling::MemoryTag::kDeviceBuffer,
				desc.byteSize);
			if (!desc.debugName.empty())
			{
				m_Buffer->setLabel(
					NS::String::string(desc.debugName.c_str(), NS::UTF8StringEncoding));
			}
		}

		[[nodiscard]] MTL::Buffer*
		GetMTLResource() const noexcept
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

		// The size asked for, not the driver's: alignment padding differs per backend.
		core::profiling::TaggedBytes m_Tracked;
	};
}
