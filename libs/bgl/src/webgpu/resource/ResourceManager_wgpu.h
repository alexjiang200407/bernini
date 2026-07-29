#pragma once

#include "resource/Buffer_wgpu.h"
#include "resource/Dsv_wgpu.h"
#include "resource/ReadbackBuffer_wgpu.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv_wgpu.h"
#include "resource/Sampler_wgpu.h"
#include "resource/Texture_wgpu.h"

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
	 * bindings are resolved into bind groups at dispatch instead.
	 */
	class ResourceManager final : public core::RefCounter<IResourceManager>
	{
	public:
		ResourceManager(
			const wgpu::Device&        device,
			const wgpu::Instance&      instance,
			const ResourceManagerDesc& desc);

		~ResourceManager() noexcept override = default;

		ResourceManager(const ResourceManager&) noexcept = delete;
		ResourceManager(ResourceManager&&) noexcept      = delete;

		ResourceManager&
		operator=(const ResourceManager&) noexcept = delete;

		ResourceManager&
		operator=(ResourceManager&&) noexcept = delete;

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

		// Resolve the raw slot index a Uniforms handle write records (DescriptorHandle stores the
		// index alone, without a generation) to its resource, for bind-group assembly at dispatch.
		// Which one to call comes from the leaf's ReflectedLayout::resourceBinding.
		//
		// A null index (an unset uniform) resolves to a 1x1 fallback of the right shape, because a
		// bind group must supply every declared binding -- the same tolerance D3D12 gets by writing
		// the null index into the cbuffer and never dereferencing it.
		[[nodiscard]] const wgpu::Buffer&
		GetBufferBindingBySlotIndex(uint32_t slotIndex) const noexcept;

		[[nodiscard]] const wgpu::TextureView&
		GetTextureBindingBySlotIndex(uint32_t slotIndex, TextureDimension dimension) const noexcept;

		[[nodiscard]] const wgpu::Sampler&
		GetSamplerBindingBySlotIndex(uint32_t slotIndex) const noexcept;

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
			kReadback,
			kTexture,
			kSampler,
			kRtv,
			kDsv
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

		wgpu::Device   m_Device;
		wgpu::Instance m_Instance;

		mutable std::mutex m_PoolMutex;

		core::slot_vector<Buffer>         m_Buffers;
		core::slot_vector<ReadbackBuffer> m_ReadbackBuffers;
		core::slot_vector<Texture>        m_Textures;
		core::slot_vector<Sampler>        m_Samplers;
		core::slot_vector<Rtv>            m_Rtvs;
		core::slot_vector<Dsv>            m_Dsvs;

		// The null-binding fallbacks (see GetBufferBindingBySlotIndex).
		Buffer  m_NullBuffer;
		Texture m_NullTexture;
		Texture m_NullCube;
		Sampler m_NullSampler;

		core::static_vector<ICommandQueue*, c_MaxRegisteredQueues> m_Queues;
		std::vector<PendingBatch>                                  m_PendingBatches;
	};
}
