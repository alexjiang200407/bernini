#pragma once
#include "cmd/TimestampHeap.h"
#include "resource/ReadbackBuffer_d3d12.h"
#include <core/ref/RefCounter.h>
#include <cstdint>
#include <span>

namespace bgl
{
	/**
	 * The RHI timestamp heap over an ID3D12QueryHeap of timestamp queries, with the readback buffer
	 * ResolveQueryData lands in beside it: a resolved slot is read straight out of that mapping.
	 */
	class TimestampHeap final : public core::RefCounter<ITimestampHeap>
	{
	public:
		TimestampHeap(ID3D12Device* device, uint32_t capacity);

		TimestampHeap(const TimestampHeap&) noexcept = delete;
		TimestampHeap(TimestampHeap&&) noexcept      = delete;

		TimestampHeap&
		operator=(const TimestampHeap&) noexcept = delete;

		TimestampHeap&
		operator=(TimestampHeap&&) noexcept = delete;

		[[nodiscard]] uint32_t
		GetCapacity() const noexcept override
		{
			return m_Capacity;
		}

		void
		Read(uint32_t first, std::span<uint64_t> out) const noexcept override;

		[[nodiscard]] ID3D12QueryHeap*
		GetD3D12QueryHeap() const noexcept
		{
			return m_Heap.Get();
		}

		[[nodiscard]] ID3D12Resource*
		GetD3D12Readback() const noexcept
		{
			return m_Readback.GetD3D12Resource();
		}

	private:
		wrl::ComPtr<ID3D12QueryHeap> m_Heap;
		// Mutable for Map: a readback mapping is a CPU-side view of memory the GPU has finished with.
		mutable ReadbackBuffer m_Readback;
		uint32_t               m_Capacity = 0;
	};
}
