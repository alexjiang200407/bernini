#pragma once
#include <string_view>

namespace bgl
{
	class BindingNameCheck;
	class ForwardPhases;
	class PassContext;

	struct DrawData;
	struct MeshletState;
	struct PassDesc;

	/**
	 * The grass the view's geoms grow: per grass bucket, one direct dispatch of one amplification
	 * group per chunk reference in the view's list, the blades built in the mesh stage. Drawn after
	 * the world so the blob-shadow decal reads grass in the depth. See docs/grass.md.
	 */
	class GrassForwardPhase
	{
	public:
		[[nodiscard]] std::string_view
		Name() const noexcept
		{
			return "Grass";
		}

		/** Whether any drawn geom has grass; a view with none attaches no pass. */
		[[nodiscard]] bool
		HasWork(const DrawData& draw) const;

		void
		Declare(PassDesc& desc) const;

		void
		Record(
			ForwardPhases&     kernels,
			MeshletState&      state,
			const DrawData&    draw,
			const PassContext& resources) const;

		/** Checks the names the phase binds into its own constant buffer. */
		static void
		CheckBindings(BindingNameCheck& check);
	};
}
