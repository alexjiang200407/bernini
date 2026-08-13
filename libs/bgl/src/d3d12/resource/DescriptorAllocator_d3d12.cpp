#include "d3d12/resource/DescriptorAllocator_d3d12.h"
#include "constants/constants.h"
#include <core/err/util.h>

namespace bgl
{
	DescriptorAllocator::DescriptorAllocator(
		ID3D12Device*               device,
		D3D12_DESCRIPTOR_HEAP_TYPE  type,
		uint32_t                    capacity,
		D3D12_DESCRIPTOR_HEAP_FLAGS flags) : m_Capacity(capacity), m_Allocated(capacity, false)
	{
		gassert(device != nullptr, "DescriptorAllocator requires a device");
		gassert(capacity > 0, "DescriptorAllocator requires a non-zero capacity");

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type                       = type;
		heapDesc.NumDescriptors             = capacity;
		heapDesc.Flags                      = flags;
		device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_Heap)) >> d3d12ErrChecker;

		m_HeapStart     = m_Heap->GetCPUDescriptorHandleForHeapStart();
		m_IncrementSize = device->GetDescriptorHandleIncrementSize(type);
		m_FreeIndices.reserve(capacity);

		// c_UnboundDescriptorIndex is never handed out, so a zero-filled uniform mirror addresses a
		// descriptor no resource occupies rather than the first one allocated. Marked allocated so
		// Free would assert rather than release it into the free list.
		gassert(capacity > c_UnboundDescriptorIndex, "The heap must have room for the sentinel");
		m_NextUntouched                       = c_UnboundDescriptorIndex + 1;
		m_Allocated[c_UnboundDescriptorIndex] = true;
	}

	uint32_t
	DescriptorAllocator::Allocate()
	{
		if (!m_FreeIndices.empty())
		{
			const auto index = m_FreeIndices.back();
			m_FreeIndices.pop_back();
			m_Allocated[index] = true;
			return index;
		}

		core::throw_runtime_error_if(
			m_NextUntouched >= m_Capacity,
			"DescriptorAllocator: heap is full ({} descriptors)",
			m_Capacity);

		const auto index   = m_NextUntouched++;
		m_Allocated[index] = true;
		return index;
	}

	void
	DescriptorAllocator::Free(uint32_t index) noexcept
	{
		gassert(index < m_Capacity, "DescriptorAllocator: index out of range");
		gassert(m_Allocated[index], "DescriptorAllocator: freeing an index that is not allocated");

		m_Allocated[index] = false;
		m_FreeIndices.push_back(index);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE
	DescriptorAllocator::GetCpuHandle(uint32_t index) const noexcept
	{
		gassert(index < m_Capacity, "DescriptorAllocator: index out of range");
		return { m_HeapStart.ptr + static_cast<SIZE_T>(index) * m_IncrementSize };
	}
}
