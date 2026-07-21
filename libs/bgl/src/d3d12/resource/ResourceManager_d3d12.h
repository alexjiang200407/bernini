#pragma once
#include "cmd/CommandQueue.h"
#include "resource/Buffer_d3d12.h"
#include "resource/Dsv_d3d12.h"
#include "resource/ReadbackBuffer_d3d12.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv_d3d12.h"
#include "resource/Sampler_d3d12.h"
#include "resource/Texture_d3d12.h"
#include <core/containers/slot_vector.h>
#include <core/containers/static_vector.h>

namespace bgl
{
	// Buffers and textures share the one shader-visible CBV_SRV_UAV heap: a slot's
	// index is the bindless descriptor index the shader uses. RT/DS-only textures
	// also take a slot (no SRV written) so every TextureHandle.idx is a heap index.
	using CbvSrvUavSlot = std::variant<Buffer, Texture>;

	// The most submission timelines that can gate one deferred free -- i.e. the most contexts
	// expected over one device. Exceeding it asserts; it is not a hard device limit.
	constexpr uint32_t c_MaxRegisteredQueues = 8;

	// One registered queue and the fence value it was at when a resource was retired against it.
	struct QueueGate
	{
		ICommandQueue* queue      = nullptr;
		uint64_t       fenceValue = 0;

		bool
		operator==(const QueueGate&) const noexcept = default;
	};

	using DeletionGate = core::static_vector<QueueGate, c_MaxRegisteredQueues>;

	enum class PendingType
	{
		kCbvSrvUav,
		kRtv,
		kDsv,
		kTexture,
		kReadback,
		kSampler,
	};

	struct PendingDeletion
	{
		PendingType type      = PendingType::kCbvSrvUav;
		uint32_t    slotIndex = 0xFFFFFFFF;
	};

	// Deferred destroys captured at the same gate share it: a burst of frees within one frame all
	// snapshot the same registered-queue fences, so they batch under one gate rather than each
	// carrying its own copy. Freed as a group once every queue in `gate` passes.
	struct PendingDeletionBatch
	{
		DeletionGate                 gate;
		std::vector<PendingDeletion> deletions;
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

		[[nodiscard]]
		BufferHandle
		CreateComputeBuffer(const ComputeBufferDesc& desc) noexcept override;

		/**
		 * Automatically creates SRV/UAV for the texture.
		 */
		[[nodiscard]]
		TextureHandle
		CreateTexture(const TextureDesc& desc) noexcept override;

		[[nodiscard]]
		TextureHandle
		CreateTexture(
			const TextureDesc&                      desc,
			std::span<const TextureSubresourceData> initialData) noexcept override;

		[[nodiscard]]
		TextureHandle
		CreateTexture(const assetlib::ImageData& image, std::string debugName = "") noexcept
			override;

		[[nodiscard]]
		SamplerHandle
		CreateSampler(const SamplerDesc& desc) noexcept override;

		[[nodiscard]]
		TextureHandle
		CreateSolidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept override;

		void
		FlushPendingTextureUploads(ICommandList* cmd) noexcept override;

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
		RegisterQueue(ICommandQueue* queue) noexcept override;

		void
		UnregisterQueue(ICommandQueue* queue) noexcept override;

		void
		DestroyRtv(RtvHandle handle, bool deferred = true) noexcept override;

		void
		DestroyBuffer(BufferHandle handle, bool deferred = true) noexcept override;

		void
		DestroyTexture(TextureHandle handle, bool deferred = true) noexcept override;

		void
		DestroySampler(SamplerHandle handle, bool deferred = true) noexcept override;

		void
		DestroyReadbackBuffer(ReadbackBufferHandle handle, bool deferred = true) noexcept override;

		void
		DestroyDsv(DsvHandle handle, bool deferred = true) noexcept override;

		void
		CleanupExpiredResources() noexcept override;

		[[nodiscard]]
		bool
		ValidBufferHandle(const BufferHandle& handle) const noexcept override;

		[[nodiscard]]
		bool
		ValidTextureHandle(const TextureHandle& handle) const noexcept override;

		[[nodiscard]]
		bool
		IsTextureCube(const TextureHandle& handle) const noexcept override;

		[[nodiscard]]
		bool
		ValidSamplerHandle(const SamplerHandle& handle) const noexcept override;

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

		TextureDesc
		GetTextureDesc(TextureHandle handle) const noexcept override;

		const Sampler&
		GetSampler(SamplerHandle handle) const noexcept override;

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

		TextureHandle
		GetRtvTexture(RtvHandle handle) const noexcept override;

		TextureHandle
		GetDsvTexture(DsvHandle handle) const noexcept override;

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

		ID3D12DescriptorHeap*
		GetSamplerHeap() const noexcept
		{
			return m_SamplerHeap.Get();
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

	private:
		// Snapshots every registered queue's next fence value -- the gate a deferred destroy recorded
		// now must clear before its slot is reclaimed.
		[[nodiscard]] DeletionGate
		CaptureGate() const noexcept;

		// Records a retired slot for deferred reclamation, appending it to the batch that shares the
		// current gate (or opening a new batch when the gate has advanced).
		void
		RetireDeferred(PendingType type, uint32_t slotIndex) noexcept;

		wrl::ComPtr<ID3D12Device>         m_Device;
		wrl::ComPtr<ID3D12DescriptorHeap> m_CbvSrvUavHeap;
		wrl::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
		wrl::ComPtr<ID3D12DescriptorHeap> m_DsvHeap;
		wrl::ComPtr<ID3D12DescriptorHeap> m_SamplerHeap;
		core::slot_vector<CbvSrvUavSlot>  m_CbvSrvUavSlots;
		core::slot_vector<Sampler>        m_Samplers;

		// RTV/DSV-only textures (no SRV): kept out of the shader-visible pool so they
		// never consume a bindless descriptor slot. SRV textures live in
		// m_CbvSrvUavSlots instead, where their slot index is the bindless index.
		core::slot_vector<Texture> m_Textures;

		// A deferred texture upload owns a contiguous copy of its decoded pixel bytes
		// (so it survives until the next flush) plus the per-subresource layout needed
		// to rebuild the upload spans into that buffer.
		struct PendingSubresource
		{
			size_t   offset;
			uint64_t rowPitch;
			uint64_t slicePitch;
		};
		struct PendingTextureUpload
		{
			TextureHandle                   handle;
			std::vector<std::byte>          bytes;
			std::vector<PendingSubresource> subresources;
		};
		std::vector<PendingTextureUpload> m_PendingTextureUploads;

		core::slot_vector<ReadbackBuffer> m_ReadbackBuffers;

		core::slot_vector<Rtv>            m_Rtvs;
		core::slot_vector<Dsv>            m_Dsvs;
		std::vector<PendingDeletionBatch> m_PendingBatches;

		// The submission timelines a deferred destroy must clear. Borrowed: a context registers its
		// queue on construction and unregisters before the queue dies.
		std::vector<ICommandQueue*> m_RegisteredQueues;
	};
}
