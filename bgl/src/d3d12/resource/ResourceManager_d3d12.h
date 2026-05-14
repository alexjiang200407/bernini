#pragma once
#include "resource/Buffer_d3d12.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv_d3d12.h"
#include "resource/Texture_d3d12.h"
#include <core/containers/slot_vector.h>
#include <variant>

namespace bgl
{
	using CbvSrvUavSlot = std::variant<Buffer, Texture>;

	struct PendingDeletion
	{
		enum class Type
		{
			kCbvSrvUav,
			kRtv,
		};

		Type     type       = Type::kCbvSrvUav;
		uint32_t slotIndex  = 0xFFFFFFFF;
		uint64_t fenceValue = 0;
	};

	class ResourceManagerImpl final
	{
	public:
		ResourceManagerImpl(
			wrl::ComPtr<ID3D12Device> device,
			uint32_t                  maxDescriptors,
			uint32_t                  maxRtvs);

		~ResourceManagerImpl() noexcept;

		/**
		 * Automatically creates SRV/UAV for the buffer.
		 */
		[[nodiscard]]
		BufferHandle
		CreateRawBuffer(const BufferDesc& desc);

		/**
		 * Automatically creates SRV/UAV for the texture.
		 */
		[[nodiscard]]
		TextureHandle
		CreateTexture(const TextureDesc& desc);

		/**
		 * Assume that desc is a correct descriptor for d3d12 resource.
		 * Does not create SRV/UAV for the texture.
		 */
		[[nodiscard]]
		TextureHandle
		CreateTexture(wrl::ComPtr<ID3D12Resource> d3d12Texture, const TextureDesc& desc);

		[[nodiscard]]
		RtvHandle
		CreateRtv(TextureHandle textureHandle, const RtvDesc& desc);

		void
		DestroyRtv(RtvHandle handle, uint64_t currentFenceValue);

		void
		DestroyBuffer(BufferHandle handle, uint64_t currentFenceValue);

		void
		DestroyTexture(TextureHandle handle, uint64_t currentFenceValue);

		void
		CleanupExpiredResources(uint64_t completedFenceValue);

		[[nodiscard]]
		bool
		ValidBufferHandle(const BufferHandle& handle) const;

		[[nodiscard]]
		bool
		ValidTextureHandle(const TextureHandle& handle) const;

		void
		SetDescriptorHeap(ID3D12CommandList* cmdList);

		const Texture&
		GetTexture(TextureHandle handle) const;

		const Buffer&
		GetBuffer(BufferHandle handle) const;

		const Rtv&
		GetRtv(RtvHandle handle) const;

		ID3D12DescriptorHeap*
		GetCbvSrvUavHeap() const
		{
			return m_CbvSrvUavHeap.Get();
		}

		ID3D12DescriptorHeap*
		GetRtvHeap() const
		{
			return m_RtvHeap.Get();
		}

		[[nodiscard]]
		bool
		ValidRtvHandle(const RtvHandle& handle) const;

	private:
		wrl::ComPtr<ID3D12Device>              m_Device;
		wrl::ComPtr<ID3D12DescriptorHeap>      m_CbvSrvUavHeap;
		wrl::ComPtr<ID3D12DescriptorHeap>      m_RtvHeap;
		wrl::ComPtr<ID3D12GraphicsCommandList> m_CommandList;
		uint32_t                               m_CbvSrvUavDescriptorSize = 0;
		uint32_t                               m_RtvDescriptorSize       = 0;
		core::slot_vector<CbvSrvUavSlot>       m_CbvSrvUavSlots;
		core::slot_vector<Rtv>                 m_Rtvs;
		std::vector<PendingDeletion>           m_PendingDeletions;

		friend class DeviceImpl;
	};
}
