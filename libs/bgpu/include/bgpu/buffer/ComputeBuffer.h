#pragma once
#include <bgpu/api.h>
#include <bgpu/buffer/GrowableGpuBuffer.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/ResourceManager.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace bgpu
{
	/**
	 * A ComputeBuffer is a GPU-only structured buffer that compute shaders fill via
	 * UAV writes. A list the CPU authors for shaders to read is an UploadBuffer instead.
	 */
	class ComputeBuffer
	{
	public:
		/**
		 * @throws std::runtime_error if the device cannot allocate the buffer.
		 */
		BGPU_API
		ComputeBuffer(ResourceManagerRef resourceManager, ComputeBufferDesc desc);

		ComputeBuffer(const ComputeBuffer&)     = delete;
		ComputeBuffer(ComputeBuffer&&) noexcept = default;

		ComputeBuffer&
		operator=(const ComputeBuffer&) = delete;

		ComputeBuffer&
		operator=(ComputeBuffer&&) noexcept = default;

		/**
		 * Reallocates at `newCount` elements, discarding the contents: this is per-frame scratch
		 * that its producing pass overwrites, so there is nothing to carry forward.
		 *
		 * @throws std::runtime_error if the device cannot allocate; the buffer is left intact.
		 */
		BGPU_API void
		Resize(uint32_t newCount);

		[[nodiscard]] const ComputeBufferDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		// Re-read every frame: Resize mints a new handle and retires the old one (see
		// GrowableGpuBuffer), so a cached descriptor index goes stale.
		[[nodiscard]] BufferHandle
		GetBufferHandle() const noexcept
		{
			return m_Storage.GetHandle();
		}

		// Retires the resources a Resize superseded. Nothing is copied forward.
		void
		Update(ICommandList* cmdList)
		{
			m_Storage.FlushGrowth(cmdList);
		}

		[[nodiscard]] uint64_t
		ByteSize() const noexcept
		{
			return static_cast<uint64_t>(m_Desc.initialCount) * m_Desc.elementSize;
		}

		void
		Clear(ICommandList* cmd) noexcept
		{
			core::ensure(cmd != nullptr, "Command list cannot be null");

			const auto zeros = std::vector<std::byte>(ByteSize(), std::byte{ 0 });
			cmd->WriteBuffer(m_Storage.GetHandle(), zeros.data(), zeros.size());
		}

	private:
		ComputeBufferDesc m_Desc;
		GrowableGpuBuffer m_Storage;
	};
}
