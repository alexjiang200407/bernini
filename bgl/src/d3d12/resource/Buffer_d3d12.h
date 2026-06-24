#pragma once
#include "resource/Buffer.h"
#include "util_d3d12.h"

namespace bgl
{
	class Buffer final
	{
	public:
		Buffer() = default;

		Buffer(
			ID3D12Device*           device,
			ID3D12DescriptorHeap*   descriptorHeap,
			uint32_t                descriptorIndex,
			const StructBufferDesc& desc);

		~Buffer() noexcept = default;

		Buffer(const Buffer&)     = delete;
		Buffer(Buffer&&) noexcept = default;

		Buffer&
		operator=(const Buffer&) = delete;

		Buffer&
		operator=(Buffer&&) noexcept = default;

		[[nodiscard]]
		ID3D12Resource*
		GetD3D12Resource() const
		{
			return m_Buffer.Get();
		}

		[[nodiscard]]
		const StructBufferDesc&
		GetDesc() const
		{
			return m_Desc;
		}

		[[nodiscard]]
		D3D12_CPU_DESCRIPTOR_HANDLE
		GetCpuHandle() const
		{
			return m_CpuHandle;
		}

		[[nodiscard]]
		bool
		IsNull() const
		{
			return m_Buffer == nullptr;
		}

	private:
		StructBufferDesc            m_Desc;
		uint32_t                    m_DescriptorIndex = 0xFFFFFFFF;
		D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle       = {};
		wrl::ComPtr<ID3D12Resource> m_Buffer;
	};
}
