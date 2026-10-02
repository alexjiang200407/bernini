#pragma once
#include <array>
#include <bgl/idl/CullView.h>
#include <bgpu/buffer/ComputeBuffer.h>
#include <bgpu/buffer/UploadBuffer.h>
#include <bgpu/resource/ResourceManager.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgpu
{
	class ICommandList;
}

namespace bgl
{
	class FrameGraph;

	/**
	 * The GPU scratch one culled frustum produces: which instances survived, where they were
	 * compacted to, and the per-bucket bases and indirect args built from that.
	 *
	 * Separate from the SceneView that owns it because these are outputs of culling *one* frustum,
	 * not per-view state.
	 */
	class CullState
	{
	public:
		/**
		 * @param paddedInstances the instance buffer's capacity rounded up to the histogram group
		 *        size; every per-slot buffer here must cover it exactly or a cull writes past the end.
		 * @param placements the placement buffer's capacity, which the level-of-detail words are
		 *        indexed by.
		 * @throws std::runtime_error if the device cannot allocate.
		 */
		CullState(
			const bgpu::ResourceManagerRef& resourceManager,
			uint32_t                        paddedInstances,
			uint32_t                        placements);

		CullState(const CullState&)     = delete;
		CullState(CullState&&) noexcept = default;

		CullState&
		operator=(const CullState&) = delete;

		CullState&
		operator=(CullState&&) noexcept = default;

		/**
		 * Grows the per-slot buffers to `paddedInstances` and the per-placement ones to `placements`.
		 * A no-op for whichever already covers its count.
		 *
		 * The per-slot ones resize together or not at all: each is indexed by instance slot and
		 * written over the whole padded range, so growing one without the others is an
		 * out-of-bounds UAV write rather than a capacity shortfall. A grown level-of-detail word
		 * starts from zero, so every placement chooses afresh, without a dissolve, the next frame.
		 *
		 * @throws std::runtime_error if the device cannot allocate; the buffers are left intact.
		 */
		void
		Resize(uint32_t paddedInstances, uint32_t placements);

		/**
		 * Makes last frame's level-of-detail words the ones this frame's cull reads, and the other
		 * pair the ones it writes. Once per draw, before ImportResources: the cull's threads all read
		 * the old word and agree on the new one, which one buffer read and written in place would
		 * not let them do.
		 */
		void
		AdvanceLodHistory() noexcept;

		// Retires the resources a Resize superseded; nothing is carried forward.
		void
		Update(bgpu::ICommandList* cmdList);

		/**
		 * Imports every buffer under `scope`, which it makes the graph's current namespace.
		 *
		 * `updateArgs` receives the names the owning view's update pass declares copy-dest, prefixed
		 * with `scope`: that pass is recorded one scope out, and resolution only ever falls outward.
		 * The cull pass's own scratch is left out: that pass seeds it, so making the view's update
		 * its last writer would be a dependency edge nothing produces.
		 */
		void
		ImportResources(
			FrameGraph&               fg,
			std::string_view          scope,
			std::vector<std::string>& updateArgs) const;

		// Non-const: the cull pass seeds these through Clear / Assign+Update, which need the
		// object rather than the handle the frame graph hands back.
		[[nodiscard]] bgpu::ComputeBuffer&
		GetDrawBucketPrefixSum() noexcept
		{
			return m_DrawBucketPrefixSum;
		}

		[[nodiscard]] bgpu::ComputeBuffer&
		GetCompactedDispatchArgs() noexcept
		{
			return m_CompactedDispatchArgs;
		}

		[[nodiscard]] bgpu::UploadBuffer<idl::CullView>&
		GetCullView() noexcept
		{
			return m_CullView;
		}

		/** One idl::InstanceVisibility per instance slot, as this frustum's last cull wrote them. */
		[[nodiscard]] const bgpu::ComputeBuffer&
		GetInstanceVisibility() const noexcept
		{
			return m_InstanceVisibility;
		}

		/** The level-of-detail words this frame's cull writes, one per placement slot. */
		[[nodiscard]] bgpu::ComputeBuffer&
		GetInstanceLod() noexcept
		{
			return m_InstanceLod[m_LodCurrent];
		}

		/** The ones it reads: what the previous cull of this frustum wrote. */
		[[nodiscard]] bgpu::ComputeBuffer&
		GetPreviousInstanceLod() noexcept
		{
			return m_InstanceLod[m_LodCurrent ^ 1u];
		}

		/**
		 * Whether the words were allocated since the last clear. The cull's clear zeroes both, so
		 * every placement starts unchosen rather than reading what the allocation held.
		 */
		[[nodiscard]] bool
		TakeLodClear() noexcept
		{
			return std::exchange(m_LodNeedsClear, false);
		}

		/**
		 * Marks a placement slot just written: its words still hold whatever the slot's previous
		 * occupant left, which would hand the new placement that one's level, dissolve and pose-source
		 * right. ClearFresh zeroes them before the next cull reads them.
		 */
		void
		MarkFresh(uint32_t placement)
		{
			m_FreshPlacements.push_back(placement);
		}

		/** Zeroes both words of every slot MarkFresh named since the last call. */
		void
		ClearFresh(bgpu::ICommandList* cmdList);

	private:
		// Written by the compaction, bounded by the dispatch args that same compaction wrote, so it
		// is never cleared between frames -- a reader only touches slots this frame's scatter filled.
		bgpu::ComputeBuffer m_CompactedInstances;

		// One word per instance slot, written by the cull pass and read by the counting sort and the
		// transparent depth-key pass.
		bgpu::ComputeBuffer m_InstanceVisibility;

		// Sized by the bucket ceiling rather than the instance count, so Resize does not reach
		// them: one running total per bucket, and the indirect args the forward pass dispatches on.
		bgpu::ComputeBuffer m_DrawBucketPrefixSum;
		bgpu::ComputeBuffer m_CompactedDispatchArgs;

		// This frustum's planes, assigned per draw and read by the cull dispatch.
		bgpu::UploadBuffer<idl::CullView> m_CullView;

		// One idl::InstanceLod per placement slot, twice: the cull reads one and writes the other,
		// and AdvanceLodHistory swaps them. Indexed by the placement's MeshInstance entry, which
		// holds still while the placement lives where the dense instance slot does not.
		std::array<bgpu::ComputeBuffer, 2> m_InstanceLod;
		uint32_t                           m_LodCurrent    = 0;
		bool                               m_LodNeedsClear = true;
		std::vector<uint32_t>              m_FreshPlacements;
	};
}
