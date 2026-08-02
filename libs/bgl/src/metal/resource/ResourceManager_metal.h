#pragma once
#include "metal_cpp.h"

#include "resource/Buffer_metal.h"
#include "resource/Dsv_metal.h"
#include "resource/ReadbackBuffer_metal.h"
#include "resource/Rtv_metal.h"
#include "resource/Sampler_metal.h"
#include "resource/Srv_metal.h"
#include "resource/Texture_metal.h"

#include "resource/ResourceManager.h"

#include <core/containers/slot_vector.h>
#include <core/containers/static_vector.h>
#include <core/ref/RefCounter.h>

namespace bgl
{
	// The most submission timelines that can gate one deferred free. Exceeding it asserts; it is not
	// a device limit.
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

	/**
	 * The Metal resource manager. Owns every GPU resource behind an index handle: buffers, readback
	 * buffers, textures, samplers, and the RTV/DSV records that name which texture a pass attaches.
	 *
	 */
	class ResourceManager final : public core::RefCounter<IResourceManager>
	{
	public:
		ResourceManager(MTL::Device* device, const ResourceManagerDesc& desc);

		[[nodiscard]] MTL::Device*
		GetMTLDevice() const noexcept
		{
			return m_Device;
		}

		// Resolves a bindless buffer handle's slot index (as carried in a DescriptorHandle) to its
		// Metal buffer, for the dispatch-time gpuAddress translation. The index is trusted -- it came
		// from a handle the engine already validated when it wrote the uniform.
		[[nodiscard]] MTL::Buffer*
		GetBufferBySlotIndex(uint32_t index) const noexcept
		{
			return m_Buffers[index].GetMTLResource();
		}

		[[nodiscard]] MTL::Texture*
		GetTextureBySlotIndex(uint32_t index) const noexcept
		{
			return m_Textures[index].GetMTLResource();
		}

		[[nodiscard]] MTL::SamplerState*
		GetSamplerBySlotIndex(uint32_t index) const noexcept
		{
			return m_Samplers[index].GetMTLResource();
		}

		BufferHandle
		CreateStructBuffer(const StructBufferDesc& desc) noexcept override;

		BufferHandle
		CreateComputeBuffer(const ComputeBufferDesc& desc) noexcept override;

		ReadbackBufferHandle
		CreateReadbackBuffer(const ReadbackBufferDesc& desc) noexcept override;

		void
		RegisterQueue(ICommandQueue* queue) noexcept override;

		void
		UnregisterQueue(ICommandQueue* queue) noexcept override;

		void
		DestroyBuffer(BufferHandle handle, bool deferred = true) noexcept override;

		void
		DestroyReadbackBuffer(ReadbackBufferHandle handle, bool deferred = true) noexcept override;

		void
		CleanupExpiredResources() noexcept override;

		const Buffer&
		GetBuffer(BufferHandle handle) const noexcept override;

		[[nodiscard]] uint64_t
		GetBufferByteSize(BufferHandle handle) const noexcept override;

		const ReadbackBuffer&
		GetReadbackBuffer(ReadbackBufferHandle handle) const noexcept override;

		const void*
		MapReadback(ReadbackBufferHandle handle) noexcept override;

		void
		UnmapReadback(ReadbackBufferHandle handle) noexcept override;

		[[nodiscard]] bool
		ValidBufferHandle(const BufferHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidReadbackBufferHandle(const ReadbackBufferHandle& handle) const noexcept override;

		// ---- render targets (no SRV/sampler upload yet -- that is the scene slice) ----

		TextureHandle
		CreateTexture(const TextureDesc& desc) noexcept override;

		SrvHandle
		CreateSrv(TextureHandle textureHandle, const SrvDesc& desc) noexcept override;

		RtvHandle
		CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) noexcept override;

		void
		DestroyTexture(TextureHandle handle, bool deferred = true) noexcept override;

