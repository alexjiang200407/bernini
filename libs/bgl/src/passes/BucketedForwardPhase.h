#pragma once
#include "gfx/DrawBucketTable.h"
#include "passes/IForwardPhase.h"
#include <cstdint>
#include <string_view>

namespace bgl
{
	/**
	 * One bucket's dispatches: both lanes, each indirect over the compaction's output to the count
	 * it left on the GPU, through the kernel `kernels` binds for it. A lane with no kernel is
	 * skipped: a bucket never demanded has none, and by the same fact no instances.
	 */
	void
	RecordDrawBucket(
		ForwardPhases&      kernels,
		bgpu::MeshletState& state,
		const DrawData&     draw,
		const PassContext&  resources,
		uint32_t            bucket);

	/**
	 * The opaque and alpha-test buckets of one geometry stage, each drawn indirect over the instance
	 * compaction's output to the count it left on the GPU. A water surface's buckets are Forward
	 * Water's.
	 */
	class BucketedForwardPhase final : public IForwardPhase
	{
	public:
		BucketedForwardPhase(GeometryStage stage, std::string_view name) noexcept;

		BucketedForwardPhase(const BucketedForwardPhase&) noexcept = delete;
		BucketedForwardPhase(BucketedForwardPhase&&) noexcept      = delete;

		BucketedForwardPhase&
		operator=(const BucketedForwardPhase&) noexcept = delete;

		BucketedForwardPhase&
		operator=(BucketedForwardPhase&&) noexcept = delete;

		~BucketedForwardPhase() noexcept override = default;

		[[nodiscard]] std::string_view
		Name() const noexcept override
		{
			return m_Name;
		}

		/** Whether the view places anything: a bucket's instances are placements, so none is none. */
		[[nodiscard]] bool
		HasWork(const DrawData& draw) const override;

		void
		Declare(PassDesc& desc, const DrawData& draw) const override;

		void
		Record(
			ForwardPhases&      kernels,
			bgpu::MeshletState& state,
			const DrawData&     draw,
			const PassContext&  resources) const override;

	private:
		GeometryStage    m_Stage;
		std::string_view m_Name;
	};
}
