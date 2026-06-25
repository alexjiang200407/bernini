#pragma once
#include "resource/Buffer_d3d12.h"
#include "resource/Dsv_d3d12.h"
#include "resource/ReadbackBuffer_d3d12.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv_d3d12.h"
#include "resource/Texture_d3d12.h"
#include <core/containers/slot_vector.h>

namespace bgl
{
	using CbvSrvUavSlot = std::variant<Buffer>;

	struct PendingDeletion
	{
		enum class Type
		{
			kCbvSrvUav,
			kRtv,
			kDsv,
			kTexture,
			kReadback,
		};

		Type     type       = Type::kCbvSrvUav;
		uint32_t slotIndex  = 0xFFFFFFFF;
		uint64_t fenceValue = 0;
	};

	class ResourceManager final : public core::RefCounter<IResourceManager>
	{
	public:
		ResourceManager(wrl::ComPtr<ID3D12Device> device, const ResourceManagerDesc& desc);

		ResourceManager(const ResourceManager&)     = delete;
		ResourceManager(ResourceManager&&) noexcept = delete;

		ResourceManager&
		operator=(const ResourceManager&) = delete;

		ResourceManager&
		operator=(ResourceManager&&) noexcept = delete;

		/**
		 * Automatically creates SRV/UAV for the buffer.
		 */
		[[nodiscard]]
		BufferHandle
		CreateStructBuffer(const StructBufferDesc& desc) noexcept override;

		/**
		 * Automatically creates SRV/UAV for the texture.
		 */
		[[nodiscard]]
		TextureHandle
		CreateTexture(const TextureDesc& desc) noexcept override;

		[[nodiscard]]
		ReadbackBufferHandle
		CreateReadbackBuffer(const ReadbackBufferDesc& desc) noexcept override;

		/**
		 * Assume that desc is a correct descriptor for d3d12 resource.
		 * Does not create SRV/UAV for the texture.
		 */
		[[nodiscard]]
		TextureHandle
		CreateTexture(wrl::ComPtr<ID3D12Resource> d3d12Texture, const TextureDesc& desc) noexcept;

		[[nodiscard]]
		RtvHandle
		CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) noexcept override;

		void
		DestroyRtv(RtvHandle handle, uint64_t currentFenceValue, bool deferred) noexcept override;

		void
		DestroyBuffer(BufferHandle handle, uint64_t currentFenceValue, bool deferred) noexcept
			override;

		void
		DestroyTexture(TextureHandle handle, uint64_t currentFenceValue, bool deferred) noexcept
			override;

		void
		DestroyReadbackBuffer(
			ReadbackBufferHandle handle,
			uint64_t             currentFenceValue,
			bool                 deferred) noexcept override;

		void
		CleanupExpiredResources(uint64_t completedFenceValue) noexcept override;

		[[nodiscard]]
		bool
		ValidBufferHandle(const BufferHandle& handle) const noexcept override;

		[[nodiscard]]
		bool
		ValidTextureHandle(const TextureHandle& handle) const noexcept override;

		[[nodiscard]]
		bool
		ValidReadbackBufferHandle(const ReadbackBufferHandle& handle) const noexcept override;

		[[nodiscard]]
		bool
		ValidRtvHandle(const RtvHandle& handle) const noexcept override;

		void
		SetDescriptorHeap(ID3D12GraphicsCommandList* cmdList) noexcept;

		const Texture&
		GetTexture(TextureHandle handle) const noexcept override;

		const Buffer&
		GetBuffer(BufferHandle handle) const noexcept override;

		const ReadbackBuffer&
		GetReadbackBuffer(ReadbackBufferHandle handle) const noexcept override;

		TextureReadbackLayout
		GetTextureReadbackLayout(TextureHandle handle) const noexcept override;

		const void*
		MapReadback(ReadbackBufferHandle handle) noexcept override;

		void
		UnmapReadback(ReadbackBufferHandle handle) noexcept override;

		const Rtv&
		GetRtv(RtvHandle handle) const noexcept override;

		ID3D12DescriptorHeap*
		GetCbvSrvUavHeap() const noexcept
		{
			return m_CbvSrvUavHeap.Get();
		}

		ID3D12DescriptorHeap*
		GetRtvHeap() const noexcept
		{
			return m_RtvHeap.Get();
		}

		wrl::ComPtr<ID3D12Device>
		GetD3D12DeviceCpy() const noexcept
		{
			return m_Device;
		}

		void
		ClearRtv(ICommandList* cmdList, RtvHandle handle, float clearVal[4]) noexcept override;

		DsvHandle
		CreateDsv(TextureHandle textureHandle, const DsvDesc& desc) noexcept override;

		const Dsv&
		GetDsv(DsvHandle handle) const noexcept override;

		bool
		ValidDsvHandle(const DsvHandle& handle) const noexcept override;

		void
		ClearDsv(ICommandList* cmdList, DsvHandle handle, float depth, uint8_t stencil) noexcept
			override;

		void
		DestroyDsv(DsvHandle handle, uint64_t currentFenceValue, bool deferred) noexcept override;

	private:
		ResourceManagerDesc               m_Desc;
		wrl::ComPtr<ID3D12Device>         m_Device;
		wrl::ComPtr<ID3D12DescriptorHeap> m_CbvSrvUavHeap;
		wrl::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
		wrl::ComPtr<ID3D12DescriptorHeap> m_DsvHeap;
		core::slot_vector<CbvSrvUavSlot>  m_CbvSrvUavSlots;

		// CPU side only
		core::slot_vector<Texture> m_Textures;

		core::slot_vector<ReadbackBuffer> m_ReadbackBuffers;

		core::slot_vector<Rtv>       m_Rtvs;
		core::slot_vector<Dsv>       m_Dsvs;
		std::vector<PendingDeletion> m_PendingDeletions;

		friend class DeviceImpl;
	};
}
