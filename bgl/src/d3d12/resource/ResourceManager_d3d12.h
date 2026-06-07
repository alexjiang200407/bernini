#pragma once
#include "resource/Buffer_d3d12.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv_d3d12.h"
#include "resource/Texture_d3d12.h"
#include <core/containers/slot_vector.h>

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

	class ResourceManager final : public core::RefCounter<IResourceManager>
	{
	public:
		ResourceManager(
			wrl::ComPtr<ID3D12Device> device,
			uint32_t                  maxDescriptors,
			uint32_t                  maxRtvs);

		/**
		 * Automatically creates SRV/UAV for the buffer.
		 */
		[[nodiscard]]
		BufferHandle
		CreateRawBuffer(const BufferDesc& desc) override;

		/**
		 * Automatically creates SRV/UAV for the texture.
		 */
		[[nodiscard]]
		TextureHandle
		CreateTexture(const TextureDesc& desc) override;

		/**
		 * Assume that desc is a correct descriptor for d3d12 resource.
		 * Does not create SRV/UAV for the texture.
		 */
		[[nodiscard]]
		TextureHandle
		CreateTexture(wrl::ComPtr<ID3D12Resource> d3d12Texture, const TextureDesc& desc);

		[[nodiscard]]
		RtvHandle
		CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) override;

		void
		DestroyRtv(RtvHandle handle, uint64_t currentFenceValue, bool deferred) override;

		void
		DestroyBuffer(BufferHandle handle, uint64_t currentFenceValue, bool deferred) override;

		void
		DestroyTexture(TextureHandle handle, uint64_t currentFenceValue, bool deferred) override;

		void
		CleanupExpiredResources(uint64_t completedFenceValue) override;

		[[nodiscard]]
		bool
		ValidBufferHandle(const BufferHandle& handle) const override;

		[[nodiscard]]
		bool
		ValidTextureHandle(const TextureHandle& handle) const override;

		[[nodiscard]]
		bool
		ValidRtvHandle(const RtvHandle& handle) const override;

		void
		SetDescriptorHeap(ID3D12GraphicsCommandList* cmdList);

		const Texture&
		GetTexture(TextureHandle handle) const override;

		const Buffer&
		GetBuffer(BufferHandle handle) const override;

		const Rtv&
		GetRtv(RtvHandle handle) const override;

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

	private:
		wrl::ComPtr<ID3D12Device>         m_Device;
		wrl::ComPtr<ID3D12DescriptorHeap> m_CbvSrvUavHeap;
		wrl::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
		uint32_t                          m_CbvSrvUavDescriptorSize = 0;
		uint32_t                          m_RtvDescriptorSize       = 0;
		core::slot_vector<CbvSrvUavSlot>  m_CbvSrvUavSlots;
		core::slot_vector<Rtv>            m_Rtvs;
		std::vector<PendingDeletion>      m_PendingDeletions;

		friend class DeviceImpl;
	};
}
