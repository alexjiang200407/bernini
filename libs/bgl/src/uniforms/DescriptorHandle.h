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

	private:
		uint32_t m_Hi = 0;
		uint32_t m_Lo = 0;
	};
}
