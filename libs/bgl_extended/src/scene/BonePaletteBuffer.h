#pragma once
#include "cmd/CommandList.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include <core/containers/multi_slot_handle.h>
#include <core/containers/multi_slot_vector.h>
#include <cstdint>

namespace bgl
{
	/**
	 * GPU-only `float4` storage a compute pass writes and a shader reads, plus the allocator that
	 * hands out slices of it. Two of these exist: a view's bone palettes, a slice per skinned
	 * instance (`cFloat4sPerBone * boneCount * 2` slots -- the pose at `time` and at `prevTime`, back
	 * to back), and the scene's bone anim tables, a slice per rig
	 * (`frameCount * boneCount * cFloat4sPerBone`).
	 *
	 * The allocation pattern is a RangeBuffer's, but the storage cannot be: a RangeBuffer mirrors its
	 * contents on the CPU and re-uploads a dirty range, which would overwrite what the pass just
	 * wrote. So allocation and storage are separate types here, and the allocator's element type is a
	 * placeholder -- only the offsets it returns are ever read, which is why its CPU side costs a
	 * quarter of what it hands out rather than all of it.
	 *
	 * **Growth discards, so whatever a holder puts here must be re-derivable.** An allocation made
	 * before a growth keeps its offset -- which is what lets a slice be held across frames -- but its
	 * contents are gone. The per-view palette is rewritten every frame and so pays nothing; the
	 * scene's tables are written once, so `Scene::RequestBoneAnimTable` re-queues every rig holding
	 * one when it sees the capacity move.
	 */
	class BonePaletteBuffer
	{
	public:
		BonePaletteBuffer() noexcept = default;

		BonePaletteBuffer(const BonePaletteBuffer&)     = delete;
		BonePaletteBuffer(BonePaletteBuffer&&) noexcept = default;

		BonePaletteBuffer&
		operator=(const BonePaletteBuffer&) = delete;

		BonePaletteBuffer&
		operator=(BonePaletteBuffer&&) noexcept = default;

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
