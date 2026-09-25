#pragma once
#include <string_view>

namespace bgl
{
	class ForwardPhases;
	class PassContext;

	struct DrawData;
	struct MeshletState;
	struct PassDesc;

	/**
	 * The depth-sorted list, every stage: one indirect dispatch through the shared blend kernel,
	 * back to front, into colour alone -- a blended surface has no single depth to reproject.
	 */
	class TransparentForwardPhase
	{
	public:
		[[nodiscard]] std::string_view
		Name() const noexcept
		{
			return "Transparent";
		}

		/** Always: the bucket counts live on the GPU, so an empty bucket is found there, not here. */
		[[nodiscard]] bool
		HasWork(const DrawData& /*draw*/) const noexcept
		{
			return true;
		}

		void
		Declare(PassDesc& desc) const;

		void
		Record(
			ForwardPhases&     kernels,
			MeshletState&      state,
			const DrawData&    draw,
			const PassContext& resources) const;
	};
}
