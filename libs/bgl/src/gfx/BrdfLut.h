#pragma once
#include "pipeline/MeshletKernel.h"
#include "resource/ResourceManager.h"

namespace bgl
{
	class ICommandList;

	/**
	 * The split-sum BRDF lookup table, generated once per device.
	 *
	 * It is a property of the shading model rather than of any environment -- the integral is taken
	 * against a white one -- so it is owned here and shared by every view, and no caller can supply a
	 * mismatched one.
	 */
	class BrdfLut
	{
	public:
		BrdfLut() = default;
		~BrdfLut() noexcept { logger::trace("~BrdfLut"); }

		BrdfLut(const BrdfLut&) noexcept = delete;
		BrdfLut(BrdfLut&&) noexcept      = delete;

		BrdfLut&
		operator=(const BrdfLut&) noexcept = delete;

		BrdfLut&
		operator=(BrdfLut&&) noexcept = delete;

		/**
		 * Creates the texture, its RTV and the pipeline. Compiles a shader, so it must run before the
		 * Slang session is released.
		 */
		void
		Init(IDevice* device, ResourceManagerRef resourceManager);

		/**
		 * Records the integration into `cmdList`, leaving the texture readable by a pixel shader. The
		 * caller submits and waits: nothing samples the table until it has been written once.
		 *
		 * @pre `cmdList` is open.
		 */
		void
		Generate(ICommandList* cmdList);

		/** Null until Init. Valid to sample only after the Generate submission has completed. */
		[[nodiscard]] TextureHandle
		Texture() const noexcept
		{
			return m_Texture;
		}

		// @pre the GPU is idle -- the frees are immediate.
		void
		Release() noexcept;

	private:
		// Square, and matching the mip-0 face size the prefilter chain is sampled at: the table is
		// smooth in both axes, so this is already well past what the interpolation can resolve.
		static constexpr uint32_t c_Dimension = 256;

		ResourceManagerRef m_ResourceManager;
		MeshletKernel      m_Kernel;
		TextureHandle      m_Texture;
		RtvHandle          m_Rtv;
	};
}
