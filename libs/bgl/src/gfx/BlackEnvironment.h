#pragma once
#include "types/EnvironmentMap.h"
#include <bgpu/cmd/CommandList.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <spdlog/spdlog.h>

namespace bgl
{
	/**
	 * What a draw samples in place of image-based lighting it does not have: a black cube for the
	 * irradiance and prefilter maps, and a black BRDF LUT for a frame that never generated one.
	 *
	 * "No environment means black" is then true of what the shaders read. Without it, an unset
	 * handle reaches the shader as an invalid index and the sample reads past the descriptor heap,
	 * which only happened to come back black.
	 */
	class BlackEnvironment
	{
	public:
		BlackEnvironment() = default;
		~BlackEnvironment() noexcept { spdlog::trace("~BlackEnvironment"); }

		BlackEnvironment(const BlackEnvironment&) noexcept = delete;
		BlackEnvironment(BlackEnvironment&&) noexcept      = delete;

		BlackEnvironment&
		operator=(const BlackEnvironment&) noexcept = delete;

		BlackEnvironment&
		operator=(BlackEnvironment&&) noexcept = delete;

		/** @throws GraphicsError if a texture or its view cannot be created. */
		void
		Init(bgpu::ResourceManagerRef resourceManager);

		/** Records the zero fill and the barriers that make both sampleable. @pre Init succeeded. */
		void
		Upload(bgpu::ICommandList* cmdList);

		/** `env` with every handle it lacks replaced by black. */
		[[nodiscard]] EnvironmentMap
		Complete(EnvironmentMap env) const noexcept;

		void
		Release() noexcept;

	private:
		bgpu::ResourceManagerRef m_ResourceManager;
		bgpu::TextureHandle      m_Cube;
		bgpu::SrvHandle          m_CubeSrv;
		bgpu::TextureHandle      m_Lut;
		bgpu::SrvHandle          m_LutSrv;
	};
}
