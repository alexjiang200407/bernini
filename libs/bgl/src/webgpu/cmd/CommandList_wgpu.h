#pragma once

#include "cmd/CommandList.h"
#include "resource/ResourceManager.h"

namespace bgl
{
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
			WGPUDevice             device,
			const CommandListDesc& desc,
			ResourceManagerRef     resourceManager) noexcept;

		~CommandList() noexcept override;

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
		[[nodiscard]] WGPUCommandBuffer
		TakeCommandBuffer() noexcept;

	private:
		WGPUDevice         m_Device = nullptr;
		CommandListDesc    m_Desc;
		ResourceManagerRef m_ResourceManager;

		WGPUCommandEncoder m_Encoder       = nullptr;
		WGPUCommandBuffer  m_CommandBuffer = nullptr;
		WGPUQueue          m_BoundQueue    = nullptr;

		std::optional<ComputeState> m_CurrentComputeState;
	};
}
