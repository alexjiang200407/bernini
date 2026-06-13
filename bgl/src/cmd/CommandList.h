#pragma once
#include "resource/Buffer.h"
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
		Open(ICommandQueue* cmdQueue, ICommandAllocator* allocator) = 0;

		virtual void
		Close() = 0;

		virtual void
		SetMeshletState(const MeshletState& gfxState) = 0;

		virtual void
		DispatchMesh(
			uint32_t threadGroupCountX,
			uint32_t threadGroupCountY,
			uint32_t threadGroupCountZ) const = 0;

		[[nodiscard]] virtual bool
		IsOpen() const = 0;

		[[nodiscard]]
		virtual QueueType
		GetType() const = 0;
	};

	using CommandListHandle = core::SharedRef<ICommandList>;
}
