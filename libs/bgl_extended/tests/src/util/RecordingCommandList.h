#pragma once
#include "cmd/CommandList.h"
#include "resource/Buffer.h"
#include "resource/Dsv.h"
#include "resource/Readback.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"
#include "types/ComputeState.h"
#include "types/MeshletState.h"
#include "types/QueueType.h"
#include <algorithm>
#include <core/ref/RefCounter.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bgl::test
{
	/**
	 * An ICommandList that records the texture work submitted to it and does nothing else, so a
	 * test can assert what a subject uploaded without a device.
	 *
	 * Only the calls a texture upload makes are recorded; the rest are no-ops rather than aborts,
	 * because a subject may legitimately mix other work onto the same list.
	 */
	class RecordingCommandList : public core::RefCounter<ICommandList>
	{
	public:
		RecordingCommandList()                            = default;
		RecordingCommandList(const RecordingCommandList&) = delete;
		RecordingCommandList(RecordingCommandList&&)      = delete;

		RecordingCommandList&
		operator=(const RecordingCommandList&) = delete;

		RecordingCommandList&
		operator=(RecordingCommandList&&) = delete;

		struct TextureWrite
		{
			TextureHandle handle;
			size_t        subresourceCount = 0;
		};

		std::vector<TextureWrite>       textureWrites;
		std::vector<TextureHandle>      barrieredTextures;
		std::vector<TextureBarrierDesc> textureBarriers;
		std::vector<std::string>        events;

		[[nodiscard]] bool
		WroteTexture(TextureHandle handle) const noexcept
		{
			return std::ranges::any_of(textureWrites, [handle](const TextureWrite& write) {
				return write.handle == handle;
			});
		}

		void
		WriteTexture(
			TextureHandle                           handle,
			std::span<const TextureSubresourceData> subresources) noexcept override
		{
			textureWrites.push_back({ handle, subresources.size() });
		}

		void
		Barrier(
			std::span<const TextureHandle>      handles,
			std::span<const TextureBarrierDesc> barriers) noexcept override
		{
			barrieredTextures.insert(barrieredTextures.end(), handles.begin(), handles.end());
			textureBarriers.insert(textureBarriers.end(), barriers.begin(), barriers.end());
		}

		void
		BeginEvent(std::string_view name) noexcept override
		{
			events.emplace_back(name);
		}

		void
		EndEvent() noexcept override
		{}

		void
		WriteBuffer(BufferHandle, const void*, size_t, size_t) noexcept override
		{}
		void
		CopyBuffer(BufferHandle, BufferHandle, uint64_t, uint64_t, uint64_t) noexcept override
		{}
		void
		CopyBufferToReadback(ReadbackBufferHandle, BufferHandle) noexcept override
		{}
		void
		CopyTextureToReadback(ReadbackBufferHandle, TextureHandle) noexcept override
		{}
		void
		Barrier(BufferHandle, const BufferBarrierDesc&) noexcept override
		{}
		void
		Barrier(TextureHandle, const TextureBarrierDesc&) noexcept override
		{}
		void
		Barrier(RtvHandle, const TextureBarrierDesc&) noexcept override
		{}
		void
		Barrier(DsvHandle, const TextureBarrierDesc&) noexcept override
		{}
		void
		Barrier(std::span<const BufferHandle>, std::span<const BufferBarrierDesc>) noexcept override
		{}
		void
		Open(ICommandQueue*, ICommandAllocator*) noexcept override
		{}
		void
		Close() noexcept override
		{}
		void
		SetMeshletState(const MeshletState&) noexcept override
		{}
		void
		DispatchMesh(uint32_t, uint32_t, uint32_t) noexcept override
		{}
		void
		DispatchMeshIndirect(uint32_t) noexcept override
		{}
		void
		SetComputeState(const ComputeState&) noexcept override
		{}
		void
		Dispatch(uint32_t, uint32_t, uint32_t) noexcept override
		{}

		[[nodiscard]] bool
		IsOpen() const noexcept override
		{
			return true;
		}

		QueueType
		GetType() const noexcept override
		{
			return QueueType::kGraphics;
		}
	};
}
