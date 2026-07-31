#pragma once
#include <core/containers/slot_handle.h>

namespace bgl
{
	// A GPU descriptor handle is a uint2. D3D12 reads it as ordinary data under the ScalarDataLayout
	// an EntryBuffer<T> uses, so it is 4-aligned there, matching two bare uint32_t. Metal reads the
	// same eight bytes as a device pointer or an MTLResourceID, which MSL aligns to 8 -- so the
	// mirror has to as well, or every member after one in a GPU struct lands somewhere else.
#if defined(RENDERER_BACKEND_METAL)
	class alignas(8) DescriptorHandle
#else
	class DescriptorHandle
#endif
	{
	public:
		DescriptorHandle() = default;
		explicit DescriptorHandle(uint32_t hi, uint32_t lo) : m_Hi(hi), m_Lo(lo) {}
		explicit DescriptorHandle(uint32_t hi) : m_Hi(hi) {}
		explicit DescriptorHandle(core::slot_handle slot) : DescriptorHandle(slot.index) {}

		// The eight bytes verbatim, for a backend whose shader reads them as one 64-bit value rather
		// than as a pair. Byte order is the machine's, which is the only order the GPU reads them in;
		// the hi/lo member names describe the D3D12 pair and do not survive this view.
		[[nodiscard]] static DescriptorHandle
		FromNative(uint64_t native) noexcept
		{
			DescriptorHandle handle;
			std::memcpy(&handle, &native, sizeof(native));
			return handle;
		}

	private:
		uint32_t m_Hi = 0;
		uint32_t m_Lo = 0;
	};
}
