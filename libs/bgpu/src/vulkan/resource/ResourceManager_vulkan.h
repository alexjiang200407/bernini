#pragma once
#include "resource/BindlessTable_vulkan.h"
#include "resource/BufferMemory_vulkan.h"
#include "resource/Buffer_vulkan.h"
#include "resource/ReadbackBuffer_vulkan.h"
#include "resource/Sampler_vulkan.h"
#include "resource/Srv_vulkan.h"
#include "resource/Texture_vulkan.h"
#include "volk_vulkan.h"
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <bgpu/resource/NativeTextureDesc.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/NativeObject.h>
#include <core/containers/slot_handle.h>
#include <core/containers/slot_vector.h>
#include <core/containers/static_vector.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

namespace bgpu
{
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

	enum class PendingType : uint8_t
	{
		kInvalid,
		kBuffer,
		kBufferSrv,
		kBufferUav,
		kReadback,
		kTexture,
		kSrv,
		kSampler,
	};

	struct PendingDeletion
	{
		PendingType type      = PendingType::kInvalid;
		uint32_t    slotIndex = 0xFFFFFFFF;

		// Set for the kinds that hold a bindless descriptor, which must outlive in-flight work as the
		// resource does, so it is handed back when the gate clears rather than at destroy time.
		uint32_t descriptorIndex = 0xFFFFFFFF;
	};

	// Deferred destroys captured at the same gate share it, freed as a group once every queue in
	// `gate` passes.
	struct PendingDeletionBatch
	{
		DeletionGate                 gate;
		std::vector<PendingDeletion> deletions;
	};

	/**
	 * The Vulkan resource manager: buffers, their second views and readbacks, textures, their views
	 * and samplers, each buffer, view and sampler a descriptor in the manager's bindless table.
	 *
	 * Every texture is in `VK_IMAGE_LAYOUT_GENERAL` from its first submission on: a new image is
	 * `UNDEFINED`, so the manager keeps the transitions of the textures it has made, and the queue
	 * that next submits a list recorded against it submits them first (TakeInitialLayouts).
	 */
	class ResourceManager final : public core::RefCounter<IResourceManager>
	{
	public:
		ResourceManager(GpuContextRef context, const ResourceManagerDesc& desc);
		~ResourceManager() noexcept override;

		ResourceManager(const ResourceManager&) = delete;
		ResourceManager(ResourceManager&&)      = delete;
		ResourceManager&
		operator=(const ResourceManager&) = delete;
		ResourceManager&
		operator=(ResourceManager&&) = delete;

		[[nodiscard]] BufferHandle
		CreateStructBuffer(const StructBufferDesc& desc) noexcept override;

		[[nodiscard]] BufferHandle
		CreateComputeBuffer(const ComputeBufferDesc& desc) noexcept override;

		[[nodiscard]] BufferHandle
		CreateRawBuffer(const RawViewDesc& desc) noexcept override;

		[[nodiscard]] BufferSrvHandle
		CreateBufferSrv(BufferHandle buffer, const BufferSrvDesc& desc) noexcept override;

		void
		DestroyBufferSrv(BufferSrvHandle handle, bool deferred = true) noexcept override;

		[[nodiscard]] bool
		ValidBufferSrvHandle(const BufferSrvHandle& handle) const noexcept override;

		[[nodiscard]] BufferUavHandle
		CreateBufferUav(BufferHandle buffer, const BufferUavDesc& desc) noexcept override;

		void
		DestroyBufferUav(BufferUavHandle handle, bool deferred = true) noexcept override;

		[[nodiscard]] bool
		ValidBufferUavHandle(const BufferUavHandle& handle) const noexcept override;

		[[nodiscard]] NativeObject
		GetNativeBuffer(BufferHandle handle, NativeObjectType type) const noexcept override;

		[[nodiscard]] BufferHandle
		ImportNativeBuffer(const NativeBufferDesc& desc) noexcept override;

		TextureHandle
		CreateTexture(const TextureDesc& desc) noexcept override;

