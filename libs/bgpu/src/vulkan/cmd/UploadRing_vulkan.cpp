#include "cmd/UploadRing_vulkan.h"
#include "resource/BufferMemory_vulkan.h"
#include <algorithm>
#include <bgpu/GpuContext.h>
#include <bgpu/MemoryTag.h>
#include <core/err/util.h>
#include <core/math.h>
#include <core/ref/SharedRef.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <utility>

namespace bgpu
{
	namespace
	{
		constexpr uint64_t c_ChunkSizeAlignment = 4096;
	}

	UploadRing::UploadRing(GpuContextRef context, const size_t chunkSize) :
		m_Context(std::move(context)), m_ChunkSize(chunkSize)
	{}

	void
	UploadRing::BeginRecording() noexcept
	{
		for (const std::unique_ptr<Chunk>& chunk : m_Chunks)
		{
			if (chunk->state == ChunkState::kRecording)
				chunk->state = ChunkState::kFree;
		}
		m_Current = nullptr;
	}

	UploadRing::Allocation
	UploadRing::Allocate(
		const uint64_t size,
		const uint64_t alignment,
		const uint64_t lastCompletedFence) noexcept
	{
		if (m_Current != nullptr)
		{
			const uint64_t offset = core::align(m_Current->writePointer, alignment);
			if (offset + size <= m_Current->memory->GetByteSize())
			{
				m_Current->writePointer = offset + size;
				return { m_Current->memory->GetVkBuffer(),
					     offset,
					     static_cast<std::byte*>(m_Current->memory->GetMapped()) + offset };
			}
		}

		m_Current               = FindChunk(size, lastCompletedFence);
		m_Current->state        = ChunkState::kRecording;
		m_Current->writePointer = size;
		return { m_Current->memory->GetVkBuffer(), 0, m_Current->memory->GetMapped() };
	}

	void
	UploadRing::Submitted(const uint64_t fenceValue) noexcept
	{
		for (const std::unique_ptr<Chunk>& chunk : m_Chunks)
		{
			if (chunk->state == ChunkState::kRecording)
			{
				chunk->state = ChunkState::kSubmitted;
				chunk->fence = fenceValue;
			}
		}
		m_Current = nullptr;
	}

	UploadRing::Chunk*
	UploadRing::FindChunk(const uint64_t size, const uint64_t lastCompletedFence) noexcept
	{
		for (const std::unique_ptr<Chunk>& chunk : m_Chunks)
		{
			if (chunk->state == ChunkState::kSubmitted && chunk->fence <= lastCompletedFence)
				chunk->state = ChunkState::kFree;
			if (chunk->state == ChunkState::kFree && chunk->memory->GetByteSize() >= size)
				return chunk.get();
		}

		const uint64_t chunkSize =
			core::align(std::max<uint64_t>(size, m_ChunkSize), c_ChunkSizeAlignment);

		auto chunk = std::make_unique<Chunk>();
		try
		{
			chunk->memory = core::SharedRef<BufferMemory>::Make(
				m_Context,
				chunkSize,
				BufferMemoryKind::kUpload,
				"Upload ring chunk");
		}
		catch (const std::exception& e)
		{
			core::fatal("Allocating a {}-byte upload chunk failed: {}", chunkSize, e.what());
		}
		chunk->tracked = TaggedBytes(MemoryTag::kDeviceBuffer, chunkSize);
		m_Chunks.push_back(std::move(chunk));
		return m_Chunks.back().get();
	}
}
