#pragma once
#include <bgl/idl/AutoPosedInstance.h>
#include <bgpu/buffer/ComputeBuffer.h>
#include <bgpu/buffer/UploadBuffer.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/ResourceManager.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace bgpu
{
	class ICommandList;
}

namespace bgl
{
	class FrameGraph;

	/**
	 * What a view's automatic placements are chosen and posed through: the dense list of them the
	 * camera's cull walks, the per-placement pose slice it writes, the pool counters and the
	 * requests it collects, and the pose list it hands the pose pass. See docs/skinning.md.
	 *
	 * The slices themselves are cut from a block of the view's palette arena the view reserves; this
	 * holds only where that block starts and how large it is.
	 */
	class AutoPoseState
	{
	public:
		/**
		 * @param placements the placement buffer's capacity, which the pose slices are indexed by.
		 * @throws std::runtime_error if the device cannot allocate.
		 */
		AutoPoseState(const bgpu::ResourceManagerRef& resourceManager, uint32_t placements);

		AutoPoseState(const AutoPoseState&)     = delete;
		AutoPoseState(AutoPoseState&&) noexcept = default;

		AutoPoseState&
		operator=(const AutoPoseState&) = delete;

		AutoPoseState&
		operator=(AutoPoseState&&) noexcept = default;

		/** Grows the per-placement slices to cover `placements`. A no-op when they already do. */
		void
		Resize(uint32_t placements);

		/**
		 * Replaces the list of automatic placements (MeshInstance entries) and the pool they are
		 * posed from: `budget` entries of the pose list, and `capacity` float4s of the palette arena
		 * from `poolStart`.
		 */
		void
		Assign(
			std::span<const uint32_t> placements,
			uint32_t                  budget,
			uint32_t                  poolStart,
			uint32_t                  capacity);

		/** Uploads the list and zeroes the pool's counters for this frame. */
		void
		Update(bgpu::ICommandList* cmdList);

		/** Imports every buffer in the graph's current namespace, naming each in `updateArgs`. */
		void
		ImportResources(FrameGraph& fg, std::vector<std::string>& updateArgs) const;

		[[nodiscard]] uint32_t
		GetPlacementCount() const noexcept
		{
			return m_PlacementCount;
		}

		/** The most placements one frame can pose: the pose list's length, and the pose dispatch's. */
		[[nodiscard]] uint32_t
		GetBudget() const noexcept
		{
			return m_Budget;
		}

		[[nodiscard]] uint32_t
		GetPoolStart() const noexcept
		{
			return m_PoolStart;
		}

		/** One idl::PosePool: this frame's counters, as the camera's cull left them. */
		[[nodiscard]] bgpu::BufferHandle
		GetPoolBuffer() const noexcept
		{
			return m_Pool.GetBufferHandle();
		}

		/** The automatic pose list: one idl::AutoPosedInstance per placement posed this frame. */
		[[nodiscard]] bgpu::BufferHandle
		GetPosedBuffer() const noexcept
		{
			return m_Posed.GetBufferHandle();
		}

		/** One idl::InstancePose per placement slot. */
		[[nodiscard]] bgpu::BufferHandle
		GetInstancePoseBuffer() const noexcept
		{
			return m_InstancePose.GetBufferHandle();
		}

	private:
		bgpu::UploadBuffer<uint32_t> m_Placements;
		bgpu::ComputeBuffer          m_InstancePose;
		bgpu::ComputeBuffer          m_DominantFrames;
		bgpu::ComputeBuffer          m_Pool;
		bgpu::ComputeBuffer          m_Posed;
		bgpu::ComputeBuffer          m_Requests;

		uint32_t m_PlacementCount = 0;
		uint32_t m_Budget         = 0;
		uint32_t m_PoolStart      = 0;
		uint32_t m_Capacity       = 0;
	};
}
