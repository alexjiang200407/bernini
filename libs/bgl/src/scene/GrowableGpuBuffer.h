#pragma once
#include "cmd/CommandList.h"
#include "resource/ResourceManager.h"

namespace bgl
{
	/**
	 * The GPU-side storage of a CPU-mirrored buffer, able to grow without a fixed ceiling.
	 *
	 * Growth allocates a *new* resource and a *new* descriptor slot rather than rewriting the
	 * existing one: a descriptor is read by the GPU when a shader runs, not when the command list
	 * is recorded, so overwriting one an in-flight frame may still reach would race. The previous
	 * resource is handed to the resource manager's deferred destroy, which holds it until every
	 * registered queue passes the fence it was at — exactly the window those frames occupy. Callers
	 * must therefore re-read GetHandle() each frame and never cache the descriptor index.
	 *
	 * Contents survive the move: FlushGrowth records a GPU-side copy of the old resource into the
	 * new one. It must run on an open command list before that frame's dirty-region uploads, or
	 * the copy overwrites them.
	 */
	class GrowableGpuBuffer
	{
	public:
		GrowableGpuBuffer() noexcept = default;

		GrowableGpuBuffer(const GrowableGpuBuffer&)     = delete;
		GrowableGpuBuffer(GrowableGpuBuffer&&) noexcept = default;

		GrowableGpuBuffer&
		operator=(const GrowableGpuBuffer&) = delete;

		GrowableGpuBuffer&
		operator=(GrowableGpuBuffer&&) noexcept = default;

		/**
		 * @throws std::runtime_error if the device cannot allocate the initial resource.
		 */
		void
		Init(
			ResourceManagerRef resourceManager,
			std::string        debugName,
			uint32_t           stride,
			uint32_t           capacity,
			bool               isUav);

		/**
		 * Replaces the storage with one holding `newCapacity` elements, preserving the contents of
		 * the old one until the next FlushGrowth. A `newCapacity` at or below the current one is a
		 * no-op.
		 *
		 * @param preserveContents false for GPU-only scratch that is rewritten every frame, which
		 *        skips the forward copy entirely.
		 * @throws std::runtime_error if the device cannot allocate the larger resource; the buffer
		 *         is left intact at its current capacity.
		 */
		void
		Grow(uint32_t newCapacity, bool preserveContents = true);

		// Records the forward copy for any pending growth and retires the superseded resources.
		void
		FlushGrowth(ICommandList* cmdList);

		[[nodiscard]] bool
		HasPendingGrowth() const noexcept
		{
			return !m_Superseded.empty();
		}

		[[nodiscard]] BufferHandle
		GetHandle() const noexcept
		{
			return m_Handle;
		}

		[[nodiscard]] uint32_t
		GetCapacity() const noexcept
		{
			return m_Capacity;
		}

		[[nodiscard]] bool
		IsInitialized() const noexcept
		{
			return !m_Handle.IsNull();
		}

		void
		Release(bool deferred) noexcept;

	private:
		ResourceManagerRef m_ResourceManager;
		BufferHandle       m_Handle;
		std::string        m_DebugName;

		// Resources replaced since the last FlushGrowth, oldest first. Only the oldest still holds
		// the data -- every one after it was superseded before anything was copied into it.
		std::vector<BufferHandle> m_Superseded;
		uint64_t                  m_CopyBytes = 0;

		uint32_t m_Capacity = 0;
		uint32_t m_Stride   = 0;
		bool     m_IsUav    = false;
	};

	/**
	 * The capacity to grow to when `required` elements must fit. Doubles while the arena is small
	 * and tapers past c_TaperBytes, where the transient old+new residency of a doubling would cost
	 * more device memory than the growth is worth.
	 */
	[[nodiscard]] uint32_t
	NextGpuBufferCapacity(uint32_t current, uint32_t required, uint32_t stride) noexcept;
}
