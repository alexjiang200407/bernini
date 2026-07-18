#pragma once
#include <core/containers/slot_handle.h>

namespace bgl
{
	// alignas(8): a GPU descriptor handle is a uint2 the shader aligns to 8, so an embedding struct
	// (e.g. PbrMaterial) pads to match. Two bare uint32_t would be align-4 and shrink the C++ mirror
	// below the GPU stride, so the generated offsetof/sizeof asserts would fail.
	class alignas(8) DescriptorHandle
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
