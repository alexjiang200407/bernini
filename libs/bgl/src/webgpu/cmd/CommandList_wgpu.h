#pragma once

#include "cmd/CommandList.h"
#include "resource/ResourceManager.h"

namespace bgl
{
	class GraphicsPipeline;

	/**
	 * Records into a WGPUCommandEncoder between Open and Close; Close finishes it into a
	 * command buffer the queue takes at submit.
	 *
	 * There is no upload ring. WebGPU stages uploads itself through wgpuQueueWriteBuffer, which
	 * is ordered against submissions on the queue rather than against this encoder -- so a
	 * write lands before any command buffer submitted after it, which is the ordering the
	 * D3D12 backend gets from copying inside the list.
	 *
	 * Barriers are no-ops: WebGPU derives its own from resource usage.
	 */
	class CommandList final : public core::RefCounter<ICommandList>
	{
	public:
		CommandList(
			const wgpu::Device&    device,
			const CommandListDesc& desc,
			ResourceManagerRef     resourceManager) noexcept;

		~CommandList() noexcept override = default;

		CommandList(const CommandList&) noexcept = delete;
		CommandList(CommandList&&) noexcept      = delete;

		CommandList&
		operator=(const CommandList&) noexcept = delete;

		CommandList&
		operator=(CommandList&&) noexcept = delete;

		void
		Open(ICommandQueue* cmdQueue, ICommandAllocator* allocator) noexcept override;

		void
		Close() noexcept override;

		[[nodiscard]] bool
		IsOpen() const noexcept override
		{
			return m_Encoder != nullptr;
		}

		[[nodiscard]] QueueType
		GetType() const noexcept override
		{
			return m_Desc.type;
		}

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
		BeginEvent(std::string_view name) noexcept override;

		void
		EndEvent() noexcept override;

		void
		SetMeshletState(const MeshletState& gfxState) noexcept override;

		void
		DispatchMesh(uint32_t x, uint32_t y, uint32_t z) noexcept override;

		void
		DispatchMeshIndirect(uint32_t argIdx) noexcept override;

		void
		SetComputeState(const ComputeState& computeState) noexcept override;

		void
		Dispatch(uint32_t x, uint32_t y, uint32_t z) noexcept override;

		/** Hands the finished command buffer to the queue, which owns it from then on. */
		[[nodiscard]] wgpu::CommandBuffer
		TakeCommandBuffer() noexcept;

		/** Clears a render-target view: a one-shot render pass with a clear load op. */
		void
		ClearRenderTarget(const wgpu::TextureView& view, const float clearColor[4]) noexcept;

		/** Clears a depth-stencil view: a one-shot render pass clearing depth (and stencil). */
		void
		ClearDepthTarget(
			const wgpu::TextureView& view,
			float                    depth,
			uint8_t                  stencil,
			bool                     hasStencil) noexcept;

		/**
		 * Opens a render pass over the framebuffer's colour and depth views, loading their existing
		 * contents (clears are separate, via ClearRtv/ClearDsv). The first viewport/scissor is
		 * applied. Must be paired with EndRenderPass; no other encoding may happen in between.
		 */
		void
		BeginRenderPass(
			const FrameBuffer&   frameBuffer,
			const ViewportState& viewportState) noexcept;

		/** Binds a graphics pipeline for subsequent draws in the open render pass. */
		void
		SetGraphicsPipeline(const GraphicsPipeline& pipeline) noexcept;

		/**
		 * Binds a meshlet kernel's graphics pipeline and its bind group -- the kernel's uniform
		 * handle-writes resolved to buffer bindings, the same way Dispatch binds a compute kernel --
		 * for subsequent draws in the open render pass.
		 */
		void
		SetGraphicsKernel(const MeshletKernel& kernel) noexcept;

		/** Issues a non-indexed draw in the open render pass. */
		void
		Draw(
			uint32_t vertexCount,
			uint32_t instanceCount = 1,
			uint32_t firstVertex   = 0,
			uint32_t firstInstance = 0) noexcept;

		/**
		 * Issues a non-indexed draw whose {vertexCount, instanceCount, firstVertex, firstInstance}
		 * arguments are read from `argsBuffer` at `offset` -- the args a meshlet-expansion kernel
		 * fills before the draw. The buffer must hold 16 bytes of draw args at that offset.
		 */
		void
		DrawIndirect(BufferHandle argsBuffer, uint64_t offset = 0) noexcept;

		/** Closes the render pass opened by BeginRenderPass. */
		void
		EndRenderPass() noexcept;

	private:
		wgpu::Device       m_Device;
		CommandListDesc    m_Desc;
		ResourceManagerRef m_ResourceManager;

		wgpu::CommandEncoder    m_Encoder;
		wgpu::CommandBuffer     m_CommandBuffer;
		wgpu::Queue             m_BoundQueue;
		wgpu::RenderPassEncoder m_RenderPass;

		std::optional<ComputeState> m_CurrentComputeState;
	};
}
