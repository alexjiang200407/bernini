#pragma once
#include "passes/IForwardPhase.h"
#include <string_view>

namespace bgl
{
	class BindingNameCheck;

	/**
	 * The scene's terrains: per terrain, one direct dispatch of one amplification thread per node
	 * of every level of its quadtree, the patches of the nodes that draw built in the mesh stage
	 * from the height texture. Drawn before the world: the ground is the largest occluder and the
	 * cheapest fragment. See docs/terrain.md.
	 */
	class TerrainForwardPhase final : public IForwardPhase
	{
	public:
		TerrainForwardPhase() = default;

		TerrainForwardPhase(const TerrainForwardPhase&) noexcept = delete;
		TerrainForwardPhase(TerrainForwardPhase&&) noexcept      = delete;

		TerrainForwardPhase&
		operator=(const TerrainForwardPhase&) noexcept = delete;

		TerrainForwardPhase&
		operator=(TerrainForwardPhase&&) noexcept = delete;

		~TerrainForwardPhase() noexcept override = default;

		[[nodiscard]] std::string_view
		Name() const noexcept override
		{
			return "Terrain";
		}

		/** Whether the scene holds a terrain; a view of one with none attaches no pass. */
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

		/** Declares the terrain buffers the phase's stages read, which Ground Color reads too. */
		void
		DeclareBuffers(PassDesc& desc) const;

		/**
		 * Every terrain drawn into the view's ground-colour texture through its bucket's albedo
		 * kernel, the stages bound from `draw` exactly as Record binds them.
		 */
		void
		RecordGroundColor(
			ForwardPhases&      kernels,
			bgpu::MeshletState& state,
			const DrawData&     draw,
			const PassContext&  resources) const;

		/** Checks the names the phase binds into its own constant buffer. */
		static void
		CheckBindings(BindingNameCheck& check);
	};
}
