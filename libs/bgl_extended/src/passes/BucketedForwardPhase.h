#pragma once
#include "gfx/DrawBucketTable.h"
#include <string_view>

namespace bgl
{
	class ForwardPhases;
	class PassContext;

	struct DrawData;
	struct MeshletState;
	struct PassDesc;

	/**
	 * The opaque and alpha-test buckets of one geometry stage, each drawn indirect over the instance
	 * compaction's output to the count it left on the GPU.
	 */
	class BucketedForwardPhase
	{
	public:
		BucketedForwardPhase(GeometryStage stage, std::string_view name) noexcept;

		[[nodiscard]] std::string_view
		Name() const noexcept
		{
			return m_Name;
		}

		void
		Declare(PassDesc& desc) const;

		void
		Record(
			ForwardPhases&     kernels,
			MeshletState&      state,
			const DrawData&    draw,
			const PassContext& resources) const;

	private:
		GeometryStage    m_Stage;
		std::string_view m_Name;
	};
}
