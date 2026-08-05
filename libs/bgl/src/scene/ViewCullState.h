#pragma once
#include "scene/ComputeBuffer.h"

namespace bgl
{
	class FrameGraph;
	class ICommandList;

	/**
	 * The GPU scratch one culled frustum produces: which instances survived, where they were
	 * compacted to, and the depth-sorted transparent list drawn from them.
	 *
	 * Separate from the SceneView that owns it because these are outputs of culling *one* frustum,
	 * not per-view state.
	 */
	class ViewCullState
	{
	public:
		ViewCullState() noexcept = default;

		ViewCullState(const ViewCullState&)     = delete;
		ViewCullState(ViewCullState&&) noexcept = default;

		ViewCullState&
		operator=(const ViewCullState&) = delete;

		ViewCullState&
		operator=(ViewCullState&&) noexcept = default;

		/**
		 * @param paddedInstances the instance buffer's capacity rounded up to the histogram group
		 *        size; every per-slot buffer here must cover it exactly or a cull writes past the end.
		 * @throws std::runtime_error if the device cannot allocate.
		 */
		void
		Init(uint32_t paddedInstances, ResourceManagerRef resourceManager);

		/**
		 * Grows the per-slot buffers to `paddedInstances`. A no-op when they already cover it.
		 *
		 * They resize together or not at all: each is indexed by instance slot and written over the
		 * whole padded range, so growing one without the others is an out-of-bounds UAV write rather
		 * than a capacity shortfall.
		 *
		 * @throws std::runtime_error if the device cannot allocate; the buffers are left intact.
		 */
		void
		Resize(uint32_t paddedInstances);

		void
		Release(bool deferred = true) noexcept;

		// Retires the resources a Resize superseded; nothing is carried forward.
		void
		Update(ICommandList* cmdList);

		// Imports every buffer under the graph's current namespace, appending each name to
		// `resourceNames` so the caller can declare them as the copy-dest args of its update pass.
		void
		ImportResources(FrameGraph& fg, std::vector<std::string>& resourceNames) const;

	private:
		// Written by the compaction, bounded by the dispatch args that same compaction wrote, so it
		// is never cleared between frames -- a reader only touches slots this frame's scatter filled.
		ComputeBuffer m_CompactedInstances;

		// One word per instance slot, written by the cull pass and read by the counting sort and the
		// transparent depth-key pass.
		ComputeBuffer m_InstanceVisibility;

		// The depth-sorted transparent path, all written by TransparentSortPass. The keys buffer is
		// sized off the instance buffer rather than the sort's capacity so the depth-key pass, which
		// appends without knowing how many instances are transparent, cannot run past the end -- only
		// the sort itself is capped.
		ComputeBuffer m_SortedTransparentInstances;
		ComputeBuffer m_TransparentSortEntries;

		// A single counter, so it is made once and never resized.
		ComputeBuffer m_TransparentSortCount;
	};
}
