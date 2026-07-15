#pragma once
#include "resource/Buffer.h"
#include "convert_d3d12.h"

namespace bgl
{
	struct BufferDesc
	{
		uint64_t    byteSize  = 0;
		bool        isUav     = false;
		std::string debugName = "Unnamed Buffer";
	};

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
	};
}
