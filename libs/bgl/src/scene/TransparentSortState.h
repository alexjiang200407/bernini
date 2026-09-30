#pragma once
#include <bgpu/buffer/ComputeBuffer.h>
#include <bgpu/resource/ResourceManager.h>
#include <cstdint>
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
	 * The GPU scratch the transparent depth sort produces: the compacted (key, instance) pairs, how
	 * many there are, the sorted list, and the indirect args the forward pass draws it with.
	 *
	 * Per view rather than per culled frustum, unlike CullState. Sorting is against a camera
	 * position, and a shadow cascade does not draw transparents, so one of these serves a view
	 * however many frustums it is culled against.
	 */
	class TransparentSortState
	{
	public:
		TransparentSortState() noexcept = default;

		TransparentSortState(const TransparentSortState&)     = delete;
		TransparentSortState(TransparentSortState&&) noexcept = default;

		TransparentSortState&
		operator=(const TransparentSortState&) = delete;

		TransparentSortState&
		operator=(TransparentSortState&&) noexcept = default;

		/**
		 * @param paddedInstances the instance buffer's capacity rounded up to the histogram group
		 *        size. The keys buffer is sized off it rather than off the sort's capacity so the
		 *        depth-key pass, which appends without knowing how many instances are transparent,
		 *        cannot run past the end; only the sort itself is capped.
		 * @throws std::runtime_error if the device cannot allocate.
		 */
		void
		Init(uint32_t paddedInstances, bgpu::ResourceManagerRef resourceManager);

		/** @throws std::runtime_error if the device cannot allocate; the buffers are left intact. */
		void
		Resize(uint32_t paddedInstances);

		void
		Release(bool deferred = true) noexcept;

		// Retires the resources a Resize superseded; nothing is carried forward.
		void
		Update(bgpu::ICommandList* cmdList);

		// Imports every buffer under the graph's current namespace. `updateArgs` receives the names
		// the owning view's update pass declares copy-dest; the sort's own seed is left out, since
		// the sort pass writes it.
		void
		ImportResources(FrameGraph& fg, std::vector<std::string>& updateArgs) const;

	private:
		bgpu::ComputeBuffer m_SortedInstances;
		bgpu::ComputeBuffer m_Entries;

		// Single counters, so they are made once and never resized.
		bgpu::ComputeBuffer m_Count;
		bgpu::ComputeBuffer m_DispatchArgs;
	};
}
