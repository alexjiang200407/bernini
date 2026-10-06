#pragma once
#include "passes/IForwardPhase.h"
#include <string_view>

namespace bgl
{
	class BindingNameCheck;
	/**
	 * The grass the view's geoms and terrains grow: per grass bucket and terrain, one direct
	 * dispatch of one amplification group per chunk reference in the view's list, the blades built
	 * in the mesh stage. Drawn after the world so the blob-shadow decal reads grass in the depth.
	 * See docs/grass.md.
	 */
	class GrassForwardPhase final : public IForwardPhase
	{
	public:
		GrassForwardPhase() = default;

		GrassForwardPhase(const GrassForwardPhase&) noexcept = delete;
		GrassForwardPhase(GrassForwardPhase&&) noexcept      = delete;

		GrassForwardPhase&
		operator=(const GrassForwardPhase&) noexcept = delete;

		GrassForwardPhase&
		operator=(GrassForwardPhase&&) noexcept = delete;

		~GrassForwardPhase() noexcept override = default;

		[[nodiscard]] std::string_view
		Name() const noexcept override
		{
			return "Grass";
		}

		/** Whether any drawn geom or terrain has grass; a view with none attaches no pass. */
		[[nodiscard]] bool
		HasWork(const DrawData& draw) const override;

		void
		Declare(PassDesc& desc) const override;

		void
		Record(
			ForwardPhases&      kernels,
			bgpu::MeshletState& state,
			const DrawData&     draw,
			const PassContext&  resources) const override;

		/** Checks the names the phase binds into its own constant buffer. */
		static void
		CheckBindings(BindingNameCheck& check);
	};
}
