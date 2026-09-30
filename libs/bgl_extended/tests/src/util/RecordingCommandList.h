#pragma once
#include <algorithm>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/MeshletState.h>
#include <bgpu/types/QueueType.h>
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
	class RecordingCommandList : public core::RefCounter<bgpu::ICommandList>
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
			bgpu::TextureHandle handle;
			size_t              subresourceCount = 0;
		};

		std::vector<TextureWrite>             textureWrites;
		std::vector<bgpu::TextureHandle>      barrieredTextures;
		std::vector<bgpu::TextureBarrierDesc> textureBarriers;
		std::vector<std::string>              events;

		[[nodiscard]] bool
		WroteTexture(bgpu::TextureHandle handle) const noexcept
		{
			return std::ranges::any_of(textureWrites, [handle](const TextureWrite& write) {
				return write.handle == handle;
			});
		}

		void
		WriteTexture(
			bgpu::TextureHandle                           handle,
			std::span<const bgpu::TextureSubresourceData> subresources) noexcept override
		{
			textureWrites.push_back({ handle, subresources.size() });
		}

		void
		Barrier(
			std::span<const bgpu::TextureHandle>      handles,
			std::span<const bgpu::TextureBarrierDesc> barriers) noexcept override
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
		BeginTiming(bgpu::ITimestampHeap&, uint32_t, uint32_t) noexcept override
		{}

		bool
		EndTiming() noexcept override
		{
			return false;
		}

		void
		ResolveTimestamps(bgpu::ITimestampHeap&, uint32_t, uint32_t) noexcept override
		{}

		void
		WriteBuffer(bgpu::BufferHandle, const void*, size_t, size_t) noexcept override
		{}
		void
		CopyBuffer(bgpu::BufferHandle, bgpu::BufferHandle, uint64_t, uint64_t, uint64_t) noexcept
			override
		{}
		void
		CopyBufferToReadback(bgpu::ReadbackBufferHandle, bgpu::BufferHandle) noexcept override
		{}
		void
		CopyTextureToReadback(bgpu::ReadbackBufferHandle, bgpu::TextureHandle) noexcept override
		{}
		void
		Barrier(bgpu::BufferHandle, const bgpu::BufferBarrierDesc&) noexcept override
		{}
		void
		Barrier(bgpu::TextureHandle, const bgpu::TextureBarrierDesc&) noexcept override
		{}
		void
		Barrier(bgpu::RtvHandle, const bgpu::TextureBarrierDesc&) noexcept override
		{}
		void
		Barrier(bgpu::DsvHandle, const bgpu::TextureBarrierDesc&) noexcept override
		{}
		void
		Barrier(
			std::span<const bgpu::BufferHandle>,
			std::span<const bgpu::BufferBarrierDesc>) noexcept override
		{}
		void
		Open(bgpu::ICommandQueue*, bgpu::ICommandAllocator*) noexcept override
		{}
		void
		Close() noexcept override
		{}
		void
		SetMeshletState(const bgpu::MeshletState&) noexcept override
		{}
		void
		DispatchMesh(uint32_t, uint32_t, uint32_t) noexcept override
		{}
		void
		DispatchMeshIndirect(uint32_t) noexcept override
		{}
		void
		DispatchMeshIndirectCount(uint32_t, uint32_t) noexcept override
		{}
		void
		SetComputeState(const bgpu::ComputeState&) noexcept override
		{}
		void
		Dispatch(uint32_t, uint32_t, uint32_t) noexcept override
		{}

		[[nodiscard]] bool
		IsOpen() const noexcept override
		{
			return true;
		}

		bgpu::QueueType
		GetType() const noexcept override
		{
			return bgpu::QueueType::kGraphics;
		}
	};
}
