#pragma once
#include "device/Device.h"
#include "pipeline/MeshletKernel.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv.h"
#include "resource/Srv.h"
#include "resource/Texture.h"
#include <cstdint>
#include <spdlog/spdlog.h>

namespace bgl
{
	class PipelineBatch;

	class ICommandList;

	/**
	 * The split-sum BRDF lookup table, generated once per device.
	 *
	 * It is a property of the shading model rather than of any environment -- the integral is taken
	 * against a white one -- so it is owned here and shared by every view, and no caller can supply a
	 * mismatched one.
	 */
	class BrdfLutGenPass
	{
	public:
		BrdfLutGenPass() = default;
		~BrdfLutGenPass() noexcept { logger::trace("~BrdfLutGenPass"); }

		BrdfLutGenPass(const BrdfLutGenPass&) noexcept = delete;
		BrdfLutGenPass(BrdfLutGenPass&&) noexcept      = delete;

		BrdfLutGenPass&
		operator=(const BrdfLutGenPass&) noexcept = delete;

		BrdfLutGenPass&
		operator=(BrdfLutGenPass&&) noexcept = delete;

		/**
		 * Creates the texture and its RTV, and requests the pipeline; Generate needs `pipelines`
		 * built first.
		 */
		void
		Init(IDevice* device, PipelineBatch& pipelines, ResourceManagerRef resourceManager);

		/**
		 * Records the integration into `cmdList`, leaving the texture readable by a pixel shader. The
		 * caller submits and waits: nothing samples the table until it has been written once.
		 *
		 * @pre `cmdList` is open.
		 */
		void
		Generate(ICommandList* cmdList);

		/**
		 * Frees the render target the integration drew into. The table is written once and only
		 * sampled afterwards, so holding the view would spend a slot of the caller's RTV budget for
		 * the lifetime of the device.
		 *
		 * @pre the Generate submission has completed -- the free is immediate, and the view is still
		 *      referenced by the command list until then.
		 */
		void
		ReleaseTarget() noexcept;

		/** The table itself, for barriers and copies. Null until Init. */
		[[nodiscard]] TextureHandle
		GetTexture() const noexcept
		{
			return m_Texture;
		}

		/** Null until Init. Valid to sample only after the Generate submission has completed. */
		[[nodiscard]] SrvHandle
		GetSrv() const noexcept
		{
			return m_Srv;
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
		SrvHandle          m_Srv;
		RtvHandle          m_Rtv;
	};
}