		[[nodiscard]] NativeObject
		GetNativeTexture(TextureHandle handle, NativeObjectType type) const noexcept override;

		/**
		 * An image a bgpu manager made is kept alive by the reference this adds; one none made -- a
		 * swapchain's -- is borrowed, and its maker keeps it alive while the handle lives, since
		 * Vulkan cannot add a reference to it.
		 */
		[[nodiscard]] TextureHandle
		ImportNativeTexture(const NativeTextureDesc& desc) noexcept override;

		[[nodiscard]] SamplerHandle
		CreateSampler(const SamplerDesc& desc) noexcept override;

		ReadbackBufferHandle
		CreateReadbackBuffer(const ReadbackBufferDesc& desc) noexcept override;

		void
		RegisterQueue(ICommandQueue* queue) noexcept override;

		void
		UnregisterQueue(ICommandQueue* queue) noexcept override;

		void
		DestroyBuffer(BufferHandle handle, bool deferred = true) noexcept override;

		void
		DestroyTexture(TextureHandle handle, bool deferred = true) noexcept override;

		void
		DestroySampler(SamplerHandle handle, bool deferred = true) noexcept override;

		void
		DestroyReadbackBuffer(ReadbackBufferHandle handle, bool deferred = true) noexcept override;

		void
		DestroySrv(SrvHandle handle, bool deferred = true) noexcept override;

		void
		DestroyRtv(RtvHandle handle, bool deferred = true) noexcept override;

		void
		DestroyDsv(DsvHandle handle, bool deferred = true) noexcept override;

		void
		CleanupExpiredResources() noexcept override;

		[[nodiscard]] SrvHandle
		CreateSrv(TextureHandle textureHandle, const SrvDesc& desc) noexcept override;

		[[nodiscard]] RtvHandle
		CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) noexcept override;

		[[nodiscard]] DsvHandle
		CreateDsv(TextureHandle textureHandle, const DsvDesc& desc) noexcept override;

		[[nodiscard]] const Rtv&
		GetRtv(RtvHandle handle) const noexcept override;

		[[nodiscard]] const Dsv&
		GetDsv(DsvHandle handle) const noexcept override;

		[[nodiscard]] TextureHandle
		GetRtvTexture(RtvHandle handle) const noexcept override;

		[[nodiscard]] TextureHandle
		GetDsvTexture(DsvHandle handle) const noexcept override;

		[[nodiscard]] const Buffer&
		GetBuffer(BufferHandle handle) const noexcept override;

		[[nodiscard]] BufferDesc
		GetBufferDesc(BufferHandle handle) const noexcept override;

		[[nodiscard]] const Texture&
		GetTexture(TextureHandle handle) const noexcept override;

		[[nodiscard]] TextureDesc
		GetTextureDesc(TextureHandle handle) const noexcept override;

		[[nodiscard]] const Sampler&
		GetSampler(SamplerHandle handle) const noexcept override;

		[[nodiscard]] const ReadbackBuffer&
		GetReadbackBuffer(ReadbackBufferHandle handle) const noexcept override;

		[[nodiscard]] TextureReadbackLayout
		GetTextureReadbackLayout(TextureHandle handle) const noexcept override;

		[[nodiscard]] const void*
		MapReadback(ReadbackBufferHandle handle) noexcept override;

		void
		UnmapReadback(ReadbackBufferHandle handle) noexcept override;

