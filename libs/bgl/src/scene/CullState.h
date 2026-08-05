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
	class CullState
	{
	public:
		CullState() noexcept = default;

		CullState(const CullState&)     = delete;
		CullState(CullState&&) noexcept = default;

		CullState&
		operator=(const CullState&) = delete;

		CullState&
		operator=(CullState&&) noexcept = default;

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

		/**
		 * Imports every buffer under the graph's current namespace.
		 *
		 * `updateArgs` receives the names the owning view's update pass declares copy-dest. The cull
		 * pass's own scratch is left out: that pass seeds it, so making the view's update its last
		 * writer would be a dependency edge nothing produces.
		 */
		void
		ImportResources(FrameGraph& fg, std::vector<std::string>& updateArgs) const;

		// Non-const: the cull pass seeds these through ComputeBuffer::Clear / WriteBuffer, which
		// need the object rather than the handle the frame graph hands back.
		[[nodiscard]] ComputeBuffer&
		GetPsoPrefixSum() noexcept
		{
			return m_PsoPrefixSum;
		}

		[[nodiscard]] ComputeBuffer&
		GetCompactedDispatchArgs() noexcept
		{
			return m_CompactedDispatchArgs;
		}

		[[nodiscard]] ComputeBuffer&
		GetCullView() noexcept
		{
			return m_CullView;
		}

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

		// Sized by the PSO bucket count rather than the instance count, so Resize does not reach
		// them: one running total per bucket, and the indirect args the forward pass dispatches on.
		ComputeBuffer m_PsoPrefixSum;
		ComputeBuffer m_CompactedDispatchArgs;

		// This frustum's planes, uploaded per draw and read by the cull dispatch.
		ComputeBuffer m_CullView;

		// The transparent sort's indirect args: one dispatch over the depth-sorted list.
		ComputeBuffer m_TransparentDispatchArgs;
	};
}
