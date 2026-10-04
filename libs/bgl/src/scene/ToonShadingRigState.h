#pragma once
#include <bgl/idl/ToonShadingRigRange.h>
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
	 * What a view's toon shading rigs are selected and evaluated through: the dense list of rigged
	 * placement ranges, the pool's counter, and the pool of evaluated blocks. See
	 * docs/toon_shading_rig.md.
	 */
	class ToonShadingRigState
	{
	public:
		/** @throws std::runtime_error if the device cannot allocate. */
		explicit ToonShadingRigState(const bgpu::ResourceManagerRef& resourceManager);

		ToonShadingRigState(const ToonShadingRigState&)     = delete;
		ToonShadingRigState(ToonShadingRigState&&) noexcept = default;

		ToonShadingRigState&
		operator=(const ToonShadingRigState&) = delete;

		ToonShadingRigState&
		operator=(ToonShadingRigState&&) noexcept = default;

		/**
		 * Replaces the ranges, numbering their placements in order. The block pool takes its full
		 * capacity the first time any range exists, and keeps it.
		 */
		void
		Assign(std::vector<idl::ToonShadingRigRange> ranges);

		/** Uploads the ranges and zeroes the pool's counter for this draw. */
		void
		Update(bgpu::ICommandList* cmdList);

		/** Imports every buffer in the graph's current namespace, naming each in `updateArgs`. */
		void
		ImportResources(FrameGraph& fg, std::vector<std::string>& updateArgs) const;

		[[nodiscard]] uint32_t
		GetRangeCount() const noexcept
		{
			return m_Ranges.Size();
		}

		/** Placements across every range: the evaluation pass's thread count. */
		[[nodiscard]] uint32_t
		GetPlacementCount() const noexcept
		{
			return m_PlacementCount;
		}

		/** One idl::ToonShadingRigPool. */
		[[nodiscard]] bgpu::BufferHandle
		GetPoolBuffer() const noexcept
		{
			return m_Pool.GetBufferHandle();
		}

		/** cToonShadingRigPoolCapacity idl::ToonShadingRigBlock once any range exists, one before. */
		[[nodiscard]] bgpu::BufferHandle
		GetBlockBuffer() const noexcept
		{
			return m_Blocks.GetBufferHandle();
		}

		[[nodiscard]] uint32_t
		GetBlockCapacity() const noexcept
		{
			return m_Blocks.GetDesc().initialCount;
		}

	private:
		bgpu::UploadBuffer<idl::ToonShadingRigRange> m_Ranges;
		bgpu::ComputeBuffer                          m_Pool;
		bgpu::ComputeBuffer                          m_Blocks;

		uint32_t m_PlacementCount = 0;
	};
}
