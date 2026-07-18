#pragma once
#include <core/containers/slot_handle.h>

namespace bgl
{
	// A GPU descriptor handle is a uint2. Under the ScalarDataLayout an EntryBuffer<T> uses, it is
	// 4-aligned, matching two bare uint32_t here; idlgen reflects that same layout, so the generated
	// offsetof/sizeof asserts pin the two together.
	class DescriptorHandle
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
