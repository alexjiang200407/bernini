#pragma once
#include "resource/Readback.h"

namespace bgl
{
	// A buffer in the D3D12 readback heap: GPU-writable (as a copy destination) and
	// CPU-readable via Map(). The destination of GPU->CPU copies.
	class ReadbackBuffer final
	{
	public:
		ReadbackBuffer() = default;
		ReadbackBuffer(ID3D12Device* device, const ReadbackBufferDesc& desc);
		~ReadbackBuffer() noexcept;

		ReadbackBuffer(const ReadbackBuffer&) = delete;
		ReadbackBuffer(ReadbackBuffer&& other) noexcept;

		ReadbackBuffer&
		operator=(const ReadbackBuffer&) = delete;

		ReadbackBuffer&
		operator=(ReadbackBuffer&& other) noexcept;

		[[nodiscard]]
		ID3D12Resource*
		GetD3D12Resource() const
		{
			return m_Buffer.Get();
		}

		[[nodiscard]]
		uint64_t
		GetByteSize() const
		{
			return m_ByteSize;
		}

		[[nodiscard]]
		bool
		IsNull() const
		{
			return m_Buffer == nullptr;
		}

		// Maps the whole buffer for reading and returns the pointer (idempotent).
		const void*
		Map();

		void
		Unmap();

	private:
		void
		ReleaseMapping() noexcept;

		uint64_t                    m_ByteSize = 0;
		void*                       m_Mapped   = nullptr;
		wrl::ComPtr<ID3D12Resource> m_Buffer;
	};
}
