#pragma once
#include "passes/IForwardPhase.h"
#include <string_view>

namespace bgpu
{
	struct MeshletKernel;
}

namespace bgl
{
	class BindingNameCheck;

	/**
	 * The static tier's water buckets, each drawn indirect over the compaction's output as the world
	 * is, after the characters and before the transparents. No depth is attached: the depth the
	 * phases before it wrote is read in the pixel stage, which hides the water behind the scene and
	 * measures how deep it is. See docs/water.md.
	 */
	class WaterForwardPhase final : public IForwardPhase
	{
	public:
		explicit WaterForwardPhase(const ForwardPhases& kernels) noexcept : m_Kernels(kernels) {}

		WaterForwardPhase(const WaterForwardPhase&) noexcept = delete;
		WaterForwardPhase(WaterForwardPhase&&) noexcept      = delete;

		WaterForwardPhase&
		operator=(const WaterForwardPhase&) noexcept = delete;

		WaterForwardPhase&
		operator=(WaterForwardPhase&&) noexcept = delete;

		~WaterForwardPhase() noexcept override = default;

		[[nodiscard]] std::string_view
		Name() const noexcept override
		{
			return "Water";
		}

		/** Whether an instance of the view has ever resolved to a water bucket. */
		[[nodiscard]] bool
		HasWork(const DrawData& draw) const override;

		[[nodiscard]] bool
		WritesDepth() const noexcept override
		{
			return false;
		}

		void
		Declare(PassDesc& desc, const DrawData& draw) const override;

		void
		Record(
			ForwardPhases&      kernels,
			bgpu::MeshletState& state,
			const DrawData&     draw,
			const PassContext&  resources) const override;

		/**
		 * Binds the frame water is shaded over into `kernel`'s waterData, when it declares one: the
		 * depth, its reconstruction, the clock and the view's terrains. ForwardPhases calls it for
		 * every kernel it binds, so only a water bucket's program pays for it.
		 */
		static void
		Bind(bgpu::MeshletKernel& kernel, const DrawData& draw, const PassContext& resources);

		/** Checks the names the phase binds into its own constant buffer. */
		static void
		CheckBindings(BindingNameCheck& check);

	private:
		const ForwardPhases& m_Kernels;
	};
}
