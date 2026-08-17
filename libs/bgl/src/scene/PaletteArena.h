#pragma once
#include "scene/ComputeBuffer.h"
#include <core/containers/multi_slot_vector.h>

namespace bgl
{
	/**
	 * The bone palettes of one view's skinned instances: GPU-only storage the pose pass writes and the
	 * skinned mesh shader reads, plus the allocator that hands each instance its slice of it. One slot
	 * is one `float4`, so a slice is `cFloat4sPerBone * boneCount * 2` slots -- the pose at `time` and
	 * the pose at `prevTime`, back to back.
	 *
	 * The allocation pattern is a RangeBuffer's, but the storage cannot be: a RangeBuffer mirrors its
	 * contents on the CPU and re-uploads a dirty range, which would overwrite what the pose pass just
	 * wrote. So allocation and storage are separate types here, and the allocator's element type is a
	 * placeholder -- only the offsets it returns are ever read, which is why its CPU side costs a
	 * quarter of what it hands out rather than all of it.
	 *
	 * Growth discards: the palette is rewritten from scratch every frame, so a resize has nothing to
	 * carry forward. An allocation made before a growth keeps its offset, which is what lets an
	 * instance hold one across frames.
	 */
	class PaletteArena
	{
	public:
		PaletteArena() noexcept = default;

		PaletteArena(const PaletteArena&)     = delete;
		PaletteArena(PaletteArena&&) noexcept = default;

		PaletteArena&
		operator=(const PaletteArena&) = delete;

		PaletteArena&
		operator=(PaletteArena&&) noexcept = default;

		/**
		 * @throws std::runtime_error if the device cannot allocate the initial storage.
		 */
		void
		Init(ResourceManagerRef resourceManager);

		/**
		 * Reserves `float4Count` contiguous float4s, growing the arena when it is full.
		 *
		 * @throws std::runtime_error if the growth cannot be allocated; nothing is reserved.
		 */
		[[nodiscard]] core::multi_slot_handle
		Allocate(uint32_t float4Count);

		void
		Free(core::multi_slot_handle handle) noexcept;

		[[nodiscard]] bool
		IsInitialized() const noexcept
		{
			return m_Storage.IsInitialized();
		}

		// Re-read every frame: a growth mints a new handle and retires the old one.
		[[nodiscard]] BufferHandle
		GetBufferHandle() const noexcept
		{
			return m_Storage.GetBufferHandle();
		}

		[[nodiscard]] uint32_t
		Capacity() const noexcept
		{
			return m_Offsets.capacity();
		}

		void
		Update(ICommandList* cmdList)
		{
			m_Storage.Update(cmdList);
		}

		void
		Release(bool deferred = true) noexcept
		{
			m_Storage.Release(deferred);
		}

	private:
		// Offsets only; see the class comment on why the element type is a placeholder.
		core::multi_slot_vector<uint32_t> m_Offsets;
		ComputeBuffer                     m_Storage;
		ResourceManagerRef                m_ResourceManager;
	};
}
