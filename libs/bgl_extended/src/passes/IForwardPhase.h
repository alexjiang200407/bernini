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
	 * One graph pass of the forward render: which draws it records and how it dispatches them. The
	 * kernels, the uniforms every forward kernel shares and the targets are ForwardPhases'; a phase
	 * declares only what its own dispatch reads, and records with kernels handed to it bound.
	 */
	class IForwardPhase
	{
	public:
		IForwardPhase()                              = default;
		IForwardPhase(const IForwardPhase&) noexcept = delete;
		IForwardPhase(IForwardPhase&&) noexcept      = delete;

		IForwardPhase&
		operator=(const IForwardPhase&) noexcept = delete;

		IForwardPhase&
		operator=(IForwardPhase&&) noexcept = delete;

		virtual ~IForwardPhase() noexcept = default;

		/** The pass is named "Forward <Name> <draw>". */
		[[nodiscard]] virtual std::string_view
		Name() const noexcept = 0;

		/**
		 * Whether this draw attaches the pass at all. Most phases always do: their counts live on
		 * the GPU, so an empty one is found there, not here.
		 */
		[[nodiscard]] virtual bool
		HasWork(const DrawData& /*draw*/) const
		{
			return true;
		}

		/**
		 * Declares what its dispatches read. Naming a resource the shared set already declares is
		 * harmless -- one pass's accesses to a resource merge into one state -- so a phase declares
		 * everything it reads and needs no knowledge of what the others do.
		 */
		virtual void
		Declare(PassDesc& desc) const = 0;

		/**
		 * Records its dispatches. `state` arrives with the viewport set; the phase sets the
		 * arguments of each dispatch, the kernel and framebuffer coming with the kernel it binds.
		 */
		virtual void
		Record(
			ForwardPhases&     kernels,
			MeshletState&      state,
			const DrawData&    draw,
			const PassContext& resources) const = 0;
	};
}
