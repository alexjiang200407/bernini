#pragma once
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"
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
		WriteBuffer(BufferHandle handle, const void* data, size_t byteSize)
		{
			WriteBuffer(handle, data, 0, byteSize);
		}

		virtual void
		WriteBuffer(BufferHandle handle, const void* data, size_t offset, size_t byteSize) = 0;

		/**
		 * Copies an entire buffer into a readback buffer for CPU access.
		 * The source buffer must already be in a copy-source state.
		 */
		virtual void
		CopyBufferToReadback(ReadbackBufferHandle dst, BufferHandle src) = 0;

		/**
		 * Copies texture subresource 0 into a readback buffer using its linear
		 * footprint (see TextureReadbackLayout). The source texture must already be
		 * in a copy-source layout.
		 */
		virtual void
		CopyTextureToReadback(ReadbackBufferHandle dst, TextureHandle src) = 0;

		virtual void
		Barrier(BufferHandle handle, const BufferBarrierDesc& barrier) = 0;

		virtual void
		Barrier(TextureHandle handle, const TextureBarrierDesc& barrier) = 0;

		virtual void
		Barrier(RtvHandle handle, const TextureBarrierDesc& barrier) = 0;

		virtual void
		Barrier(DsvHandle handle, const TextureBarrierDesc& barrier) = 0;

		virtual void
		Open(ICommandQueue* cmdQueue, ICommandAllocator* allocator) = 0;

		virtual void
		Close() = 0;

		virtual void
		SetMeshletState(const MeshletState& gfxState) = 0;

		virtual void
		DispatchMesh(
			uint32_t threadGroupCountX,
			uint32_t threadGroupCountY,
			uint32_t threadGroupCountZ) = 0;

		[[nodiscard]] virtual bool
		IsOpen() const = 0;

		[[nodiscard]]
		virtual QueueType
		GetType() const = 0;
	};

	using CommandListHandle = core::SharedRef<ICommandList>;
}
