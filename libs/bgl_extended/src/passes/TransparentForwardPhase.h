#pragma once
#include "passes/IForwardPhase.h"
#include <string_view>

namespace bgl
{
	/**
	 * The depth-sorted list, every stage: one indirect dispatch through the shared blend kernel,
	 * back to front, into colour alone -- a blended surface has no single depth to reproject.
	 */
	class TransparentForwardPhase final : public IForwardPhase
	{
	public:
		TransparentForwardPhase() = default;

		TransparentForwardPhase(const TransparentForwardPhase&) noexcept = delete;
		TransparentForwardPhase(TransparentForwardPhase&&) noexcept      = delete;

		TransparentForwardPhase&
		operator=(const TransparentForwardPhase&) noexcept = delete;

		TransparentForwardPhase&
		operator=(TransparentForwardPhase&&) noexcept = delete;

		~TransparentForwardPhase() noexcept override = default;

		[[nodiscard]] std::string_view
		Name() const noexcept override
		{
			return "Transparent";
		}

		void
		Declare(PassDesc& desc) const override;

		void
		Record(
			ForwardPhases&     kernels,
			MeshletState&      state,
			const DrawData&    draw,
			const PassContext& resources) const override;
	};
}
