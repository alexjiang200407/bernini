#pragma once
#include "cmd/CommandAllocator.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"
#include "types/GraphicsState.h"
#include "types/QueueType.h"

#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class ICommandAllocator;

	class ICommandList : public core::Ref
	{
	public:
		/**
		 * Writes data to a buffer resource.
		 * The buffer will transition to kCopyDest access and kCopy sync
		 */
		void
		WriteBuffer(BufferHandle handle, const void* data, size_t byteSize)
		{
			WriteBuffer(handle, data, 0, byteSize);
		}

		virtual void
		WriteBuffer(BufferHandle handle, const void* data, size_t offset, size_t byteSize) = 0;

		virtual void
		Barrier(BufferHandle handle, const BufferBarrierDesc& barrier) = 0;

		virtual void
		Barrier(TextureHandle handle, const TextureBarrierDesc& barrier) = 0;

		virtual void
		Barrier(RtvHandle handle, const TextureBarrierDesc& barrier) = 0;

		virtual void
		Open(ICommandAllocator* allocator) = 0;

		virtual void
		Close() = 0;

		virtual void
		SetGraphicsState(const GraphicsState& gfxState) = 0;

		virtual void
		DrawInstanced(uint32_t vertexCount, uint32_t instanceCount) const = 0;

		[[nodiscard]] virtual bool
		IsOpen() const = 0;

		[[nodiscard]]
		virtual QueueType
		GetType() const = 0;
	};

	using CommandListHandle = core::SharedRef<ICommandList>;
}
