#pragma once
#include "resource/BufferMemory_vulkan.h"
#include "volk_vulkan.h"
#include <bgpu/GpuContext.h>
#include <bgpu/MemoryTag.h>
#include <core/ref/SharedRef.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace bgpu
{
	/**
	 * A command list's staging memory: host-visible chunks it writes uploads and constant buffers
	 * into while recording, each reused once the fence of the submission that read it has passed.
	 * Keeps every chunk it made until it dies, so its cost is the list's peak, charged to device
	 * buffer as D3D12's ring is.
	 *
	 * The fences are compared raw, so every submission must be on one queue's timeline.
	 */
	class UploadRing final
	{
	public:
		struct Allocation
		{
			VkBuffer buffer = VK_NULL_HANDLE;
			uint64_t offset = 0;
			void*    cpu    = nullptr;
		};

		UploadRing(GpuContextRef context, size_t chunkSize);

		/**
		 * Frees whatever a recording that was never submitted left in use: its chunks were never
		 * read, so they are not in flight.
		 */
		void
		BeginRecording() noexcept;

		/** `size` bytes at `alignment`, writable through `cpu` until this recording is submitted. */
		[[nodiscard]] Allocation
		Allocate(uint64_t size, uint64_t alignment, uint64_t lastCompletedFence) noexcept;

		/** Every chunk this recording wrote is in flight until `fenceValue` completes. */
		void
		Submitted(uint64_t fenceValue) noexcept;

	private:
		enum class ChunkState : uint8_t
		{
			kFree,
			kRecording,
			kSubmitted,
		};

		struct Chunk
		{
			core::SharedRef<BufferMemory> memory;
			ChunkState                    state        = ChunkState::kFree;
			uint64_t                      fence        = 0;
			uint64_t                      writePointer = 0;
			TaggedBytes                   tracked;
		};

		[[nodiscard]] Chunk*
		FindChunk(uint64_t size, uint64_t lastCompletedFence) noexcept;

		GpuContextRef                       m_Context;
		size_t                              m_ChunkSize = 0;
		std::vector<std::unique_ptr<Chunk>> m_Chunks;
		Chunk*                              m_Current = nullptr;
	};
}
