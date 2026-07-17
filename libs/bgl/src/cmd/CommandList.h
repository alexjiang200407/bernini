#pragma once
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"
#include "types/ComputeState.h"
#include "types/MeshletState.h"
#include "types/QueueType.h"

#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	struct CommandListDesc
	{
		QueueType type;
		size_t    uploadChunkSize = 64 * 1024;
	};

	class ICommandAllocator;
	class ICommandQueue;

	class ICommandList : public core::Ref
	{
	public:
		ICommandList()                             = default;
		ICommandList(const ICommandList&) noexcept = delete;
		ICommandList(ICommandList&&) noexcept      = delete;

		ICommandList&
		operator=(const ICommandList&) noexcept = delete;

		ICommandList&
		operator=(ICommandList&&) noexcept = delete;

		/**
		 * Writes data to a buffer resource.
		 * The buffer must have correct state
		 */
		void
		WriteBuffer(BufferHandle handle, const void* data, size_t byteSize) noexcept
		{
			WriteBuffer(handle, data, 0, byteSize);
		}

		virtual void
		WriteBuffer(
			BufferHandle handle,
			const void*  data,
			size_t       offset,
			size_t       byteSize) noexcept = 0;

		/**
		 * Uploads CPU pixel data into every subresource of a texture. The texture must
		 * be in a copy-dest layout; the caller barriers it to a shader-resource layout
		 * afterwards. Subresources are ordered as D3D12 expects (mips of slice 0, then
		 * slice 1, ...).
		 */
		virtual void
		WriteTexture(
			TextureHandle                           handle,
			std::span<const TextureSubresourceData> subresources) noexcept = 0;

		/**
		 * Copies an entire buffer into a readback buffer for CPU access.
		 * The source buffer must already be in a copy-source state.
		 */
		virtual void
		CopyBufferToReadback(ReadbackBufferHandle dst, BufferHandle src) noexcept = 0;

		/**
		 * Copies texture subresource 0 into a readback buffer using its linear
		 * footprint (see TextureReadbackLayout). The source texture must already be
		 * in a copy-source layout.
		 */
		virtual void
		CopyTextureToReadback(ReadbackBufferHandle dst, TextureHandle src) noexcept = 0;

		/**
		 * Barriers are handled by the FrameGraph. Shouldn't barrier directly
		 */
		virtual void
		Barrier(BufferHandle handle, const BufferBarrierDesc& barrier) noexcept = 0;

		/**
		 * Barriers are handled by the FrameGraph. Shouldn't barrier directly
		 */
		virtual void
		Barrier(TextureHandle handle, const TextureBarrierDesc& barrier) noexcept = 0;

		/**
		 * Barriers are handled by the FrameGraph. Shouldn't barrier directly
		 */
		virtual void
		Barrier(RtvHandle handle, const TextureBarrierDesc& barrier) noexcept = 0;

		/**
		 * Barriers are handled by the FrameGraph. Shouldn't barrier directly
		 */
		virtual void
		Barrier(DsvHandle handle, const TextureBarrierDesc& barrier) noexcept = 0;

		/**
		 * Barriers are handled by the FrameGraph. Shouldn't barrier directly
		 */
		virtual void
		Barrier(
			std::span<const BufferHandle>      handles,
			std::span<const BufferBarrierDesc> barriers) noexcept = 0;

		/**
		 * Barriers are handled by the FrameGraph. Shouldn't barrier directly
		 */
		virtual void
		Barrier(
			std::span<const TextureHandle>      handles,
			std::span<const TextureBarrierDesc> barriers) noexcept = 0;

		virtual void
		Open(ICommandQueue* cmdQueue, ICommandAllocator* allocator) noexcept = 0;

		virtual void
		Close() noexcept = 0;

		/**
		 * Debug-only GPU work markers
		 */
		virtual void
		BeginEvent(std::string_view name) noexcept = 0;

		virtual void
		EndEvent() noexcept = 0;

		virtual void
		SetMeshletState(const MeshletState& gfxState) noexcept = 0;

		virtual void
		DispatchMesh(
			uint32_t threadGroupCountX,
			uint32_t threadGroupCountY,
			uint32_t threadGroupCountZ) noexcept = 0;

		virtual void
		DispatchMeshIndirect(uint32_t argIdx) noexcept = 0;

		virtual void
		SetComputeState(const ComputeState& computeState) noexcept = 0;

		virtual void
		Dispatch(
			uint32_t threadGroupCountX,
			uint32_t threadGroupCountY,
			uint32_t threadGroupCountZ) noexcept = 0;

#if defined(BERNINI_GPU_DEBUG)
		/**
		 * Sets the GPU-assertion debug buffer that subsequent compute Dispatches
		 * auto-bind into a shader's implicit `gDebug` cbuffer (see dbg.slang). A null
		 * handle disables auto-binding. Debug builds only; defaults to a no-op so
		 * lightweight ICommandList mocks need not implement it.
		 */
		virtual void
		SetActiveDebugBuffer(BufferHandle handle) noexcept
		{
			(void)handle;
		}
#endif

		[[nodiscard]] virtual bool
		IsOpen() const noexcept = 0;

		[[nodiscard]]
		virtual QueueType
		GetType() const noexcept = 0;
	};

	using CommandListRef = core::SharedRef<ICommandList>;
}
