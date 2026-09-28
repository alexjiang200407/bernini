#include "cmd/TimestampHeap_d3d12.h"
#include "cmd/TimestampHeap.h"
#include "resource/Readback.h"
#include "resource/ReadbackBuffer_d3d12.h"
#include <cstdint>
#include <cstring>
#include <span>

namespace bgl
{
	TimestampHeap::TimestampHeap(ID3D12Device* device, uint32_t capacity) : m_Capacity(capacity)
	{
		gassert(device != nullptr, "Device cannot be null");
		gassert(capacity > 0, "A timestamp heap needs at least one slot");

		D3D12_QUERY_HEAP_DESC desc = {};
		desc.Type                  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		desc.Count                 = capacity;
		device->CreateQueryHeap(&desc, IID_PPV_ARGS(&m_Heap)) >> d3d12ErrChecker;
		m_Heap->SetName(L"Timestamp Heap");

		auto rbDesc      = ReadbackBufferDesc();
		rbDesc.byteSize  = static_cast<uint64_t>(capacity) * sizeof(uint64_t);
		rbDesc.debugName = "Timestamp Readback";
		m_Readback       = ReadbackBuffer(device, rbDesc);
	}

	void
	TimestampHeap::Read(uint32_t first, std::span<uint64_t> out) const noexcept
	{
		gassert(first + out.size() <= m_Capacity, "Timestamp read outside the heap");

		if (out.empty())
		{
			return;
		}

		const auto* mapped = static_cast<const uint64_t*>(m_Readback.Map());
		gassert(mapped != nullptr, "Failed to map the timestamp readback");
		std::memcpy(out.data(), mapped + first, out.size() * sizeof(uint64_t));
		m_Readback.Unmap();
	}
}
