#pragma once
#include "gfx/DrawBucketTable.h"
#include "passes/IForwardPhase.h"
#include <string_view>

namespace bgl
{
	/**
	 * The opaque and alpha-test buckets of one geometry stage, each drawn indirect over the instance
	 * compaction's output to the count it left on the GPU.
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

		void
		Declare(PassDesc& desc) const override;

		void
		Record(
			ForwardPhases&     kernels,
			MeshletState&      state,
			const DrawData&    draw,
			const PassContext& resources) const override;

	private:
		GeometryStage    m_Stage;
		std::string_view m_Name;
	};
}
