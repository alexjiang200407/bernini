#pragma once

#include "resource/Buffer_wgpu.h"
#include "resource/ReadbackBuffer_wgpu.h"
#include "resource/ResourceManager.h"

#include <core/containers/slot_vector.h>
#include <core/containers/static_vector.h>

namespace bgl
{
	/**
	 * Owns buffers and readback buffers behind generation-checked slot handles.
	 *
	 * The deferred-destruction model is the D3D12 one and is backend-agnostic: a destroy retires
	 * the slot immediately -- staling every copy of the handle -- and records the fence each
	 * registered queue was at, so the slot is reclaimed only once all of them pass it.
	 *
	 * Unlike D3D12 a slot index is *not* a descriptor index: WebGPU has no descriptor heap, so
	 * bindings are resolved into bind groups at dispatch instead. Textures, samplers and views
	 * are not implemented yet and fail loudly.
	 */
	class ResourceManager final : public core::RefCounter<IResourceManager>
	{
	public:
		ResourceManager(WGPUDevice device, WGPUInstance instance, const ResourceManagerDesc& desc);

		~ResourceManager() noexcept override;

		BufferHandle
		CreateStructBuffer(const StructBufferDesc& desc) noexcept override;

		BufferHandle
		CreateComputeBuffer(const ComputeBufferDesc& desc) noexcept override;

		ReadbackBufferHandle
		CreateReadbackBuffer(const ReadbackBufferDesc& desc) noexcept override;

		TextureHandle
		CreateTexture(const TextureDesc& desc) noexcept override;

		[[nodiscard]] SamplerHandle
		CreateSampler(const SamplerDesc& desc) noexcept override;

		[[nodiscard]] RtvHandle
		CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) noexcept override;

		[[nodiscard]] DsvHandle
		CreateDsv(TextureHandle textureHandle, const DsvDesc& desc) noexcept override;

		void
		RegisterQueue(ICommandQueue* queue) noexcept override;

		void
		UnregisterQueue(ICommandQueue* queue) noexcept override;

		void
		DestroyBuffer(BufferHandle handle, bool deferred) noexcept override;

		void
		DestroyReadbackBuffer(ReadbackBufferHandle handle, bool deferred) noexcept override;

		void
		DestroyTexture(TextureHandle handle, bool deferred) noexcept override;

		void
		DestroySampler(SamplerHandle handle, bool deferred) noexcept override;

		void
		DestroyRtv(RtvHandle handle, bool deferred) noexcept override;

		void
		DestroyDsv(DsvHandle handle, bool deferred) noexcept override;

		void
		CleanupExpiredResources() noexcept override;

		[[nodiscard]] const Buffer&
		GetBuffer(BufferHandle handle) const noexcept override;

		[[nodiscard]] const ReadbackBuffer&
		GetReadbackBuffer(ReadbackBufferHandle handle) const noexcept override;

		[[nodiscard]] const void*
		MapReadback(ReadbackBufferHandle handle) noexcept override;

		void
		UnmapReadback(ReadbackBufferHandle handle) noexcept override;

		[[nodiscard]] bool
		ValidBufferHandle(const BufferHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidReadbackBufferHandle(const ReadbackBufferHandle& handle) const noexcept override;

		[[nodiscard]] const Rtv&
		GetRtv(RtvHandle handle) const noexcept override;

		[[nodiscard]] const Dsv&
		GetDsv(DsvHandle handle) const noexcept override;

		[[nodiscard]] TextureHandle
		GetRtvTexture(RtvHandle handle) const noexcept override;

		[[nodiscard]] TextureHandle
		GetDsvTexture(DsvHandle handle) const noexcept override;

		[[nodiscard]] const Texture&
		GetTexture(TextureHandle handle) const noexcept override;

		[[nodiscard]] TextureDesc
		GetTextureDesc(TextureHandle handle) const noexcept override;

		[[nodiscard]] const Sampler&
		GetSampler(SamplerHandle handle) const noexcept override;

		[[nodiscard]] TextureReadbackLayout
		GetTextureReadbackLayout(TextureHandle handle) const noexcept override;

		[[nodiscard]] bool
		ValidTextureHandle(const TextureHandle& handle) const noexcept override;

		[[nodiscard]] bool
		IsTextureCube(const TextureHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidSamplerHandle(const SamplerHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidRtvHandle(const RtvHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidDsvHandle(const DsvHandle& handle) const noexcept override;

		void
		ClearRtv(ICommandList* cmdList, RtvHandle handle, float clearVal[4]) noexcept override;

		void
		ClearDsv(ICommandList* cmdList, DsvHandle handle, float depth, uint8_t stencil) noexcept
			override;

	private:
		static constexpr uint32_t c_MaxRegisteredQueues = 8;

		enum class PendingType : uint8_t
		{
			kBuffer,
			kReadback
		};

		struct QueueGate
		{
			ICommandQueue* queue;
			uint64_t       fenceValue;

			bool
			operator==(const QueueGate&) const = default;
		};

		using DeletionGate = core::static_vector<QueueGate, c_MaxRegisteredQueues>;

		struct PendingDeletion
		{
			PendingType type;
			uint32_t    slotIndex;
		};

		struct PendingBatch
		{
			DeletionGate                 gate;
			std::vector<PendingDeletion> deletions;
		};

		[[nodiscard]] DeletionGate
		CaptureGate() const noexcept;

		void
		RetireDeferred(PendingType type, uint32_t slotIndex) noexcept;

		WGPUDevice   m_Device   = nullptr;
		WGPUInstance m_Instance = nullptr;

		mutable std::mutex m_PoolMutex;

		core::slot_vector<Buffer>         m_Buffers;
		core::slot_vector<ReadbackBuffer> m_ReadbackBuffers;

		core::static_vector<ICommandQueue*, c_MaxRegisteredQueues> m_Queues;
		std::vector<PendingBatch>                                  m_PendingBatches;
	};
}
