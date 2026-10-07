#pragma once
#include "cmd/UploadRing_vulkan.h"
#include "volk_vulkan.h"
#include <bgpu/cmd/CommandList.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/FrameBuffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/MeshletState.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/ref/RefCounter.h>
#include <core/str/str.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace bgpu
{
	class Buffer;
	class CommandAllocator;
	class ICommandAllocator;
	class ICommandQueue;
	class ITimestampHeap;
	class ResourceManager;

	/**
	 * A command list recording into a command buffer its allocator hands it at each Open, on the
	 * family of the queue it is opened with. Every recording begins with a full memory barrier: D3D12
	 * decays every resource's state between ExecuteCommandLists calls, and Vulkan orders nothing
	 * between two submissions on one queue.
	 *
	 * A mesh dispatch draws inside dynamic rendering, which D3D12 has no notion of: the first draw
	 * begins it on its meshlet state's frame buffer, draws on that same frame buffer stay inside it,
	 * and every other command ends it first.
	 */
	class CommandList final : public core::RefCounter<ICommandList>
	{
	public:
		CommandList(const CommandListDesc& desc, ResourceManagerRef resourceManager);
		~CommandList() noexcept override;

		CommandList(const CommandList&) = delete;
		CommandList(CommandList&&)      = delete;
		CommandList&
		operator=(const CommandList&) = delete;
		CommandList&
		operator=(CommandList&&) = delete;

		void
		WriteBuffer(
			BufferHandle handle,
			const void*  data,
			size_t       gpuBufferOffset,
			size_t       byteSize) noexcept override;

		void
		WriteTexture(
			TextureHandle                           handle,
			std::span<const TextureSubresourceData> subresources) noexcept override;

		void
		CopyBuffer(
			BufferHandle dst,
			BufferHandle src,
			uint64_t     dstOffset,
			uint64_t     srcOffset,
			uint64_t     byteSize) noexcept override;

		void
		CopyBufferToReadback(ReadbackBufferHandle dst, BufferHandle src) noexcept override;

		void
		CopyTextureToReadback(ReadbackBufferHandle dst, TextureHandle src) noexcept override;

		void
		Barrier(BufferHandle handle, const BufferBarrierDesc& barrier) noexcept override;

		void
		Barrier(TextureHandle handle, const TextureBarrierDesc& barrier) noexcept override;

		void
		Barrier(RtvHandle handle, const TextureBarrierDesc& barrier) noexcept override;

		void
		Barrier(DsvHandle handle, const TextureBarrierDesc& barrier) noexcept override;

		void
		Barrier(
			std::span<const BufferHandle>      handles,
			std::span<const BufferBarrierDesc> barriers) noexcept override;

		void
		Barrier(
			std::span<const TextureHandle>      handles,
			std::span<const TextureBarrierDesc> barriers) noexcept override;

		void
		Open(ICommandQueue* cmdQueue, ICommandAllocator* allocator) noexcept override;

		void
		Close() noexcept override;

		void
		BeginEvent(std::string_view name) noexcept override;

		void
		EndEvent() noexcept override;

		void
		BeginTiming(ITimestampHeap& heap, uint32_t startSlot, uint32_t endSlot) noexcept override;

		bool
		EndTiming() noexcept override;

		void
		ResolveTimestamps(ITimestampHeap& heap, uint32_t first, uint32_t count) noexcept override;

		void
		SetMeshletState(const MeshletState& gfxState) noexcept override;

		void
		DispatchMesh(
			uint32_t threadGroupCountX,
			uint32_t threadGroupCountY,
			uint32_t threadGroupCountZ) noexcept override;

		void
		DispatchMeshIndirect(uint32_t argIdx) noexcept override;

		void
		DispatchMeshIndirectCount(uint32_t argIdx, uint32_t countIdx) noexcept override;

		void
		SetComputeState(const ComputeState& computeState) noexcept override;

		void
		Dispatch(
			uint32_t threadGroupCountX,
			uint32_t threadGroupCountY,
			uint32_t threadGroupCountZ) noexcept override;

#if defined(BERNINI_GPU_DEBUG)
		void
		SetActiveDebugBuffer(BufferHandle handle) noexcept override;
#endif

		[[nodiscard]] bool
		IsOpen() const noexcept override
		{
			return m_Open;
		}

		[[nodiscard]] QueueType
		GetType() const noexcept override
		{
			return m_Desc.type;
		}

		/** The command buffer of the last recording, for its queue to submit. */
		[[nodiscard]] VkCommandBuffer
		GetVkCommandBuffer() const noexcept
		{
			return m_CommandBuffer;
		}

		/** Clears `range` of `image` to `value`; a colour target's ClearRtv. */
		void
		ClearColor(
			VkImage                        image,
			const VkImageSubresourceRange& range,
			const VkClearColorValue&       value) noexcept;

		/** Clears `range` of `image` to `value`; a depth target's ClearDsv. */
		void
		ClearDepthStencil(
			VkImage                         image,
			const VkImageSubresourceRange&  range,
			const VkClearDepthStencilValue& value) noexcept;

		/** The manager every handle this list records is one of. */
		[[nodiscard]] ResourceManager&
		GetResourceManager() const noexcept;

		/** Called by the queue as it submits: this recording's uploads are in flight to `fenceValue`. */
		void
		Submitted(uint64_t fenceValue) noexcept;

	private:
		/**
		 * The command buffer, for any command but a draw: the rendering a draw began is ended first.
		 * Every recording but a draw's reaches the buffer through this, so none can be made inside
		 * rendering by forgetting to end it.
		 */
		[[nodiscard]] VkCommandBuffer
		Commands() noexcept;

		/** The command buffer, for a draw: inside the rendering ApplyMeshletState began. */
		[[nodiscard]] VkCommandBuffer
		DrawCommands() const noexcept;

		/** Ends the rendering a draw began, if one is open. */
		void
		EndRendering() noexcept;

		/** Begins rendering into `frameBuffer`'s attachments, loaded and stored as D3D12 keeps them. */
		void
		BeginRendering(const FrameBuffer& frameBuffer) noexcept;

		/**
		 * Renders into the meshlet state's frame buffer and binds its pipeline, constants and
		 * viewports: what every mesh dispatch records first.
		 */
		void
		ApplyMeshletState() noexcept;

		/**
		 * Writes each of `uniforms` into a constants set of `constantsLayout` at the binding
		 * `bindings` gives its root parameter, and binds that set and the bindless table.
		 */
		void
		BindSets(
			VkCommandBuffer                               commands,
			VkPipelineBindPoint                           bindPoint,
			VkPipelineLayout                              layout,
			VkDescriptorSetLayout                         constantsLayout,
			std::span<const uint32_t>                     bindings,
			const core::str::unordered_str_map<Uniforms>& uniforms) noexcept;

		/** `bytes` of `data` written to the ring and bound to binding `binding` of `set`. */
		void
		WriteConstants(
			VkDescriptorSet set,
			uint32_t        binding,
			const void*     data,
			size_t          bytes) noexcept;

		[[nodiscard]] const Buffer&
		GetBuffer(BufferHandle handle) const noexcept;

		[[nodiscard]] const Texture&
		GetTexture(TextureHandle handle) const noexcept;

		CommandListDesc    m_Desc;
		ResourceManagerRef m_ResourceManager;
		UploadRing         m_UploadRing;

		// Recorded into directly only by Open, Close and the rendering bracket; every other
		// command goes through Commands() or DrawCommands().
		VkCommandBuffer             m_CommandBuffer = VK_NULL_HANDLE;
		CommandAllocator*           m_Allocator     = nullptr;
		std::optional<ComputeState> m_ComputeState;
		std::optional<MeshletState> m_MeshletState;

		// The frame buffer the open rendering draws into; meaningless while m_Rendering is false.
		FrameBuffer m_RenderingFrameBuffer;
		bool        m_Rendering          = false;
		uint32_t    m_TimestampValidBits = 0;
		uint64_t    m_UniformAlignment   = 256;
		uint64_t    m_LastCompletedFence = 0;
		bool        m_Open               = false;
#if defined(BERNINI_GPU_DEBUG)
		BufferHandle m_ActiveDebugBuffer;
#endif

		// The one queue this list may ever be opened with: the ring reclaims chunks by comparing raw
		// fence values, which is only sound on one timeline. An identity, never dereferenced.
		const ICommandQueue* m_BoundQueue = nullptr;

		// The timed span in flight, if any. Borrowed for the span: the heap outlives every list that
		// records into it.
		VkQueryPool m_TimingPool    = VK_NULL_HANDLE;
		uint32_t    m_TimingEndSlot = 0;

		// Every slot a span of this list wrote: a resolve waits for its slots, so one no span wrote
		// would never become available and hang the queue.
		std::vector<std::pair<VkQueryPool, uint32_t>> m_TimedSlots;
	};
}
