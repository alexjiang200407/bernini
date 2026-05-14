#pragma once
#include "resource/Buffer.h"
#include "util.h"

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

		~Buffer() noexcept;

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
		const BufferDesc&
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

		[[nodiscard]]
		void*
		GetMappedPtr() const
		{
			return m_MappedPtr;
		}

	private:
		BufferDesc                  m_Desc;
		uint32_t                    m_DescriptorIndex = 0xFFFFFFFF;
		D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle       = {};
		void*                       m_MappedPtr = nullptr;  // TODO: Synchronization is required
		wrl::ComPtr<ID3D12Resource> m_Buffer;
	};
}
