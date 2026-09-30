#pragma once
#include "passes/PassInitContext.h"
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <cstdint>
#include <spdlog/spdlog.h>

namespace bgpu
{
	class PipelineBatch;
	class ICommandList;
}

namespace bgl
{
	/**
	 * The split-sum BRDF lookup table, generated at most once per device -- on the first frame
	 * that draws a PBR-lit bucket, not at device creation, so a scene shaded entirely by lit
	 * surfaces never builds it.
	 *
	 * It is a property of the shading model rather than of any environment -- the integral is taken
	 * against a white one -- so it is owned here and shared by every view, and no caller can supply a
	 * mismatched one.
	 */
	class BrdfLutGenPass
	{
	public:
		BrdfLutGenPass() = default;
		~BrdfLutGenPass() noexcept { spdlog::trace("~BrdfLutGenPass"); }

		BrdfLutGenPass(const BrdfLutGenPass&) noexcept = delete;
		BrdfLutGenPass(BrdfLutGenPass&&) noexcept      = delete;

		BrdfLutGenPass&
		operator=(const BrdfLutGenPass&) noexcept = delete;

		BrdfLutGenPass&
		operator=(BrdfLutGenPass&&) noexcept = delete;

		/** Requests the pipeline; Generate needs `pipelines` built first. Creates no resource. */
		void
		Init(const PassInitContext& ctx);

		/**
		 * Creates the texture and records the integration into `cmdList`, leaving the texture
		 * readable by a pixel shader: commands recorded after this on the same list may sample it.
		 * The render target it drew into is freed deferred -- the table is written once and only
		 * sampled afterwards, so holding the view would spend a slot of the caller's RTV budget for
		 * the lifetime of the device.
		 *
		 * @pre `cmdList` is open, and Init's pipeline batch has been built.
		 */
		void
		Generate(bgpu::ICommandList* cmdList);

		/** Whether Generate has run: the one question laziness makes worth asking. */
		[[nodiscard]] bool
		Generated() const noexcept
		{
			return !m_Texture.IsNull();
		}

		/** The table itself, for barriers and copies. Null until Generate. */
		[[nodiscard]] bgpu::TextureHandle
		GetTexture() const noexcept
		{
			return m_Texture;
		}

		/** Null until Generate; a scene that never demands PBR shading hands this out null. */
		[[nodiscard]] bgpu::SrvHandle
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

		bgpu::ResourceManagerRef m_ResourceManager;
		bgpu::MeshletKernel      m_Kernel;
		bgpu::TextureHandle      m_Texture;
		bgpu::SrvHandle          m_Srv;
	};
}