		void
		DestroySrv(SrvHandle handle, bool deferred = true) noexcept override;

		void
		DestroyRtv(RtvHandle handle, bool deferred = true) noexcept override;

		const Texture&
		GetTexture(TextureHandle handle) const noexcept override;

		TextureDesc
		GetTextureDesc(TextureHandle handle) const noexcept override;

		const Rtv&
		GetRtv(RtvHandle handle) const noexcept override;

		TextureHandle
		GetRtvTexture(RtvHandle handle) const noexcept override;

		TextureReadbackLayout
		GetTextureReadbackLayout(TextureHandle handle) const noexcept override;

		[[nodiscard]] bool
		ValidTextureHandle(const TextureHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidSrvHandle(const SrvHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidRtvHandle(const RtvHandle& handle) const noexcept override;

		void
		ClearRtv(ICommandList* cmdList, RtvHandle handle, float clearVal[4]) noexcept override;

		[[nodiscard]] SamplerHandle
		CreateSampler(const SamplerDesc& desc) noexcept override;

		void
		DestroySampler(SamplerHandle handle, bool deferred = true) noexcept override;

		const Sampler&
		GetSampler(SamplerHandle handle) const noexcept override;

		[[nodiscard]] bool
		ValidSamplerHandle(const SamplerHandle& handle) const noexcept override;

		[[nodiscard]] DsvHandle
		CreateDsv(TextureHandle textureHandle, const DsvDesc& desc) noexcept override;

		void
		DestroyDsv(DsvHandle handle, bool deferred = true) noexcept override;

		const Dsv&
		GetDsv(DsvHandle handle) const noexcept override;

		TextureHandle
		GetDsvTexture(DsvHandle handle) const noexcept override;

		[[nodiscard]] bool
		ValidDsvHandle(const DsvHandle& handle) const noexcept override;

		void
		ClearDsv(ICommandList* cmdList, DsvHandle handle, float depth, uint8_t stencil) noexcept
			override;

		[[nodiscard]] bool
		IsTextureCube(const TextureHandle& handle) const noexcept override;

		// Every live texture, for an encoder to declare resident. See the definition for why all of
		// them rather than the ones a draw names.
		[[nodiscard]] std::span<MTL::Resource* const>
		GetLiveTextureResources() noexcept;

	private:
		enum class PendingType
		{
			kBuffer,
			kReadback,
			kTexture,
			kSrv,
			kRtv,
			kDsv,
			kSampler,
		};

		struct PendingDeletion
		{
			PendingType type      = PendingType::kBuffer;
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

		// Snapshots every registered queue's next fence value -- the gate a deferred destroy records.
		[[nodiscard]] DeletionGate
		CaptureGate() const noexcept;

		// Adds a retired slot to the current gate's batch, opening a new one when the gate has moved.
		void
		RetireDeferred(PendingType type, uint32_t slotIndex) noexcept;

		MTL::Device*                      m_Device = nullptr;
		core::slot_vector<Buffer>         m_Buffers;
		core::slot_vector<ReadbackBuffer> m_Readbacks;
		core::slot_vector<Texture>        m_Textures;
		std::vector<MTL::Resource*>       m_LiveTextures;
		bool                              m_LiveTexturesDirty = true;
		core::slot_vector<Srv>            m_Srvs;
		core::slot_vector<Rtv>            m_Rtvs;
		core::slot_vector<Dsv>            m_Dsvs;
		core::slot_vector<Sampler>        m_Samplers;

		core::static_vector<ICommandQueue*, c_MaxRegisteredQueues> m_RegisteredQueues;
		std::vector<PendingDeletionBatch>                          m_PendingBatches;

		// Serializes creation, destruction and the cleanup sweep, as the RHI promises. Get*/Valid*
		// stay lock-free: the pools are fixed-capacity so their storage never moves, and only a
		// handle's owner may destroy it.
		mutable std::mutex m_PoolMutex;
	};
}