		[[nodiscard]] bool
		ValidBufferHandle(const BufferHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidTextureHandle(const TextureHandle& handle) const noexcept override;

		[[nodiscard]] bool
		IsTextureCube(const TextureHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidSrvHandle(const SrvHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidSamplerHandle(const SamplerHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidReadbackBufferHandle(const ReadbackBufferHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidRtvHandle(const RtvHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidDsvHandle(const DsvHandle& handle) const noexcept override;

		void
		ClearRtv(ICommandList* cmdList, RtvHandle handle, float clearVal[4]) noexcept override;

		void
		ClearDsv(ICommandList* cmdList, DsvHandle handle, float depth, uint8_t stencil) noexcept
			override;

		/**
		 * Appends the transition of every texture made since the last call, out of `UNDEFINED`
		 * into the layout textures keep, and forgets them: the caller submits them ahead of any
		 * work that may use one.
		 */
		void
		TakeInitialLayouts(std::vector<VkImageMemoryBarrier2>& barriers) noexcept;

		/** The set a command list binds at BindlessTable::c_Set for every dispatch. */
		[[nodiscard]] VkDescriptorSet
		GetBindlessSet() const noexcept
		{
			return m_Table.GetSet();
		}

		[[nodiscard]] const GpuContextRef&
		GetContext() const noexcept
		{
			return m_Context;
		}

	private:
		/**
		 * A pool slot and a descriptor pointed at `memory`, or a null handle with nothing taken when
		 * either is exhausted; the error is already logged.
		 *
		 * @pre m_PoolMutex is held.
		 */
		[[nodiscard]] BufferHandle
		AddBuffer(core::SharedRef<BufferMemory> memory, BufferDesc desc, bool tracked) noexcept;

		/** A device buffer of `desc`, or a null handle; the error is already logged. */
		[[nodiscard]] BufferHandle
		CreateDeviceBuffer(BufferDesc desc) noexcept;

		/**
		 * A second descriptor onto `buffer` from `pool`, or a null slot; the error is already logged.
		 *
		 * @pre m_PoolMutex is held.
		 */
		[[nodiscard]] core::slot_handle
		AddView(
			core::slot_vector<uint32_t>& pool,
			BufferHandle                 buffer,
			std::string_view             debugName,
			uint32_t&                    descriptorIndex) noexcept;

		// Snapshots every registered queue's next fence value: the gate a deferred destroy recorded
		// now must clear before its slot is reclaimed.
		[[nodiscard]] DeletionGate
		CaptureGate() const noexcept;

		void
		RetireDeferred(
			PendingType type,
			uint32_t    slotIndex,
			uint32_t    descriptorIndex = 0xFFFFFFFF) noexcept;

		/**
		 * The texture `handle` names.
		 *
		 * @pre `handle` is valid.
		 */
		[[nodiscard]] const Texture&
		TextureAt(TextureHandle handle) const noexcept;

		/**
		 * Drops the pending initial transition of the texture in pool slot `textureSlot`, if any: its
		 * image is going before any submission made it.
		 */
		void
		ForgetInitialLayout(uint32_t textureSlot) noexcept;

		// Declared first, destroyed last: every Vulkan object below belongs to its device.
		GpuContextRef       m_Context;
		ResourceManagerDesc m_Desc;
		BindlessTable       m_Table;

		core::slot_vector<Buffer>         m_Buffers;
		core::slot_vector<uint32_t>       m_BufferSrvs;
		core::slot_vector<uint32_t>       m_BufferUavs;
		core::slot_vector<ReadbackBuffer> m_ReadbackBuffers;
		core::slot_vector<Texture>        m_Textures;
		core::slot_vector<Srv>            m_Srvs;
		core::slot_vector<Sampler>        m_Samplers;

		// The images made and not yet submitted, with the aspects their transition names.
		struct PendingLayout
		{
			VkImage            image       = VK_NULL_HANDLE;
			VkImageAspectFlags aspects     = 0;
			uint32_t           textureSlot = 0xFFFFFFFF;
		};
		std::vector<PendingLayout> m_PendingLayouts;

		std::vector<PendingDeletionBatch>                          m_PendingBatches;
		core::static_vector<ICommandQueue*, c_MaxRegisteredQueues> m_RegisteredQueues;

		// Serializes slot allocation, retirement and reclamation, the descriptor table, the deletion
		// batches, the pending layouts and the queue registry. Get*/Valid* reads stay lockless: the
		// pools never move.
		mutable std::mutex m_PoolMutex;
	};
}
