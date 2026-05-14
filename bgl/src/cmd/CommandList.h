#pragma once
#include "cmd/CommandAllocator.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"
#include "types/GraphicsState.h"
#include "types/QueueType.h"
#include <core/pimpl/RefCountPImpl.h>

namespace bgl
{
	class CommandListImpl;
	class CommandList : public core::RefCountPImpl<CommandListImpl>
	{
	public:
		CommandList() = default;

		[[nodiscard]]
		bool
		IsInitialized() const noexcept
		{
			return GetImpl() != nullptr;
		}

		/**
		 * Writes data to a buffer resource.
		 * The buffer will transition to kCopyDest access and kCopy sync
		 */
		void
		WriteBuffer(BufferHandle handle, const void* data, size_t byteSize);

		void
		WriteBuffer(BufferHandle handle, const void* data, size_t offset, size_t byteSize);

		void
		Barrier(BufferHandle handle, const BufferBarrierDesc& barrier);

		void
		Barrier(TextureHandle handle, const TextureBarrierDesc& barrier);

		void
		Barrier(RtvHandle handle, const TextureBarrierDesc& barrier);

		void
		Open(CommandAllocator allocator);

		void
		Close();

		void
		SetGraphicsState(const GraphicsState& gfxState);

		void
		DrawInstanced(uint32_t vertexCount, uint32_t instanceCount) const;

		[[nodiscard]] bool
		IsOpen() const;

		[[nodiscard]]
		QueueType
		GetType() const;

	private:
		friend class CommandListImpl;
		friend class DeviceImpl;
	};
}
