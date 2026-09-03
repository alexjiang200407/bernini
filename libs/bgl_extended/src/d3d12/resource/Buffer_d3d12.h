#pragma once
#include "convert_d3d12.h"
#include "resource/Buffer.h"

#include <core/profiling/TaggedBytes.h>

namespace bgl
{
	class Buffer final
	{
	public:
		Buffer() = default;

		Buffer(
			ID3D12Device*         device,
			ID3D12DescriptorHeap* descriptorHeap,
			uint32_t              descriptorIndex,
			const BufferDesc&     desc);

		~Buffer() noexcept = default;

		Buffer(const Buffer&)     = delete;
		Buffer(Buffer&&) noexcept = default;

		Buffer&
		operator=(const Buffer&) = delete;

		Buffer&
		operator=(Buffer&&) noexcept = default;

		[[nodiscard]]
		ID3D12Resource*
		GetD3D12Resource() const noexcept
		{
			return m_Buffer.Get();
		}

		[[nodiscard]]
		const BufferDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]]
		D3D12_CPU_DESCRIPTOR_HANDLE
		GetCpuHandle() const noexcept
		{
			return m_CpuHandle;
		}

		[[nodiscard]]
		uint32_t
		GetDescriptorIndex() const noexcept
		{
			return m_DescriptorIndex;
		}

		[[nodiscard]]
		bool
		IsNull() const noexcept
		{
			return m_Buffer == nullptr;
		}

	private:
		BufferDesc                  m_Desc;
		uint32_t                    m_DescriptorIndex = 0xFFFFFFFF;
		D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle       = {};
		wrl::ComPtr<ID3D12Resource> m_Buffer;

		// The requested size, not the driver's padded allocation: a buffer is reasoned about as the
		// bytes that were asked for, and per-backend alignment would make the two platforms'
		// reports incomparable. A texture is the other way round -- see Texture_d3d12.cpp.
		core::profiling::TaggedBytes m_Tracked;
	};
}
