#include "d3d12/resource/DescriptorAllocator_d3d12.h"
#include <bgpu/constants/constants.h>
#include <core/err/util.h>
#include <cstdint>
#include <directx/d3d12.h>

namespace bgpu
{
	DescriptorAllocator::DescriptorAllocator(
		ID3D12Device*               device,
		D3D12_DESCRIPTOR_HEAP_TYPE  type,
		uint32_t                    capacity,
		D3D12_DESCRIPTOR_HEAP_FLAGS flags) : m_Indices(capacity)
	{
		core::ensure(device != nullptr, "DescriptorAllocator requires a device");

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type                       = type;
		heapDesc.NumDescriptors             = capacity;
		heapDesc.Flags                      = flags;
		device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_Heap)) >> d3d12ErrChecker;

		m_HeapStart     = m_Heap->GetCPUDescriptorHandleForHeapStart();
		m_IncrementSize = device->GetDescriptorHandleIncrementSize(type);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE
	DescriptorAllocator::GetCpuHandle(uint32_t index) const noexcept
	{
		core::ensure(index < GetCapacity(), "DescriptorAllocator: index out of range");
		return { m_HeapStart.ptr + static_cast<SIZE_T>(index) * m_IncrementSize };
	}
}
